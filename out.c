/* generating troff output */
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

static int o_s = 10;
static int o_f = 1;
static int o_m = 0;

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

static void out_clr(int n)
{
	if (n >= 0 && o_m != n) {
		o_m = n;
		out("m%s\n", clr_str(o_m));
	}
}

static int tok_num(char **s, int scale)
{
	while (**s == ' ' || **s == '\t')
		(*s)++;
	return eval_up(s, scale);
}

static int tok_numpt(char **s, int scale, int *i)
{
	char *o;
	while (**s == ' ' || **s == '\t')
		(*s)++;
	o = *s;
	*i = eval_up(s, scale);
	return o == *s ? 1 : 0;
}

static void out_draw(char *s)
{
	int c = *s++;
	out("D%c", c);
	switch (tolower(c)) {
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
	case '~':
	case 'p':
		while (*s) {
			int h, v;
			if (tok_numpt(&s, 'm', &h) || tok_numpt(&s, 'v', &v)) {
				outnn(" ");
				while (*s && *s != ' ')
					outnn("%c", *s++);
			} else {
				outnn(" %d", h);
				outnn(" %d", v);
			}
		}
		break;
	}
	outnn("\n");
}

static void outg(char *c, int fn, int sz)
{
	int ofn = o_f;
	int osz = o_s;
	out_ft(fn);
	out_ps(sz);
	if (utf8one(c))
		outnn("c%s%s", c, c[1] ? "\n" : "");
	else
		out("C%s\n", c[0] == c_ec && c[1] == '(' ? c + 2 : c);
	out_ft(ofn);
	out_ps(osz);
}

static void outc(char *c)
{
	struct glyph *g = dev_glyph(c, o_f);
	struct font *fn = dev_font(o_f);
	int cwid, bwid;
	if (!g)
		return;
	cwid = font_gwid(g->font, dev_font(o_f), o_s, g->wid);
	bwid = font_wid(g->font, o_s, g->wid);
	if (font_mapped(g->font, c))
		c = g->name;
	if (font_getcs(fn))
		outnn("h%d", (cwid - bwid) / 2);
	outg(c, dev_fontpos(g->font), font_zoom(g->font, o_s));
	if (font_getbd(fn)) {
		outnn("h%d", font_getbd(fn) - 1);
		outg(c, dev_fontpos(g->font), font_zoom(g->font, o_s));
		outnn("h%d", -font_getbd(fn) + 1);
	}
	if (font_getcs(fn))
		outnn("h%d", -(cwid - bwid) / 2);
	outnn("h%d", cwid);
}

void out_x(char *s)
{
	out("x X %s\n", s);
}

void out_line(char *s)
{
	char *c;
	int t;
	while ((t = escread(&s, &c)) >= 0) {
		if (!t) {
			if (c[0] == c_ni || (c[0] == '\\' && c[1] == '\\')) {
				c[0] = c[1];
				c[1] = '\0';
			}
			if (c[0] == '\t' || c[0] == '' || c_hymark(c))
				continue;
			outc(cmap_map(c));
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
				out_clr(clr_get(c));
			break;
		case 's':
			out_ps(eval(c, 0));
			break;
		case 'v':
			outnn("v%d", eval(c, 'v'));
			break;
		case 'X':
			out_x(c);
			break;
		}
	}
}
