#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

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
	return c != 0;
}

int utf8read(char **s, char *d)
{
	int l = utf8len((unsigned char) **s);
	int i;
	for (i = 0; i < l; i++)
		d[i] = (*s)[i];
	d[l] = '\0';
	*s += l;
	return l;
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

static void escarg(char **sp, char *d, int cmd)
{
	char *s = *sp;
	int q;
	if (strchr(ESC_P, cmd)) {
		if (cmd == 's' && (*s == '-' || *s == '+'))
			*d++ = *s++;
		if (*s == '(') {
			s++;
			*d++ = *s++;
			*d++ = *s++;
		} else if (!n_cp && *s == '[') {
			s++;
			while (*s && *s != ']')
				*d++ = *s++;
			if (*s == ']')
				s++;
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
	*sp = s;
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

/*
 * read a glyph or output troff request
 *
 * This functions reads from s either an output troff request
 * (only the ones emitted by wb.c) or a glyph name and updates
 * s.  The return value is the name of the troff request (the
 * argument is copied into d) or zero for glyph names (it is
 * copied into d).  Returns -1 when the end of s is reached.
 */
int out_readc(char **s, char *d)
{
	char *r = d;
	if (!**s)
		return -1;
	utf8read(s, d);
	if (d[0] == c_ec) {
		utf8read(s, d + 1);
		if (d[1] == '(') {
			utf8read(s, d);
			utf8read(s, d + strlen(d));
		} else if (!n_cp && d[1] == '[') {
			while (**s && **s != ']')
				*r++ = *(*s)++;
			if (**s == ']')
				(*s)++;
		} else if (strchr("CDfhmsvXx", d[1])) {
			int c = d[1];
			escarg(s, d, d[1]);
			return c == 'C' ? 0 : c;
		}
	}
	if (d[0] == c_ni)
		utf8read(s, d + 1);
	return 0;
}

void out_line(char *s)
{
	struct glyph *g;
	char c[ILNLEN + GNLEN * 4];
	int t;
	while ((t = out_readc(&s, c)) >= 0) {
		if (c[0] == c_ni) {
			c[0] = c[1];
			c[1] = '\0';
		}
		if (!t) {
			if (c[0] == '\t' || c[0] == '' || !strcmp(c_hc, c))
				continue;
			g = dev_glyph(c, o_f);
			if (utf8len((unsigned char) c[0]) == strlen(c))
				outnn("c%s%s", c, c[1] ? "\n" : "");
			else
				out("C%s\n", c[0] == c_ec && c[1] == '(' ? c + 2 : c);
			outnn("h%d", charwid(g ? g->wid : SC_DW, o_s));
			continue;
		}
		switch (t) {
		case 'D':
			out_draw(c);
			break;
		case 'f':
			out_ft(dev_pos(c));
			break;
		case 'h':
			outnn("h%d", eval(c, 'm'));
			break;
		case 'm':
			if (!n_cp)
				out("m%s\n", clr_str(clr_get(c)));
			break;
		case 's':
			out_ps(eval_re(c, o_s, '\0'));
			break;
		case 'v':
			outnn("v%d", eval(c, 'v'));
			break;
		case 'X':
			out("x X %s\n", c);
			break;
		}
	}
}
