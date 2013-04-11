#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

struct inbuf {
	char *buf;
	char **args;
	int pos;
	int len;
	int backed;
	struct inbuf *prev;
};

static struct inbuf in_main = {.backed = -1};
static struct inbuf *buf = &in_main;

static char **args_init(char **args);
static void args_free(char **args);

void in_push(char *s, char **args)
{
	struct inbuf *next = malloc(sizeof(*buf));
	int len = strlen(s);
	next->buf = malloc(len + 1);
	strcpy(next->buf, s);
	next->pos = 0;
	next->len = len;
	next->backed = -1;
	next->prev = buf;
	next->args = args ? args_init(args) : NULL;
	buf = next;
}

static void in_pop(void)
{
	struct inbuf *old = buf;
	buf = buf->prev;
	if (old->args)
		args_free(old->args);
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
	if (buf->pos >= buf->len)
		return -1;
	/* replacing \\ with \ only for buffers inserted via in_push() */
	if (buf->buf[buf->pos] == '\\' && buf->buf[buf->pos + 1] == '\\')
		buf->pos++;
	return buf->buf[buf->pos++];
}

void in_back(int c)
{
	buf->backed = c;
}

char *in_arg(int i)
{
	struct inbuf *cur = buf;
	while (!cur->args && cur->prev)
		cur = cur->prev;
	return cur->args && cur->args[i - 1] ? cur->args[i - 1] : "";
}

static char **args_init(char **args)
{
	char **out = malloc(NARGS * sizeof(*out));
	int i;
	for (i = 0; i < NARGS; i++) {
		out[i] = NULL;
		if (args[i]) {
			int len = strlen(args[i]) + 1;
			out[i] = malloc(len);
			memcpy(out[i], args[i], len);
		}
	}
	return out;
}

static void args_free(char **args)
{
	int i;
	for (i = 0; i < NARGS; i++)
		if (args[i])
			free(args[i]);
	free(args);
}
