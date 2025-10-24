/* input stream management */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

struct inbuf {
	char path[PATHLEN];	/* for file buffers */
	FILE *fin;
	char *buf;		/* for string buffers */
	char **args;
	int unbuf[32];		/* unread characters */
	int un;			/* number of unread characters */
	int pos;
	int len;
	int chr;		/* last character read from fin */
	int lnum;		/* file line number */
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

void in_so(char *path)
{
	FILE *fin = path && path[0] ? fopen(path, "r") : stdin;
	if (!fin) {
		errmsg("neatroff: failed to open <%s>\n", path);
		return;
	}
	in_new();
	buf->fin = fin;
	buf->lnum = 1;
	if (path)
		snprintf(buf->path, sizeof(buf->path), "%s", path);
}

void in_lf(char *path, int lnum)
{
	struct inbuf *cur = buf;
	while (cur && !cur->fin)
		cur = cur->prev;
	if (path)
		snprintf(cur->path, sizeof(cur->path), "%s", path);
	cur->lnum = lnum - 1;
}

void in_queue(char *path)
{
	if (nfiles < NFILES)
		snprintf(files[nfiles++], PATHLEN, "%s", path ? path : "");
}

static void in_pop(void)
{
	struct inbuf *old = buf;
	buf = buf->prev;
	if (old->args)
		args_free(old->args);
	if (old->fin && old->fin != stdin)
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

int in_next(void)
{
	int c;
	while (buf || !in_nextfile()) {
		if (buf->un)
			return buf->unbuf[--buf->un];
		if (buf->buf && buf->pos < buf->len)
			break;
		if (!buf->buf && (c = getc(buf->fin)) >= 0) {
			if (buf->chr == '\n')
				buf->lnum++;
			buf->chr = c;
			return c;
		}
		in_pop();
	}
	return buf ? (unsigned char) buf->buf[buf->pos++] : -1;
}

void in_back(int c)
{
	if (c < 0)
		return;
	if (buf && buf->un < sizeof(buf->unbuf))
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
	return cur && cur->args && i < NARGS &&
		cur->args[i] ? cur->args[i] : "";
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

void in_shift(void)
{
	struct inbuf *cur = buf;
	while (cur && !cur->args)
		cur = cur->prev;
	if (cur && cur->args) {
		free(cur->args[1]);
		memmove(cur->args + 1, cur->args + 2,
			(NARGS - 2) * sizeof(cur->args[0]));
		cur->args[NARGS - 1] = NULL;
	}
}

char *in_filename(void)
{
	struct inbuf *cur = buf;
	while (cur && !cur->fin)
		cur = cur->prev;
	return cur && cur->path[0] ? cur->path : "-";
}

int in_lnum(void)
{
	struct inbuf *cur = buf;
	while (cur && !cur->fin)
		cur = cur->prev;
	return cur ? cur->lnum : 0;
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
