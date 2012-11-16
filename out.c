#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

static int out_blank = 0;

static int utf8len(int c)
{
	if (c <= 0x7f)
		return 1;
	if (c >= 0xfc)
		return 6;
	if (c >= 0xf8)
		return 5;
	if (c >= 0xf0)
		return 4;
	if (c >= 0xe0)
		return 3;
	if (c >= 0xc0)
		return 2;
	return 1;
}

static int nextchar(char *s)
{
	int c = tr_next();
	int l = utf8len(c);
	int i;
	if (c < 0)
		return 0;
	s[0] = c;
	for (i = 1; i < l; i++)
		s[i] = tr_next();
	s[l] = '\0';
	return l;
}

static void out_sp(int n)
{
	printf("H%d", n_o + n_i);
	printf("v%d\n", n_v * (n + 1));
	out_blank = 0;
}

void tr_br(int argc, char **args)
{
	out_sp(0);
}

void tr_sp(int argc, char **args)
{
	out_sp(argc > 1 ? atoi(args[1]) : 1);
}

void render(void)
{
	char c[LLEN];
	struct glyph *g;
	int fp = n_f;
	int ps = n_s;
	while (nextchar(c) > 0) {
		g = NULL;
		if (c[0] == '\\') {
			nextchar(c);
			if (c[0] == '(') {
				int l = nextchar(c);
				l += nextchar(c + l);
				c[l] = '\0';
			}
		}
		g = dev_glyph(c);
		if (ps != n_s) {
			printf("s%d\n", n_s);
			ps = n_s;
		}
		if (fp != n_f) {
			printf("f%d\n", n_f);
			fp = n_f;
		}
		if (g) {
			if (out_blank)
				printf("h%d", dev_spacewid() * n_s / dev_uwid);
			if (utf8len(c[0]) == strlen(c)) {
				printf("c%s%s", c, c[1] ? "\n" : "");
			} else {
				printf("C%s\n", c);
			}
			printf("h%d", g->wid * n_s / dev_uwid);
			out_blank = 0;
		} else {
			out_blank = 1;
		}
	}
}
