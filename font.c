/* font handling */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

/* convert wid in device unitwidth size to size sz */
#define DEVWID(sz, wid)		(((wid) * (sz) + (dev_uwid / 2)) / dev_uwid)
#define GHASH(g1, g2)		((((g2) + 1) << 16) | ((g1) + 1))

#define GF_PAT		1	/* gsub/gpos pattern glyph */
#define GF_REP		2	/* gsub replacement glyph */
#define GF_CON		4	/* context glyph */
#define GF_GRP		8	/* glyph group */

/* glyph pattern for gsub and gpos tables; each grule has some gpats */
struct gpat {
	short g;		/* glyph index */
	short flg;		/* pattern flags; GF_* */
	short x, y, xadv, yadv;	/* gpos data */
};

/* glyph substitution and positioning rules */
struct grule {
	struct gpat *pats;
	short len;	/* pats[] length */
	short feat;	/* feature owning this rule */
	short pos;	/* position of this rule in the file */
	int hash;	/* hash of this rule for sorting and comparison */
};

struct font {
	char name[FNLEN];
	char fontname[FNLEN];
	int spacewid;
	int special;
	int cs, cs_ps, bd, zoom;	/* for .cs, .bd, .fzoom requests */
	struct glyph gl[NGLYPHS];	/* glyphs present in the font */
	int gl_n;			/* number of glyphs in the font */
	struct dict gl_dict;		/* mapping from gl[i].id to i */
	struct dict ch_dict;		/* charset mapping */
	struct dict ch_map;		/* characters mapped via font_map() */
	/* font features */
	char feat_name[NFEATS][8];	/* feature names */
	int feat_set[NFEATS];		/* feature enabled */
	int feat_n;
	/* glyph substitution and positioning */
	struct grule gsub[NGRULES];	/* glyph substitution rules */
	int gsub_n;
	struct grule gpos[NGRULES];	/* glyph positioning rules */
	int gpos_n;
	int *ggrp[NGRULES];		/* glyph groups */
	int ggrp_len[NGRULES];
};

/* find a glyph by its name */
struct glyph *font_find(struct font *fn, char *name)
{
	int i = dict_get(&fn->ch_map, name);
	if (i == -1)
		i = dict_get(&fn->ch_dict, name);
	return i >= 0 ? fn->gl + i : NULL;
}

/* find a glyph by its device-dependent identifier */
struct glyph *font_glyph(struct font *fn, char *id)
{
	int i = dict_get(&fn->gl_dict, id);
	return i >= 0 ? &fn->gl[i] : NULL;
}

static struct glyph *font_glyphput(struct font *fn, char *id, char *name, int type)
{
	int i = fn->gl_n++;
	struct glyph *g;
	g = &fn->gl[i];
	strcpy(g->id, id);
	strcpy(g->name, name);
	g->type = type;
	g->font = fn;
	dict_put(&fn->gl_dict, g->id, i);
	return g;
}

/* map character name to the given glyph; remove the mapping if id is NULL */
int font_map(struct font *fn, char *name, char *id)
{
	int gidx = -1;
	if (id)
		gidx = font_glyph(fn, id) ? font_glyph(fn, id) - fn->gl : -2;
	dict_put(&fn->ch_map, name, gidx);
	return 0;
}

/* return nonzero if character name has been mapped with font_map() */
int font_mapped(struct font *fn, char *name)
{
	return dict_get(&fn->ch_map, name) != -1;
}

/* enable/disable ligatures; first bit for liga and the second bit for rlig */
static int font_featlg(struct font *fn, int val)
{
	int ret = 0;
	ret |= font_feat(fn, "liga", val & 1);
	ret |= font_feat(fn, "rlig", val & 2) << 1;
	return ret;
}

/* enable/disable pairwise kerning */
static int font_featkn(struct font *fn, int val)
{
	return font_feat(fn, "kern", val);
}

/* glyph index in fn->glyphs[] */
static int font_idx(struct font *fn, struct glyph *g)
{
	return g ? g - fn->gl : -1;
}

/* compare their hashes, then their positions to make qsort() stable */
static int grulecmp(void *v1, void *v2)
{
	struct grule *r1 = v1;
	struct grule *r2 = v2;
	return r1->hash == r2->hash ? r1->pos - r2->pos : r1->hash - r2->hash;
}

/* the hashing function for grule structs, based on their first two glyphs */
static int grule_hash(struct grule *rule)
{
	int g1 = -1, g2 = -1;
	int i = 0;
	/* finding the first glyph; -1 if FG_GRP */
	while (i < rule->len && rule->pats[i].flg & (GF_REP | GF_CON))
		i++;		/* skipping replacement and context glyphs */
	if (i < rule->len && rule->pats[i].flg == GF_PAT)
		g1 = rule->pats[i].g;
	i++;
	/* finding the second glyph; -1 if FG_GRP */
	while (i < rule->len && rule->pats[i].flg & GF_REP)
		i++;		/* skipping replacement glyphs */
	if (i < rule->len && rule->pats[i].flg == GF_PAT)
		g2 = rule->pats[i].g;
	return GHASH(g1, g2);
}

static int grule_find(struct grule *rules, int n, int hash)
{
	int l = 0;
	int h = n;
	while (l < h) {
		int m = (l + h) >> 1;
		if (rules[m].hash >= hash)
			h = m;
		else
			l = m + 1;
	}
	return l;
}

static int font_gpatmatch(struct font *fn, struct gpat *p, int g)
{
	int i;
	if (!(p->flg & GF_GRP))
		return p->g == g;
	for (i = 0; i < fn->ggrp_len[p->g]; i++)
		if (fn->ggrp[p->g][i] == g)
			return 1;
	return 0;
}

static int font_rulematch(struct font *fn, struct grule *rule,
			int *src, int slen, int *dst, int dlen)
{
	int sidx = 0;		/* the index of matched glyphs in src */
	int ncon = 0;		/* number of initial context glyphs */
	struct gpat *pats = rule->pats;
	int j;
	if (!fn->feat_set[rule->feat])
		return 0;
	/* the number of initial context glyphs */
	for (j = 0; j < rule->len && pats[j].flg & GF_CON; j++)
		ncon++;
	if (dlen < ncon)
		return 0;
	/* matching the base pattern */
	for (; j < rule->len; j++) {
		if (pats[j].flg & GF_REP)
			continue;
		if (sidx >= slen || !font_gpatmatch(fn, &pats[j], src[sidx]))
			return 0;
		sidx++;
	}
	/* matching the initial context */
	for (j = 0; j < rule->len && pats[j].flg & GF_CON; j++)
		if (!font_gpatmatch(fn, &pats[j], dst[j - ncon]))
			return 0;
	return 1;
}

/* perform all possible gpos rules on src */
static void font_performgpos(struct font *fn, int *src, int slen,
		int *x, int *y, int *xadv, int *yadv)
{
	struct grule *gpos = fn->gpos;
	int n = fn->gpos_n;
	int i, j, k;
	for (i = 0; i < slen; i++) {
		/* possible hash values for matching gpos rules at src + slen */
		for (j = 0; j < 4 && j < (slen << 1); j++) {
			int hash = GHASH(j & 1 ? src[i] : -1, j & 2 ? src[i + 1] : -1);
			int idx = grule_find(gpos, n, hash);
			while (idx < n && gpos[idx].hash == hash) {
				if (font_rulematch(fn, &gpos[idx],
						src + i, slen - i, src + i, i)) {
					struct gpat *pats = gpos[idx].pats;
					/* we should accumulate the values... */
					for (k = 0; k < gpos[idx].len; k++) {
						x[i + k] = pats[k].x;
						y[i + k] = pats[k].y;
						xadv[i + k] = pats[k].xadv;
						yadv[i + k] = pats[k].yadv;
					}
				}
				idx++;
			}
		}
	}
}

/* find the first gsub rule after pos that matches any glyph in src */
static struct grule *font_firstgsub(struct font *fn, int pos, int *src, int slen)
{
	struct grule *rules = fn->gsub;
	int n = fn->gsub_n;
	struct grule *best = NULL;
	int i, j;
	for (i = 0; i < slen; i++) {
		/* possible hash values for matching gsub rules at src + slen */
		for (j = 0; j < 2 && i + j < slen; j++) {
			int hash = GHASH(src[i], j ? src[i + 1] : -1);
			int idx = grule_find(rules, n, hash);
			while (idx < n && rules[idx].hash == hash &&
					(!best || rules[idx].pos < best->pos)) {
				if (rules[idx].pos >= pos)
					if (font_rulematch(fn, &rules[idx],
							src + i, slen - i, src + i, i))
						best = &rules[idx];
				idx++;
			}
		}
	}
	return best;
}

/* apply the given gsub rule to all matches in src */
static int font_gsubapply(struct font *fn, struct grule *rule,
			int *src, int slen, int *smap)
{
	int dst[WORDLEN];
	int dlen = 0;
	int dmap[WORDLEN];
	int i, j;
	memset(dmap, 0, slen * sizeof(dmap[i]));
	for (i = 0; i < slen; i++) {
		int hash1 = GHASH(src[i], -1);
		int hash2 = GHASH(src[i], i + 1 < slen ? src[i + 1] : -1);
		int hmatch = rule->hash == hash1 || rule->hash == hash2;
		dmap[dlen] = smap[i];
		if (hmatch && font_rulematch(fn, rule, src + i,
					slen - i, dst + dlen, dlen)) {
			for (j = 0; j < rule->len; j++) {
				if (rule->pats[j].flg & GF_REP)
					dst[dlen++] = rule->pats[j].g;
				if (rule->pats[j].flg & GF_PAT)
					i++;
			}
			i--;
		} else {
			dst[dlen++] = src[i];
		}
	}
	memcpy(src, dst, dlen * sizeof(dst[0]));
	memcpy(smap, dmap, dlen * sizeof(dmap[0]));
	return dlen;
}

/* perform all possible gsub rules on src */
static int font_performgsub(struct font *fn, int *src, int slen, int *smap)
{
	int i = 0;
	while (i >= 0) {
		struct grule *rule = font_firstgsub(fn, i, src, slen);
		if (rule)
			slen = font_gsubapply(fn, rule, src, slen, smap);
		i = rule ? rule->pos + 1 : -1;
	}
	return slen;
}

int font_layout(struct font *fn, struct glyph **gsrc, int nsrc, int sz,
		struct glyph **gdst, int *dmap,
		int *x, int *y, int *xadv, int *yadv, int lg, int kn)
{
	int dst[WORDLEN];
	int ndst = nsrc;
	int i;
	int featlg, featkn;
	/* initialising dst */
	for (i = 0; i < nsrc; i++)
		dst[i] = font_idx(fn, gsrc[i]);
	for (i = 0; i < ndst; i++)
		dmap[i] = i;
	memset(x, 0, ndst * sizeof(x[0]));
	memset(y, 0, ndst * sizeof(y[0]));
	memset(xadv, 0, ndst * sizeof(xadv[0]));
	memset(yadv, 0, ndst * sizeof(yadv[0]));
	/* substitution rules */
	if (lg)
		featlg = font_featlg(fn, 3);
	ndst = font_performgsub(fn, dst, ndst, dmap);
	if (lg)
		font_featlg(fn, featlg);
	/* positioning rules */
	if (kn)
		featkn = font_featkn(fn, 1);
	font_performgpos(fn, dst, ndst, x, y, xadv, yadv);
	if (kn)
		font_featkn(fn, featkn);
	for (i = 0; i < ndst; i++)
		gdst[i] = fn->gl + dst[i];
	return ndst;
}

static int font_readchar(struct font *fn, FILE *fin, int *n, struct glyph **g)
{
	char tok[ILNLEN];
	char name[ILNLEN];
	char id[ILNLEN];
	int type;
	if (*n + 1 == NGLYPHS)
		errmsg("neatroff: NGLYPHS too low\n");
	if (*n >= NGLYPHS)
		return 0;
	if (fscanf(fin, "%s %s", name, tok) != 2)
		return 1;
	if (!strcmp("---", name))
		sprintf(name, "c%04d", *n);
	if (strcmp("\"", tok)) {
		if (fscanf(fin, "%d %s", &type, id) != 2)
			return 1;
		*g = font_glyph(fn, id);
		if (!*g) {
			*g = font_glyphput(fn, id, name, type);
			sscanf(tok, "%hd,%hd,%hd,%hd,%hd", &(*g)->wid,
				&(*g)->llx, &(*g)->lly, &(*g)->urx, &(*g)->ury);
		}
	}
	dict_put(&fn->ch_dict, name, *g - fn->gl);
	(*n)++;
	return 0;
}

static int font_findfeat(struct font *fn, char *feat, int mk)
{
	int i;
	for (i = 0; i < fn->feat_n; i++)
		if (!strcmp(feat, fn->feat_name[i]))
			return i;
	if (mk)
		strcpy(fn->feat_name[fn->feat_n], feat);
	return mk ? fn->feat_n++ : -1;
}

static struct gpat *font_gpat(struct font *fn, int len)
{
	struct gpat *pats = xmalloc(len * sizeof(pats[0]));
	memset(pats, 0, len * sizeof(pats[0]));
	return pats;
}

static struct grule *font_gsub(struct font *fn, char *feat, int len)
{
	struct grule *rule;
	struct gpat *pats = font_gpat(fn, len);
	if (fn->gsub_n + 1 == LEN(fn->gsub))
		errmsg("neatroff: NGRULES too low\n");
	if (fn->gsub_n >= LEN(fn->gsub) || !pats)
		return NULL;
	rule = &fn->gsub[fn->gsub_n++];
	rule->pats = pats;
	rule->len = len;
	rule->feat = font_findfeat(fn, feat, 1);
	return rule;
}

static struct grule *font_gpos(struct font *fn, char *feat, int len)
{
	struct grule *rule;
	struct gpat *pats = font_gpat(fn, len);
	if (fn->gpos_n + 1 == LEN(fn->gpos))
		errmsg("neatroff: NGRULES too low\n");
	if (fn->gpos_n >= LEN(fn->gpos) || !pats)
		return NULL;
	rule = &fn->gpos[fn->gpos_n++];
	rule->pats = pats;
	rule->len = len;
	rule->feat = font_findfeat(fn, feat, 1);
	return rule;
}

static int font_readgpat(struct font *fn, struct gpat *p, char *s)
{
	if (s[0] == '@') {
		p->g = atoi(s + 1);
		if (fn->ggrp_len[p->g] == 1)
			p->g = fn->ggrp[p->g][0];
		else
			p->flg |= GF_GRP;
	} else {
		p->g = font_idx(fn, font_glyph(fn, s));
	}
	return p->g < 0;
}

static int font_readgsub(struct font *fn, FILE *fin)
{
	char tok[128];
	struct grule *rule;
	int i, n;
	if (fscanf(fin, "%s %d", tok, &n) != 2)
		return 1;
	if (!(rule = font_gsub(fn, tok, n)))
		return 0;
	for (i = 0; i < n; i++) {
		if (fscanf(fin, "%s", tok) != 1)
			return 1;
		if (tok[0] == '-')
			rule->pats[i].flg = GF_PAT;
		if (tok[0] == '=')
			rule->pats[i].flg = GF_CON;
		if (tok[0] == '+')
			rule->pats[i].flg = GF_REP;
		if (!tok[0] || font_readgpat(fn, &rule->pats[i], tok + 1))
			return 0;
	}
	return 0;
}

static int font_readgpos(struct font *fn, FILE *fin)
{
	char tok[128];
	char *col;
	struct grule *rule;
	int i, n;
	if (fscanf(fin, "%s %d", tok, &n) != 2)
		return 1;
	if (!(rule = font_gpos(fn, tok, n)))
		return 0;
	for (i = 0; i < n; i++) {
		if (fscanf(fin, "%s", tok) != 1)
			return 1;
		col = strchr(tok, ':');
		if (col)
			*col = '\0';
		rule->pats[i].flg = GF_PAT;
		if (!tok[0] || font_readgpat(fn, &rule->pats[i], tok))
			return 0;
		if (col)
			sscanf(col + 1, "%hd%hd%hd%hd",
				&rule->pats[i].x, &rule->pats[i].y,
				&rule->pats[i].xadv, &rule->pats[i].yadv);
	}
	return 0;
}

static int font_readggrp(struct font *fn, FILE *fin)
{
	char tok[ILNLEN];
	int id, n, i, g;
	if (fscanf(fin, "%d %d", &id, &n) != 2)
		return 1;
	if (id >= LEN(fn->ggrp)) {
		errmsg("neatroff: NGRULES too low\n");
		return 0;
	}
	if (fn->ggrp[id])
		free(fn->ggrp[id]);
	fn->ggrp[id] = xmalloc(n * sizeof(fn->ggrp[id][0]));
	fn->ggrp_len[id] = 0;
	for (i = 0; i < n; i++) {
		if (fscanf(fin, "%s", tok) != 1)
			return 1;
		g = font_idx(fn, font_glyph(fn, tok));
		if (g >= 0)
			fn->ggrp[id][fn->ggrp_len[id]++] = g;
	}
	return 0;
}

static int font_readkern(struct font *fn, FILE *fin)
{
	char c1[ILNLEN], c2[ILNLEN];
	struct grule *rule;
	int val;
	if (fscanf(fin, "%s %s %d", c1, c2, &val) != 3)
		return 1;
	if (!(rule = font_gpos(fn, "kern", 2)))
		return 0;
	rule->pats[0].g = font_idx(fn, font_glyph(fn, c1));
	rule->pats[1].g = font_idx(fn, font_glyph(fn, c2));
	rule->pats[0].xadv = val;
	rule->pats[0].flg = GF_PAT;
	rule->pats[1].flg = GF_PAT;
	return 0;
}

static void font_lig(struct font *fn, char *lig)
{
	char c[GNLEN];
	int g[WORDLEN];
	struct grule *rule;
	char *s = lig;
	int j, n = 0;
	while (utf8read(&s, c) > 0)
		g[n++] = font_idx(fn, font_find(fn, c));
	if (!(rule = font_gsub(fn, "liga", n + 1)))
		return;
	for (j = 0; j < n; j++) {
		rule->pats[j].g = g[j];
		rule->pats[j].flg = GF_PAT;
	}
	rule->pats[n].g = font_idx(fn, font_find(fn, lig));
	rule->pats[n].flg = GF_REP;
}

static void skipline(FILE* filp)
{
	int c;
	do {
		c = getc(filp);
	} while (c != '\n' && c != EOF);
}

struct font *font_open(char *path)
{
	struct font *fn;
	struct glyph *ch_g = NULL;	/* last glyph in the charset */
	int ch_n = 0;			/* number of glyphs in the charset */
	char tok[ILNLEN];
	FILE *fin;
	char ligs[512][GNLEN];
	int ligs_n = 0;
	int i;
	fin = fopen(path, "r");
	if (!fin)
		return NULL;
	fn = xmalloc(sizeof(*fn));
	if (!fn) {
		fclose(fin);
		return NULL;
	}
	memset(fn, 0, sizeof(*fn));
	dict_init(&fn->gl_dict, NGLYPHS, -1, 0, 0);
	dict_init(&fn->ch_dict, NGLYPHS, -1, 1, 0);
	dict_init(&fn->ch_map, NGLYPHS, -1, 1, 0);
	while (fscanf(fin, "%s", tok) == 1) {
		if (!strcmp("char", tok)) {
			font_readchar(fn, fin, &ch_n, &ch_g);
		} else if (!strcmp("kern", tok)) {
			font_readkern(fn, fin);
		} else if (!strcmp("ligatures", tok)) {
			while (fscanf(fin, "%s", ligs[ligs_n]) == 1) {
				if (!strcmp("0", ligs[ligs_n]))
					break;
				if (ligs_n < LEN(ligs))
					ligs_n++;
			}
		} else if (!strcmp("gsub", tok)) {
			font_readgsub(fn, fin);
		} else if (!strcmp("gpos", tok)) {
			font_readgpos(fn, fin);
		} else if (!strcmp("ggrp", tok)) {
			font_readggrp(fn, fin);
		} else if (!strcmp("spacewidth", tok)) {
			fscanf(fin, "%d", &fn->spacewid);
		} else if (!strcmp("special", tok)) {
			fn->special = 1;
		} else if (!strcmp("name", tok)) {
			fscanf(fin, "%s", fn->name);
		} else if (!strcmp("fontname", tok)) {
			fscanf(fin, "%s", fn->fontname);
		} else if (!strcmp("charset", tok)) {
			while (!font_readchar(fn, fin, &ch_n, &ch_g))
				;
			break;
		}
		skipline(fin);
	}
	for (i = 0; i < ligs_n; i++)
		font_lig(fn, ligs[i]);
	fclose(fin);
	for (i = 0; i < fn->gsub_n; i++)
		fn->gsub[i].pos = i;
	for (i = 0; i < fn->gpos_n; i++)
		fn->gpos[i].pos = i;
	for (i = 0; i < fn->gsub_n; i++)
		fn->gsub[i].hash = grule_hash(&fn->gsub[i]);
	for (i = 0; i < fn->gpos_n; i++)
		fn->gpos[i].hash = grule_hash(&fn->gpos[i]);
	qsort(fn->gsub, fn->gsub_n, sizeof(fn->gsub[0]), (void *) grulecmp);
	qsort(fn->gpos, fn->gpos_n, sizeof(fn->gpos[0]), (void *) grulecmp);
	return fn;
}

void font_close(struct font *fn)
{
	int i;
	for (i = 0; i < fn->gsub_n; i++)
		free(fn->gsub[i].pats);
	for (i = 0; i < fn->gpos_n; i++)
		free(fn->gpos[i].pats);
	for (i = 0; i < LEN(fn->ggrp); i++)
		free(fn->ggrp[i]);
	dict_done(&fn->gl_dict);
	dict_done(&fn->ch_dict);
	dict_done(&fn->ch_map);
	free(fn);
}

int font_special(struct font *fn)
{
	return fn->special;
}

/* return width w for the given font and size */
int font_wid(struct font *fn, int sz, int w)
{
	sz = font_zoom(fn, sz);
	return w >= 0 ? DEVWID(sz, w) : -DEVWID(sz, -w);
}

/* glyph width, where cfn is the current font and fn is glyph's font */
int font_gwid(struct font *fn, struct font *cfn, int sz, int w)
{
	if (cfn->cs)
		return cfn->cs * (font_zoom(fn, cfn->cs_ps ? cfn->cs_ps : sz)
					* SC_IN / 72) / 36;
	return font_wid(fn, sz, w) + (font_getbd(cfn) ? font_getbd(cfn) - 1 : 0);
}

/* space width for the give word space or sentence space */
int font_swid(struct font *fn, int sz, int ss)
{
	return font_gwid(fn, fn, sz, (fn->spacewid * ss + 6) / 12);
}

int font_getcs(struct font *fn)
{
	return fn->cs;
}

void font_setcs(struct font *fn, int cs, int ps)
{
	fn->cs = cs;
	fn->cs_ps = ps;
}

int font_getbd(struct font *fn)
{
	return fn->bd;
}

void font_setbd(struct font *fn, int bd)
{
	fn->bd = bd;
}

int font_zoom(struct font *fn, int sz)
{
	return fn->zoom ? (sz * fn->zoom + 500) / 1000 : sz;
}

void font_setzoom(struct font *fn, int zoom)
{
	fn->zoom = zoom;
}

/* enable/disable font features; returns the previous value */
int font_feat(struct font *fn, char *name, int val)
{
	int idx = font_findfeat(fn, name, 0);
	int old = idx >= 0 ? fn->feat_set[idx] : 0;
	if (idx >= 0)
		fn->feat_set[idx] = val != 0;
	return old;
}
