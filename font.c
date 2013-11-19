#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

struct glyph *font_find(struct font *fn, char *name)
{
	int i = fn->chead[(unsigned char) name[0]];
	while (i >= 0) {
		if (!strcmp(name, fn->c[i]))
			return fn->g[i];
		i = fn->cnext[i];
	}
	return NULL;
}

struct glyph *font_glyph(struct font *fn, char *id)
{
	int i = fn->ghead[(unsigned char) id[0]];
	while (i >= 0) {
		if (!strcmp(fn->glyphs[i].id, id))
			return &fn->glyphs[i];
		i = fn->gnext[i];
	}
	return NULL;
}

struct glyph *font_glyphput(struct font *fn, char *id, char *name, int wid, int type)
{
	int i = fn->nglyphs++;
	struct glyph *g;
	g = &fn->glyphs[i];
	strcpy(g->id, id);
	strcpy(g->name, name);
	g->wid = wid;
	g->type = type;
	g->font = fn;
	fn->gnext[i] = fn->ghead[(unsigned char) id[0]];
	fn->ghead[(unsigned char) id[0]] = i;
	return g;
}

/* glyph index in fn->glyphs[] */
static int font_idx(struct font *fn, struct glyph *g)
{
	return g ? g - fn->glyphs : -1;
}

/*
 * Given a list of characters in the reverse order, font_lig()
 * returns the number of characters from the beginning of this
 * list that form a ligature in this font.  Zero naturally means
 * no ligature was matched.
 */
int font_lig(struct font *fn, char **c, int n)
{
	int i;
	/* concatenated characters in c[], in the correct order */
	char s[GNLEN * 2] = "";
	/* b[i] is the number of character of c[] in s + i */
	int b[GNLEN * 2] = {0};
	int len = 0;
	for (i = 0; i < n; i++) {
		char *cur = c[n - i - 1];
		b[len] = n - i;
		strcpy(s + len, cur);
		len += strlen(cur);
	}
	for (i = 0; i < fn->lgn; i++) {
		int l = strlen(fn->lg[i]);
		if (b[len - l] > 1 && !strcmp(s + len - l, fn->lg[i]))
			if (font_find(fn, fn->lg[i]))
				return b[len - l];
	}
	return 0;
}

/* return nonzero if s is a ligature */
int font_islig(struct font *fn, char *s)
{
	int i;
	for (i = 0; i < fn->lgn; i++)
		if (!strcmp(s, fn->lg[i]))
			return 1;
	return 0;
}

/* return pairwise kerning value between c1 and c2 */
int font_kern(struct font *fn, char *c1, char *c2)
{
	int i1, i2, i;
	i1 = font_idx(fn, font_find(fn, c1));
	i2 = font_idx(fn, font_find(fn, c2));
	if (i1 < 0 || i2 < 0)
		return 0;
	i = fn->knhead[i1];
	while (i >= 0) {
		if (fn->knpair[i] == i2)
			return fn->knval[i];
		i = fn->knnext[i];
	}
	return 0;
}

static int font_readchar(struct font *fn, FILE *fin)
{
	char tok[ILNLEN];
	char name[ILNLEN];
	char id[ILNLEN];
	struct glyph *glyph = NULL;
	int wid, type;
	if (fn->n >= NGLYPHS)
		return 1;
	if (fscanf(fin, "%s %s", name, tok) != 2)
		return 1;
	if (!strcmp("---", name))
		sprintf(name, "c%04d", fn->n);
	if (strcmp("\"", tok)) {
		wid = atoi(tok);
		if (fscanf(fin, "%d %s", &type, id) != 2)
			return 1;
		glyph = font_glyph(fn, id);
		if (!glyph)
			glyph = font_glyphput(fn, id, name, wid, type);
	} else {
		glyph = fn->g[fn->n - 1];
	}
	strcpy(fn->c[fn->n], name);
	fn->g[fn->n] = glyph;
	fn->cnext[fn->n] = fn->chead[(unsigned char) name[0]];
	fn->chead[(unsigned char) name[0]] = fn->n;
	fn->n++;
	return 0;
}

static int font_readkern(struct font *fn, FILE *fin)
{
	char c1[ILNLEN], c2[ILNLEN];
	int i1, i2, val;
	if (fscanf(fin, "%s %s %d", c1, c2, &val) != 3)
		return 1;
	i1 = font_idx(fn, font_glyph(fn, c1));
	i2 = font_idx(fn, font_glyph(fn, c2));
	if (fn->knn < NKERNS && i1 >= 0 && i2 >= 0) {
		fn->knnext[fn->knn] = fn->knhead[i1];
		fn->knhead[i1] = fn->knn;
		fn->knval[fn->knn] = val;
		fn->knpair[fn->knn] = i2;
		fn->knn++;
	}
	return 0;
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
	int i;
	fin = fopen(path, "r");
	if (!fin)
		return NULL;
	fn = malloc(sizeof(*fn));
	if (!fn) {
		fclose(fin);
		return NULL;
	}
	memset(fn, 0, sizeof(*fn));
	for (i = 0; i < LEN(fn->ghead); i++)
		fn->ghead[i] = -1;
	for (i = 0; i < LEN(fn->chead); i++)
		fn->chead[i] = -1;
	for (i = 0; i < LEN(fn->knhead); i++)
		fn->knhead[i] = -1;
	while (fscanf(fin, "%s", tok) == 1) {
		if (!strcmp("char", tok)) {
			font_readchar(fn, fin);
		} else if (!strcmp("kern", tok)) {
			font_readkern(fn, fin);
		} else if (!strcmp("spacewidth", tok)) {
			fscanf(fin, "%d", &fn->spacewid);
		} else if (!strcmp("special", tok)) {
			fn->special = 1;
		} else if (!strcmp("name", tok)) {
			fscanf(fin, "%s", fn->name);
		} else if (!strcmp("fontname", tok)) {
			fscanf(fin, "%s", fn->fontname);
		} else if (!strcmp("ligatures", tok)) {
			while (fscanf(fin, "%s", tok) == 1) {
				if (!strcmp("0", tok))
					break;
				if (fn->lgn < NLIGS)
					strcpy(fn->lg[fn->lgn++], tok);
			}
		} else if (!strcmp("charset", tok)) {
			while (!font_readchar(fn, fin))
				;
			break;
		}
		skipline(fin);
	}
	fclose(fin);
	return fn;
}

void font_close(struct font *fn)
{
	free(fn);
}
