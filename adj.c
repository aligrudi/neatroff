#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define ADJ_LLEN(a)	MAX(0, (a)->ll - ((a)->lt >= 0 ? (a)->lt : (a)->li))

struct word {
	int beg;	/* word beginning offset */
	int end;	/* word ending offset */
	int wid;	/* word width */
	int gap;	/* the space before this word */
	int els_neg;	/* pre-extra line space */
	int els_pos;	/* post-extra line space */
};

struct adj {
	char buf[LNLEN];		/* line buffer */
	int len;
	struct word words[NWORDS];	/* words in buf */
	int nwords;
	int wid;			/* total width of buf */
	struct word *word;		/* current word */
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
	return !a->word && a->wid > ADJ_LLEN(a);
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
static int adj_move(struct adj *a, int n, char *s, int *els_neg, int *els_pos)
{
	struct word *cur;
	int lendiff;
	int w = 0;
	int i;
	*els_neg = 0;
	*els_pos = 0;
	for (i = 0; i < n; i++) {
		cur = &a->words[i];
		s += sprintf(s, "\\h'%du'", cur->gap);
		memcpy(s, a->buf + cur->beg, cur->end - cur->beg);
		s += cur->end - cur->beg;
		w += cur->wid + cur->gap;
		if (cur->els_neg < *els_neg)
			*els_neg = cur->els_neg;
		if (cur->els_pos > *els_pos)
			*els_pos = cur->els_pos;
	}
	*s = '\0';
	if (!n)
		return 0;
	lendiff = n < a->nwords ? a->words[n].beg : a->len;
	memmove(a->buf, a->buf + lendiff, a->len - lendiff + 1);
	a->len -= lendiff;
	a->nwords -= n;
	memmove(a->words, a->words + n, a->nwords * sizeof(a->words[0]));
	a->wid -= w;
	for (i = 0; i < a->nwords; i++) {
		a->words[i].beg -= lendiff;
		a->words[i].end -= lendiff;
	}
	if (a->nwords)		/* apply the new .l and .i */
		adj_confupdate(a);
	return w;
}

/* fill and copy a line into s */
int adj_fill(struct adj *a, int ad_b, int fill, char *s,
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

static void adj_wordbeg(struct adj *adj, int gap)
{
	adj->word = &adj->words[adj->nwords++];
	adj->word->beg = adj->len;
	adj->word->wid = 0;
	adj->word->gap = gap;
	adj->wid += gap;
	adj->word->els_neg = 0;
	adj->word->els_pos = 0;
}

static void adj_wordend(struct adj *adj)
{
	if (adj->word)
		adj->word->end = adj->len;
	adj->word = NULL;
}

/* insert s into the adjustment buffer */
void adj_put(struct adj *adj, int wid, char *s, ...)
{
	va_list ap;
	if (!strcmp(s, " ")) {
		adj_wordend(adj);
		adj->gap += wid;
		adj->swid = wid;
		return;
	}
	if (!strcmp(s, "\n")) {
		adj_wordend(adj);
		adj->nls++;
		adj->gap = 0;
		adj->swid = wid;
		return;
	}
	if (!adj->nwords)	/* apply the new .l and .i */
		adj_confupdate(adj);
	if (!adj->word) {
		if (adj->nls && !adj->gap && adj->nwords >= 1)
			adj->gap = adj->swid;
		adj_wordbeg(adj, adj->gap);
		adj->nls = 0;
		adj->gap = 0;
	}
	va_start(ap, s);
	adj->len += vsprintf(adj->buf + adj->len, s, ap);
	va_end(ap);
	adj->word->wid += wid;
	adj->wid += wid;
}

/* extra line-space requests */
void adj_els(struct adj *adj, int els)
{
	if (!adj->word)
		adj_put(adj, 0, "");
	if (els < adj->word->els_neg)
		adj->word->els_neg = els;
	if (els > adj->word->els_pos)
		adj->word->els_pos = els;
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
