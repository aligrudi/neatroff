#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

int utf8len(int c)
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

static char *utf8get(char *d, char *s)
{
	int l = utf8len(*s);
	int i;
	for (i = 0; i < l; i++)
		d[i] = s[i];
	d[l] = '\0';
	return s + l;
}

static int o_s = 10;
static int o_f = 1;

static void out_ps(int n)
{
	if (o_s != n) {
		o_s = n;
		printf("s%d\n", o_s);
	}
}

static void out_ft(int n)
{
	if (n >= 0 && o_f != n) {
		o_f = n;
		printf("f%d\n", o_f);
	}
}

static char *escarg(char *s, char *d)
{
	if (*s == '(') {
		s++;
		*d++ = *s++;
		*d++ = *s++;
	} else if (*s == '\'') {
		s++;
		while (*s >= 0 && *s != '\'')
			*d++ = *s++;
		if (*s == '\'')
			s++;
	} else {
		*d++ = *s++;
	}
	*d = '\0';
	return s;
}

void out_put(char *s)
{
	struct glyph *g;
	char c[LLEN];
	char arg[LINELEN];
	int o_blank = 0;
	printf("v%d\n", n_v);
	printf("H%d\n", n_o + n_i);
	while (*s) {
		s = utf8get(c, s);
		if (c[0] == '\\') {
			s = utf8get(c, s);
			if (c[0] == '(') {
				s = utf8get(c, s);
				s = utf8get(c + strlen(c), s);
			} else if (strchr("sf", c[0])) {
				s = escarg(s, arg);
				if (c[0] == 's') {
					out_ps(tr_int(arg, o_s, '\0'));
					continue;
				}
				if (c[0] == 'f') {
					out_ft(dev_font(arg));
					continue;
				}
			}
		}
		g = dev_glyph(c, o_f);
		if (g) {
			if (o_blank)
				printf("h%d", charwid(dev_spacewid(), o_s));
			if (utf8len(c[0]) == strlen(c)) {
				printf("c%s%s", c, c[1] ? "\n" : "");
			} else {
				printf("C%s\n", c);
			}
			printf("h%d", charwid(g->wid, o_s));
			o_blank = 0;
		} else {
			o_blank = 1;
		}
	}
}
