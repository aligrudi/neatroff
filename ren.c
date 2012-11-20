#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

struct word {
	int beg;	/* word beginning offset in buf */
	int end;	/* word ending offset in buf */
	int wid;	/* word width */
	int blanks;	/* blanks before word */
};

static char buf[LINELEN];		/* output buffer */
static int buflen;
static struct word words[LINELEN];	/* words in the buffer */
static int nwords;
static int wid;				/* total width of the buffer */

static int ren_next(void)
{
	int c = tr_next();
	if (c < 0)
		return -1;
	buf[buflen++] = c;
	return c;
}

static int nextchar(char *s)
{
	int c = ren_next();
	int l = utf8len(c);
	int i;
	if (c < 0)
		return 0;
	s[0] = c;
	for (i = 1; i < l; i++)
		s[i] = ren_next();
	s[l] = '\0';
	return l;
}

static void down(int n)
{
	printf("v%d\n", n);
}

static void adjust(char *s)
{
	struct word *last = words;
	int w = 0;
	int lendiff;
	int i;
	if (!nwords) {
		s[0] = '\0';
		return;
	}
	while (last < words + nwords && w + last->wid + last->blanks <= n_l) {
		w += last->wid + last->blanks;
		last++;
	}
	if (last > words)
		last--;
	memcpy(s, buf, last->end);
	s[last->end] = '\0';
	lendiff = last + 1 < words + nwords ? last[1].beg : buflen;
	memmove(buf, buf + lendiff, buflen - lendiff);
	buflen -= lendiff;
	nwords -= last - words + 1;
	memmove(words, last + 1, nwords * sizeof(words[0]));
	wid -= w;
	for (i = 0; i < nwords; i++) {
		words[i].beg -= lendiff;
		words[i].end -= lendiff;
	}
	if (nwords)
		wid -= words[0].blanks;
	words[0].blanks = 0;
}

void tr_br(int argc, char **args)
{
	char out[LINELEN];
	buf[buflen] = '\0';
	if (buflen) {
		adjust(out);
		out_put(out);
	}
}

void tr_sp(int argc, char **args)
{
	tr_br(0, NULL);
	if (argc > 1)
		down(tr_int(args[1], 0, 'v'));
}

static void ren_ps(char *s)
{
	n_s = tr_int(s, n_s, '\0');
}

void tr_ps(int argc, char **args)
{
	if (argc >= 2)
		ren_ps(args[1]);
	buflen += sprintf(buf + buflen, "\\s(%2d", n_s);
}

static int ren_ft(char *name)
{
	int fn = dev_font(name);
	if (fn >= 0)
		n_f = fn;
	return fn;
}

void tr_ft(int argc, char **args)
{
	int fn;
	if (argc < 2)
		return;
	fn = ren_ft(args[1]);
	if (fn >= 0)
		buflen += sprintf(buf + buflen, "\\f%d", n_f);
}

void tr_fp(int argc, char **args)
{
	if (argc < 3)
		return;
	if (dev_mnt(atoi(args[1]), args[2], argc > 3 ? args[3] : args[2]) < 0)
		errmsg("troff: failed to mount %s\n", args[2]);
}

static void escarg(char *s)
{
	int c;
	c = ren_next();
	if (c == '(') {
		*s++ = ren_next();
		*s++ = ren_next();
		*s = '\0';
		return;
	}
	if (c == '\'') {
		while (1) {
			c = ren_next();
			if (c == '\'' || c < 0)
				break;
			*s++ = c;
		}
		*s = '\0';
		return;
	}
	*s++ = c;
	*s = '\0';
}

void render(void)
{
	char c[LLEN];
	char arg[LINELEN];
	struct glyph *g;
	struct word *word = NULL;
	int word_beg;
	int blanks = 0;
	tr_br(0, NULL);
	while (nextchar(c) > 0) {
		g = NULL;
		word_beg = buflen - strlen(c);
		if (c[0] == '\\') {
			nextchar(c);
			if (c[0] == '(') {
				int l = nextchar(c);
				l += nextchar(c + l);
				c[l] = '\0';
			} else if (strchr("sf", c[0])) {
				escarg(arg);
				if (c[0] == 'f')
					ren_ft(arg);
				if (c[0] == 's')
					ren_ps(arg);
				continue;
			}
		}
		g = dev_glyph(c, n_f);
		if (!g) {
			blanks += charwid(dev_spacewid(), n_s);
			word = NULL;
			continue;
		}
		if (!word) {
			word = &words[nwords++];
			word->beg = word_beg;
			word->wid = 0;
			word->blanks = blanks;
			wid += blanks;
			blanks = 0;
		}
		word->end = buflen;
		word->wid += charwid(g->wid, n_s);
		wid += charwid(g->wid, n_s);
		if (wid > n_l) {
			word = NULL;
			tr_br(0, NULL);
		}
	}
	tr_br(0, NULL);
}
