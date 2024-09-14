/* helper for drawing commands in ren.c */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static int cwid(char *c)
{
	struct wb wb;
	int w;
	wb_init(&wb);
	wb_putexpand(&wb, c);
	w = wb_wid(&wb);
	wb_done(&wb);
	return w;
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
	n = w ? l / w : 0;
	rem = w ? l % w : l;
	/* length less than character width */
	if (l < w) {
		n = 1;
		rem = 0;
		wb_hmov(wb, -(w - l) / 2);
	}
	/* the initial gap */
	if (rem) {
		if (hchar(c)) {
			wb_putexpand(wb, c);
			wb_hmov(wb, rem - w);
		} else {
			wb_hmov(wb, rem);
		}
	}
	for (i = 0; i < n; i++)
		wb_putexpand(wb, c);
	/* moving back */
	if (l < w)
		wb_hmov(wb, -(w - l + 1) / 2);
}

static void ren_vline(struct wb *wb, int l, char *c)
{
	int w, n, i, rem, hw, neg;
	neg = l < 0;
	w = SC_EM;	/* character height */
	hw = cwid(c);	/* character width */
	/* negative length; moving backwards */
	if (l < 0) {
		wb_vmov(wb, l);
		l = -l;
	}
	n = w ? l / w : 0;
	rem = w ? l % w : l;
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
			wb_putexpand(wb, c);
			wb_hmov(wb, -hw);
			wb_vmov(wb, rem - w);
		} else {
			wb_vmov(wb, rem);
		}
	}
	for (i = 0; i < n; i++) {
		wb_vmov(wb, w);
		wb_putexpand(wb, c);
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

void ren_dcmd(struct wb *wb, char *s)
{
	int h1, h2, v1, v2, w;
	int c = *s++;
	switch (tolower(c)) {
	case 'l':
		h1 = tok_num(&s, 'm');
		v1 = tok_num(&s, 'v');
		wb_drawl(wb, c, h1, v1);
		break;
	case 'c':
		h1 = tok_num(&s, 'm');
		wb_drawc(wb, c, h1);
		break;
	case 'e':
		h1 = tok_num(&s, 'm');
		v1 = tok_num(&s, 'v');
		wb_drawe(wb, c, h1, v1);
		break;
	case 'a':
		h1 = tok_num(&s, 'm');
		v1 = tok_num(&s, 'v');
		h2 = tok_num(&s, 'm');
		v2 = tok_num(&s, 'v');
		wb_drawa(wb, c, h1, v1, h2, v2);
		break;
	case '~':
	case 'p':
		wb_drawxbeg(wb, c);
		while (*s) {
			if (tok_numpt(&s, 'm', &h1) || tok_numpt(&s, 'v', &v1)) {
				char tok[64];
				int i = 0;
				while (i < sizeof(tok) - 1 && *s && *s != ' ')
					tok[i++] = *s++;
				tok[i] = '\0';
				wb_drawxcmd(wb, tok);
			} else {
				wb_drawxdot(wb, h1, v1);
			}
		}
		wb_drawxend(wb);
		break;
	case 't':
		w = tok_num(&s, 'u');
		wb_drawt(wb, c, w);
		break;
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
		ren_char(&wb2, sstr_next, sstr_back);
		if (wb_wid(&wb2) > w)
			w = wb_wid(&wb2);
		wb_hmov(&wb2, -wb_wid(&wb2));
		wb_vmov(&wb2, SC_EM);
		n++;
		c = sstr_next();
	}
	sstr_pop();
	center = -(n * SC_EM + SC_EM) / 2;
	wb_vmov(wb, center + SC_EM);
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
		ren_char(&wb3, sstr_next, sstr_back);
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

void ren_zcmd(struct wb *wb, char *arg)
{
	int h, v;
	int c;
	h = wb_hpos(wb);
	v = wb_vpos(wb);
	sstr_push(arg);
	while ((c = sstr_next()) >= 0) {
		sstr_back(c);
		ren_char(wb, sstr_next, sstr_back);
	}
	sstr_pop();
	wb_hmov(wb, h - wb_hpos(wb));
	wb_vmov(wb, v - wb_vpos(wb));
}
