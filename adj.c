#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define ADJ_LLEN(a)	MAX(0, (a)->ll - ((a)->lt >= 0 ? (a)->lt : (a)->li))

struct word {
	struct sbuf s;
	int wid;	/* word width */
	int gap;	/* the space before this word */
	int els_neg;	/* pre-extra line space */
	int els_pos;	/* post-extra line space */
};

struct adj {
	struct word words[NWORDS];	/* words in buf */
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

/* move n words from the adjustment buffer to s */
static int adj_move(struct adj *a, int n, struct sbuf *s, int *els_neg, int *els_pos)
{
	struct word *cur;
	int w = 0;
	int i;
	*els_neg = 0;
	*els_pos = 0;
	for (i = 0; i < n; i++) {
		cur = &a->words[i];
		sbuf_printf(s, "\\h'%du'", cur->gap);
		sbuf_append(s, sbuf_buf(&cur->s));
		sbuf_done(&cur->s);
		w += cur->wid + cur->gap;
		if (cur->els_neg < *els_neg)
			*els_neg = cur->els_neg;
		if (cur->els_pos > *els_pos)
			*els_pos = cur->els_pos;
	}
	if (!n)
		return 0;
	a->nwords -= n;
	memmove(a->words, a->words + n, a->nwords * sizeof(a->words[0]));
	a->wid -= w;
	if (a->nwords)		/* apply the new .l and .i */
		adj_confupdate(a);
	return w;
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
	for (n = 0; n < a->nwords; n++) {
		if (n && w + a->words[n].wid + a->words[n].gap > llen)
			break;
		w += a->words[n].wid + a->words[n].gap;
	}
	if (ad_b && n > 1 && n < a->nwords) {
		adj_div = (llen - w) / (n - 1);
		adj_rem = llen - w - adj_div * (n - 1);
		a->wid += llen - w;
		for (i = 0; i < n - 1; i++)
			a->words[i + 1].gap += adj_div + (i < adj_rem);
	}
	w = adj_move(a, n, s, els_neg, els_pos);
	if (a->nwords)
		a->wid -= a->words[0].gap;
	a->words[0].gap = 0;
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

static void adj_word(struct adj *adj, struct wb *wb)
{
	struct word *cur = &adj->words[adj->nwords++];
	cur->wid = wb_wid(wb);
	cur->gap = adj->gap;
	adj->wid += cur->wid + adj->gap;
	wb_getels(wb, &cur->els_neg, &cur->els_pos);
	sbuf_init(&cur->s);
	sbuf_append(&cur->s, sbuf_buf(&wb->sbuf));
	wb_reset(wb);
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
