#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

struct inbuf {
	char path[64];		/* for file buffers */
	FILE *fin;
	char *buf;		/* for string buffers */
	char **args;
	int pos;
	int len;
	int backed;
	struct inbuf *prev;
};

static struct inbuf *buf;
static char files[NFILES][PATHLEN];
static int nfiles;
static int cfile;

static char **args_init(char **args);
static void args_free(char **args);

static void in_new(void)
{
	struct inbuf *next = malloc(sizeof(*next));
	memset(next, 0, sizeof(*next));
	next->backed = -1;
	next->prev = buf;
	buf = next;
}

void in_push(char *s, char **args)
{
	int len = strlen(s);
	in_new();
	buf->buf = malloc(len + 1);
	buf->len = len;
	strcpy(buf->buf, s);
	buf->args = args ? args_init(args) : NULL;
}

void in_source(char *path)
{
	FILE *fin = path && path[0] ? fopen(path, "r") : stdin;
	if (fin) {
		in_new();
		buf->fin = fin;
		if (path)
			snprintf(buf->path, sizeof(buf->path) - 1, "%s", path);
	}
}

void in_queue(char *path)
{
	if (nfiles < NFILES)
		snprintf(files[nfiles++], PATHLEN - 1, "%s", path ? path : "");
}

static void in_pop(void)
{
	struct inbuf *old = buf;
	buf = buf->prev;
	if (old->args)
		args_free(old->args);
	if (old->fin && old->path[0])
		fclose(old->fin);
	free(old->buf);
	free(old);
}

static int in_nextfile(void)
{
	while (!buf && cfile < nfiles)
		in_source(files[cfile++]);
	return !buf;
}

int in_next(void)
{
	int c;
	if (!buf && in_nextfile())
		return -1;
	if (buf->backed >= 0) {
		c = buf->backed;
		buf->backed = -1;
		return c;
	}
	while (buf || !in_nextfile()) {
		if (buf->buf && buf->pos < buf->len)
			break;
		if (!buf->buf && (c = getc(buf->fin)) >= 0)
			return c;
		in_pop();
	}
	if (!buf)
		return -1;
	/* replacing \\ with \ only for buffers inserted via in_push() */
	if (buf->buf[buf->pos] == '\\' && buf->buf[buf->pos + 1] == '\\')
		buf->pos++;
	return buf->buf[buf->pos++];
}

void in_back(int c)
{
	if (buf)
		buf->backed = c;
}

char *in_arg(int i)
{
	struct inbuf *cur = buf;
	while (cur && !cur->args)
		cur = cur->prev;
	return cur && cur->args && cur->args[i - 1] ? cur->args[i - 1] : "";
}

char *in_filename(void)
{
	struct inbuf *cur = buf;
	while (cur && !cur->fin)
		cur = cur->prev;
	return cur && cur->path[0] ? cur->path : "-";
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
