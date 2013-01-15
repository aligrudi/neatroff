#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define SBUF_SZ		1024

void sbuf_init(struct sbuf *sbuf)
{
	sbuf->s = malloc(SBUF_SZ);
	sbuf->sz = SBUF_SZ;
	sbuf->n = 0;
}

static void sbuf_extend(struct sbuf *sbuf, int amount)
{
	char *s = sbuf->s;
	sbuf->s = malloc(amount);
	sbuf->sz = amount;
	memcpy(sbuf->s, s, sbuf->n);
}

void sbuf_add(struct sbuf *sbuf, int c)
{
	if (sbuf->n + 2 >= sbuf->sz)
		sbuf_extend(sbuf, sbuf->sz * 2);
	sbuf->s[sbuf->n++] = c;
}

void sbuf_append(struct sbuf *sbuf, char *s)
{
	int len = strlen(s);
	if (sbuf->n + len + 1 >= sbuf->sz)
		sbuf_extend(sbuf, sbuf->n + len + 1);
	memcpy(sbuf->s + sbuf->n, s, len);
	sbuf->n += len;
}

void sbuf_putnl(struct sbuf *sbuf)
{
	if (sbuf->n && sbuf->s[sbuf->n - 1] != '\n')
		sbuf_add(sbuf, '\n');
}

int sbuf_empty(struct sbuf *sbuf)
{
	return !sbuf->n;
}

char *sbuf_buf(struct sbuf *sbuf)
{
	sbuf->s[sbuf->n] = '\0';
	return sbuf->s;
}

void sbuf_done(struct sbuf *sbuf)
{
	free(sbuf->s);
}
