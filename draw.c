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
	if (c[0] == c_ec)
		return c[1] == '_' || c[1] == '-';
	if (!c[1])
		return c[0] == '_';
	return (c[0] == 'r' && c[1] == 'u') || (c[0] == 'u' && c[1] == 'l') ||
		(c[0] == 'r' && c[1] == 'n');
}

static int vchar(char *c)
{
	if (!c[1])
		return c[0] == '_';
	return (c[0] == 'b' && c[1] == 'v') || (c[0] == 'b' && c[1] == 'r');
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
	char lc[GNLEN];
	int l = eval_up(&arg, 'm');
	if (arg[0] == c_ec && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (!*arg || charread(&arg, lc) < 0)
		strcpy(lc, "ru");
	if (l)
		ren_hline(wb, l, lc);
}

void ren_vlcmd(struct wb *wb, char *arg)
{
	char lc[GNLEN];
	int l = eval_up(&arg, 'v');
	if (arg[0] == c_ec && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (!*arg || charread(&arg, lc) < 0)
		strcpy(lc, "br");
	if (l)
		ren_vline(wb, l, lc);
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

void ren_bcmd(struct wb *wb, char *arg)
{
	struct wb wb2;
	int n = 0, w = 0;
	int c, center;
	sstr_push(arg);		/* using ren_char()'s interface */
	wb_init(&wb2);
	c = sstr_next();
	while (c >= 0) {
		sstr_back(c);
		ren_char(&wb2, sstr_next, sstr_back, NULL);
		if (wb_wid(&wb2) > w)
			w = wb_wid(&wb2);
		wb_hmov(&wb2, -wb_wid(&wb2));
		wb_vmov(&wb2, SC_HT);
		n++;
		c = sstr_next();
	}
	sstr_pop();
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
	sstr_push(arg);		/* using ren_char()'s interface */
	wb_init(&wb2);
	wb_init(&wb3);
	c = sstr_next();
	while (c >= 0) {
		sstr_back(c);
		ren_char(&wb3, sstr_next, sstr_back, NULL);
		wc = wb_wid(&wb3);
		if (wc > w)
			w = wc;
		wb_hmov(&wb2, -wc / 2);
		wb_cat(&wb2, &wb3);
		wb_hmov(&wb2, -wc / 2);
		c = sstr_next();
	}
	sstr_pop();
	wb_hmov(wb, w / 2);
	wb_cat(wb, &wb2);
	wb_hmov(wb, w / 2);
	wb_done(&wb3);
	wb_done(&wb2);
}
