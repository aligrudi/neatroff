/* variable length string buffer */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define ALIGN(n, a)	(((n) + (a) - 1) & ~((a) - 1))
#define SBUFSZ		512

static void sbuf_extend(struct sbuf *sbuf, int amount)
{
	sbuf->sz = ALIGN(amount, SBUFSZ);
	sbuf->s = mextend(sbuf->s, sbuf->n, sbuf->sz, sizeof(sbuf->s[0]));
}

void sbuf_init(struct sbuf *sbuf)
{
	memset(sbuf, 0, sizeof(*sbuf));
	sbuf_extend(sbuf, SBUFSZ);
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

void sbuf_printf(struct sbuf *sbuf, char *s, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, s);
	vsnprintf(buf, sizeof(buf), s, ap);
	va_end(ap);
	sbuf_append(sbuf, buf);
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

/* shorten the sbuf */
void sbuf_cut(struct sbuf *sbuf, int n)
{
	if (sbuf->n > n)
		sbuf->n = n;
}

void sbuf_done(struct sbuf *sbuf)
{
	free(sbuf->s);
}

char *sbuf_out(struct sbuf *sbuf)
{
	char *s = sbuf->s;
	memset(sbuf, 0, sizeof(*sbuf));
	return s;
}
