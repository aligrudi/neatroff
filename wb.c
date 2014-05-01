/* word buffer */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

/* the current font, size and color */
#define R_F(wb)		((wb)->r_f >= 0 ? (wb)->r_f : n_f)	/* current font */
#define R_S(wb)		((wb)->r_s >= 0 ? (wb)->r_s : n_s)	/* current size */
#define R_M(wb)		((wb)->r_m >= 0 ? (wb)->r_m : n_m)	/* current color */
/* italic correction */
#define glyph_ic(g)	(MAX(0, (g)->urx - (g)->wid))
#define glyph_icleft(g)	(MAX(0, -(g)->llx))
/* like DEVWID() but handles negative w */
#define SDEVWID(sz, w)	((w) >= 0 ? DEVWID((sz), (w)) : -DEVWID((sz), -(w)))
/* the maximum and minimum values of bounding box coordinates */
#define BBMAX		(1 << 29)
#define BBMIN		-BBMAX

void wb_init(struct wb *wb)
{
	memset(wb, 0, sizeof(*wb));
	sbuf_init(&wb->sbuf);
	wb->f = -1;
	wb->s = -1;
	wb->m = -1;
	wb->r_f = -1;
	wb->r_s = -1;
	wb->r_m = -1;
	wb->icleft_ll = -1;
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
	if (!n_cp && wb->m != R_M(wb)) {
		sbuf_printf(&wb->sbuf, "%cm[%s]", c_ec, clr_str(R_M(wb)));
		wb->m = R_M(wb);
	}
	wb_stsb(wb);
}

/* pending font, size or color changes */
static int wb_pendingfont(struct wb *wb)
{
	return wb->f != R_F(wb) || wb->s != R_S(wb) ||
			(!n_cp && wb->m != R_M(wb));
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

/* make sure nothing is appended to wb after the last wb_put() */
static void wb_prevcheck(struct wb *wb)
{
	if (wb->prev_ll != sbuf_len(&wb->sbuf))
		wb->prev_n = 0;
}

/* mark wb->prev_c[] as valid */
static void wb_prevok(struct wb *wb)
{
	wb->prev_ll = sbuf_len(&wb->sbuf);
}

/* append c to wb->prev_c[] */
static void wb_prevput(struct wb *wb, char *c, int ll)
{
	if (wb->prev_n == LEN(wb->prev_c))
		wb->prev_n--;
	memmove(wb->prev_l + 1, wb->prev_l, wb->prev_n * sizeof(wb->prev_l[0]));
	memmove(wb->prev_h + 1, wb->prev_h, wb->prev_n * sizeof(wb->prev_h[0]));
	memmove(wb->prev_c + 1, wb->prev_c, wb->prev_n * sizeof(wb->prev_c[0]));
	wb->prev_l[0] = ll;
	wb->prev_h[0] = wb->h;
	strcpy(wb->prev_c[0], c);
	wb->prev_n++;
	wb_prevok(wb);
}

/* strip the last i characters from wb */
static void wb_prevpop(struct wb *wb, int i)
{
	int n = wb->prev_n - i;
	sbuf_cut(&wb->sbuf, wb->prev_l[i - 1]);
	wb->h = wb->prev_h[i - 1];
	memmove(wb->prev_l, wb->prev_l + i, n * sizeof(wb->prev_l[0]));
	memmove(wb->prev_h, wb->prev_h + i, n * sizeof(wb->prev_h[0]));
	memmove(wb->prev_c, wb->prev_c + i, n * sizeof(wb->prev_c[0]));
	wb->prev_n = n;
	wb->prev_ll = sbuf_len(&wb->sbuf);
}

/* return the i-th last character inserted via wb_put() */
static char *wb_prev(struct wb *wb, int i)
{
	wb_prevcheck(wb);
	return i < wb->prev_n ? wb->prev_c[i] : NULL;
}

static struct glyph *wb_prevglyph(struct wb *wb)
{
	return wb_prev(wb, 0) ? dev_glyph(wb_prev(wb, 0), wb->f) : NULL;
}

void wb_put(struct wb *wb, char *c)
{
	struct glyph *g;
	int ll, zerowidth;
	if (c[0] == '\n') {
		wb->part = 0;
		return;
	}
	if (c[0] == ' ') {
		wb_hmov(wb, spacewid(R_F(wb), R_S(wb)));
		return;
	}
	if (c[0] == '\t' || c[0] == '' ||
			(c[0] == c_ni && (c[1] == '\t' || c[1] == ''))) {
		sbuf_append(&wb->sbuf, c);
		return;
	}
	g = dev_glyph(c, R_F(wb));
	zerowidth = !strcmp(c_hc, c) || !strcmp(c_bp, c);
	if (!g && c[0] == c_ec && !zerowidth) {	/* unknown escape */
		memmove(c, c + 1, strlen(c));
		g = dev_glyph(c, R_F(wb));
	}
	if (g && !zerowidth && wb->icleft_ll == sbuf_len(&wb->sbuf))
		if (glyph_icleft(g))
			wb_hmov(wb, SDEVWID(R_S(wb), glyph_icleft(g)));
	wb->icleft_ll = -1;
	wb_font(wb);
	wb_prevcheck(wb);		/* make sure wb->prev_c[] is valid */
	ll = sbuf_len(&wb->sbuf);	/* sbuf length before inserting c */
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
		wb_prevput(wb, c, ll);
		if (!n_cp && g)
			wb_bbox(wb, SDEVWID(wb->s, g->llx),
				SDEVWID(wb->s, g->lly),
				SDEVWID(wb->s, g->urx),
				SDEVWID(wb->s, g->ury));
		wb->h += charwid(wb->f, wb->s, g ? g->wid : 0);
		wb->ct |= g ? g->type : 0;
		wb_stsb(wb);
	}
}

/* just like wb_put(), but call cdef_expand() if c is defined */
void wb_putexpand(struct wb *wb, char *c)
{
	if (cdef_expand(wb, c, R_F(wb)))
		wb_put(wb, c);
}

/* return zero if c formed a ligature with its previous character */
int wb_lig(struct wb *wb, char *c)
{
	char lig[GNLEN] = "";
	char *cs[LIGLEN + 2];
	int i = -1;
	int ligpos;
	if (wb_pendingfont(wb))		/* font changes disable ligatures */
		return 1;
	cs[0] = c;
	while (wb_prev(wb, ++i))
		cs[i + 1] = wb_prev(wb, i);
	ligpos = font_lig(dev_font(R_F(wb)), cs, i + 1);
	if (ligpos > 1) {
		for (i = 0; i < ligpos - 1; i++)
			strcat(lig, wb_prev(wb, ligpos - i - 2));
		strcat(lig, c);
		wb_prevpop(wb, ligpos - 1);
		wb_put(wb, lig);
		return 0;
	}
	return 1;
}

/* return 0 if pairwise kerning was done */
int wb_kern(struct wb *wb, char *c)
{
	int val;
	if (wb_pendingfont(wb) || !wb_prev(wb, 0))
		return 1;
	val = font_kern(dev_font(R_F(wb)), wb_prev(wb, 0), c);
	if (val)
		wb_hmov(wb, charwid(R_F(wb), R_S(wb), val));
	wb_prevok(wb);		/* kerning should not prevent ligatures */
	return !val;
}

int wb_part(struct wb *wb)
{
	return wb->part;
}

void wb_setpart(struct wb *wb)
{
	wb->part = 1;
}

void wb_drawl(struct wb *wb, int c, int h, int v)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du %du'", c_ec, c, h, v);
	wb->h += h;
	wb->v += v;
	wb_stsb(wb);
}

void wb_drawc(struct wb *wb, int c, int r)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du'", c_ec, c, r);
	wb->h += r;
}

void wb_drawe(struct wb *wb, int c, int h, int v)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du %du'", c_ec, c, h, v);
	wb->h += h;
}

void wb_drawa(struct wb *wb, int c, int h1, int v1, int h2, int v2)
{
	wb_font(wb);
	sbuf_printf(&wb->sbuf, "%cD'%c %du %du %du %du'", c_ec, c, h1, v1, h2, v2);
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
	wb_done(wb);
	wb_init(wb);
}

char *wb_buf(struct wb *wb)
{
	return sbuf_buf(&wb->sbuf);
}

static void wb_putc(struct wb *wb, int t, char *s)
{
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
	}
}

void wb_cat(struct wb *wb, struct wb *src)
{
	char *s = sbuf_buf(&src->sbuf);
	char d[ILNLEN];
	int c, part;
	while ((c = escread(&s, d)) >= 0)
		wb_putc(wb, c, d);
	part = src->part;
	wb->r_s = -1;
	wb->r_f = -1;
	wb->r_m = -1;
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

/* return 1 if wb ends a sentence (.?!) */
int wb_eos(struct wb *wb)
{
	int i = 0;
	while (wb_prev(wb, i) && strchr("'\")]*", wb_prev(wb, i)[0]))
		i++;
	return wb_prev(wb, i) && strchr(".?!", wb_prev(wb, i)[0]);
}

void wb_wconf(struct wb *wb, int *ct, int *st, int *sb,
		int *llx, int *lly, int *urx, int *ury)
{
	*ct = wb->ct;
	*st = -wb->st;
	*sb = -wb->sb;
	*llx = wb->llx < BBMAX ? wb->llx : 0;
	*lly = wb->lly < BBMAX ? -wb->lly : 0;
	*urx = wb->urx > BBMIN ? wb->urx : 0;
	*ury = wb->ury > BBMIN ? -wb->ury : 0;
}

/* skip troff requests; return 1 if read c_hc */
static int skipreqs(char **s, struct wb *w1)
{
	char d[ILNLEN];
	char *r = *s;
	int c;
	if (w1)
		wb_reset(w1);
	while ((c = escread(s, d)) > 0) {
		if (w1)
			wb_putc(w1, c, d);
		r = *s;
	}
	if (c < 0 || !strcmp(c_hc, d))
		return 1;
	*s = r;
	return 0;
}

/* return the size of \(hy if appended to wb */
int wb_dashwid(struct wb *wb)
{
	struct glyph *g = dev_glyph("hy", R_F(wb));
	return charwid(R_F(wb), R_S(wb), g ? g->wid : 0);
}

/* find explicit hyphenation positions: dashes, \: and \% */
int wb_hyphmark(char *word, int *hyidx, int *hyins)
{
	char d[ILNLEN];
	char *s = word;
	int c, n = 0;
	if (skipreqs(&s, NULL))
		return -1;
	while ((c = escread(&s, d)) >= 0 && n < NHYPHS) {
		if (!c && !strcmp(c_hc, d)) {
			hyins[n] = 1;
			hyidx[n++] = s - word;
		}
		if (!c && (!strcmp(c_bp, d) || !strcmp("-", d) ||
				(!strcmp("em", d) || !strcmp("hy", d)))) {
			hyins[n] = 0;
			hyidx[n++] = s - word;
		}
	}
	return n;
}

/* find the hyphenation positions of the given word */
int wb_hyph(char *src, int *hyidx, int flg)
{
	char word[ILNLEN];	/* word to pass to hyphenate() */
	char hyph[ILNLEN];	/* hyphenation points returned from hyphenate() */
	char *iw[ILNLEN];	/* beginning of i-th char in word */
	char *is[ILNLEN];	/* beginning of i-th char in s */
	int n = 0;		/* the number of characters in word */
	int nhy = 0;		/* number of hyphenations found */
	char d[ILNLEN];
	struct wb wb;
	char *s = src;
	char *prev_s = s;
	char *wp = word, *we = word + sizeof(word);
	int i, c;
	wb_init(&wb);
	skipreqs(&s, &wb);
	while ((c = escread(&s, d)) >= 0 && (c > 0 || strlen(d) + 1 < we - wp)) {
		wb_putc(&wb, c, d);
		if (c == 0) {
			iw[n] = wp;
			is[n] = prev_s;
			/* ignore multi-char aliases except for ligatures */
			if (!utf8one(d) && !font_islig(dev_font(R_F(&wb)), d))
				strcpy(d, ".");
			strcpy(wp, d);
			wp = strchr(wp, '\0');
			n++;
		}
		prev_s = s;
	}
	wb_done(&wb);
	if (n < 3)
		return 0;
	hyphenate(hyph, word, flg);
	for (i = 1; i < n - 1 && nhy < NHYPHS; i++)
		if (hyph[iw[i] - word])
			hyidx[nhy++] = is[i] - src;
	return nhy;
}

void wb_italiccorrection(struct wb *wb)
{
	struct glyph *g = wb_prevglyph(wb);
	if (g && glyph_ic(g))
		wb_hmov(wb, SDEVWID(wb->s, glyph_ic(g)));
}

void wb_italiccorrectionleft(struct wb *wb)
{
	wb->icleft_ll = sbuf_len(&wb->sbuf);
}

void wb_fnszget(struct wb *wb, int *fn, int *sz, int *m)
{
	*fn = wb->r_f;
	*sz = wb->r_s;
	*m = wb->r_m;
}

void wb_fnszset(struct wb *wb, int fn, int sz, int m)
{
	wb->r_f = fn;
	wb->r_s = sz;
	wb->r_m = m;
}

void wb_catstr(struct wb *wb, char *s, char *end)
{
	char d[ILNLEN];
	int c;
	while (s < end && (c = escread(&s, d)) >= 0)
		wb_putc(wb, c, d);
}
