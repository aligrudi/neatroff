#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "xroff.h"

void wb_init(struct wb *wb)
{
	memset(wb, 0, sizeof(*wb));
	sbuf_init(&wb->sbuf);
	wb->f = -1;
	wb->s = -1;
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
	if (wb->f != n_f) {
		sbuf_printf(&wb->sbuf, "%cf(%02d", c_ec, n_f);
		wb->f = n_f;
	}
	if (wb->s != n_s) {
		sbuf_printf(&wb->sbuf, "%cs(%02d", c_ec, n_s);
		wb->s = n_s;
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
		wb_hmov(wb, charwid(dev_spacewid(), n_s));
		return;
	}
	g = dev_glyph(c, n_f);
	wb_font(wb);
	sbuf_append(&wb->sbuf, c);
	wb->h += charwid(g ? g->wid : SC_DW, n_s);
	wb->ct |= g ? g->type : 0;
	wb_stsb(wb);
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

void wb_reset(struct wb *wb)
{
	sbuf_done(&wb->sbuf);
	sbuf_init(&wb->sbuf);
	wb->els_pos = 0;
	wb->els_neg = 0;
	wb->ct = 0;
	wb->sb = 0;
	wb->st = 0;
	wb->h = 0;
	wb->v = 0;
	wb->f = -1;
	wb->s = -1;
}

void wb_cat(struct wb *wb, struct wb *src)
{
	sbuf_append(&wb->sbuf, sbuf_buf(&src->sbuf));
	if (src->f >= 0)
		wb->f = src->f;
	if (src->s >= 0)
		wb->s = src->s;
	wb_els(wb, src->els_neg);
	wb_els(wb, src->els_pos);
	if (src->part)
		wb->part = src->part;
	wb->ct |= src->ct;
	wb->st = MIN(wb->st, wb->v + src->st);
	wb->sb = MAX(wb->sb, wb->v + src->sb);
	wb->h += src->h;
	wb->v += src->v;
	wb_reset(src);
}

int wb_wid(struct wb *wb)
{
	return wb->h;
}

int wb_empty(struct wb *wb)
{
	return sbuf_empty(&wb->sbuf);
}

void wb_getels(struct wb *wb, int *els_neg, int *els_pos)
{
	*els_neg = wb->els_neg;
	*els_pos = wb->els_pos;
}

void wb_wconf(struct wb *wb, int *ct, int *st, int *sb)
{
	*ct = wb->ct;
	*st = -wb->st;
	*sb = -wb->sb;
}
