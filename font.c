/* font handling */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

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
	struct glyph glyphs[NGLYPHS];
	int nglyphs;
	int spacewid;
	int special;
	int cs, bd;			/* for .cs and .bd requests */
	struct dict gdict;		/* mapping from glyphs[i].id to i */
	/* charset section characters */
	char c[NGLYPHS][GNLEN];		/* character names in charset */
	struct glyph *g[NGLYPHS];	/* character glyphs in charset */
	struct glyph *g_map[NGLYPHS];	/* character remapped via font_map() */
	int n;				/* number of characters in charset */
	struct dict cdict;		/* mapping from c[i] to i */
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
	int i = dict_get(&fn->cdict, name);
	if (i < 0)
		return NULL;
	return fn->g_map[i] ? fn->g_map[i] : fn->g[i];
}

/* find a glyph by its device-dependent identifier */
struct glyph *font_glyph(struct font *fn, char *id)
{
	int i = dict_get(&fn->gdict, id);
	return i >= 0 ? &fn->glyphs[i] : NULL;
}

static struct glyph *font_glyphput(struct font *fn, char *id, char *name, int type)
{
	int i = fn->nglyphs++;
	struct glyph *g;
	g = &fn->glyphs[i];
	strcpy(g->id, id);
	strcpy(g->name, name);
	g->type = type;
	g->font = fn;
	dict_put(&fn->gdict, g->id, i);
	return g;
}

/* map character name to the given glyph */
int font_map(struct font *fn, char *name, struct glyph *g)
{
	int i = dict_get(&fn->cdict, name);
	if (g && g->font != fn)
		return 1;
	if (i < 0) {
		if (fn->n >= NGLYPHS)
			return 1;
		i = fn->n++;
		strcpy(fn->c[i], name);
		dict_put(&fn->cdict, fn->c[i], i);
	}
	fn->g_map[i] = g;
	return 0;
}

/* return nonzero if character name has been mapped with font_map() */
int font_mapped(struct font *fn, char *name)
{
	int i = dict_get(&fn->cdict, name);
	return i >= 0 && fn->g_map[i];
}

/* enable/disable ligatures; first bit for liga and the second bit for rlig */
static int font_featlg(struct font *fn, int val)
{
	int ret = 0;
	if (val & 1)
		ret |= font_feat(fn, "liga", val);
	if (val & 2)
		ret |= font_feat(fn, "rlig", val) << 1;
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
	return g ? g - fn->glyphs : -1;
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
	return rules[l].hash == hash ? l : -1;
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

static struct grule *font_findrule(struct font *fn, struct grule *rules, int n,
		int *src, int slen, int *dst, int dlen)
{
	int idx[4] = {-1, -1, -1, -1};
	int hash[4];
	int i, j;
	for (j = 0; j < 4 && j < (slen << 1); j++) {
		hash[j] = GHASH(j & 1 ? src[0] : -1, j & 2 ? src[1] : -1);
		idx[j] = grule_find(rules, n, hash[j]);
	}
	while (1) {
		i = -1;		/* finding the first rule among idx[] */
		for (j = 0; j < 4; j++)
			if (idx[j] >= 0 && idx[j] < n && rules[idx[j]].hash == hash[j])
				if (i < 0 || rules[idx[j]].pos <= rules[idx[i]].pos)
					i = j;
		if (i < 0)
			break;
		if (font_rulematch(fn, rules + idx[i], src, slen, dst, dlen))
			return rules + idx[i];
		idx[i]++;
	}
	return NULL;
}

int font_layout(struct font *fn, struct glyph **gsrc, int nsrc, int sz,
		struct glyph **gdst, int *dmap,
		int *x, int *y, int *xadv, int *yadv, int lg, int kn)
{
	int src[WORDLEN], dst[WORDLEN];
	int ndst = 0;
	int i, j;
	int featlg, featkn;
	for (i = 0; i < nsrc; i++)
		src[i] = font_idx(fn, gsrc[i]);
	if (lg)
		featlg = font_featlg(fn, 3);
	for (i = 0; i < nsrc; i++) {
		struct grule *rule = font_findrule(fn, fn->gsub, fn->gsub_n,
				src + i, nsrc - i, dst + ndst, ndst);
		dmap[ndst] = i;
		if (rule) {
			for (j = 0; j < rule->len; j++) {
				if (rule->pats[j].flg & GF_REP)
					dst[ndst++] = rule->pats[j].g;
				if (rule->pats[j].flg & GF_PAT)
					i++;
			}
			i--;
		} else {
			dst[ndst++] = src[i];
		}
	}
	if (lg)
		font_featlg(fn, featlg);
	memset(x, 0, ndst * sizeof(x[0]));
	memset(y, 0, ndst * sizeof(y[0]));
	memset(xadv, 0, ndst * sizeof(xadv[0]));
	memset(yadv, 0, ndst * sizeof(yadv[0]));
	for (i = 0; i < ndst; i++)
		gdst[i] = fn->glyphs + dst[i];
	if (kn)
		featkn = font_featkn(fn, 1);
	for (i = 0; i < ndst; i++) {
		struct grule *rule = font_findrule(fn, fn->gpos, fn->gpos_n,
				dst + i, ndst - i, dst + i, i);
		if (!rule)
			continue;
		for (j = 0; j < rule->len; j++) {
			x[i + j] = rule->pats[j].x;
			y[i + j] = rule->pats[j].y;
			xadv[i + j] = rule->pats[j].xadv;
			yadv[i + j] = rule->pats[j].yadv;
		}
	}
	if (kn)
		font_featkn(fn, featkn);
	return ndst;
}

static int font_readchar(struct font *fn, FILE *fin)
{
	char tok[ILNLEN];
	char name[ILNLEN];
	char id[ILNLEN];
	struct glyph *glyph = NULL;
	int type;
	if (fn->n + 1 == NGLYPHS)
		errmsg("neatroff: NGLYPHS too low\n");
	if (fn->n >= NGLYPHS)
		return 0;
	if (fscanf(fin, "%s %s", name, tok) != 2)
		return 1;
	if (!strcmp("---", name))
		sprintf(name, "c%04d", fn->n);
	if (!strcmp("\"", tok)) {
		glyph = fn->g[fn->n - 1];
	} else {
		if (fscanf(fin, "%d %s", &type, id) != 2)
			return 1;
		glyph = font_glyph(fn, id);
		if (!glyph) {
			glyph = font_glyphput(fn, id, name, type);
			sscanf(tok, "%hd,%hd,%hd,%hd,%hd", &glyph->wid,
				&glyph->llx, &glyph->lly,
				&glyph->urx, &glyph->ury);
		}
	}
	strcpy(fn->c[fn->n], name);
	fn->g[fn->n] = glyph;
	dict_put(&fn->cdict, fn->c[fn->n], fn->n);
	fn->n++;
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
	dict_init(&fn->gdict, NGLYPHS, -1, 0, 0);
	dict_init(&fn->cdict, NGLYPHS, -1, 0, 0);
	while (fscanf(fin, "%s", tok) == 1) {
		if (!strcmp("char", tok)) {
			font_readchar(fn, fin);
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
			while (!font_readchar(fn, fin))
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
	dict_done(&fn->gdict);
	dict_done(&fn->cdict);
	free(fn);
}

int font_special(struct font *fn)
{
	return fn->special;
}

int font_spacewid(struct font *fn)
{
	return fn->spacewid;
}

int font_getcs(struct font *fn)
{
	return fn->cs;
}

void font_setcs(struct font *fn, int cs)
{
	fn->cs = cs;
}

int font_getbd(struct font *fn)
{
	return fn->bd;
}

void font_setbd(struct font *fn, int bd)
{
	fn->bd = bd;
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
