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

/* move words inside an fmt struct */
static void fmt_movewords(struct fmt *a, int dst, int src, int len)
{
	memmove(a->words + dst, a->words + src, len * sizeof(a->words[0]));
}

/* move words from the buffer to s */
static int fmt_wordscopy(struct fmt *f, int beg, int end,
		struct sbuf *s, int *els_neg, int *els_pos)
{
	struct word *wcur;
	int w = 0;
	int i;
	*els_neg = 0;
	*els_pos = 0;
	for (i = beg; i < end; i++) {
		wcur = &f->words[i];
		sbuf_printf(s, "%ch'%du'", c_ec, wcur->gap);
		sbuf_append(s, wcur->s);
		w += wcur->wid + wcur->gap;
		if (wcur->elsn < *els_neg)
			*els_neg = wcur->elsn;
		if (wcur->elsp > *els_pos)
			*els_pos = wcur->elsp;
		free(wcur->s);
	}
	if (beg < end) {
		wcur = &f->words[end - 1];
		if (wcur->hy)
			sbuf_append(s, "\\(hy");
		w += wcur->hy;
	}
	return w;
}

static int fmt_nlines(struct fmt *f)
{
	return f->lines_head - f->lines_tail;
}

/* the total width of the specified words in f->words[] */
static int fmt_wordslen(struct fmt *f, int beg, int end)
{
	int i, w = 0;
	for (i = beg; i < end; i++)
		w += f->words[i].wid + f->words[i].gap;
	return beg < end ? w + f->words[end - 1].hy : 0;
}

/* the number of stretchable spaces in f */
static int fmt_spaces(struct fmt *f, int beg, int end)
{
	int i, n = 0;
	for (i = beg + 1; i < end; i++)
		if (f->words[i].str)
			n++;
	return n;
}

/* the amount of stretchable spaces in f */
static int fmt_spacessum(struct fmt *f, int beg, int end)
{
	int i, n = 0;
	for (i = beg + 1; i < end; i++)
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

/* extract words from beg to end; shrink or stretch spaces if needed */
static int fmt_extractline(struct fmt *f, int beg, int end, int str)
{
	int fmt_div, fmt_rem;
	int w, i, nspc, llen;
	struct line *l;
	if (!(l = fmt_mkline(f)))
		return 1;
	llen = FMT_LLEN(f);
	w = fmt_wordslen(f, beg, end);
	if (str && FMT_ADJ(f) && n_j & AD_K) {
		fmt_keshideh(f, beg, end, llen - w);
		w = fmt_wordslen(f, beg, end);
	}
	nspc = fmt_spaces(f, beg, end);
	if (nspc && FMT_ADJ(f) && (llen < w || str)) {
		fmt_div = (llen - w) / nspc;
		fmt_rem = (llen - w) % nspc;
		if (fmt_rem < 0) {
			fmt_div--;
			fmt_rem += nspc;
		}
		for (i = beg + 1; i < end; i++)
			if (f->words[i].str)
				f->words[i].gap += fmt_div + (fmt_rem-- > 0);
	}
	l->wid = fmt_wordscopy(f, beg, end, &l->sbuf, &l->elsn, &l->elsp);
	return 0;
}

static int fmt_sp(struct fmt *f)
{
	if (fmt_fillwords(f, 1))
		return 1;
	if (fmt_extractline(f, 0, f->words_n, 0))
		return 1;
	f->filled = 0;
	f->nls--;
	f->nls_sup = 0;
	f->words_n = 0;
	f->fillreq = 0;
	return 0;
}

/* fill as many lines as possible; if br, put the remaining words in a line */
int fmt_fill(struct fmt *f, int br)
{
	if (fmt_fillwords(f, br))
		return 1;
	if (br) {
		f->filled = 0;
		if (f->words_n)
			if (fmt_sp(f))
				return 1;
	}
	return 0;
}

void fmt_space(struct fmt *fmt)
{
	fmt->gap += font_swid(dev_font(n_f), n_s, n_ss);
}

int fmt_newline(struct fmt *f)
{
	f->gap = 0;
	if (!FMT_FILL(f)) {
		f->nls++;
		fmt_sp(f);
		return 0;
	}
	if (f->nls >= 1)
		if (fmt_sp(f))
			return 1;
	if (f->nls == 0 && !f->filled && !f->words_n)
		fmt_sp(f);
	f->nls++;
	return 0;
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
	/* not used */
	(void) f;
	word->s = xmalloc(len + 1);
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
		icost = i == n ? wb_cost(&wbc) : hygap[i] * 10000000;
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
		if (fmt_sp(f))
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
					w->s = xmalloc(strlen(wb_buf(&wb)) + 1);
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
static int fmt_hydepth(struct fmt *f, int pos)
{
	int n = 0;
	while (pos > 0 && f->words[pos - 1].hy && ++n < 5)
		pos = f->best_pos[pos];
	return n;
}

static long hycost(int depth)
{
	if (n_hlm > 0 && depth > n_hlm)
		return 10000000;
	if (depth >= 3)
		return n_hycost + n_hycost2 + n_hycost3;
	if (depth == 2)
		return n_hycost + n_hycost2;
	return depth ? n_hycost : 0;
}

/* the cost of putting a line break before word pos */
static long fmt_findcost(struct fmt *f, int pos)
{
	int i, hyphenated;
	long cur;
	int llen = MAX(1, FMT_LLEN(f));
	int lwid = 0;		/* current line length */
	int swid = 0;		/* amount of stretchable spaces */
	int nspc = 0;		/* number of stretchable spaces */
	int dwid = 0;		/* equal to swid, unless swid is zero */
	if (pos <= 0)
		return 0;
	if (f->best_pos[pos] >= 0)
		return f->best[pos] + f->words[pos - 1].cost;
	lwid = f->words[pos - 1].hy;	/* non-zero if the last word is hyphenated */
	hyphenated = f->words[pos - 1].hy != 0;
	i = pos - 1;
	while (i >= 0) {
		lwid += f->words[i].wid;
		if (i + 1 < pos)
			lwid += f->words[i + 1].gap;
		if (i + 1 < pos && f->words[i + 1].str) {
			swid += f->words[i + 1].gap;
			nspc++;
		}
		if (lwid > llen + swid * n_ssh / 100 && i + 1 < pos)
			break;
		dwid = swid;
		if (!dwid && i > 0)	/* no stretchable spaces */
			dwid = f->words[i - 1].swid;
		cur = fmt_findcost(f, i) + FMT_COST(llen, lwid, dwid, nspc);
		if (hyphenated)
			cur += hycost(1 + fmt_hydepth(f, i));
		if (f->best_pos[pos] < 0 || cur < f->best[pos]) {
			f->best_pos[pos] = i;
			f->best_dep[pos] = f->best_dep[i] + 1;
			f->best[pos] = cur;
		}
		i--;
	}
	return f->best[pos] + f->words[pos - 1].cost;
}

static int fmt_bestpos(struct fmt *f, int pos)
{
	fmt_findcost(f, pos);
	return MAX(0, f->best_pos[pos]);
}

static int fmt_bestdep(struct fmt *f, int pos)
{
	fmt_findcost(f, pos);
	return MAX(0, f->best_dep[pos]);
}

/* return the last filled word */
static int fmt_breakparagraph(struct fmt *f, int pos, int br)
{
	int i;
	int best = -1;
	long cost, best_cost = 0;
	int llen = FMT_LLEN(f);
	int lwid = 0;		/* current line length */
	int swid = 0;		/* amount of stretchable spaces */
	int nspc = 0;		/* number of stretchable spaces */
	/* not used */
	(void) swid;
	if (f->fillreq > 0 && f->fillreq <= f->words_n) {
		fmt_findcost(f, f->fillreq);
		return f->fillreq;
	}
	if (pos > 0 && f->words[pos - 1].wid >= llen) {
		fmt_findcost(f, pos);
		return pos;
	}
	i = pos - 1;
	lwid = 0;
	if (f->words[i].hy)	/* the last word is hyphenated */
		lwid += f->words[i].hy;
	while (i >= 0) {
		lwid += f->words[i].wid;
		if (i + 1 < pos)
			lwid += f->words[i + 1].gap;
		if (i + 1 < pos && f->words[i + 1].str) {
			swid += f->words[i + 1].gap;
			nspc++;
		}
		if (lwid > llen && i + 1 < pos)
			break;
		cost = fmt_findcost(f, i);
		/* the cost of formatting short lines; should prevent widows */
		if (br && n_pmll && lwid < llen * n_pmll / 100) {
			int pmll = llen * n_pmll / 100;
			cost += (long) n_pmllcost * (pmll - lwid) / pmll;
		}
		if (best < 0 || cost < best_cost) {
			best = i;
			best_cost = cost;
		}
		i--;
	}
	return best;
}

/* extract the first nreq formatted lines before the word at pos */
static int fmt_head(struct fmt *f, int nreq, int pos, int nohy)
{
	int best = pos;		/* best line break for nreq-th line */
	int prev, next;		/* best line breaks without hyphenation */
	if (nreq <= 0 || fmt_bestdep(f, pos) < nreq)
		return pos;
	/* finding the optimal line break for nreq-th line */
	while (best > 0 && fmt_bestdep(f, best) > nreq)
		best = fmt_bestpos(f, best);
	prev = best;
	next = best;
	if (!nohy)
		return best;
	/* finding closest line breaks without hyphenation */
	while (prev > 1 && f->words[prev - 1].hy &&
			fmt_bestdep(f, prev - 1) == nreq)
		prev--;
	while (next < pos && f->words[next - 1].hy &&
			fmt_bestdep(f, next) == nreq)
		next++;
	/* choosing the best of them */
	if (!f->words[prev - 1].hy && !f->words[next - 1].hy)
		return fmt_findcost(f, prev) <= fmt_findcost(f, next) ? prev : next;
	if (!f->words[prev - 1].hy)
		return prev;
	if (!f->words[next - 1].hy)
		return next;
	return best;
}

/* break f->words[0..end] into lines according to fmt_bestpos() */
static int fmt_break(struct fmt *f, int end)
{
	int beg, ret = 0;
	beg = fmt_bestpos(f, end);
	if (beg > 0)
		ret += fmt_break(f, beg);
	f->words[beg].gap = 0;
	if (fmt_extractline(f, beg, end, 1))
		return ret;
	if (beg > 0)
		fmt_confupdate(f);
	return ret + (end - beg);
}

/* estimated number of lines until traps or the end of a page */
static int fmt_safelines(void)
{
	int lnht = MAX(1, n_L) * n_v;
	return n_v > 0 ? (f_nexttrap() + lnht - 1) / lnht : 1000;
}

/* fill the words collected in the buffer */
static int fmt_fillwords(struct fmt *f, int br)
{
	int nreq;	/* the number of lines until a trap */
	int end;	/* the final line ends before this word */
	int end_head;	/* like end, but only the first nreq lines included */
	int head = 0;	/* only nreq first lines have been formatted */
	int llen;	/* line length, taking shrinkable spaces into account */
	int n, i;
	if (!FMT_FILL(f))
		return 0;
	llen = fmt_wordslen(f, 0, f->words_n) -
		fmt_spacessum(f, 0, f->words_n) * n_ssh / 100;
	/* not enough words to fill */
	if ((f->fillreq <= 0 || f->words_n < f->fillreq) && llen <= FMT_LLEN(f))
		return 0;
	/* lines until a trap or page end */
	nreq = fmt_safelines();
	/* if line settings are changed, output a single line */
	if (fmt_confchanged(f))
		nreq = 1;
	/* enough lines are collected already */
	if (nreq > 0 && nreq <= fmt_nlines(f))
		return 1;
	/* resetting positions */
	f->best = malloc((f->words_n + 1) * sizeof(f->best[0]));
	f->best_pos = malloc((f->words_n + 1) * sizeof(f->best_pos[0]));
	f->best_dep = malloc((f->words_n + 1) * sizeof(f->best_dep[0]));
	memset(f->best, 0, (f->words_n + 1) * sizeof(f->best[0]));
	memset(f->best_dep, 0, (f->words_n + 1) * sizeof(f->best_dep[0]));
	for (i = 0; i < f->words_n + 1; i++)
		f->best_pos[i] = -1;
	end = fmt_breakparagraph(f, f->words_n, br);
	if (nreq > 0) {
		int nohy = 0;	/* do not hyphenate the last line */
		if (n_hy & HY_LAST && nreq == fmt_nlines(f))
			nohy = 1;
		end_head = fmt_head(f, nreq - fmt_nlines(f), end, nohy);
		head = end_head < end;
		end = end_head;
	}
	/* recursively add lines */
	n = end > 0 ? fmt_break(f, end) : 0;
	f->words_n -= n;
	f->fillreq -= n;
	fmt_movewords(f, 0, n, f->words_n);
	f->filled = n && !f->words_n;
	if (f->words_n)
		f->words[0].gap = 0;
	if (f->words_n)		/* apply the new .l and .i */
		fmt_confupdate(f);
	free(f->best);
	free(f->best_pos);
	free(f->best_dep);
	f->best = NULL;
	f->best_pos = NULL;
	f->best_dep = NULL;
	return head || n != end;
}

struct fmt *fmt_alloc(void)
{
	struct fmt *fmt = xmalloc(sizeof(*fmt));
	memset(fmt, 0, sizeof(*fmt));
	return fmt;
}

void fmt_free(struct fmt *fmt)
{
	free(fmt->lines);
	free(fmt->words);
	free(fmt);
}

int fmt_wid(struct fmt *fmt)
{
	return fmt_wordslen(fmt, 0, fmt->words_n) + fmt_wordgap(fmt);
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
