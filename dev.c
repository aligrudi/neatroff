/* output device */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static char dev_dir[PATHLEN];	/* device directory */
static char dev_dev[PATHLEN];	/* output device name */
int dev_res;			/* device resolution */
int dev_uwid;			/* device unitwidth */
int dev_hor;			/* minimum horizontal movement */
int dev_ver;			/* minimum vertical movement */

/* mounted fonts */
static char fn_name[NFONTS][FNLEN];	/* font names */
static struct font *fn_font[NFONTS];	/* font structs */
static int fn_n;			/* number of device fonts */

/* .fspecial request */
static char fspecial_fn[NFONTS][FNLEN];	/* .fspecial first arguments */
static char fspecial_sp[NFONTS][FNLEN];	/* .fspecial special fonts */
static int fspecial_n;			/* number of fonts in fspecial_sp[] */

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
	if (pos >= NFONTS)
		return -1;
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
	for (i = 0; i < NFONTS; i++) {
		if (fn_font[i])
			font_close(fn_font[i]);
		fn_font[i] = NULL;
	}
}

/* glyph handling functions */

static struct glyph *dev_find(char *c, int fn, int byid)
{
	struct glyph *(*find)(struct font *fn, char *name);
	struct glyph *g;
	int i;
	find = byid ? font_glyph : font_find;
	if ((g = find(fn_font[fn], c)))
		return g;
	for (i = 0; i < fspecial_n; i++)
		if (dev_pos(fspecial_fn[i]) == fn && dev_pos(fspecial_sp[i]) >= 0)
			if ((g = find(dev_font(dev_pos(fspecial_sp[i])), c)))
				return g;
	for (i = 0; i < NFONTS; i++)
		if (fn_font[i] && font_special(fn_font[i]))
			if ((g = find(fn_font[i], c)))
				return g;
	return NULL;
}

struct glyph *dev_glyph(char *c, int fn)
{
	if ((c[0] == c_ec || c[0] == c_ni) && c[1] == c_ec)
		c++;
	if (c[0] == c_ec && c[1] == '(')
		c += 2;
	c = cmap_map(c);
	if (!strncmp("GID=", c, 4))
		return dev_find(c + 4, fn, 1);
	return dev_find(c, fn, 0);
}

/* return the mounted position of a font */
int dev_pos(char *id)
{
	int i;
	if (isdigit(id[0])) {
		int num = atoi(id);
		if (num < 0 || num >= NFONTS || !fn_font[num]) {
			errmsg("bad font position\n");
			return -1;
		}
		return num;
	}
	for (i = 1; i < NFONTS; i++)
		if (!strcmp(fn_name[i], id))
			return i;
	if (!strcmp(fn_name[0], id))
		return 0;
	return dev_mnt(0, id, id);
}

/* return the mounted position of a font struct */
int dev_fontpos(struct font *fn)
{
	int i;
	for (i = 0; i < NFONTS; i++)
		if (fn_font[i] == fn)
			return i;
	return 0;
}

/* return the font struct at pos */
struct font *dev_font(int pos)
{
	return pos >= 0 && pos < NFONTS ? fn_font[pos] : NULL;
}

void tr_fspecial(char **args)
{
	char *fn = args[1];
	int i;
	if (!fn) {
		fspecial_n = 0;
		return;
	}
	for (i = 2; i < NARGS; i++) {
		if (args[i] && fspecial_n < LEN(fspecial_fn)) {
			strcpy(fspecial_fn[fspecial_n], fn);
			strcpy(fspecial_sp[fspecial_n], args[i]);
			fspecial_n++;
		}
	}
}
