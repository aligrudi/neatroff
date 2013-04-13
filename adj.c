#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

struct word {
	int beg;	/* word beginning offset */
	int end;	/* word ending offset */
	int wid;	/* word width */
	int gap;	/* the space before this word */
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
};

/* does the adjustment buffer need to be flushed without filling? */
static int adj_fullnf(struct adj *a)
{
	/* blank lines; indented lines; newlines when buffer is empty */
	return a->nls > 1 || (a->nls && a->gap) || (a->nls && !a->nwords);
}

/* does the adjustment buffer need to be flushed? */
int adj_full(struct adj *a, int mode, int linelen)
{
	if (mode == ADJ_N)
		return a->nls;
	if (adj_fullnf(a))
		return 1;
	return !a->word && a->wid > linelen;
}

/* is the adjustment buffer empty? */
int adj_empty(struct adj *a, int mode)
{
	return mode == ADJ_N ? !a->nls : !a->nwords && !adj_fullnf(a);
}

/* set space width */
void adj_swid(struct adj *adj, int swid)
{
	adj->swid = swid;
}

/* move n words from the adjustment buffer to s */
static int adj_move(struct adj *a, int n, char *s)
{
	struct word *cur;
	int lendiff;
	int w = 0;
	int i;
	for (i = 0; i < n; i++) {
		cur = &a->words[i];
		s += sprintf(s, "\\h'%du'", cur->gap);
		memcpy(s, a->buf + cur->beg, cur->end - cur->beg);
		s += cur->end - cur->beg;
		w += cur->wid + cur->gap;
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
	return w;
}

/* fill and copy a line into s */
int adj_fill(struct adj *a, int mode, int ll, char *s)
{
	int adj_div, adj_rem;
	int w = 0;
	int i, n;
	if (mode == ADJ_N || adj_fullnf(a)) {
		a->nls--;
		return adj_move(a, a->nwords, s);
	}
	for (n = 0; n < a->nwords; n++) {
		if (n && w + a->words[n].wid + a->words[n].gap > ll)
			break;
		w += a->words[n].wid + a->words[n].gap;
	}
	if (mode == ADJ_B && n > 1 && n < a->nwords) {
		adj_div = (ll - w) / (n - 1);
		adj_rem = ll - w - adj_div * (n - 1);
		a->wid += ll - w;
		for (i = 0; i < n - 1; i++)
			a->words[i + 1].gap += adj_div + (i < adj_rem);
	}
	w = adj_move(a, n, s);
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
	return adj->wid;
}
