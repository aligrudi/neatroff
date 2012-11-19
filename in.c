#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

struct inbuf {
	char *buf;
	int pos;
	int len;
	int backed;
	struct inbuf *prev;
};

static struct inbuf in_main = {.backed = -1};
static struct inbuf *buf = &in_main;

void in_push(char *s)
{
	struct inbuf *next = malloc(sizeof(*buf));
	int len = strlen(s);
	next->buf = malloc(len + 1);
	strcpy(next->buf, s);
	next->pos = 0;
	next->len = len;
	next->backed = -1;
	next->prev = buf;
	buf = next;
}

static void in_pop(void)
{
	struct inbuf *old = buf;
	buf = buf->prev;
	free(old->buf);
	free(old);
}

int in_next(void)
{
	int c = buf->backed;
	buf->backed = -1;
	if (c >= 0)
		return c;
	while (buf->pos == buf->len && buf->prev)
		in_pop();
	if (!buf->buf)
		return getchar();
	return buf->pos < buf->len ? buf->buf[buf->pos++] : -1;
}

void in_back(int c)
{
	buf->backed = c;
}
