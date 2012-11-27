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

static char buf[LNLEN];			/* output buffer */
static int buflen;
static struct word words[NWORDS];	/* words in the buffer */
static int nwords;
static int wid;				/* total width of the buffer */
static struct word *word;		/* current word */
static int ren_backed = -1;		/* pushed back character */

static int ren_next(void)
{
	int c = ren_backed >= 0 ? ren_backed : tr_next();
	ren_backed = -1;
	return c;
}

static void ren_back(int c)
{
	ren_backed = c;
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

static void adjust(char *s, int adj)
{
	struct word *last = words;
	struct word *cur;
	int w = 0;
	int lendiff;
	int i;
	int adj_div = 0;
	int adj_rem = 0;
	int n;
	while (last < words + nwords && w + last->wid + last->blanks <= n_l) {
		w += last->wid + last->blanks;
		last++;
	}
	if (last > words)
		last--;
	n = last - words + 1;
	if (adj && n > 1) {
		adj_div = (n_l - w) / (n - 1);
		adj_rem = n_l - w - adj_div * (n - 1);
	}
	for (i = 0; i < n - 1; i++)
		words[i + 1].blanks += adj_div + (i < adj_rem);
	for (cur = words; cur <= last; cur++) {
		s += sprintf(s, "\\h'%du'", cur->blanks);
		memcpy(s, buf + cur->beg, cur->end - cur->beg);
		s += cur->end - cur->beg;
	}
	*s = '\0';
	lendiff = n < nwords ? last[1].beg : buflen;
	memmove(buf, buf + lendiff, buflen - lendiff);
	buflen -= lendiff;
	nwords -= n;
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

static void ren_br(int sp, int adj);

void tr_br(char **args)
{
	ren_br(0, 0);
}

void tr_sp(char **args)
{
	int sp = 0;
	if (args[1])
		sp = tr_int(args[1], 0, 'v');
	ren_br(sp, 0);
}

void ren_page(int pg)
{
	n_nl = -1;
	n_d = 0;
	n_pg = pg;
	printf("p%d\n", pg);
	printf("V%d\n", 0);
}

void tr_bp(char **args)
{
	ren_br(0, 0);
	ren_page(args[1] ? tr_int(args[1], n_pg, 'v') : n_pg + 1);
}

static void ren_ps(char *s)
{
	int ps = !*s || !strcmp("0", s) ? n_s0 : tr_int(s, n_s, '\0');
	n_s0 = n_s;
	n_s = ps;
}

void tr_ps(char **args)
{
	if (args[1])
		ren_ps(args[1]);
}

static void ren_ft(char *s)
{
	int fn = !*s || !strcmp("P", s) ? n_f0 : dev_font(s);
	if (fn >= 0) {
		n_f0 = n_f;
		n_f = fn;
	}
}

void tr_ft(char **args)
{
	if (args[1])
		ren_ft(args[1]);
}

void tr_fp(char **args)
{
	if (!args[2])
		return;
	if (dev_mnt(atoi(args[1]), args[2], args[3] ? args[3] : args[2]) < 0)
		errmsg("troff: failed to mount %s\n", args[2]);
}

static void escarg(char *s, int cmd)
{
	int c;
	c = ren_next();
	if (cmd == 's' && (c == '-' || c == '+')) {
		*s++ = c;
		c = ren_next();
	}
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
	if (cmd == 's' && c >= '1' && c <= '3') {
		c = ren_next();
		if (isdigit(c))
			*s++ = c;
		else
			ren_back(c);
	}
	*s = '\0';
}

static void down(int n)
{
	n_d += n;
	n_nl = n_d;
	printf("v%d\n", n);
}

static void ren_br(int sp, int adj)
{
	char out[LNLEN];
	buf[buflen] = '\0';
	if (nwords) {
		adjust(out, wid > n_l ? n_ad : adj);
		down(n_v);
		printf("H%d\n", n_o + n_i);
		output(out);
	}
	if (sp)
		down(sp);
}

void render(void)
{
	char c[GNLEN * 2];
	char arg[ILNLEN];
	struct glyph *g;
	int g_wid;
	int blanks = 0;
	int newline = 0;
	int r_s = n_s;
	int r_f = n_f;
	int esc = 0;
	int space_br = 0;	/* .br caused by indented lines */
	ren_br(0, 0);
	while (nextchar(c) > 0) {
		g = NULL;
		if (!word && wid > n_l)
			ren_br(0, wid > n_l ? n_ad : 0);
		if (c[0] == ' ' || c[0] == '\n') {
			if (word) {
				word->end = buflen;
				word = NULL;
			}
			if (newline && c[0] == '\n')
				ren_br(n_v, 0);
			if (newline && c[0] == ' ' && !space_br) {
				space_br = 1;
				ren_br(0, 0);
			}
			if (c[0] == '\n') {
				blanks = 0;
				newline = 1;
				space_br = 0;
			}
			if (c[0] == ' ')
				blanks += charwid(dev_spacewid(), n_s);
			continue;
		}
		esc = 0;
		if (c[0] == '\\') {
			esc = 1;
			nextchar(c);
			if (c[0] == '(') {
				int l = nextchar(c);
				l += nextchar(c + l);
				c[l] = '\0';
			} else if (strchr("sf", c[0])) {
				escarg(arg, c[0]);
				if (c[0] == 'f')
					ren_ft(arg);
				if (c[0] == 's')
					ren_ps(arg);
				continue;
			}
		}
		if (!word) {
			word = &words[nwords++];
			word->beg = buflen;
			word->wid = 0;
			if (newline && !blanks && nwords > 1)
				word->blanks = charwid(dev_spacewid(), n_s);
			else
				word->blanks = blanks;
			wid += word->blanks;
			newline = 0;
			blanks = 0;
		}
		if (r_s != n_s) {
			buflen += sprintf(buf + buflen, "\\s(%02d", n_s);
			r_s = n_s;
		}
		if (r_f != n_f) {
			buflen += sprintf(buf + buflen, "\\f(%02d", n_f);
			r_f = n_f;
		}
		if (utf8len(c[0]) == strlen(c))
			buflen += sprintf(buf + buflen, "%s%s", esc ? "\\" : "", c);
		else
			buflen += sprintf(buf + buflen, "\\(%s", c);
		g = dev_glyph(c, n_f);
		g_wid = charwid(g ? g->wid : dev_spacewid(), n_s);
		word->wid += g_wid;
		wid += g_wid;
	}
	ren_br(0, wid > n_l ? n_ad : 0);
	ren_br(0, 0);
}
