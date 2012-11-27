/*
 * neatroff troff clone
 *
 * Copyright (C) 2012 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the modified BSD license.
 */
#include <stdarg.h>
#include <stdio.h>
#include "xroff.h"

static void g_init(void)
{
	n_f = 1;
	n_o = SC_IN;
	n_p = SC_IN * 11;
	n_l = SC_IN * 65 / 10;
	n_i = 0;
	n_s = 10;
	n_v = 12 * SC_PT;
	n_s0 = n_s;
	n_f0 = n_f;
	n_ad = 1;
}

static void compile(void)
{
	printf("s%d\n", n_s);
	printf("f%d\n", n_f);
	ren_page(1);
	render();
	printf("V%d\n", n_p);
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
	g_init();
	compile();
	dev_close();
	return 0;
}
