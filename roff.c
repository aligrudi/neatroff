/*
 * neatroff troff clone
 *
 * Copyright (C) 2012-2013 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the modified BSD license.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

static void g_init(void)
{
	n_o = SC_IN;
	n_p = SC_IN * 11;
	n_lg = 1;
	n_kn = 0;
}

void errmsg(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

int main(int argc, char **argv)
{
	int i;
	char path[PATHLEN];
	int ret;
	dev_open(TROFFROOT "/font/devutf");
	env_init();
	tr_init();
	g_init();
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-' || !argv[i][0])
			break;
		if (argv[i][1] == 'C')
			n_cp = 1;
		if (argv[i][1] == 'm') {
			sprintf(path, TROFFROOT "/tmac/tmac.%s", argv[i] + 2);
			in_queue(path);
		}
	}
	if (i == argc)
		in_queue(NULL);	/* reading from standard input */
	for (; i < argc; i++)
		in_queue(!strcmp("-", argv[i]) ? NULL : argv[i]);
	str_set(REG('.', 'P'), TROFFROOT);
	out("s%d\n", n_s);
	out("f%d\n", n_f);
	ret = render();
	out("V%d\n", n_p);
	env_done();
	dev_close();
	return ret;
}
