#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static int cwid(char *c)
{
	struct glyph *g = dev_glyph(c, n_f);
	return charwid(n_f, n_s, g ? g->wid : SC_DW);
}

static int hchar(char *c)
{
	if (c[0] != c_ec)
		return c[0] == '_';
	if (c[1] != '(')
		return c[1] == '_' || c[1] == '-';
	return (c[2] == 'r' && c[3] == 'u') || (c[2] == 'u' && c[3] == 'l') ||
		(c[2] == 'r' && c[3] == 'n');
}

static int vchar(char *c)
{
	if (c[0] != c_ec || c[1] != '(')
		return c[0] == '_';
	return (c[2] == 'b' && c[3] == 'v') || (c[2] == 'b' && c[3] == 'r');
}

void ren_hline(struct wb *wb, int l, char *c)
{
	int w, n, i, rem;
	w = cwid(c);
	/* negative length; moving backwards */
	if (l < 0) {
		wb_hmov(wb, l);
		l = -l;
	}
	n = l / w;
	rem = l % w;
	/* length less than character width */
	if (l < w) {
		n = 1;
		rem = 0;
		wb_hmov(wb, -(w - l) / 2);
	}
	/* the initial gap */
	if (rem) {
		if (hchar(c)) {
			wb_put(wb, c);
			wb_hmov(wb, rem - w);
		} else {
			wb_hmov(wb, rem);
		}
	}
	for (i = 0; i < n; i++)
		wb_put(wb, c);
	/* moving back */
	if (l < w)
		wb_hmov(wb, -(w - l + 1) / 2);
}

static void ren_vline(struct wb *wb, int l, char *c)
{
	int w, n, i, rem, hw, neg;
	neg = l < 0;
	w = SC_HT;	/* character height */
	hw = cwid(c);	/* character width */
	/* negative length; moving backwards */
	if (l < 0) {
		wb_vmov(wb, l);
		l = -l;
	}
	n = l / w;
	rem = l % w;
	/* length less than character width */
	if (l < w) {
		n = 1;
		rem = 0;
		wb_vmov(wb, -w + l / 2);
	}
	/* the initial gap */
	if (rem) {
		if (vchar(c)) {
			wb_vmov(wb, w);
			wb_put(wb, c);
			wb_hmov(wb, -hw);
			wb_vmov(wb, rem - w);
		} else {
			wb_vmov(wb, rem);
		}
	}
	for (i = 0; i < n; i++) {
		wb_vmov(wb, w);
		wb_put(wb, c);
		wb_hmov(wb, -hw);
	}
	/* moving back */
	if (l < w)
		wb_vmov(wb, l / 2);
	if (neg)
		wb_vmov(wb, -l);
	wb_hmov(wb, hw);
}

void ren_hlcmd(struct wb *wb, char *arg)
{
	char lc[GNLEN] = {c_ec, '(', 'r', 'u'};
	int l = eval_up(&arg, 'm');
	if (arg[0] == c_ec && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (l)
		ren_hline(wb, l, *arg ? arg : lc);
}

void ren_vlcmd(struct wb *wb, char *arg)
{
	char lc[GNLEN] = {c_ec, '(', 'b', 'r'};
	int l = eval_up(&arg, 'v');
	if (arg[0] == c_ec && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (l)
		ren_vline(wb, l, *arg ? arg : lc);
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

void ren_dcmd(struct wb *wb, char *s)
{
	int h1, h2, v1, v2;
	int c = *s++;
	switch (c) {
	case 'l':
		h1 = tok_num(&s, 'm');
		v1 = tok_num(&s, 'v');
		wb_drawl(wb, h1, v1);
		break;
	case 'c':
		h1 = tok_num(&s, 'm');
		wb_drawc(wb, h1);
		break;
	case 'e':
		h1 = tok_num(&s, 'm');
		v1 = tok_num(&s, 'v');
		wb_drawe(wb, h1, v1);
		break;
	case 'a':
		h1 = tok_num(&s, 'm');
		v1 = tok_num(&s, 'v');
		h2 = tok_num(&s, 'm');
		v2 = tok_num(&s, 'v');
		wb_drawa(wb, h1, v1, h2, v2);
		break;
	default:
		wb_drawxbeg(wb, c);
		while (*s) {
			h1 = tok_num(&s, 'm');
			v1 = tok_num(&s, 'v');
			wb_drawxdot(wb, h1, v1);
		}
		wb_drawxend(wb);
	}
}

/*
 * the implementation of \b and \o
 *
 * ren_bcmd() and ren_ocmd() call ren_char(), which requires
 * next() and back() functions, similar to ren_next() and ren_back().
 * ln_*() here provide such an interface for the given string,
 * added via ln_push().  ln_*() may be called recursively to
 * handle \o'\b"ab"c'.
 */
static char *ln_s;

static int ln_next(void)
{
	return *ln_s ? (unsigned char) *ln_s++ : -1;
}

static void ln_back(int c)
{
	ln_s--;
}

static char *ln_push(char *s)
{
	char *old_s = ln_s;
	ln_s = s;
	return old_s;
}

static void ln_pop(char *s)
{
	ln_s = s;
}

void ren_bcmd(struct wb *wb, char *arg)
{
	struct wb wb2;
	int n = 0, w = 0;
	int c, center;
	char *ln_prev = ln_push(arg);
	wb_init(&wb2);
	c = ln_next();
	while (c >= 0) {
		ln_back(c);
		ren_char(&wb2, ln_next, ln_back);
		if (wb_wid(&wb2) > w)
			w = wb_wid(&wb2);
		wb_hmov(&wb2, -wb_wid(&wb2));
		wb_vmov(&wb2, SC_HT);
		n++;
		c = ln_next();
	}
	ln_pop(ln_prev);
	center = -(n * SC_HT + SC_EM) / 2;
	wb_vmov(wb, center + SC_HT);
	wb_cat(wb, &wb2);
	wb_done(&wb2);
	wb_vmov(wb, center);
	wb_hmov(wb, w);
}

void ren_ocmd(struct wb *wb, char *arg)
{
	struct wb wb2, wb3;
	int w = 0, wc;
	int c;
	char *ln_prev = ln_push(arg);
	wb_init(&wb2);
	wb_init(&wb3);
	c = ln_next();
	while (c >= 0) {
		ln_back(c);
		ren_char(&wb3, ln_next, ln_back);
		wc = wb_wid(&wb3);
		if (wc > w)
			w = wc;
		wb_hmov(&wb2, -wc / 2);
		wb_cat(&wb2, &wb3);
		wb_hmov(&wb2, -wc / 2);
		c = ln_next();
	}
	ln_pop(ln_prev);
	wb_hmov(wb, w / 2);
	wb_cat(wb, &wb2);
	wb_hmov(wb, w / 2);
	wb_done(&wb3);
	wb_done(&wb2);
}
