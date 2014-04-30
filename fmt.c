/*
 * line formatting buffer for line adjustment and hyphenation
 *
 * The line formatting buffer does two main functions: breaking
 * words into lines (possibly after hyphenating some of them), and, if
 * requested, adjusting the space between words in a line.  In this
 * file the first step is referred to as filling.
 *
 * Inputs are specified via these functions:
 * + fmt_word(): for appending space-separated words.
 * + fmt_space(): for appending spaces.
 * + fmt_newline(): for appending new lines.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define FMT_LLEN(f)	MAX(0, (f)->ll - (f)->li)
#define FMT_FILL(f)	(!n_ce && n_u)
#define FMT_ADJ(f)	(n_u && !n_na && !n_ce && (n_j & AD_B) == AD_B)
#define FMT_SWID(f)	(spacewid(n_f, n_s))

struct word {
	char *s;
	int wid;	/* word's width */
	int elsn, elsp;	/* els_neg and els_pos */
	int gap;	/* the space before this word */
	int hy;		/* hyphen width if inserted after this word */
	int str;	/* does the spece before it stretch */
};

struct line {
	struct sbuf sbuf;
	int wid, li, ll;
	int elsn, elsp;
};

struct fmt {
	/* queued words */
	struct word words[NWORDS];
	int nwords;
	/* queued lines */
	struct line lines[NLINES];
	int l_head, l_tail;
	/* for paragraph adjustment */
	long best[NWORDS];
	int best_pos[NWORDS];
	/* current line */
	int gap;		/* space before the next word */
	int nls;		/* newlines before the next word */
	int li, ll;		/* current line indentation and length */
	int filled;		/* filled all words in the last fmt_fill() */
	int eos;		/* last word ends a sentence */
};

/* .ll, .in and .ti are delayed until the partial line is output */
static void fmt_confupdate(struct fmt *f)
{
	f->ll = n_l;
	f->li = n_ti >= 0 ? n_ti : n_i;
	n_ti = -1;
}

static int fmt_confchanged(struct fmt *f)
{
	return f->ll != n_l || f->li != (n_ti >= 0 ? n_ti : n_i);
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

/* the total width of the specified words in f->words[] */
static int fmt_wordslen(struct fmt *f, int beg, int end)
{
	int i, w = 0;
	for (i = beg; i < end; i++)
		w += f->words[i].wid + f->words[i].gap;
	return beg < end ? w + f->words[end - 1].hy : 0;
}

/* the number stretchable spaces in f */
static int fmt_spaces(struct fmt *f, int beg, int end)
{
	int i, n = 0;
	for (i = beg + 1; i < end; i++)
		if (f->words[i].str)
			n++;
	return n;
}

/* return the next line in the buffer */
int fmt_nextline(struct fmt *f, struct sbuf *sbuf, int *w,
		int *li, int *ll, int *els_neg, int *els_pos)
{
	struct line *l;
	l = &f->lines[f->l_tail];
	if (f->l_head == f->l_tail)
		return 1;
	*li = l->li;
	*ll = l->ll;
	*w = l->wid;
	*els_neg = l->elsn;
	*els_pos = l->elsp;
	sbuf_append(sbuf, sbuf_buf(&l->sbuf));
	sbuf_done(&l->sbuf);
	f->l_tail = (f->l_tail + 1) % NLINES;
	return 0;
}

static struct line *fmt_mkline(struct fmt *f)
{
	struct line *l = &f->lines[f->l_head];
	if ((f->l_head + 1) % NLINES == f->l_tail)
		return NULL;
	f->l_head = (f->l_head + 1) % NLINES;
	l->li = f->li;
	l->ll = f->ll;
	sbuf_init(&l->sbuf);
	return l;
}

static int fmt_sp(struct fmt *f)
{
	struct line *l;
	fmt_fill(f, 0);
	l = fmt_mkline(f);
	if (!l)
		return 1;
	f->filled = 0;
	f->nls--;
	l->wid = fmt_wordscopy(f, 0, f->nwords, &l->sbuf, &l->elsn, &l->elsp);
	f->nwords = 0;
	return 0;
}

void fmt_br(struct fmt *f)
{
	fmt_fill(f, 0);
	f->filled = 0;
	if (f->nwords)
		fmt_sp(f);
}

void fmt_space(struct fmt *fmt)
{
	fmt->gap += FMT_SWID(fmt);
}

void fmt_newline(struct fmt *f)
{
	f->nls++;
	f->gap = 0;
	if (!FMT_FILL(f)) {
		fmt_sp(f);
		return;
	}
	if (f->nls == 1 && !f->filled && !f->nwords)
		fmt_sp(f);
	if (f->nls > 1) {
		if (!f->filled)
			fmt_sp(f);
		fmt_sp(f);
	}
}

/* copy word buffer wb in fmt->words[i] */
static void fmt_insertword(struct fmt *f, struct wb *wb, int gap)
{
	int hyidx[NHYPHS];
	int hywid[NHYPHS];
	int hydash[NHYPHS];
	struct word *w;
	char *beg, *end;
	char *src = wb_buf(wb);
	int n, i;
	n = wb_hyph(src, hyidx, hywid, hydash, n_hy);
	for (i = 0; i <= n; i++) {
		w = &f->words[f->nwords++];
		beg = src + (i > 0 ? hyidx[i - 1] : 0);
		end = src + (i < n ? hyidx[i] : strlen(src));
		w->s = malloc(end - beg + 1);
		memcpy(w->s, beg, end - beg);
		w->s[end - beg] = '\0';
		if (n) {
			w->wid = (i < n ? hywid[i] : wb_wid(wb)) -
				(i > 0 ? hywid[i - 1] : 0);
		} else {
			w->wid = wb_wid(wb);
		}
		w->elsn = wb->els_neg;
		w->elsp = wb->els_pos;
		w->hy = i < n ? hydash[i] : 0;
		w->str = i == 0;
		w->gap = i == 0 ? gap : 0;
	}
}

/* insert wb into fmt */
void fmt_word(struct fmt *f, struct wb *wb)
{
	if (f->nwords == NWORDS || fmt_confchanged(f))
		fmt_fill(f, 0);
	if (wb_empty(wb) || f->nwords == NWORDS)
		return;
	if (FMT_FILL(f) && f->nls && f->gap)
		fmt_sp(f);
	if (!f->nwords)		/* apply the new .l and .i */
		fmt_confupdate(f);
	if (f->nls && !f->gap && f->nwords >= 1)
		f->gap = (f->nwords && f->eos) ? FMT_SWID(f) * 2 : FMT_SWID(f);
	f->eos = wb_eos(wb);
	fmt_insertword(f, wb, f->filled ? 0 : f->gap);
	f->filled = 0;
	f->nls = 0;
	f->gap = 0;
}

/* assuming an empty line has cost 10000; take care of integer overflow */
#define POW2(x)				((x) * (x))
#define FMT_COST(lwid, llen, pen)	(POW2(((llen) - (lwid)) * 1000l / (llen)) / 100l + (pen) * 10l)

/* the cost of putting a line break before word pos */
static long fmt_findcost(struct fmt *f, int pos)
{
	int i, pen = 0;
	long cur;
	int lwid = 0;
	int llen = FMT_LLEN(f);
	if (pos <= 0)
		return 0;
	if (f->best_pos[pos] >= 0)
		return f->best[pos];
	i = pos - 1;
	lwid = 0;
	if (f->words[i].hy)	/* the last word is hyphenated */
		lwid += f->words[i].hy;
	if (f->words[i].hy)
		pen = n_hyp;
	while (i >= 0) {
		lwid += f->words[i].wid;
		if (i + 1 < pos)
			lwid += f->words[i + 1].gap;
		if (lwid > llen && pos - i > 1)
			break;
		cur = fmt_findcost(f, i) + FMT_COST(lwid, llen, pen);
		if (f->best_pos[pos] < 0 || cur < f->best[pos]) {
			f->best_pos[pos] = i;
			f->best[pos] = cur;
		}
		i--;
	}
	return f->best[pos];
}

static int fmt_bestpos(struct fmt *f, int pos)
{
	fmt_findcost(f, pos);
	return MAX(0, f->best_pos[pos]);
}

/* return the last filled word */
static int fmt_breakparagraph(struct fmt *f, int pos, int all)
{
	int i;
	long best = 0;
	int best_i = -1;
	int llen = FMT_LLEN(f);
	int lwid = 0;
	if (all || (pos > 0 && f->words[pos - 1].wid >= llen)) {
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
		if (lwid > llen && pos - i > 1)
			break;
		if (best_i < 0 || fmt_findcost(f, i) < best) {
			best_i = i;
			best = fmt_findcost(f, i);
		}
		i--;
	}
	return best_i;
}

/* break f->words[0..end] into lines according to fmt_bestpos() */
static int fmt_break(struct fmt *f, int end)
{
	int llen, fmt_div, fmt_rem, beg;
	int w, i, nspc;
	struct line *l;
	int ret = 0;
	beg = fmt_bestpos(f, end);
	if (beg > 0)
		ret += fmt_break(f, beg);
	l = fmt_mkline(f);
	if (!l)
		return ret;
	llen = FMT_LLEN(f);
	f->words[beg].gap = 0;
	w = fmt_wordslen(f, beg, end);
	nspc = fmt_spaces(f, beg, end);
	if (FMT_ADJ(f) && nspc) {
		fmt_div = (llen - w) / nspc;
		fmt_rem = (llen - w) % nspc;
		for (i = beg + 1; i < end; i++)
			if (f->words[i].str)
				f->words[i].gap += fmt_div + (fmt_rem-- > 0);
	}
	l->wid = fmt_wordscopy(f, beg, end, &l->sbuf, &l->elsn, &l->elsp);
	if (beg > 0)
		fmt_confupdate(f);
	return ret + (end - beg);
}

int fmt_fill(struct fmt *f, int all)
{
	int end, n, i;
	if (!FMT_FILL(f))
		return 0;
	/* not enough words to fill */
	if (!all && fmt_wordslen(f, 0, f->nwords) <= FMT_LLEN(f))
		return 0;
	/* resetting positions */
	for (i = 0; i < f->nwords + 1; i++)
		f->best_pos[i] = -1;
	end = fmt_breakparagraph(f, f->nwords, all);
	/* recursively add lines */
	n = fmt_break(f, end);
	f->nwords -= n;
	fmt_movewords(f, 0, n, f->nwords);
	f->filled = n && !f->nwords;
	if (f->nwords)
		f->words[0].gap = 0;
	if (f->nwords)		/* apply the new .l and .i */
		fmt_confupdate(f);
	return n != end;
}

struct fmt *fmt_alloc(void)
{
	struct fmt *fmt = malloc(sizeof(*fmt));
	memset(fmt, 0, sizeof(*fmt));
	return fmt;
}

void fmt_free(struct fmt *fmt)
{
	free(fmt);
}

int fmt_wid(struct fmt *fmt)
{
	return fmt_wordslen(fmt, 0, fmt->nwords) +
		(fmt->nls ? FMT_SWID(fmt) : fmt->gap);
}

int fmt_morewords(struct fmt *fmt)
{
	return fmt_morelines(fmt) || fmt->nwords;
}

int fmt_morelines(struct fmt *fmt)
{
	return fmt->l_head != fmt->l_tail;
}
