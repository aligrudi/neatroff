#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define ADJ_LLEN(a)	MAX(0, (a)->ll - ((a)->lt >= 0 ? (a)->lt : (a)->li))

struct adj {
	struct wb wbs[NWORDS];		/* words in buf */
	int gaps[NWORDS];		/* gaps before words */
	int nwords;
	int wid;			/* total width of buf */
	int swid;			/* current space width */
	int gap;			/* space before the next word */
	int nls;			/* newlines before the next word */
	int l, i, t;			/* current .l, .i and ti */
	int ll, li, lt;			/* current line's .l, .i and ti */
};

void adj_ll(struct adj *adj, int ll)
{
	adj->l = ll;
}

void adj_ti(struct adj *adj, int ti)
{
	adj->t = ti;
}

void adj_in(struct adj *adj, int in)
{
	adj->i = in;
}

/* .ll, .in and .ti are delayed until the partial line is output */
static void adj_confupdate(struct adj *adj)
{
	adj->ll = adj->l;
	adj->li = adj->i;
	adj->lt = adj->t;
	adj->t = -1;
}

/* does the adjustment buffer need to be flushed without filling? */
static int adj_fullnf(struct adj *a)
{
	/* blank lines; indented lines; newlines when buffer is empty */
	return a->nls > 1 || (a->nls && a->gap) || (a->nls && !a->nwords);
}

/* does the adjustment buffer need to be flushed? */
int adj_full(struct adj *a, int fill)
{
	if (!fill)
		return a->nls;
	if (adj_fullnf(a))
		return 1;
	return a->wid > ADJ_LLEN(a);
}

/* is the adjustment buffer empty? */
int adj_empty(struct adj *a, int fill)
{
	return !fill ? !a->nls : !a->nwords && !adj_fullnf(a);
}

/* set space width */
void adj_swid(struct adj *adj, int swid)
{
	adj->swid = swid;
}

/* move words inside an adj struct */
static void adj_movewords(struct adj *a, int dst, int src, int len)
{
	memmove(a->wbs + dst, a->wbs + src, len * sizeof(a->wbs[0]));
	memmove(a->gaps + dst, a->gaps + src, len * sizeof(a->gaps[0]));
}

static int adj_linewid(struct adj *a, int n)
{
	int i, w = 0;
	for (i = 0; i < n; i++)
		w += wb_wid(&a->wbs[i]) + a->gaps[i];
	return w;
}

static int adj_linefit(struct adj *a, int llen)
{
	int i, w = 0;
	for (i = 0; i < a->nwords && w <= llen; i++)
		w += wb_wid(&a->wbs[i]) + a->gaps[i];
	return i - 1;
}

/* move n words from the adjustment buffer to s */
static int adj_move(struct adj *a, int n, struct sbuf *s, int *els_neg, int *els_pos)
{
	struct wb *cur;
	int w = 0;
	int i;
	*els_neg = 0;
	*els_pos = 0;
	for (i = 0; i < n; i++) {
		cur = &a->wbs[i];
		sbuf_printf(s, "%ch'%du'", c_ec, a->gaps[i]);
		sbuf_append(s, sbuf_buf(&cur->sbuf));
		w += wb_wid(cur) + a->gaps[i];
		wb_done(cur);
		if (cur->els_neg < *els_neg)
			*els_neg = cur->els_neg;
		if (cur->els_pos > *els_pos)
			*els_pos = cur->els_pos;
	}
	if (!n)
		return 0;
	a->nwords -= n;
	adj_movewords(a, 0, n, a->nwords);
	a->wid = adj_linewid(a, a->nwords);
	if (a->nwords)		/* apply the new .l and .i */
		adj_confupdate(a);
	return w;
}

/* try to hyphenate the n-th word */
static void adj_hyph(struct adj *a, int n, int w)
{
	struct wb w1, w2;
	wb_init(&w1);
	wb_init(&w2);
	if (wb_hyph(&a->wbs[n], w, &w1, &w2)) {
		wb_done(&w1);
		wb_done(&w2);
		return;
	}
	adj_movewords(a, n + 2, n + 1, a->nwords - n);
	wb_done(&a->wbs[n]);
	memcpy(&a->wbs[n], &w1, sizeof(w1));
	memcpy(&a->wbs[n + 1], &w2, sizeof(w2));
	a->nwords++;
	a->gaps[n + 1] = 0;
	a->wid = adj_linewid(a, a->nwords);
}

/* fill and copy a line into s */
int adj_fill(struct adj *a, int ad_b, int fill, struct sbuf *s,
		int *ll, int *in, int *ti, int *els_neg, int *els_pos)
{
	int adj_div, adj_rem;
	int w = 0;
	int i, n;
	int llen = ADJ_LLEN(a);
	*ll = a->ll;
	*in = a->li;
	*ti = a->lt;
	if (!fill || adj_fullnf(a)) {
		a->nls--;
		return adj_move(a, a->nwords, s, els_neg, els_pos);
	}
	n = adj_linefit(a, llen);
	if (n < a->nwords)
		adj_hyph(a, n, llen - adj_linewid(a, n) - a->gaps[n]);
	n = adj_linefit(a, llen);
	if (!n && a->nwords)
		n = 1;
	w = adj_linewid(a, n);
	if (ad_b && n > 1 && n < a->nwords) {
		adj_div = (llen - w) / (n - 1);
		adj_rem = (llen - w) % (n - 1);
		for (i = 0; i < n - 1; i++)
			a->gaps[i + 1] += adj_div + (i < adj_rem);
	}
	w = adj_move(a, n, s, els_neg, els_pos);
	if (a->nwords)
		a->wid -= a->gaps[0];
	a->gaps[0] = 0;
	return w;
}

void adj_sp(struct adj *adj)
{
	adj->gap += adj->swid;
}

void adj_nl(struct adj *adj)
{
	adj->nls++;
	adj->gap = 0;
}

/* ignore the previous newline */
void adj_nonl(struct adj *adj)
{
	if (adj->nls)
		adj->gap += adj->swid;
	adj->nls = 0;
}

static void adj_word(struct adj *adj, struct wb *wb)
{
	int i = adj->nwords++;
	wb_init(&adj->wbs[i]);
	adj->gaps[i] = adj->gap;
	adj->wid += wb_wid(wb) + adj->gap;
	wb_cat(&adj->wbs[i], wb);
}

/* insert wb into the adjustment buffer */
void adj_wb(struct adj *adj, struct wb *wb)
{
	if (wb_empty(wb) || adj->nwords == NWORDS)
		return;
	if (!adj->nwords)	/* apply the new .l and .i */
		adj_confupdate(adj);
	if (adj->nls && !adj->gap && adj->nwords >= 1)
		adj->gap = adj->swid;
	adj_word(adj, wb);
	adj->nls = 0;
	adj->gap = 0;
}

struct adj *adj_alloc(void)
{
	struct adj *adj = malloc(sizeof(*adj));
	memset(adj, 0, sizeof(*adj));
	return adj;
}

void adj_free(struct adj *adj)
{
	free(adj);
}

int adj_wid(struct adj *adj)
{
	return adj->wid + (adj->nls ? adj->swid : adj->gap);
}
