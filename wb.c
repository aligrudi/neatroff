/* word buffer */
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

/* the current font, size and color */
#define R_F(wb)		((wb)->r_f >= 0 ? (wb)->r_f : n_f)	/* current font */
#define R_S(wb)		((wb)->r_s >= 0 ? (wb)->r_s : n_s)	/* current size */
#define R_M(wb)		((wb)->r_m >= 0 ? (wb)->r_m : n_m)	/* current color */
#define R_CD(b)		((wb)->r_cd >= 0 ? (wb)->r_cd : n_cd)	/* current direction */
/* italic correction */
#define glyph_ic(g)	(MAX(0, (g)->urx - (g)->wid))
#define glyph_icleft(g)	(MAX(0, -(g)->llx))
/* the maximum and minimum values of bounding box coordinates */
#define BBMAX		(1 << 29)
#define BBMIN		-BBMAX

static void wb_flushsub(struct wb *wb);

void wb_init(struct wb *wb)
{
	memset(wb, 0, sizeof(*wb));
	sbuf_init(&wb->sbuf);
	wb->sub_collect = 1;
	wb->f = -1;
	wb->s = -1;
	wb->m = -1;
	wb->cd = -1;
	wb->r_f = -1;
	wb->r_s = -1;
	wb->r_m = -1;
	wb->r_cd = -1;
	wb->llx = BBMAX;
	wb->lly = BBMAX;
	wb->urx = BBMIN;
	wb->ury = BBMIN;
}

void wb_done(struct wb *wb)
{
	sbuf_done(&wb->sbuf);
}

/* update wb->st and wb->sb */
static void wb_stsb(struct wb *wb)
{
	wb->st = MIN(wb->st, wb->v - (wb->s * SC_IN / 72));
	wb->sb = MAX(wb->sb, wb->v);
}

/* update bounding box */
static void wb_bbox(struct wb *wb, int llx, int lly, int urx, int ury)
{
	wb->llx = MIN(wb->llx, wb->h + llx);
	wb->lly = MIN(wb->lly, -wb->v + lly);
	wb->urx = MAX(wb->urx, wb->h + urx);
	wb->ury = MAX(wb->ury, -wb->v + ury);
}

/* pending font, size or color changes */
static int wb_pendingfont(struct wb *wb)
{
	return wb->f != R_F(wb) || wb->s != R_S(wb) ||
			(!n_cp && wb->m != R_M(wb));
}

/* pending direction change */
static int wb_pendingdir(struct wb *wb)
{
	return wb->cd != R_CD(wb);
}

/* append font and size to the buffer if needed */
static void wb_flushfont(struct wb *wb)
{
	if (wb->f != R_F(wb)) {
		sbuf_printf(&wb->sbuf, "%cf(%02d", c_ec, R_F(wb));
		wb->f = R_F(wb);
	}
	if (wb->s != R_S(wb)) {
		if (R_S(wb) < 100)
			sbuf_printf(&wb->sbuf, "%cs(%02d", c_ec, R_S(wb));
		else
			sbuf_printf(&wb->sbuf, "%cs[%d]", c_ec, R_S(wb));
		wb->s = R_S(wb);
	}
	if (!n_cp && wb->m != R_M(wb)) {
		sbuf_printf(&wb->sbuf, "%cm[%s]", c_ec, clr_str(R_M(wb)));
		wb->m = R_M(wb);
	}
	wb_stsb(wb);
}

/* append current text direction to the buffer if needed */
void wb_flushdir(struct wb *wb)
{
	if (wb->cd != R_CD(wb)) {
		wb_flushsub(wb);
		if (dir_do)
			sbuf_printf(&wb->sbuf, "%c%c", c_ec, R_CD(wb) > 0 ? '<' : '>');
		wb->cd = R_CD(wb);
	}
}

/* apply font and size changes and flush the collected subword */
static void wb_flush(struct wb *wb)
{
	wb_flushsub(wb);
	wb_flushdir(wb);
	wb_flushfont(wb);
}

void wb_hmov(struct wb *wb, int n)
{
	wb_flushsub(wb);
	wb->h += n;
	sbuf_printf(&wb->sbuf, "%ch'%du'", c_ec, n);
}

void wb_vmov(struct wb *wb, int n)
{
	wb_flushsub(wb);
	wb->v += n;
	sbuf_printf(&wb->sbuf, "%cv'%du'", c_ec, n);
}

void wb_els(struct wb *wb, int els)
{
	wb_flushsub(wb);
	if (els > wb->els_pos)
		wb->els_pos = els;
	if (els < wb->els_neg)
		wb->els_neg = els;
	sbuf_printf(&wb->sbuf, "%cx'%du'", c_ec, els);
}

void wb_etc(struct wb *wb, char *x)
{
	wb_flush(wb);
	sbuf_printf(&wb->sbuf, "%cX%s", c_ec, x);
}

static void wb_putbuf(struct wb *wb, char *c)
{
	struct glyph *g;
	int zerowidth;
	if (c[0] == '\t' || c[0] == '' ||
			(c[0] == c_ni && (c[1] == '\t' || c[1] == ''))) {
		sbuf_append(&wb->sbuf, c);
		return;
	}
	g = dev_glyph(c, wb->f);
	zerowidth = c_hymark(c);
	if (!g && c[0] == c_ec && !zerowidth) {	/* unknown escape */
		memmove(c, c + 1, strlen(c));
		g = dev_glyph(c, wb->f);
	}
	if (g && !zerowidth && wb->icleft && glyph_icleft(g))
		wb_hmov(wb, font_wid(g->font, wb->s, glyph_icleft(g)));
	wb->icleft = 0;
	if (!c[1] || c[0] == c_ec || c[0] == c_ni || utf8one(c)) {
		if (c[0] == c_ni && c[1] == c_ec)
			sbuf_printf(&wb->sbuf, "%c%c", c_ec, c_ec);
		else
			sbuf_append(&wb->sbuf, c);
	} else {
		if (c[1] && !c[2])
			sbuf_printf(&wb->sbuf, "%c(%s", c_ec, c);
		else
			sbuf_printf(&wb->sbuf, "%cC'%s'", c_ec, c);
	}
	if (!zerowidth) {
		if (!n_cp && g) {
			if (g->llx || g->lly || g->urx || g->ury) {
				int llx = font_wid(g->font, wb->s, g->llx);
				int lly = font_wid(g->font, wb->s, g->lly);
				int urx = font_wid(g->font, wb->s, g->urx);
				int ury = font_wid(g->font, wb->s, g->ury);
				wb_bbox(wb, llx, lly, urx, ury);
			} else {	/* no bounding box information */
				int ht = wb->s * SC_PT;
				int urx = font_wid(g->font, wb->s, g->wid);
				int lly = (g->type & 1) ? -ht / 2 : 0;
				int ury = (g->type & 2) ? ht : ht / 2;
				wb_bbox(wb, 0, lly, urx, ury);
			}
		}
		wb->h += g ? font_gwid(g->font, dev_font(wb->f), wb->s, g->wid) : 0;
		wb->ct |= g ? g->type : 0;
		wb_stsb(wb);
	}
}

/* return nonzero if it cannot be hyphenated */
static int wb_hyph(char src[][GNLEN], int src_n, char *src_hyph, int flg)
{
	char word[WORDLEN * GNLEN];	/* word to pass to hyphenate() */
	char hyph[WORDLEN * GNLEN];	/* hyphenation points of word */
	int smap[WORDLEN];		/* the mapping from src[] to word[] */
	char *s, *d;
	int i;
	d = word;
	*d = '\0';
	for (i = 0; i < src_n; i++) {
		s = src[i];
		smap[i] = d - word;
		if (c_hystop(s))
			return 1;
		if (c_hymark(s))
			continue;
		d += hy_cput(d, s);
	}
	memset(hyph, 0, (d - word) * sizeof(hyph[0]));
	hyphenate(hyph, word, flg);
	for (i = 0; i < src_n; i++)
		src_hyph[i] = hyph[smap[i]];
	return 0;
}

static int wb_collect(struct wb *wb, int val)
{
	int old = wb->sub_collect;
	wb->sub_collect = val;
	return old;
}

/* output the collected characters; only for those present in wb->f font */
static void wb_flushsub(struct wb *wb)
{
	struct font *fn;
	struct glyph *gsrc[WORDLEN];
	struct glyph *gdst[WORDLEN];
	int x[WORDLEN], y[WORDLEN], xadv[WORDLEN], yadv[WORDLEN];
	int dmap[WORDLEN];
	char src_hyph[WORDLEN];
	int dst_n, i;
	int sidx = 0;
	if (!wb->sub_n || !wb->sub_collect)
		return;
	wb->sub_collect = 0;
	fn = dev_font(wb->f);
	if (!n_hy || wb_hyph(wb->sub_c, wb->sub_n, src_hyph, n_hy))
		memset(src_hyph, 0, sizeof(src_hyph));
	/* call font_layout() for collected glyphs; skip hyphenation marks */
	while (sidx < wb->sub_n) {
		int beg = sidx;
		for (; sidx < wb->sub_n && !c_hymark(wb->sub_c[sidx]); sidx++)
			gsrc[sidx - beg] = font_find(fn, wb->sub_c[sidx]);
		dst_n = font_layout(fn, gsrc, sidx - beg, wb->s,
				gdst, dmap, x, y, xadv, yadv, n_lg, n_kn);
		for (i = 0; i < dst_n; i++) {
			int xd[2] = {x[i], xadv[i] - x[i]};
			int yd[2] = {y[i], yadv[i] - y[i]};
			if (xd[wb->cd])
				wb_hmov(wb, font_wid(fn, wb->s, xd[wb->cd]));
			if (yd[wb->cd])
				wb_vmov(wb, font_wid(fn, wb->s, yd[wb->cd]));
			if (src_hyph[beg + dmap[i]])
				wb_putbuf(wb, c_hc);
			if (gdst[i] == gsrc[dmap[i]])
				wb_putbuf(wb, wb->sub_c[beg + dmap[i]]);
			else
				wb_putbuf(wb, gdst[i]->name);
			if (xd[1 - wb->cd])
				wb_hmov(wb, font_wid(fn, wb->s, xd[1 - wb->cd]));
			if (yd[1 - wb->cd])
				wb_vmov(wb, font_wid(fn, wb->s, yd[1 - wb->cd]));
		}
		for (; sidx < wb->sub_n && c_hymark(wb->sub_c[sidx]); sidx++)
			wb_putbuf(wb, wb->sub_c[sidx]);
	}
	wb->sub_n = 0;
	wb->icleft = 0;
	wb->sub_collect = 1;
}

void wb_put(struct wb *wb, char *c)
{
	if (c[0] == '\n') {
		wb->part = 0;
		return;
	}
	if (wb_pendingdir(wb))
		wb_flushdir(wb);
	if (c[0] == ' ') {
		wb_flushsub(wb);
		wb_hmov(wb, font_swid(dev_font(R_F(wb)), R_S(wb), n_ss));
		return;
	}
	if (!strcmp(c_nb, c)) {
		wb_flushsub(wb);
		sbuf_append(&wb->sbuf, c);
		wb->h += font_swid(dev_font(R_F(wb)), R_S(wb), n_ss);
		return;
	}
	if (wb_pendingfont(wb) || wb->sub_n == LEN(wb->sub_c))
		wb_flush(wb);
	if (wb->sub_collect) {
		if (font_find(dev_font(wb->f), c) || c_hymark(c))
			strcpy(wb->sub_c[wb->sub_n++], c);
		else
			wb_putraw(wb, c);
	} else {
		wb_putbuf(wb, c);
	}
}

/* just like wb_put() but disable subword collection */
void wb_putraw(struct wb *wb, char *c)
{
	int collect;
	wb_flushsub(wb);
	collect = wb_collect(wb, 0);
	wb_put(wb, c);
	wb_collect(wb, collect);
}

/* just like wb_put(), but call cdef_expand() if c is defined */
void wb_putexpand(struct wb *wb, char *c)
{
	if (cdef_expand(wb, c, R_F(wb)))
		wb_put(wb, c);
}

int wb_part(struct wb *wb)
{
	return wb->part;
}

void wb_setpart(struct wb *wb)
{
	wb->part = 1;
}

int wb_cost(struct wb *wb)
{
	return wb->cost;
}

void wb_setcost(struct wb *wb, int cost)
{
	wb->cost = cost;
}

void wb_drawl(struct wb *wb, int c, int h, int v)
{
	wb_flush(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du %du'", c_ec, c, h, v);
	wb->h += h;
	wb->v += v;
	wb_stsb(wb);
}

void wb_drawc(struct wb *wb, int c, int r)
{
	wb_flush(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du'", c_ec, c, r);
	wb->h += r;
}

void wb_drawe(struct wb *wb, int c, int h, int v)
{
	wb_flush(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du %du'", c_ec, c, h, v);
	wb->h += h;
}

void wb_drawa(struct wb *wb, int c, int h1, int v1, int h2, int v2)
{
	wb_flush(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du %du %du %du'",
			c_ec, c, h1, v1, h2, v2);
	wb->h += h1 + h2;
	wb->v += v1 + v2;
	wb_stsb(wb);
}

void wb_drawt(struct wb *wb, int c, int wid)
{
	wb_flush(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %d'", c_ec, c, wid);
}

void wb_drawxbeg(struct wb *wb, int c)
{
	wb_flush(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c", c_ec, c);
}

void wb_drawxdot(struct wb *wb, int h, int v)
{
	sbuf_printf(&wb->sbuf, " %du %du", h, v);
	wb->h += h;
	wb->v += v;
	wb_stsb(wb);
}

void wb_drawxcmd(struct wb *wb, char *cmd)
{
	sbuf_printf(&wb->sbuf, " %s", cmd);
}

void wb_drawxend(struct wb *wb)
{
	sbuf_printf(&wb->sbuf, "'");
}

void wb_reset(struct wb *wb)
{
	wb_done(wb);
	wb_init(wb);
}

char *wb_buf(struct wb *wb)
{
	wb_flushsub(wb);
	return sbuf_buf(&wb->sbuf);
}

static void wb_putc(struct wb *wb, int t, char *s)
{
	if (t && t != 'C')
		wb_flushsub(wb);
	switch (t) {
	case 0:
	case 'C':
		wb_put(wb, s);
		break;
	case 'D':
		ren_dcmd(wb, s);
		break;
	case 'f':
		wb->r_f = atoi(s);
		break;
	case 'h':
		wb_hmov(wb, atoi(s));
		break;
	case 'm':
		wb->r_m = clr_get(s);
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
	case '<':
	case '>':
		wb->r_cd = t == '<';
		wb_flushdir(wb);
		break;
	}
}

void wb_cat(struct wb *wb, struct wb *src)
{
	char *s, *d;
	int c, part;
	int collect;
	wb_flushsub(src);
	wb_flushsub(wb);
	collect = wb_collect(wb, 0);
	s = sbuf_buf(&src->sbuf);
	while ((c = escread(&s, &d)) >= 0)
		wb_putc(wb, c, d);
	part = src->part;
	wb->r_s = -1;
	wb->r_f = -1;
	wb->r_m = -1;
	wb->r_cd = -1;
	wb_reset(src);
	src->part = part;
	wb_collect(wb, collect);
}

int wb_wid(struct wb *wb)
{
	wb_flushsub(wb);
	return wb->h;
}

int wb_hpos(struct wb *wb)
{
	wb_flushsub(wb);
	return wb->h;
}

int wb_vpos(struct wb *wb)
{
	wb_flushsub(wb);
	return wb->v;
}

int wb_empty(struct wb *wb)
{
	return !wb->sub_n && sbuf_empty(&wb->sbuf);
}

/* return 1 if wb ends a sentence (.?!) */
int wb_eos(struct wb *wb)
{
	int i = wb->sub_n - 1;
	while (i > 0 && c_eostran(wb->sub_c[i]))
		i--;
	return i >= 0 && c_eossent(wb->sub_c[i]);
}

void wb_wconf(struct wb *wb, int *ct, int *st, int *sb,
		int *llx, int *lly, int *urx, int *ury)
{
	wb_flushsub(wb);
	*ct = wb->ct;
	*st = -wb->st;
	*sb = -wb->sb;
	*llx = wb->llx < BBMAX ? wb->llx : 0;
	*lly = wb->lly < BBMAX ? -wb->lly : 0;
	*urx = wb->urx > BBMIN ? wb->urx : 0;
	*ury = wb->ury > BBMIN ? -wb->ury : 0;
}

static struct glyph *wb_prevglyph(struct wb *wb)
{
	return wb->sub_n ? dev_glyph(wb->sub_c[wb->sub_n - 1], wb->f) : NULL;
}

void wb_italiccorrection(struct wb *wb)
{
	struct glyph *g = wb_prevglyph(wb);
	if (g && glyph_ic(g))
		wb_hmov(wb, font_wid(g->font, wb->s, glyph_ic(g)));
}

void wb_italiccorrectionleft(struct wb *wb)
{
	wb_flushsub(wb);
	wb->icleft = 1;
}

void wb_fnszget(struct wb *wb, int *fn, int *sz, int *m, int *cd)
{
	wb_flushsub(wb);
	*fn = wb->r_f;
	*sz = wb->r_s;
	*m = wb->r_m;
	*cd = wb->r_cd;
}

void wb_fnszset(struct wb *wb, int fn, int sz, int m, int cd)
{
	wb->r_f = fn;
	wb->r_s = sz;
	wb->r_m = m;
	wb->r_cd = cd;
}

void wb_catstr(struct wb *wb, char *s, char *end)
{
	int collect, c;
	char *d;
	wb_flushsub(wb);
	collect = wb_collect(wb, 0);
	while (s < end && (c = escread(&s, &d)) >= 0)
		wb_putc(wb, c, d);
	wb_collect(wb, collect);
}

/* return the size of \(hy if appended to wb */
int wb_hywid(struct wb *wb)
{
	struct glyph *g = dev_glyph("hy", wb->f);
	return g ? font_gwid(g->font, dev_font(R_F(wb)), R_S(wb), g->wid) : 0;
}

/* return the size of space if appended to wb */
int wb_swid(struct wb *wb)
{
	return font_swid(dev_font(R_F(wb)), R_S(wb), n_ss);
}

static char *keshideh_chars[] = {
	"ﺒ", "ﺑ", "ﭙ", "ﭘ", "ﺘ", "ﺗ", "ﺜ", "ﺛ", "ﺴ", "ﺳ",
	"ﺸ", "ﺷ", "ﻔ", "ﻓ", "ﻘ", "ﻗ", "ﮑ", "ﮐ", "ﮕ", "ﮔ",
	"ﻤ", "ﻣ", "ﻨ", "ﻧ", "ﻬ", "ﻫ", "ﯿ", "ﯾ",
};

static int keshideh(char *c)
{
	int i;
	for (i = 0; i < LEN(keshideh_chars); i++)
		if (!strcmp(keshideh_chars[i], c))
			return 1;
	return 0;
}

/* insert keshideh */
int wb_keshideh(char *word, struct wb *dst, int wid)
{
	char p[GNLEN] = "";
	char *s, *d, *s_prev = NULL, *s_kesh = NULL;
	int ins = 0;
	int c;
	/* find the last keshideh position */
	s = word;
	while ((c = escread(&s, &d)) >= 0) {
		wb_putc(dst, c, d);
		if (!c && keshideh(p)) {
			struct glyph *g = dev_glyph("ـ", R_F(dst));
			int kw = g ? font_gwid(g->font,
					dev_font(R_F(dst)), R_S(dst), g->wid) : 0;
			if (g && kw < wid) {
				s_kesh = s_prev;
				ins = kw;
			}
		}
		s_prev = s;
		strcpy(p, c ? "" : d);
	}
	/* insert the keshideh at s_kesh */
	s = word;
	wb_reset(dst);
	while ((c = escread(&s, &d)) >= 0) {
		wb_putc(dst, c, d);
		if (s == s_kesh)
			wb_putc(dst, 0, "ـ");
	}
	return ins;
}
