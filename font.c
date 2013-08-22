#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static void skipline(FILE* filp)
{
	int c;
	do {
		c = getc(filp);
	} while (c != '\n' && c != EOF);
}

struct glyph *font_find(struct font *fn, char *name)
{
	int i = fn->head[(unsigned char) name[0]];
	while (i >= 0) {
		if (!strcmp(name, fn->c[i]))
			return fn->g[i];
		i = fn->next[i];
	}
	return NULL;
}

struct glyph *font_glyph(struct font *fn, char *id)
{
	int i;
	for (i = 0; i < fn->nglyphs; i++)
		if (!strcmp(fn->glyphs[i].id, id))
			return &fn->glyphs[i];
	return NULL;
}

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
	for (i = 0; i < fn->nlig; i++) {
		int l = strlen(fn->lig[i]);
		if (b[len - l] > 1 && !strcmp(s + len - l, fn->lig[i]))
			if (font_find(fn, fn->lig[i]))
				return b[len - l];
	}
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

static int font_section(struct font *fn, FILE *fin, char *name);

static void font_charset(struct font *fn, FILE *fin)
{
	char tok[ILNLEN];
	char name[ILNLEN];
	char id[ILNLEN];
	struct glyph *glyph = NULL;
	struct glyph *prev = NULL;
	int wid, type;
	while (fscanf(fin, "%s", name) == 1) {
		if (!font_section(fn, fin, name))
			break;
		if (fn->n >= NGLYPHS) {
			skipline(fin);
			continue;
		}
		fscanf(fin, "%s", tok);
		glyph = prev;
		if (!strcmp("---", name))
			sprintf(name, "c%04d", fn->n);
		if (strcmp("\"", tok)) {
			wid = atoi(tok);
			fscanf(fin, "%d %s", &type, id);
			skipline(fin);
			glyph = &fn->glyphs[fn->nglyphs++];
			strcpy(glyph->id, id);
			strcpy(glyph->name, name);
			glyph->wid = wid;
			glyph->type = type;
			glyph->font = fn;
		}
		prev = glyph;
		strcpy(fn->c[fn->n], name);
		fn->g[fn->n] = glyph;
		fn->next[fn->n] = fn->head[(unsigned char) name[0]];
		fn->head[(unsigned char) name[0]] = fn->n;
		fn->n++;
	}
}

static void font_kernpairs(struct font *fn, FILE *fin)
{
	char c1[ILNLEN], c2[ILNLEN];
	int i1, i2, val;
	while (fscanf(fin, "%s", c1) == 1) {
		if (!font_section(fn, fin, c1))
			break;
		if (fscanf(fin, "%s %d", c2, &val) != 2)
			break;
		if (fn->knn < NKERNS) {
			i1 = font_idx(fn, font_find(fn, c1));
			i2 = font_idx(fn, font_find(fn, c2));
			if (i1 >= 0 && i2 >= 0) {
				fn->knnext[fn->knn] = fn->knhead[i1];
				fn->knhead[i1] = fn->knn;
				fn->knval[fn->knn] = val;
				fn->knpair[fn->knn] = i2;
				fn->knn++;
			}
		}
	}
}

static int font_section(struct font *fn, FILE *fin, char *name)
{
	if (!strcmp("charset", name)) {
		font_charset(fn, fin);
		return 0;
	}
	if (!strcmp("kernpairs", name)) {
		font_kernpairs(fn, fin);
		return 0;
	}
	return 1;
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
	memset(fn, 0, sizeof(*fn));
	for (i = 0; i < LEN(fn->head); i++)
		fn->head[i] = -1;
	for (i = 0; i < LEN(fn->knhead); i++)
		fn->knhead[i] = -1;
	while (fscanf(fin, "%s", tok) == 1) {
		if (tok[0] == '#') {
			skipline(fin);
			continue;
		}
		if (!strcmp("spacewidth", tok)) {
			fscanf(fin, "%d", &fn->spacewid);
			continue;
		}
		if (!strcmp("special", tok)) {
			fn->special = 1;
			continue;
		}
		if (!strcmp("name", tok)) {
			fscanf(fin, "%s", fn->name);
			continue;
		}
		if (!strcmp("fontname", tok)) {
			fscanf(fin, "%s", fn->fontname);
			continue;
		}
		if (!strcmp("named", tok)) {
			skipline(fin);
			continue;
		}
		if (!strcmp("ligatures", tok)) {
			while (fscanf(fin, "%s", tok) == 1) {
				if (!strcmp("0", tok))
					break;
				if (fn->nlig < NLIGS)
					strcpy(fn->lig[fn->nlig++], tok);
			}
			skipline(fin);
			continue;
		}
		if (!font_section(fn, fin, tok))
			break;
		skipline(fin);
	}
	fclose(fin);
	return fn;
}

void font_close(struct font *fn)
{
	free(fn);
}
