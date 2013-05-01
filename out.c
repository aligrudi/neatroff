#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

static int out_nl = 1;

/* output troff code; newlines may appear only at the end of s */
static void out_out(char *s, va_list ap)
{
	out_nl = strchr(s, '\n') != NULL;
	vfprintf(stdout, s, ap);
}

/* output troff code; no preceding newline is necessary */
static void outnn(char *s, ...)
{
	va_list ap;
	va_start(ap, s);
	out_out(s, ap);
	va_end(ap);
}

/* output troff cmd; should appear after a newline */
void out(char *s, ...)
{
	va_list ap;
	if (!out_nl)
		outnn("\n");
	va_start(ap, s);
	out_out(s, ap);
	va_end(ap);
}

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

char *utf8get(char *d, char *s)
{
	int l = utf8len((unsigned char) *s);
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
		out("s%d\n", o_s);
	}
}

static void out_ft(int n)
{
	if (n >= 0 && o_f != n) {
		o_f = n;
		out("f%d\n", o_f);
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

static int tok_num(char **s, int scale)
{
	char tok[ILNLEN];
	char *d = tok;
	while (isspace(**s))
		(*s)++;
	while (**s && !isspace(**s))
		*d++ = *(*s)++;
	*d = '\0';
	return eval(tok, scale);
}

static void out_draw(char *s)
{
	int c = *s++;
	out("D%c", c);
	switch (c) {
	case 'l':
		outnn(" %d", tok_num(&s, 'm'));
		outnn(" %d", tok_num(&s, 'v'));
		outnn(" .");			/* dpost requires this */
		break;
	case 'c':
		outnn(" %d", tok_num(&s, 'm'));
		break;
	case 'e':
		outnn(" %d", tok_num(&s, 'm'));
		outnn(" %d", tok_num(&s, 'v'));
		break;
	case 'a':
		outnn(" %d", tok_num(&s, 'm'));
		outnn(" %d", tok_num(&s, 'v'));
		outnn(" %d", tok_num(&s, 'm'));
		outnn(" %d", tok_num(&s, 'v'));
		break;
	default:
		outnn(" %d", tok_num(&s, 'm'));
		outnn(" %d", tok_num(&s, 'v'));
		while (*s) {
			outnn(" %d", tok_num(&s, 'm'));
			outnn(" %d", tok_num(&s, 'v'));
		}
		break;
	}
	outnn("\n");
}

void out_line(char *s)
{
	struct glyph *g;
	char c[GNLEN * 4];
	char arg[ILNLEN];
	while (*s) {
		s = utf8get(c, s);
		if (c[0] == '\\') {
			s = utf8get(c + 1, s);
			if (c[1] == '(') {
				s = utf8get(c + 2, s);
				s = utf8get(c + strlen(c), s);
			} else if (c[1] == '\\') {
				c[1] = '\0';
			} else if (strchr("DfhsvX", c[1])) {
				s = escarg(s, arg, c[1]);
				if (c[1] == 'D') {
					out_draw(arg);
					continue;
				}
				if (c[1] == 'f') {
					out_ft(dev_font(arg));
					continue;
				}
				if (c[1] == 'h') {
					outnn("h%d", eval(arg, 'm'));
					continue;
				}
				if (c[1] == 's') {
					out_ps(eval_re(arg, o_s, '\0'));
					continue;
				}
				if (c[1] == 'v') {
					outnn("v%d", eval(arg, 'v'));
					continue;
				}
				if (c[1] == 'X') {
					out("x X %s\n", arg);
					continue;
				}
			}
		}
		g = dev_glyph(c, o_f);
		if (utf8len(c[0]) == strlen(c))
			outnn("c%s%s", c, c[1] ? "\n" : "");
		else
			out("C%s\n", c[0] == '\\' && c[1] == '(' ? c + 2 : c);
		outnn("h%d", charwid(g ? g->wid : SC_DW, o_s));
	}
}
