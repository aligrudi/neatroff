/*
 * NEATROFF TYPESETTING SYSTEM
 *
 * Copyright (C) 2012-2016 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
	fprintf(stderr, "%s", msg);
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

/* parse the argument of -r and -d options */
static void cmddef(char *arg, int *reg, char **def)
{
	char regname[RNLEN] = "";
	char *eq = strchr(arg, '=');
	memcpy(regname, arg, eq ? MIN(RNLEN - 1, eq - arg) : 1);
	*reg = map(regname);
	*def = eq ? eq + 1 : arg + 1;
}

/* find the macro specified with -m option */
static int cmdmac(char *dir, char *arg)
{
	char path[PATHLEN];
	snprintf(path, sizeof(path), "%s/%s.tmac", dir, arg);
	if (!xopens(path))
		snprintf(path, sizeof(path), "%s/tmac.%s", dir, arg);
	if (!xopens(path))
		snprintf(path, sizeof(path), "%s/%s", dir, arg);
	if (!xopens(path))
		return 1;
	in_queue(path);
	return 0;
}

static char *usage =
	"Usage: neatroff [options] input\n\n"
	"Options:\n"
	"  -mx   \tinclude macro x\n"
	"  -rx=y \tset number register x to y\n"
	"  -dx=y \tdefine string register x as y\n"
	"  -C    \tenable compatibility mode\n"
	"  -Tdev \tset output device\n"
	"  -Fdir \tset font directory (" TROFFFDIR ")\n"
	"  -Mdir \tset macro directory (" TROFFMDIR ")\n";

int main(int argc, char **argv)
{
	char *fontdir = TROFFFDIR;
	char *macrodir = TROFFMDIR;
	char *mac, *def, *dev = "utf";
	int reg, ret;
	int i;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-' || !argv[i][1])
			break;
		switch (argv[i][1]) {
		case 'C':
			n_cp = 1;
			break;
		case 'm':
			mac = argv[i] + 2;
			if (strchr(mac, '/') || (cmdmac(macrodir, mac) && cmdmac(".", mac)))
				in_queue(mac);
			break;
		case 'r':
			cmddef(argv[i][2] ? argv[i] + 2 : argv[++i], &reg, &def);
			num_set(reg, eval_re(def, num_get(reg), 'u'));
			break;
		case 'd':
			cmddef(argv[i][2] ? argv[i] + 2 : argv[++i], &reg, &def);
			str_set(reg, def);
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
			fprintf(stderr, "%s", usage);
			return 1;
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
	dir_done();
	return ret;
}
