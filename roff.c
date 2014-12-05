/*
 * neatroff troff clone
 *
 * Copyright (C) 2012-2014 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the Modified BSD license.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "roff.h"

void errmsg(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void errdie(char *msg)
{
	fprintf(stderr, msg);
	exit(1);
}

void *mextend(void *old, long oldsz, long newsz, int memsz)
{
	void *new = xmalloc(newsz * memsz);
	memcpy(new, old, oldsz * memsz);
	memset(new + oldsz * memsz, 0, (newsz - oldsz) * memsz);
	free(old);
	return new;
}

void *xmalloc(long len)
{
	void *m = malloc(len);
	if (!m)
		errdie("neatroff: malloc() failed\n");
	return m;
}

static int xopens(char *path)
{
	FILE *filp = fopen(path, "r");
	if (filp)
		fclose(filp);
	return filp != NULL;
}

static char *usage =
	"Usage: neatroff [options] input\n\n"
	"Options:\n"
	"  -mx   \tinclude macro x\n"
	"  -C    \tenable compatibility mode\n"
	"  -Tdev \tset output device\n"
	"  -Fdir \tset font directory (" TROFFFDIR ")\n"
	"  -Mdir \tset macro directory (" TROFFMDIR ")\n";

int main(int argc, char **argv)
{
	char path[PATHLEN];
	char *fontdir = TROFFFDIR;
	char *macrodir = TROFFMDIR;
	char *dev = "utf";
	int i;
	int ret;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-' || !argv[i][1])
			break;
		switch (argv[i][1]) {
		case 'C':
			n_cp = 1;
			break;
		case 'm':
			snprintf(path, sizeof(path), "%s/%s.tmac",
				macrodir, argv[i] + 2);
			if (!xopens(path))
				snprintf(path, sizeof(path), "%s/tmac.%s",
					macrodir, argv[i] + 2);
			if (!xopens(path))
				snprintf(path, sizeof(path), "%s/%s",
					macrodir, argv[i] + 2);
			in_queue(path);
			break;
		case 'F':
			fontdir = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'M':
			macrodir = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'T':
			dev = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		default:
			printf("%s", usage);
			return 0;
		}
	}
	if (dev_open(fontdir, dev)) {
		fprintf(stderr, "neatroff: cannot open device %s\n", dev);
		return 1;
	}
	hyph_init();
	env_init();
	tr_init();
	if (i == argc)
		in_queue(NULL);	/* reading from standard input */
	for (; i < argc; i++)
		in_queue(!strcmp("-", argv[i]) ? NULL : argv[i]);
	out("s%d\n", n_s);
	out("f%d\n", n_f);
	ret = render();
	out("V%d\n", n_p);
	hyph_done();
	tr_done();
	env_done();
	dev_close();
	map_done();
	return ret;
}
