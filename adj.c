#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "xroff.h"

static void adj_nf(struct adj *a, int n, char *s)
{
	struct word *cur;
	int lendiff;
	int w = 0;
	int i;
	for (i = 0; i < n; i++) {
		cur = &a->words[i];
		s += sprintf(s, "\\h'%du'", cur->blanks);
		memcpy(s, a->buf + cur->beg, cur->end - cur->beg);
		s += cur->end - cur->beg;
		w += cur->wid + cur->blanks;
	}
	*s = '\0';
	lendiff = n < a->nwords ? a->words[n].beg : a->len;
	memmove(a->buf, a->buf + lendiff, a->len - lendiff);
	a->len -= lendiff;
	a->nwords -= n;
	memmove(a->words, a->words + n, a->nwords * sizeof(a->words[0]));
	a->wid -= w;
	for (i = 0; i < a->nwords; i++) {
		a->words[i].beg -= lendiff;
		a->words[i].end -= lendiff;
	}
}

void adj_fi(struct adj *a, int mode, int ll, char *s)
{
	int adj_div, adj_rem;
	int w = 0;
	int i, n;
	if (mode == ADJ_N) {
		adj_nf(a, a->nwords, s);
		return;
	}
	for (n = 0; n < a->nwords; n++) {
		if (n && w + a->words[n].wid + a->words[n].blanks > ll)
			break;
		w += a->words[n].wid + a->words[n].blanks;
	}
	if (mode == ADJ_B && n > 1 && n < a->nwords) {
		adj_div = (ll - w) / (n - 1);
		adj_rem = ll - w - adj_div * (n - 1);
		a->wid += ll - w;
		for (i = 0; i < n - 1; i++)
			a->words[i + 1].blanks += adj_div + (i < adj_rem);
	}
	adj_nf(a, n, s);
	if (a->nwords)
		a->wid -= a->words[0].blanks;
	a->words[0].blanks = 0;
}

void adj_wordbeg(struct adj *adj, int blanks)
{
	adj->word = &adj->words[adj->nwords++];
	adj->word->beg = adj->len;
	adj->word->wid = 0;
	adj->word->blanks = blanks;
	adj->wid += blanks;
}

void adj_wordend(struct adj *adj)
{
	adj->word->end = adj->len;
	adj->word = NULL;
}

void adj_putcmd(struct adj *adj, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	adj->len += vsprintf(adj->buf + adj->len, fmt, ap);
	va_end(ap);
}

void adj_putchar(struct adj *adj, int wid, char *s)
{
	strcpy(adj->buf + adj->len, s);
	adj->len += strlen(s);
	adj->word->wid += wid;
	adj->wid += wid;
}

int adj_inword(struct adj *adj)
{
	return adj->word != NULL;
}

int adj_inbreak(struct adj *adj, int ll)
{
	return !adj_inword(adj) && adj->wid > ll;
}
