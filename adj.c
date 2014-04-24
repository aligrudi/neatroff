/* adjustment buffer for putting words into lines */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define ADJ_LLEN(a)	MAX(0, (a)->ll - (a)->li)

struct adj {
	char *words[NWORDS];	/* words to be adjusted */
	int wids[NWORDS];	/* the width of words */
	int elsn[NWORDS];	/* els_neg of words */
	int elsp[NWORDS];	/* els_pos of words */
	int gaps[NWORDS];	/* gaps before words */
	int nwords;
	int wid;		/* total width of buf */
	int swid;		/* current space width */
	int gap;		/* space before the next word */
	int nls;		/* newlines before the next word */
	int li, ll;		/* current line indentation and length */
	int filled;		/* filled all words in the last adj_fill() */
	int eos;		/* last word ends a sentence */
};

/* .ll, .in and .ti are delayed until the partial line is output */
static void adj_confupdate(struct adj *adj)
{
	adj->ll = n_l;
	adj->li = n_ti > 0 ? n_ti : n_i;
	n_ti = 0;
}

/* does the adjustment buffer need to be flushed without filling? */
static int adj_fullnf(struct adj *a)
{
	/* blank lines; indented lines; newlines when buffer is empty */
	return a->nls > 1 || (a->nls && a->gap) ||
			(a->nls - a->filled > 0 && !a->nwords);
}

/* does the adjustment buffer need to be flushed? */
int adj_full(struct adj *a, int fill)
{
	if (!fill)
		return a->nls - a->filled > 0;
	if (adj_fullnf(a))
		return 1;
	return a->nwords && a->wid > ADJ_LLEN(a);
}

/* is the adjustment buffer empty? */
int adj_empty(struct adj *a, int fill)
{
	return !fill ? a->nls - a->filled <= 0 : !a->nwords && !adj_fullnf(a);
}

/* set space width */
void adj_swid(struct adj *adj, int swid)
{
	adj->swid = swid;
}

/* move words inside an adj struct */
static void adj_movewords(struct adj *a, int dst, int src, int len)
{
	memmove(a->words + dst, a->words + src, len * sizeof(a->words[0]));
	memmove(a->wids + dst, a->wids + src, len * sizeof(a->wids[0]));
	memmove(a->elsn + dst, a->elsn + src, len * sizeof(a->elsn[0]));
	memmove(a->elsp + dst, a->elsp + src, len * sizeof(a->elsp[0]));
	memmove(a->gaps + dst, a->gaps + src, len * sizeof(a->gaps[0]));
}

static char *adj_strdup(char *s)
{
	int l = strlen(s);
	char *r = malloc(l + 1);
	memcpy(r, s, l + 1);
	return r;
}

/* copy word buffer wb in adj->words[i] */
static void adj_word(struct adj *adj, int i, struct wb *wb, int gap)
{
	adj->words[i] = adj_strdup(wb_buf(wb));
	adj->wids[i] = wb_wid(wb);
	adj->elsn[i] = wb->els_neg;
	adj->elsp[i] = wb->els_pos;
	adj->gaps[i] = gap;
}

static int adj_linewid(struct adj *a, int n)
{
	int i, w = 0;
	for (i = 0; i < n; i++)
		w += a->wids[i] + a->gaps[i];
	return w;
}

static int adj_linefit(struct adj *a, int llen)
{
	int i, w = 0;
	for (i = 0; i < a->nwords; i++) {
		w += a->wids[i] + a->gaps[i];
		if (w > llen)
			return i;
	}
	return i;
}

/* move n words from the adjustment buffer to s */
static int adj_move(struct adj *a, int n, struct sbuf *s, int *els_neg, int *els_pos)
{
	int w = 0;
	int i;
	*els_neg = 0;
	*els_pos = 0;
	for (i = 0; i < n; i++) {
		sbuf_printf(s, "%ch'%du'", c_ec, a->gaps[i]);
		sbuf_append(s, a->words[i]);
		w += a->wids[i] + a->gaps[i];
		if (a->elsn[i] < *els_neg)
			*els_neg = a->elsn[i];
		if (a->elsp[i] > *els_pos)
			*els_pos = a->elsp[i];
		free(a->words[i]);
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
static void adj_hyph(struct adj *a, int n, int w, int hyph)
{
	struct wb w1, w2;
	int flg = hyph | (n ? 0 : HY_ANY);
	wb_init(&w1);
	wb_init(&w2);
	if (!wb_hyph(a->words[n], w, &w1, &w2, flg)) {
		adj_movewords(a, n + 2, n + 1, a->nwords - n);
		free(a->words[n]);
		adj_word(a, n, &w1, a->gaps[n]);
		adj_word(a, n + 1, &w2, 0);
		a->nwords++;
		a->wid = adj_linewid(a, a->nwords);
	}
	wb_done(&w1);
	wb_done(&w2);
}

/* fill and copy a line into s */
int adj_fill(struct adj *a, int ad_b, int fill, int hyph, struct sbuf *s,
		int *li, int *ll, int *els_neg, int *els_pos)
{
	int adj_div, adj_rem;
	int w = 0;
	int i, n;
	int llen = ADJ_LLEN(a);
	*ll = a->ll;
	*li = a->li;
	if (!fill || adj_fullnf(a)) {
		a->filled = 0;
		a->nls--;
		return adj_move(a, a->nwords, s, els_neg, els_pos);
	}
	n = adj_linefit(a, llen);
	if (n < a->nwords)
		adj_hyph(a, n, llen - adj_linewid(a, n) - a->gaps[n], hyph);
	n = adj_linefit(a, llen);
	if (!n && a->nwords)
		n = 1;
	w = adj_linewid(a, n);
	if (ad_b && n > 1) {
		adj_div = (llen - w) / (n - 1);
		adj_rem = (llen - w) % (n - 1);
		for (i = 0; i < n - 1; i++)
			a->gaps[i + 1] += adj_div + (i < adj_rem);
	}
	w = adj_move(a, n, s, els_neg, els_pos);
	if (a->nwords)
		a->wid -= a->gaps[0];
	a->gaps[0] = 0;
	a->filled = n && !a->nwords;
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

/* insert wb into the adjustment buffer */
void adj_wb(struct adj *adj, struct wb *wb)
{
	if (wb_empty(wb) || adj->nwords == NWORDS)
		return;
	if (!adj->nwords)	/* apply the new .l and .i */
		adj_confupdate(adj);
	if (adj->nls && !adj->gap && adj->nwords >= 1)
		adj->gap = (adj->nwords && adj->eos) ? adj->swid * 2 : adj->swid;
	adj_word(adj, adj->nwords++, wb, adj->filled ? 0 : adj->gap);
	adj->filled = 0;
	adj->wid += adj->wids[adj->nwords - 1] + adj->gaps[adj->nwords - 1];
	adj->nls = 0;
	adj->gap = 0;
	adj->eos = wb_eos(wb);
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
