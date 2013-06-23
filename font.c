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
	int i;
	for (i = 0; i < fn->n; i++)
		if (name[0] == fn->c[i][0] && !strcmp(name, fn->c[i]))
			return fn->g[i];
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
		fn->n++;
	}
}

static void font_kernpairs(struct font *fn, FILE *fin)
{
	char c1[ILNLEN], c2[ILNLEN];
	int val;
	while (fscanf(fin, "%s", c1) == 1) {
		if (!font_section(fn, fin, c1))
			break;
		if (fscanf(fin, "%s %d", c2, &val) != 2)
			break;
		if (fn->nkern < NKERNS) {
			strcpy(fn->kern_c1[fn->nkern], c1);
			strcpy(fn->kern_c2[fn->nkern], c2);
			fn->kern[fn->nkern] = val;
			fn->nkern++;
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

/* return 1 if lig is a ligature */
int font_lig(struct font *fn, char *lig)
{
	int i;
	for (i = 0; i < fn->nlig; i++)
		if (!strcmp(lig, fn->lig[i]))
			return font_find(fn, lig) != NULL;
	return 0;
}

/* return pairwise kerning value between c1 and c2 */
int font_kern(struct font *fn, char *c1, char *c2)
{
	int i;
	for (i = 0; i < fn->nkern; i++)
		if (!strcmp(fn->kern_c1[i], c1) && !strcmp(fn->kern_c2[i], c2))
			return fn->kern[i];
	return 0;
}

struct font *font_open(char *path)
{
	struct font *fn = malloc(sizeof(*fn));
	char tok[ILNLEN];
	FILE *fin;
	fin = fopen(path, "r");
	memset(fn, 0, sizeof(*fn));
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
	}
	fclose(fin);
	return fn;
}

void font_close(struct font *fn)
{
	free(fn);
}
