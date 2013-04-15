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
		OUT("s%d\n", o_s);
	}
}

static void out_ft(int n)
{
	if (n >= 0 && o_f != n) {
		o_f = n;
		OUT("f%d\n", o_f);
	}
}

static char *escarg(char *s, char *d, int cmd)
{
	int q;
	if (strchr(ESC_P, cmd)) {
		if (cmd == 's' && (*s == '-' || *s == '+'))
			*d++ = *s++;
		if (*s == '(') {
			s++;
			*d++ = *s++;
			*d++ = *s++;
		} else {
			*d++ = *s++;
			if (cmd == 's' && s[-1] >= '1' && s[-1] <= '3')
				if (isdigit(*s))
					*d++ = *s++;
		}
	}
	if (strchr(ESC_Q, cmd)) {
		q = *s++;
		while (*s && *s != q)
			*d++ = *s++;
		if (*s == q)
			s++;
	}
	if (cmd == 'z')
		*d++ = *s++;
	*d = '\0';
	return s;
}

static char *tok_str(char *d, char *s)
{
	while (isspace(*s))
		s++;
	while (*s && !isspace(*s))
		*d++ = *s++;
	*d = '\0';
	return s;
}

static char *tok_num(int *d, char *s, char **cc, int scale)
{
	char tok[ILNLEN];
	s = tok_str(tok, s);
	*d = eval(tok, 0, scale);
	if (*cc)
		*cc += sprintf(*cc, " %du", *d);
	else
		OUT(" %d", *d);
	return s;
}

/* parse \D arguments and copy them into cc; return the width */
int out_draw(char *s, char *cc)
{
	int h1, h2, v1, v2;
	int hd = 0, vd = 0;
	int c = *s++;
	if (cc)
		*cc++ = c;
	else
		OUT("D%c", c);
	switch (c) {
	case 'l':
		s = tok_num(&h1, s, &cc, 'm');
		s = tok_num(&v1, s, &cc, 'v');
		if (!cc)			/* dpost requires this */
			OUT(" .");
		hd = h1;
		vd = v1;
		break;
	case 'c':
		s = tok_num(&h1, s, &cc, 'm');
		hd = h1;
		vd = 0;
		break;
	case 'e':
		s = tok_num(&h1, s, &cc, 'm');
		s = tok_num(&v1, s, &cc, 'v');
		hd = h1;
		vd = 0;
		break;
	case 'a':
		s = tok_num(&h1, s, &cc, 'm');
		s = tok_num(&v1, s, &cc, 'v');
		s = tok_num(&h2, s, &cc, 'm');
		s = tok_num(&v2, s, &cc, 'v');
		hd = h1 + h2;
		vd = v1 + v2;
		break;
	default:
		s = tok_num(&h1, s, &cc, 'm');
		s = tok_num(&v1, s, &cc, 'v');
		hd = h1;
		vd = v1;
		while (*s) {
			s = tok_num(&h2, s, &cc, 'm');
			s = tok_num(&v2, s, &cc, 'v');
			hd += h2;
			vd += v2;
		}
		break;
	}
	if (cc)
		*cc = '\0';
	else
		OUT("\n");
	return hd;
}

void output(char *s)
{
	struct glyph *g;
	char c[GNLEN * 2];
	char arg[ILNLEN];
	while (*s) {
		s = utf8get(c, s);
		if (c[0] == '\\') {
			s = utf8get(c, s);
			if (c[0] == '(') {
				s = utf8get(c, s);
				s = utf8get(c + strlen(c), s);
			} else if (strchr("Dfhsv", c[0])) {
				s = escarg(s, arg, c[0]);
				if (c[0] == 'D') {
					out_draw(arg, NULL);
					continue;
				}
				if (c[0] == 'f') {
					out_ft(dev_font(arg));
					continue;
				}
				if (c[0] == 'h') {
					OUT("h%d", eval(arg, 0, 'm'));
					continue;
				}
				if (c[0] == 's') {
					out_ps(eval(arg, o_s, '\0'));
					continue;
				}
				if (c[0] == 'v') {
					OUT("v%d", eval(arg, 0, 'v'));
					continue;
				}
			}
		}
		g = dev_glyph(c, o_f);
		if (g) {
			if (utf8len(c[0]) == strlen(c)) {
				OUT("c%s%s", c, c[1] ? "\n" : "");
			} else {
				OUT("C%s\n", c);
			}
		}
		OUT("h%d", charwid(g ? g->wid : dev_spacewid(), o_s));
	}
}
