#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

struct inbuf {
	char path[64];		/* for file buffers */
	FILE *fin;
	char *buf;		/* for string buffers */
	char **args;
	int unbuf[8];		/* unread characters */
	int un;			/* number of unread characters */
	int pos;
	int len;
	int nl;			/* read \n, if the previous char was not */
	struct inbuf *prev;
};

static struct inbuf *buf;
static char files[NFILES][PATHLEN];
static int nfiles;
static int cfile;
static int in_last[2];		/* the last chars returned from in_next() */

static char **args_init(char **args);
static void args_free(char **args);

static void in_new(void)
{
	struct inbuf *next = malloc(sizeof(*next));
	memset(next, 0, sizeof(*next));
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

void in_pushnl(char *s, char **args)
{
	in_push(s, args);
	buf->nl = 1;
}

void in_so(char *path)
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

void in_nx(char *path)
{
	while (buf)
		in_pop();
	if (path)
		in_so(path);
}

void in_ex(void)
{
	while (buf)
		in_pop();
	cfile = nfiles;
}

static int in_nextfile(void)
{
	while (!buf && cfile < nfiles)
		in_so(files[cfile++]);
	return !buf;
}

static int in_read(void)
{
	int c;
	while (buf || !in_nextfile()) {
		if (buf->un)
			return buf->unbuf[--buf->un];
		if (buf->nl-- > 0 && in_last[0] != '\n')
			return '\n';
		if (buf->buf && buf->pos < buf->len)
			break;
		if (!buf->buf && (c = getc(buf->fin)) >= 0)
			return c;
		in_pop();
	}
	if (!buf)
		return -1;
	/* replacing \\ with \ only for buffers inserted via in_push() */
	if (buf->buf[buf->pos] == c_ec && buf->buf[buf->pos + 1] == c_ec)
		buf->pos++;
	return (unsigned char) buf->buf[buf->pos++];
}

int in_next(void)
{
	in_last[1] = in_last[0];
	in_last[0] = in_read();
	return in_last[0];
}

void in_back(int c)
{
	in_last[0] = in_last[1];
	if (buf)
		buf->unbuf[buf->un++] = c;
}

int in_top(void)
{
	return buf && buf->un ? buf->unbuf[buf->un - 1] : -1;
}

char *in_arg(int i)
{
	struct inbuf *cur = buf;
	while (cur && !cur->args)
		cur = cur->prev;
	return cur && cur->args && cur->args[i - 1] ? cur->args[i - 1] : "";
}

int in_nargs(void)
{
	struct inbuf *cur = buf;
	int n = 0;
	while (cur && !cur->args)
		cur = cur->prev;
	while (cur && cur->args && cur->args[n])
		n++;
	return n;
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
