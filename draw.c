#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

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

static int cwid(char *c)
{
	struct glyph *g = dev_glyph(c, n_f);
	return charwid(g ? g->wid : SC_DW, n_s);
}

static int hchar(char *c)
{
	if (c[0] != '\\')
		return c[0] == '_';
	if (c[1] != '(')
		return c[1] == '_' || c[1] == '-';
	return (c[2] == 'r' && c[3] == 'u') || (c[2] == 'u' && c[3] == 'l') ||
		(c[2] == 'r' && c[3] == 'n');
}

static int vchar(char *c)
{
	if (c[0] != '\\' || c[1] != '(')
		return c[0] == '_';
	return (c[2] == 'b' && c[3] == 'v') || (c[2] == 'b' && c[3] == 'r');
}

void ren_hline(struct wb *wb, char *arg)
{
	char *lc = "\\(ru";
	int w, l, n, i, rem;
	l = eval_up(&arg, 'm');
	if (!l)
		return;
	if (arg[0] == '\\' && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (*arg)
		lc = arg;
	w = cwid(lc);
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
		if (hchar(lc)) {
			wb_put(wb, lc);
			wb_hmov(wb, rem - w);
		} else {
			wb_hmov(wb, rem);
		}
	}
	for (i = 0; i < n; i++)
		wb_put(wb, lc);
	/* moving back */
	if (l < w)
		wb_hmov(wb, -(w - l + 1) / 2);
}

void ren_vline(struct wb *wb, char *arg)
{
	char *lc = "\\(br";
	int w, l, n, i, rem, hw, neg;
	l = eval_up(&arg, 'm');
	if (!l)
		return;
	neg = l < 0;
	if (arg[0] == '\\' && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (*arg)
		lc = arg;
	w = SC_HT;	/* character height */
	hw = cwid(lc);		/* character width */
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
		if (vchar(lc)) {
			wb_vmov(wb, w);
			wb_put(wb, lc);
			wb_hmov(wb, -hw);
			wb_vmov(wb, rem - w);
		} else {
			wb_vmov(wb, rem);
		}
	}
	for (i = 0; i < n; i++) {
		wb_vmov(wb, w);
		wb_put(wb, lc);
		wb_hmov(wb, -hw);
	}
	/* moving back */
	if (l < w)
		wb_vmov(wb, l / 2);
	if (neg)
		wb_vmov(wb, -l);
	wb_hmov(wb, hw);
}

void ren_bracket(struct wb *wb, char *arg)
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

void ren_over(struct wb *wb, char *arg)
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

void ren_draw(struct wb *wb, char *s)
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
