#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

char dev_dir[PATHLEN];	/* device directory */
char dev_dev[PATHLEN];	/* output device name */
int dev_res;		/* device resolution */
int dev_uwid;		/* device unitwidth */
int dev_hor;		/* minimum horizontal movement */
int dev_ver;		/* minimum vertical movement */

/* mounted fonts */
static char fn_name[NFONTS][FNLEN];	/* font names */
static struct font *fn_font[NFONTS];	/* font structs */
static int fn_n;			/* number of mounted fonts */

static void skipline(FILE* filp)
{
	int c;
	do {
		c = getc(filp);
	} while (c != '\n' && c != EOF);
}

static void dev_prologue(void)
{
	out("x T %s\n", dev_dev);
	out("x res %d %d %d\n", dev_res, dev_hor, dev_ver);
	out("x init\n");
}

int dev_mnt(int pos, char *id, char *name)
{
	char path[PATHLEN];
	struct font *fn;
	sprintf(path, "%s/dev%s/%s", dev_dir, dev_dev, name);
	fn = font_open(path);
	if (!fn)
		return -1;
	if (fn_font[pos])
		font_close(fn_font[pos]);
	if (fn_name[pos] != name)	/* ignore if fn_name[pos] is passed */
		strcpy(fn_name[pos], id);
	fn_font[pos] = fn;
	out("x font %d %s\n", pos, name);
	return pos;
}

int dev_open(char *dir, char *dev)
{
	char path[PATHLEN];
	char tok[ILNLEN];
	int i;
	FILE *desc;
	strcpy(dev_dir, dir);
	strcpy(dev_dev, dev);
	sprintf(path, "%s/dev%s/DESC", dir, dev);
	desc = fopen(path, "r");
	if (!desc)
		return 1;
	while (fscanf(desc, "%s", tok) == 1) {
		if (tok[0] == '#') {
			skipline(desc);
			continue;
		}
		if (!strcmp("fonts", tok)) {
			fscanf(desc, "%d", &fn_n);
			for (i = 0; i < fn_n; i++)
				fscanf(desc, "%s", fn_name[i + 1]);
			fn_n++;
			continue;
		}
		if (!strcmp("sizes", tok)) {
			while (fscanf(desc, "%s", tok) == 1)
				if (!strcmp("0", tok))
					break;
			continue;
		}
		if (!strcmp("res", tok)) {
			fscanf(desc, "%d", &dev_res);
			continue;
		}
		if (!strcmp("unitwidth", tok)) {
			fscanf(desc, "%d", &dev_uwid);
			continue;
		}
		if (!strcmp("hor", tok)) {
			fscanf(desc, "%d", &dev_hor);
			continue;
		}
		if (!strcmp("ver", tok)) {
			fscanf(desc, "%d", &dev_ver);
			continue;
		}
		if (!strcmp("charset", tok))
			break;
		skipline(desc);
	}
	fclose(desc);
	dev_prologue();
	for (i = 0; i < fn_n; i++)
		if (*fn_name[i])
			dev_mnt(i, fn_name[i], fn_name[i]);
	return 0;
}

static void dev_epilogue(void)
{
	out("x trailer\n");
	out("x stop\n");
}

void dev_close(void)
{
	int i;
	dev_epilogue();
	for (i = 0; i < fn_n; i++) {
		if (fn_font[i])
			font_close(fn_font[i]);
		fn_font[i] = NULL;
	}
}

/* glyph handling functions */

struct glyph *dev_glyph(char *c, int fn)
{
	struct glyph *g;
	int i;
	if ((c[0] == c_ec || c[0] == c_ni) && c[1] == c_ec)
		c++;
	if (c[0] == c_ec && c[1] == '(')
		c += 2;
	c = tr_map(c);
	g = font_find(fn_font[fn], c);
	if (g)
		return g;
	for (i = 0; i < fn_n; i++)
		if (fn_font[i] && fn_font[i]->special)
			if ((g = font_find(fn_font[i], c)))
				return g;
	return NULL;
}

struct glyph *dev_glyph_byid(char *id, int fn)
{
	return font_glyph(fn_font[fn], id);
}

int dev_kernpair(char *c1, char *c2)
{
	return 0;
}

/* return the mounted position of font */
int dev_pos(char *id)
{
	int i;
	if (isdigit(id[0])) {
		int num = atoi(id);
		if (num < 0 || num >= fn_n || !fn_font[num]) {
			errmsg("bad font position\n");
			return -1;
		}
		return num;
	}
	for (i = 1; i < fn_n; i++)
		if (!strcmp(fn_name[i], id))
			return i;
	if (!strcmp(fn_name[0], id))
		return 0;
	return dev_mnt(0, id, id);
}

/* return the font struct at pos */
struct font *dev_font(int pos)
{
	return pos >= 0 && pos < fn_n ? fn_font[pos] : NULL;
}

int dev_getcs(int fn)
{
	return dev_font(fn)->cs;
}

void dev_setcs(int fn, int cs)
{
	if (fn >= 0)
		dev_font(fn)->cs = cs;
}

int dev_getbd(int fn)
{
	return dev_font(fn)->bd;
}

void dev_setbd(int fn, int bd)
{
	if (fn >= 0)
		dev_font(fn)->bd = bd;
}
