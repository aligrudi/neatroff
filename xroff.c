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
}

static void compile(void)
{
	printf("s%d\n", n_s);
	printf("f%d\n", n_f);
	printf("H%d\n", n_o);
	printf("V%d\n", n_v);
	render();
	printf("V%d\n", n_p);
}

void errmsg(char *msg)
{
	fprintf(stderr, "%s", msg);
}

int main(void)
{
	dev_open("/root/troff/home/font/devutf");
	g_init();
	compile();
	dev_close();
	return 0;
}
