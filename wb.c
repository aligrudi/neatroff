#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

#define R_F(wb)		((wb)->r_f >= 0 ? (wb)->r_f : n_f)	/* current font */
#define R_S(wb)		((wb)->r_s >= 0 ? (wb)->r_s : n_s)	/* current size */

void wb_init(struct wb *wb)
{
	memset(wb, 0, sizeof(*wb));
	sbuf_init(&wb->sbuf);
	wb->f = -1;
	wb->s = -1;
	wb->r_f = -1;
	wb->r_s = -1;
}

void wb_done(struct wb *wb)
{
	sbuf_done(&wb->sbuf);
}

/* update wb->st and wb->sb */
static void wb_stsb(struct wb *wb)
{
	wb->st = MIN(wb->st, wb->v - SC_HT);
	wb->sb = MAX(wb->sb, wb->v);
}

/* append font and size to the buffer if needed */
static void wb_font(struct wb *wb)
{
	if (wb->f != R_F(wb)) {
		sbuf_printf(&wb->sbuf, "%cf(%02d", c_ec, R_F(wb));
		wb->f = R_F(wb);
	}
	if (wb->s != R_S(wb)) {
		sbuf_printf(&wb->sbuf, "%cs(%02d", c_ec, R_S(wb));
		wb->s = R_S(wb);
	}
	wb_stsb(wb);
}

void wb_hmov(struct wb *wb, int n)
{
	wb->h += n;
	sbuf_printf(&wb->sbuf, "%ch'%du'", c_ec, n);
}

void wb_vmov(struct wb *wb, int n)
{
	wb->v += n;
	sbuf_printf(&wb->sbuf, "%cv'%du'", c_ec, n);
}

void wb_els(struct wb *wb, int els)
{
	if (els > wb->els_pos)
		wb->els_pos = els;
	if (els < wb->els_neg)
		wb->els_neg = els;
	sbuf_printf(&wb->sbuf, "%cx'%du'", c_ec, els);
}

void wb_etc(struct wb *wb, char *x)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cX%s", c_ec, x);
}

void wb_put(struct wb *wb, char *c)
{
	struct glyph *g;
	if (c[0] == '\n') {
		wb->part = 0;
		return;
	}
	if (c[0] == ' ') {
		wb_hmov(wb, charwid(dev_spacewid(), R_S(wb)));
		return;
	}
	if (c[0] == '\t' || c[0] == '' ||
			(c[0] == c_ni && (c[1] == '\t' || c[1] == ''))) {
		sbuf_append(&wb->sbuf, c);
		return;
	}
	g = dev_glyph(c, R_F(wb));
	wb_font(wb);
	if (!c[1] || c[0] == c_ec || c[0] == c_ni ||
			utf8len((unsigned char) c[0]) == strlen(c)) {
		sbuf_append(&wb->sbuf, c);
	} else {
		if (c[1] && !c[2])
			sbuf_printf(&wb->sbuf, "%c(%s", c_ec, c);
		else
			sbuf_printf(&wb->sbuf, "%cC'%s'", c_ec, c);
	}
	if (strcmp(c_hc, c)) {
		wb->h += charwid(g ? g->wid : SC_DW, R_S(wb));
		wb->ct |= g ? g->type : 0;
		wb_stsb(wb);
	}
}

/* return zero if c formed a ligature with its previous character */
int wb_lig(struct wb *wb, char *c)
{
	char *p = sbuf_last(&wb->sbuf);
	char lig[GNLEN];
	if (!p || strlen(p) + strlen(c) + 4 > GNLEN)
		return 1;
	if (p[0] == c_ec && p[1] == '(')
		p += 2;
	sprintf(lig, "%s%s", p, c);
	if (dev_lig(R_F(wb), lig)) {
		wb->h = wb->prev_h;
		sbuf_pop(&wb->sbuf);
		wb_put(wb, lig);
		return 0;
	}
	return 1;
}

int wb_part(struct wb *wb)
{
	return wb->part;
}

void wb_setpart(struct wb *wb)
{
	wb->part = 1;
}

void wb_drawl(struct wb *wb, int h, int v)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'l %du %du'", c_ec, h, v);
	wb->h += h;
	wb->v += v;
	wb_stsb(wb);
}

void wb_drawc(struct wb *wb, int r)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'c %du'", c_ec, r);
	wb->h += r;
}

void wb_drawe(struct wb *wb, int h, int v)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'e %du %du'", c_ec, h, v);
	wb->h += h;
}

void wb_drawa(struct wb *wb, int h1, int v1, int h2, int v2)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'a %du %du %du %du'", c_ec, h1, v1, h2, v2);
	wb->h += h1 + h2;
	wb->v += v1 + v2;
	wb_stsb(wb);
}

void wb_drawxbeg(struct wb *wb, int c)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c", c_ec, c);
}

void wb_drawxdot(struct wb *wb, int h, int v)
{
	sbuf_printf(&wb->sbuf, " %du %du", h, v);
	wb->h += h;
	wb->v += v;
	wb_stsb(wb);
}

void wb_drawxend(struct wb *wb)
{
	sbuf_printf(&wb->sbuf, "'");
}

static void wb_reset(struct wb *wb)
{
	wb_done(wb);
	wb_init(wb);
}

static void wb_putc(struct wb *wb, int t, char *s)
{
	switch (t) {
	case 0:
	case 'C':
		wb_put(wb, s);
		break;
	case 'D':
		ren_draw(wb, s);
		break;
	case 'f':
		wb->r_f = atoi(s);
		break;
	case 'h':
		wb_hmov(wb, atoi(s));
		break;
	case 's':
		wb->r_s = atoi(s);
		break;
	case 'v':
		wb_vmov(wb, atoi(s));
		break;
	case 'x':
		wb_els(wb, atoi(s));
		break;
	case 'X':
		wb_etc(wb, s);
		break;
	}
}

void wb_cat(struct wb *wb, struct wb *src)
{
	char *s = sbuf_buf(&src->sbuf);
	char d[ILNLEN];
	int c, part;
	while ((c = out_readc(&s, d)) >= 0)
		wb_putc(wb, c, d);
	part = src->part;
	wb->r_s = -1;
	wb->r_f = -1;
	wb_reset(src);
	src->part = part;
}

int wb_wid(struct wb *wb)
{
	return wb->h;
}

int wb_empty(struct wb *wb)
{
	return sbuf_empty(&wb->sbuf);
}

void wb_wconf(struct wb *wb, int *ct, int *st, int *sb)
{
	*ct = wb->ct;
	*st = -wb->st;
	*sb = -wb->sb;
}

/* skip troff requests; return 1 if read c_hc */
static int skipreqs(char **s, struct wb *w1)
{
	char d[ILNLEN];
	char *r = *s;
	int c;
	wb_reset(w1);
	while ((c = out_readc(s, d)) > 0) {
		wb_putc(w1, c, d);
		r = *s;
	}
	if (c < 0 || !strcmp(c_hc, d))
		return 1;
	*s = r;
	return 0;
}

static char *dashpos(char *s, int w, struct wb *w1, int any)
{
	char d[ILNLEN];
	char *r = NULL;
	int c;
	skipreqs(&s, w1);
	while ((c = out_readc(&s, d)) == 0) {
		wb_putc(w1, c, d);
		if (wb_wid(w1) > w && (!any || r))
			break;
		if (!strcmp("-", d) || (!strcmp("em", d) || !strcmp("hy", d)))
			r = s;
	}
	return r;
}

static int wb_dashwid(struct wb *wb)
{
	struct glyph *g = dev_glyph("hy", R_F(wb));
	return charwid(g ? g->wid : SC_DW, R_S(wb));
}

static char *indicatorpos(char *s, int w, struct wb *w1, int flg)
{
	char d[ILNLEN];
	char *r = NULL;
	int c;
	skipreqs(&s, w1);
	while ((c = out_readc(&s, d)) == 0) {
		wb_putc(w1, c, d);
		if (wb_wid(w1) + wb_dashwid(w1) > w && (!(flg & HY_ANY) || r))
			break;
		if (!strcmp(c_hc, d))
			r = s;
	}
	return r;
}

static char *hyphpos(char *s, int w, struct wb *w1, int flg)
{
	char word[ILNLEN];
	char *map[ILNLEN];	/* mapping from word to s */
	char hyph[ILNLEN];
	char d[ILNLEN];
	char *prev_s = s;
	char *r = NULL;
	int fit = 0;
	char *wp = word, *we = word + sizeof(word);
	int beg, end;
	int i, c;
	skipreqs(&s, w1);
	while ((c = out_readc(&s, d)) == 0 && wp + strlen(d) + 1 < we) {
		wb_putc(w1, c, d);
		strcpy(wp, d);
		while (*wp)
			map[wp++ - word] = prev_s;
		if (wb_wid(w1) + wb_dashwid(w1) <= w)
			fit = wp - word;
		prev_s = s;
	}
	if (strlen(word) < 4)
		return NULL;
	hyphenate(hyph, word);
	beg = flg & HY_FIRSTTWO ? 3 : 2;
	end = strlen(word) - (flg & HY_FINAL ? 2 : 1);
	for (i = beg; i < end; i++)
		if (hyph[i] && (i <= fit || ((flg & HY_ANY) && !r)))
			r = map[i];
	return r;
}

static void dohyph(char *s, char *pos, int dash, struct wb *w1, struct wb *w2)
{
	char d[ILNLEN];
	int c = -1;
	wb_reset(w1);
	wb_reset(w2);
	while (s != pos && (c = out_readc(&s, d)) >= 0)
		wb_putc(w1, c, d);
	if (dash)
		wb_putc(w1, 0, "hy");
	w2->r_s = w1->r_s;
	w2->r_f = w1->r_f;
	while ((c = out_readc(&s, d)) >= 0)
		wb_putc(w2, c, d);
}

/* hyphenate wb into w1 and w2; return zero on success */
int wb_hyph(struct wb *wb, int w, struct wb *w1, struct wb *w2, int flg)
{
	char *s = sbuf_buf(&wb->sbuf);
	char *dp, *hp, *p;
	if (skipreqs(&s, w1))
		return 1;
	dp = dashpos(sbuf_buf(&wb->sbuf), w, w1, flg & HY_ANY);
	hp = indicatorpos(sbuf_buf(&wb->sbuf), w, w1, flg & HY_ANY);
	p = flg & HY_ANY ? MIN(dp, hp) : MAX(dp, hp);
	if (!p && flg & HY_MASK)
		p = hyphpos(sbuf_buf(&wb->sbuf), w, w1, flg & HY_ANY);
	if (p)
		dohyph(sbuf_buf(&wb->sbuf), p, p != dp, w1, w2);
	return !p;
}
