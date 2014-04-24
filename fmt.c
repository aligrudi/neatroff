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
	int wid;
	int elsn, elsp;
	int gap;
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

/* move words inside an fmt struct */
static void fmt_movewords(struct fmt *a, int dst, int src, int len)
{
	memmove(a->words + dst, a->words + src, len * sizeof(a->words[0]));
}

static char *fmt_strdup(char *s)
{
	int l = strlen(s);
	char *r = malloc(l + 1);
	memcpy(r, s, l + 1);
	return r;
}

/* copy word buffer wb in fmt->words[i] */
static void fmt_insertword(struct fmt *f, int i, struct wb *wb, int gap)
{
	struct word *w = &f->words[i];
	w->s = fmt_strdup(wb_buf(wb));
	w->wid = wb_wid(wb);
	w->elsn = wb->els_neg;
	w->elsp = wb->els_pos;
	w->gap = gap;
}

/* the total width of the first n words in f->words[] */
static int fmt_wordslen(struct fmt *f, int n)
{
	int i, w = 0;
	for (i = 0; i < n; i++)
		w += f->words[i].wid + f->words[i].gap;
	return w;
}

/* select as many words as can be fit in llen */
static int fmt_linefit(struct fmt *f, int llen)
{
	int i, w = 0;
	for (i = 0; i < f->nwords; i++) {
		w += f->words[i].wid + f->words[i].gap;
		if (w > llen)
			return i;
	}
	return i;
}

/* move n words from the buffer to s */
static int fmt_move(struct fmt *f, int n, struct sbuf *s, int *els_neg, int *els_pos)
{
	struct word *wcur;
	int w = 0;
	int i;
	*els_neg = 0;
	*els_pos = 0;
	for (i = 0; i < n; i++) {
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
	if (!n)
		return 0;
	f->nwords -= n;
	fmt_movewords(f, 0, n, f->nwords);
	if (f->nwords)		/* apply the new .l and .i */
		fmt_confupdate(f);
	return w;
}

/* try to hyphenate the n-th word */
static void fmt_hyph(struct fmt *f, int n, int w, int hyph)
{
	struct wb w1, w2;
	int flg = hyph | (n ? 0 : HY_ANY);
	wb_init(&w1);
	wb_init(&w2);
	if (!wb_hyph(f->words[n].s, w, &w1, &w2, flg)) {
		fmt_movewords(f, n + 2, n + 1, f->nwords - n);
		free(f->words[n].s);
		fmt_insertword(f, n, &w1, f->words[n].gap);
		fmt_insertword(f, n + 1, &w2, 0);
		f->nwords++;
	}
	wb_done(&w1);
	wb_done(&w2);
}

/* estimated number of lines until traps or the end of a page */
static int ren_safelines(void)
{
	return f_nexttrap() / (MAX(1, n_L) * n_v);
}

static int fmt_nlines(struct fmt *f)
{
	if (f->l_tail <= f->l_head)
		return f->l_head - f->l_tail;
	return NLINES - f->l_tail + f->l_head;
}

int fmt_fill(struct fmt *f, int all)
{
	int llen, fmt_div, fmt_rem;
	int w = 0;
	int i, n;
	struct line *l;
	int hyph = n_hy;
	if (!FMT_FILL(f))
		return 0;
	while (f->nwords) {
		l = &f->lines[f->l_head];
		llen = FMT_LLEN(f);
		if ((f->l_head + 1) % NLINES == f->l_tail)
			return 1;
		l->li = f->li;
		l->ll = f->ll;
		n = fmt_linefit(f, llen);
		if (n == f->nwords && !all)
			break;
		if ((n_hy & HY_LAST) && ren_safelines() < 2 + fmt_nlines(f))
			hyph = 0;	/* disable hyphenation for final lines */
		if (n < f->nwords)
			fmt_hyph(f, n, llen - fmt_wordslen(f, n) -
					f->words[n].gap, hyph);
		n = fmt_linefit(f, llen);
		if (!n && f->nwords)
			n = 1;
		w = fmt_wordslen(f, n);
		if (FMT_ADJ(f) && n > 1) {
			fmt_div = (llen - w) / (n - 1);
			fmt_rem = (llen - w) % (n - 1);
			for (i = 0; i < n - 1; i++)
				f->words[i + 1].gap += fmt_div + (i < fmt_rem);
		}
		sbuf_init(&l->sbuf);
		l->wid = fmt_move(f, n, &l->sbuf, &l->elsn, &l->elsp);
		f->words[0].gap = 0;
		f->filled = n && !f->nwords;
		f->l_head = (f->l_head + 1) % NLINES;
	}
	return 0;
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

static int fmt_sp(struct fmt *f)
{
	struct line *l;
	fmt_fill(f, 0);
	if ((f->l_head + 1) % NLINES == f->l_tail)
		return 1;
	l = &f->lines[f->l_head];
	f->filled = 0;
	f->nls--;
	l->li = f->li;
	l->ll = f->ll;
	sbuf_init(&l->sbuf);
	l->wid = fmt_move(f, f->nwords, &l->sbuf, &l->elsn, &l->elsp);
	f->l_head = (f->l_head + 1) % NLINES;
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

/* insert wb into fmt */
void fmt_word(struct fmt *f, struct wb *wb)
{
	if (f->nwords == NWORDS)
		fmt_fill(f, 0);
	if (wb_empty(wb) || f->nwords == NWORDS)
		return;
	if (FMT_FILL(f) && f->nls && f->gap)
		fmt_sp(f);
	if (!f->nwords)		/* apply the new .l and .i */
		fmt_confupdate(f);
	if (f->nls && !f->gap && f->nwords >= 1)
		f->gap = (f->nwords && f->eos) ? FMT_SWID(f) * 2 : FMT_SWID(f);
	fmt_insertword(f, f->nwords++, wb, f->filled ? 0 : f->gap);
	f->filled = 0;
	f->nls = 0;
	f->gap = 0;
	f->eos = wb_eos(wb);
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
	return fmt_wordslen(fmt, fmt->nwords) +
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
