/*
 * line formatting buffer for line adjustment and hyphenation
 *
 * The line formatting buffer does two main functions: breaking
 * words into lines (possibly after breaking them at their
 * hyphenation points), and, if requested, adjusting the space
 * between words in a line.  In this file the first step is
 * referred to as filling.
 *
 * Functions like fmt_word() return nonzero on failure, which
 * means the call should be repeated after fetching previously
 * formatted lines via fmt_nextline().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define FMT_LLEN(f)	MAX(0, (f)->ll - (f)->li - (f)->lI)
#define FMT_FILL(f)	(!n_ce && n_u)
#define FMT_ADJ(f)	(n_u && !n_na && !n_ce && (n_j & AD_B) == AD_B)

static int fmt_fillwords(struct fmt *f, int br);

struct word {
	char *s;
	int wid;	/* word's width */
	int elsn, elsp;	/* els_neg and els_pos */
	int gap;	/* the space before this word */
	int hy;		/* hyphen width if inserted after this word */
	int str;	/* does the space before it stretch */
	int cost;	/* the extra cost of line break after this word */
	int swid;	/* space width after this word (i.e., \w' ') */
};

struct line {
	struct sbuf sbuf;
	int wid, li, ll, lI;
	int elsn, elsp;
};

struct fmt {
	/* queued words */
	struct word *words;
	int words_n, words_sz;
	/* queued lines */
	struct line *lines;
	int lines_head, lines_tail, lines_sz;
	/* for paragraph adjustment */
	long *best;
	int *best_pos;
	int *best_dep;
	/* current line */
	int gap;		/* space before the next word */
	int nls;		/* newlines before the next word */
	int nls_sup;		/* suppressed newlines */
	int li, ll, lI;		/* current line indentation and length */
	int filled;		/* filled all words in the last fmt_fill() */
	int eos;		/* last word ends a sentence */
	int fillreq;		/* fill after the last word (\p) */
};

/* .ll, .in and .ti are delayed until the partial line is output */
static void fmt_confupdate(struct fmt *f)
{
	f->ll = n_l;
	f->li = n_ti >= 0 ? n_ti : n_i;
	f->lI = n_tI >= 0 ? n_tI : n_I;
	n_ti = -1;
	n_tI = -1;
}

static int fmt_confchanged(struct fmt *f)
{
	return f->ll != n_l || f->li != (n_ti >= 0 ? n_ti : n_i) ||
		f->lI != (n_tI >= 0 ? n_tI : n_I);
}

/* move words from the buffer to s */
static int fmt_wordscopy(struct fmt *f, int end, struct sbuf *s, int *els_neg, int *els_pos)
{
	struct word *wcur;
	int w = 0;
	int i;
	*els_neg = 0;
	*els_pos = 0;
	for (i = 0; i < end; i++) {
		wcur = &f->words[i];
		if (wcur->gap)
			sbuf_printf(s, "%ch'%du'", c_ec, wcur->gap);
		sbuf_append(s, wcur->s);
		w += wcur->wid + wcur->gap;
		if (wcur->elsn < *els_neg)
			*els_neg = wcur->elsn;
		if (wcur->elsp > *els_pos)
			*els_pos = wcur->elsp;
		free(wcur->s);
	}
	if (end) {
		wcur = &f->words[end - 1];
		if (wcur->hy)
			sbuf_append(s, "\\(hy");
		w += wcur->hy;
	}
	f->words_n -= end;
	f->fillreq -= end;
	memmove(f->words, f->words + end, f->words_n * sizeof(f->words[0]));
	return w;
}

static int fmt_nlines(struct fmt *f)
{
	return f->lines_head - f->lines_tail;
}

/* the total width of the specified words in f->words[] */
static int fmt_wordslen(struct fmt *f, int end)
{
	int i, w = 0;
	for (i = 0; i < end; i++)
		w += f->words[i].wid + f->words[i].gap;
	return end ? w + f->words[end - 1].hy : 0;
}

/* the number of stretchable spaces in f */
static int fmt_spaces(struct fmt *f, int end)
{
	int i, n = 0;
	for (i = 1; i < end; i++)
		if (f->words[i].str)
			n++;
	return n;
}

/* the amount of stretchable spaces in f */
static int fmt_spacessum(struct fmt *f, int end)
{
	int i, n = 0;
	for (i = 1; i < end; i++)
		if (f->words[i].str)
			n += f->words[i].gap;
	return n;
}

/* return the next line in the buffer */
char *fmt_nextline(struct fmt *f, int *w,
		int *li, int *lI, int *ll, int *els_neg, int *els_pos)
{
	struct line *l;
	if (f->lines_head == f->lines_tail)
		return NULL;
	l = &f->lines[f->lines_tail++];
	*li = l->li;
	*lI = l->lI;
	*ll = l->ll;
	*w = l->wid;
	*els_neg = l->elsn;
	*els_pos = l->elsp;
	return sbuf_out(&l->sbuf);
}

static struct line *fmt_mkline(struct fmt *f)
{
	struct line *l;
	if (f->lines_head == f->lines_tail) {
		f->lines_head = 0;
		f->lines_tail = 0;
	}
	if (f->lines_head == f->lines_sz) {
		f->lines_sz += 256;
		f->lines = mextend(f->lines, f->lines_head,
			f->lines_sz, sizeof(f->lines[0]));
	}
	l = &f->lines[f->lines_head++];
	l->li = f->li;
	l->lI = f->lI;
	l->ll = f->ll;
	sbuf_init(&l->sbuf);
	return l;
}

static void fmt_keshideh(struct fmt *f, int beg, int end, int wid);

/* extract words from fmt struct; shrink or stretch spaces if needed */
static int fmt_extractline(struct fmt *f, int end, int str)
{
	int fmt_div, fmt_rem;
	int w, i, nspc, llen;
	struct line *l;
	if (!(l = fmt_mkline(f)))
		return 1;
	llen = FMT_LLEN(f);
	w = fmt_wordslen(f, end);
	if (str && FMT_ADJ(f) && n_j & AD_K) {
		fmt_keshideh(f, 0, end, llen - w);
		w = fmt_wordslen(f, end);
	}
	nspc = fmt_spaces(f, end);
	if (nspc && FMT_ADJ(f) && (llen < w || str)) {
		fmt_div = (llen - w) / nspc;
		fmt_rem = (llen - w) % nspc;
		if (fmt_rem < 0) {
			fmt_div--;
			fmt_rem += nspc;
		}
		for (i = 1; i < end; i++)
			if (f->words[i].str)
				f->words[i].gap += fmt_div + (fmt_rem-- > 0);
	}
	l->wid = fmt_wordscopy(f, end, &l->sbuf, &l->elsn, &l->elsp);
	return 0;
}

/* output a line break */
static int fmt_break(struct fmt *f)
{
	return fmt_extractline(f, f->words_n, 0);
}

/* output all collected words */
static int fmt_flush(struct fmt *f)
{
	if (FMT_FILL(f) && fmt_fillwords(f, 1))
		return 1;
	if (!FMT_FILL(f) && fmt_break(f))
		return 1;
	f->filled = 0;
	f->nls_sup = 0;
	f->fillreq = 0;
	return 0;
}

int fmt_newline(struct fmt *f)
{
	f->gap = 0;
	if (!FMT_FILL(f)) {
		f->nls++;
		fmt_flush(f);
		return 0;
	}
	if (f->nls && fmt_flush(f))
		return 1;
	if ((f->nls || (!f->filled && !f->words_n)) && fmt_break(f))
		return 1;
	f->nls++;
	return 0;
}

/* fill as many lines as possible; if br, put the remaining words in a line */
int fmt_fill(struct fmt *f, int br)
{
	return fmt_fillwords(f, br);
}

void fmt_space(struct fmt *fmt)
{
	fmt->gap += font_swid(dev_font(n_f), n_s, n_ss);
}

/* format the paragraph after the next word (\p) */
int fmt_fillreq(struct fmt *f)
{
	if (f->fillreq > 0)
		if (fmt_fillwords(f, 0))
			return 1;
	f->fillreq = f->words_n + 1;
	return 0;
}

static void fmt_wb2word(struct fmt *f, struct word *word, struct wb *wb,
			int hy, int str, int gap, int cost)
{
	int len = strlen(wb_buf(wb));
	word->s = malloc(len + 1);
	memcpy(word->s, wb_buf(wb), len + 1);
	word->wid = wb_wid(wb);
	word->elsn = wb->els_neg;
	word->elsp = wb->els_pos;
	word->hy = hy ? wb_hywid(wb) : 0;
	word->str = str;
	word->gap = gap;
	word->cost = cost;
	word->swid = wb_swid(wb);
}

/* find explicit break positions: dashes, \:, \%, and \~ */
static int fmt_hyphmarks(char *word, int *hyidx, int *hyins, int *hygap)
{
	char *s = word;
	char *d = NULL;
	int c, n = 0;
	int lastchar = 0;
	while ((c = escread(&s, &d)) > 0)
		;
	if (c < 0 || !strcmp(c_hc, d))
		return -1;
	while ((c = escread(&s, &d)) >= 0 && n < NHYPHSWORD) {
		if (!c) {
			if (!strcmp(c_hc, d)) {
				hyins[n] = 1;
				hyidx[n++] = s - word;
			}
			if (c_hydash(d)) {
				hyins[n] = 0;
				hyidx[n++] = s - word;
			}
			if (!strcmp(c_nb, d)) {
				hygap[n] = 1;
				hyidx[n++] = s - word;
			}
			lastchar = s - word;
		}
	}
	/* cannot break the end of a word */
	while (n > 0 && hyidx[n - 1] == lastchar)
		n--;
	return n;
}

static struct word *fmt_mkword(struct fmt *f)
{
	if (f->words_n == f->words_sz) {
		f->words_sz += 256;
		f->words = mextend(f->words, f->words_n,
			f->words_sz, sizeof(f->words[0]));
	}
	return &f->words[f->words_n++];
}

static void fmt_insertword(struct fmt *f, struct wb *wb, int gap)
{
	int hyidx[NHYPHSWORD];		/* sub-word boundaries */
	int hyins[NHYPHSWORD] = {0};	/* insert dash */
	int hygap[NHYPHSWORD] = {0};	/* stretchable no-break space */
	char *src = wb_buf(wb);
	struct wb wbc;
	char *beg;
	char *end;
	int n, i;
	int cf, cs, cm, ccd;
	n = fmt_hyphmarks(src, hyidx, hyins, hygap);
	if (n <= 0) {
		fmt_wb2word(f, fmt_mkword(f), wb, 0, 1, gap, wb_cost(wb));
		return;
	}
	/* update f->fillreq considering the new sub-words */
	if (f->fillreq == f->words_n + 1)
		f->fillreq += n;
	wb_init(&wbc);
	/* add sub-words */
	for (i = 0; i <= n; i++) {
		int ihy = i < n && hyins[i];		/* dash width */
		int istr = i == 0 || hygap[i - 1];	/* stretchable */
		int igap;				/* gap width */
		int icost;				/* hyphenation cost */
		beg = src + (i > 0 ? hyidx[i - 1] : 0);
		end = src + (i < n ? hyidx[i] : strlen(src));
		if (i < n && hygap[i])			/* remove \~ */
			end -= strlen(c_nb);
		wb_catstr(&wbc, beg, end);
		wb_fnszget(&wbc, &cf, &cs, &cm, &ccd);
		icost = i == n ? wb_cost(wb) : hygap[i] * 10000000;
		igap = i == 0 ? gap : hygap[i - 1] * wb_swid(&wbc);
		fmt_wb2word(f, fmt_mkword(f), &wbc, ihy, istr, igap, icost);
		wb_reset(&wbc);
		wb_fnszset(&wbc, cf, cs, cm, ccd);	/* restoring wbc */
	}
	wb_done(&wbc);
}

/* the amount of space necessary before the next word */
static int fmt_wordgap(struct fmt *f)
{
	int nls = f->nls || f->nls_sup;
	int swid = font_swid(dev_font(n_f), n_s, n_ss);
	if (f->eos && f->words_n)
		if ((nls && !f->gap) || (!nls && f->gap == 2 * swid))
			return swid + font_swid(dev_font(n_f), n_s, n_sss);
	return (nls && !f->gap && f->words_n) ? swid : f->gap;
}

/* insert wb into fmt */
int fmt_word(struct fmt *f, struct wb *wb)
{
	if (wb_empty(wb))
		return 0;
	if (fmt_confchanged(f))
		if (fmt_fillwords(f, 0))
			return 1;
	if (FMT_FILL(f) && f->nls && f->gap)
		if (fmt_flush(f))
			return 1;
	if (!f->words_n)		/* apply the new .l and .i */
		fmt_confupdate(f);
	f->gap = fmt_wordgap(f);
	f->eos = wb_eos(wb);
	fmt_insertword(f, wb, f->filled ? 0 : f->gap);
	f->filled = 0;
	f->nls = 0;
	f->nls_sup = 0;
	f->gap = 0;
	return 0;
}

/* insert keshideh characters */
static void fmt_keshideh(struct fmt *f, int beg, int end, int wid)
{
	struct wb wb;
	int kw, i = 0, c = 0;
	struct word *w;
	int cnt = 0;
	do {
		cnt = 0;
		for (c = 0; c < 2; c++) {
			for (i = end - 1 - c; i >= beg; i -= 2) {
				w = &f->words[i];
				wb_init(&wb);
				kw = wb_keshideh(w->s, &wb, wid);
				if (kw > 0) {
					free(w->s);
					w->s = malloc(strlen(wb_buf(&wb)) + 1);
					strcpy(w->s, wb_buf(&wb));
					w->wid = wb_wid(&wb);
					wid -= kw;
					cnt++;
				}
				wb_done(&wb);
			}
		}
	} while (cnt);
}

struct fmt *fmt_alloc(void)
{
	struct fmt *fmt = malloc(sizeof(*fmt));
	memset(fmt, 0, sizeof(*fmt));
	return fmt;
}

void fmt_free(struct fmt *fmt)
{
	int i;
	free(fmt->lines);
	for (i = 0; i < fmt->words_n; i++)
		free(fmt->words[i].s);
	free(fmt->words);
	free(fmt);
}

int fmt_wid(struct fmt *fmt)
{
	return fmt_wordslen(fmt, fmt->words_n) + fmt_wordgap(fmt);
}

int fmt_morewords(struct fmt *fmt)
{
	return fmt_morelines(fmt) || fmt->words_n;
}

int fmt_morelines(struct fmt *fmt)
{
	return fmt->lines_head != fmt->lines_tail;
}

/* suppress the last newline */
void fmt_suppressnl(struct fmt *fmt)
{
	if (fmt->nls) {
		fmt->nls--;
		fmt->nls_sup = 1;
	}
}

/* estimated number of lines until traps or the end of a page */
static int fmt_safelines(void)
{
	int lnht = MAX(1, n_L) * n_v;
	return n_v > 0 ? (f_nexttrap() + lnht - 1) / lnht : 1000;
}

static int fmta_fill(struct word *words, int words_n, int *out, int out_n,
	int llen, int all, int br, int nohy);

/*
 * Fill the collected words.
 *
 * It return 0 unless the call must be repeated for the following reasons.
 *
 * + Line break is necessary (br is given), but reached a trap.
 * + Line break is necessary (br is given), but fillreq (\p) specified.
 * + More lines can be extracted; stopped at a trap.
 * + There is no more room for buffering new lines (no longer happens).
 *
 * It fills at least one line, if the distance to the next trap is zero.
 */
static int fmt_fillwords(struct fmt *f, int br)
{
	int nrem;	/* the number of lines until a trap */
	int nreq;	/* the number of lines to emit */
	int llen;	/* total length, taking shrinkable spaces into account */
	int nohy = -1;	/* no hyphenation allowed on this line */
	int out[64];	/* number of words in filled lines */
	int out_n;	/* number of lines filled */
	int fillreq;
	int i, n;
	if (!FMT_FILL(f) || !f->words_n)
		return 0;
	llen = fmt_wordslen(f, f->words_n) -
		fmt_spacessum(f, f->words_n) * n_ssh / 100;
	fillreq = f->fillreq > 0 && f->fillreq <= f->words_n ? f->fillreq : -1;
	/* not enough words to fill */
	if (!br && fillreq <= 0 && llen <= FMT_LLEN(f))
		return 0;
	/* lines until a trap or page end */
	nreq = nrem = fmt_safelines() - fmt_nlines(f);
	/* enough lines are collected already */
	if (nreq <= 0 && fmt_safelines() > 0)
		return 1;
	if (fmt_safelines() == 0)
		nrem = nreq = 1;
	/* if line settings are changed, output a single line */
	if (fmt_confchanged(f))
		nreq = 1;
	if (nrem > 0 && n_hy & HY_LAST)
		nohy = nrem;
	/* execute the filling algorithm */
	out_n = fmta_fill(f->words, fillreq > 0 ? fillreq : f->words_n,
		out, MIN(LEN(out), nrem), FMT_LLEN(f), fillreq > 0, br, nohy);
	/* extract filled lines */
	n = nreq > 0 ? MIN(nreq, out_n) : MIN(1, out_n);
	for (i = 0; i < n; i++) {
		int cur = out[i];
		if (fmt_extractline(f, cur, cur != f->words_n || !br || cur == fillreq))
			break;
		if (f->words_n)
			f->words[0].gap = 0;
	}
	f->filled = i && !f->words_n;
	if (f->words_n)		/* apply the new .l and .i */
		fmt_confupdate(f);
	return i < out_n || fillreq > 0 || (br && f->words_n) ||
		(n == nreq && fmt_wordslen(f, f->words_n) > FMT_LLEN(f));
}

/* Line Filling Algorithm */

/* approximate 8 * sqrt(cost) */
static long scaledown(long cost)
{
	long ret = 0;
	int i;
	for (i = 0; i < 14; i++)
		ret += ((cost >> (i * 2)) & 3) << (i + 3);
	return ret < (1 << 13) ? ret : (1 << 13);
}

/* the cost of putting lwid words in a line of length llen */
static long FMT_COST(int llen, int lwid, int swid, int nspc)
{
	/* the ratio that the stretchable spaces of the line should be spread */
	long ratio = labs((llen - lwid) * 100l / (swid ? swid : 1));
	/* ratio too large; scaling it down */
	if (ratio > 4000)
		ratio = 4000 + scaledown(ratio - 4000);
	/* assigning a cost of 100 to each space stretching 100 percent */
	return ratio * ratio / 100l * (nspc ? nspc : 1);
}

/* the number of hyphenations in consecutive lines ending at pos */
static int fmt_hydepth(struct word *words, int *best_pos, int pos)
{
	int n = 0;
	while (pos > 0 && words[pos - 1].hy && ++n < 5)
		pos = best_pos[pos];
	return n;
}

/* return the first word of the last filled line */
static int fmta_last(struct word *words, int words_n, int llen, int br,
		int nohy, long *best, int *best_pos, int *best_dep)
{
	int last = -1;
	long cost, last_cost = 0;
	int lwid = 0;		/* current line length */
	int swid = 0;		/* amount of stretchable spaces */
	int nspc = 0;		/* number of stretchable spaces */
	int pos = words_n;
	int i = pos - 1;
	if (words[i].hy)	/* the last word is hyphenated */
		lwid += words[i].hy;
	while (i >= 0) {
		lwid += words[i].wid;
		if (i + 1 < pos)
			lwid += words[i + 1].gap;
		if (i + 1 < pos && words[i + 1].str) {
			swid += words[i + 1].gap;
			nspc++;
		}
		if (lwid > llen + swid * n_ssh / 100)
			break;
		cost = best[i];
		/* the cost of formatting short lines; should prevent widows */
		if (br && n_pmll && lwid < llen * n_pmll / 100) {
			int pmll = llen * n_pmll / 100;
			cost += (long) n_pmllcost * (pmll - lwid) / pmll;
		}
		if (last < 0 || cost < last_cost) {
			last = i;
			last_cost = cost;
		}
		i--;
	}
	return last >= 0 ? last : pos - 1;
}

/* return the last filled word */
static int fmta_break(int *best_pos, int pos, int *out, int nreq)
{
	int beg = pos > 0 ? best_pos[pos] : 0;
	int idx = beg > 0 ? fmta_break(best_pos, beg, out, nreq) : 0;
	if (idx < nreq)
		out[idx] = pos - beg;
	return pos > 0 ? idx + 1 : 0;
}

/*
 * Fill the given words and return the number of filled lines.
 *
 * Parameters:
 * + nreq: the maximum number of lines to return (the size of the out array).
 * + all: put all words in filled lines.
 * + br: the last line is not filled.
 * + nohy: the index of the line that must not be hyphenated.
 * + out: the number of words in each of the filled output lines.
 */
static int fmta_fill(struct word *words, int words_n, int *out, int out_n,
	int llen, int all, int br, int nohy)
{
	int ssh0 = n_ssh, hlm0 = n_hlm;
	int hycost[] = {n_hycost, n_hycost + n_hycost2, n_hycost + n_hycost2 + n_hycost3};
	long *best = malloc((words_n + 1) * sizeof(best[0]));
	int *best_pos = malloc((words_n + 1) * sizeof(best_pos[0]));
	int *best_dep = malloc((words_n + 1) * sizeof(best_dep[0]));
	int last = words_n;	/* beginning of the last line */
	int cnt;		/* filled line count */
	int i, pos;
	for (i = 0; i < words_n + 1; i++)
		best_pos[i] = -1;
	for (pos = 1; pos <= words_n; pos++) {
		int lwid = 0;		/* current line length */
		int swid = 0;		/* amount of stretchable spaces */
		int nspc = 0;		/* number of stretchable spaces */
		int dwid = 0;		/* equal to swid, unless swid is zero */
		int hyphenated = words[pos - 1].hy != 0;
		lwid = words[pos - 1].hy;	/* non-zero if the last word is hyphenated */
		i = pos - 1;
		while (i >= 0) {
			long cur;
			lwid += words[i].wid;
			if (i + 1 < pos)
				lwid += words[i + 1].gap;
			if (i + 1 < pos && words[i + 1].str) {
				swid += words[i + 1].gap;
				nspc++;
			}
			if (lwid > llen + swid * ssh0 / 100 && i + 1 < pos)
				break;
			dwid = swid;
			if (!dwid && i > 0)	/* no stretchable spaces */
				dwid = words[i - 1].swid;
			cur = best[i] + FMT_COST(llen, lwid, dwid, nspc);
			if (hyphenated && best_dep[i] + 1 == nohy)
					cur += 10000000;
			if (hyphenated) {
				int dep = fmt_hydepth(words, best_pos, i);
				if (hlm0 <= 0 || dep < hlm0)
					cur += hycost[MIN(dep, LEN(hycost) - 1)];
				else
					cur += 10000000;
			}
			if (best_pos[pos] < 0 || cur + words[pos - 1].cost < best[pos]) {
				best_pos[pos] = i;
				best_dep[pos] = best_dep[i] + 1;
				best[pos] = cur + words[pos - 1].cost;
			}
			i--;
		}
	}
	if (!all)
		last = fmta_last(words, words_n, llen, br, nohy, best, best_pos, best_dep);
	cnt = fmta_break(best_pos, last, out, out_n);
	if (!all && cnt < out_n && br && last < words_n)
		out[cnt++] = words_n - last;
	free(best);
	free(best_dep);
	free(best_pos);
	return MIN(out_n, cnt);
}
