/*
 * neatroff troff clone
 *
 * Copyright (C) 2012-2013 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the modified BSD license.
 */
#include <stdarg.h>
#include <stdio.h>
#include "xroff.h"

static void g_init(void)
{
	n_o = SC_IN;
	n_p = SC_IN * 11;
}

static void compile(void)
{
	OUT("s%d\n", n_s);
	OUT("f%d\n", n_f);
	ren_page(1);
	render();
	OUT("V%d\n", n_p);
}

void errmsg(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

int main(void)
{
	dev_open("/root/troff/home/font/devutf");
	env_init();
	tr_init();
	g_init();
	compile();
	env_free();
	dev_close();
	return 0;
}
