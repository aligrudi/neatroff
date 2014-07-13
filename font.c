/* font handling */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

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
	int type;
	if (fn->n >= NGLYPHS)
		return 1;
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
			sscanf(tok, "%d,%d,%d,%d,%d", &glyph->wid,
				&glyph->llx, &glyph->lly, &glyph->urx, &glyph->ury);
		}
	}
	strcpy(fn->c[fn->n], name);
	fn->g[fn->n] = glyph;
	dict_put(&fn->cdict, fn->c[fn->n], fn->n);
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
	fn = xmalloc(sizeof(*fn));
	if (!fn) {
		fclose(fin);
		return NULL;
	}
	memset(fn, 0, sizeof(*fn));
	dict_init(&fn->gdict, NGLYPHS, -1, 0, 0);
	dict_init(&fn->cdict, NGLYPHS, -1, 0, 0);
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
	dict_done(&fn->gdict);
	dict_done(&fn->cdict);
	free(fn);
}
