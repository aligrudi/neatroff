#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define SBUF_SZ		512

static void sbuf_extend(struct sbuf *sbuf, int amount)
{
	char *s = sbuf->s;
	sbuf->sz = (MAX(1, amount) + SBUF_SZ - 1) & ~(SBUF_SZ - 1);
	sbuf->s = malloc(sbuf->sz);
	if (sbuf->n)
		memcpy(sbuf->s, s, sbuf->n);
	free(s);
}

void sbuf_init(struct sbuf *sbuf)
{
	memset(sbuf, 0, sizeof(*sbuf));
	sbuf_extend(sbuf, SBUF_SZ);
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
	sbuf->prev_n = sbuf->n;
	sbuf->n += len;
}

void sbuf_printf(struct sbuf *sbuf, char *s, ...)
{
	char buf[ILNLEN];
	va_list ap;
	va_start(ap, s);
	vsprintf(buf, s, ap);
	va_end(ap);
	sbuf_append(sbuf, buf);
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

int sbuf_len(struct sbuf *sbuf)
{
	return sbuf->n;
}

/* undo last sbuf_append() */
void sbuf_pop(struct sbuf *sbuf)
{
	if (sbuf->prev_n < sbuf->n)
		sbuf->n = sbuf->prev_n;
}

void sbuf_done(struct sbuf *sbuf)
{
	free(sbuf->s);
}
