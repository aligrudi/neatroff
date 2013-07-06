#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "roff.h"

/* returns a static buffer */
char *clr_str(int c)
{
	static char clr_buf[32];
	if (!c)
		return "0";
	sprintf(clr_buf, "#%02x%02x%02x", CLR_R(c), CLR_G(c), CLR_B(c));
	return clr_buf;
}

static struct color {
	char *name;
	int value;
} colors[] = {
	{"black", CLR_RGB(0, 0, 0)},
	{"red", CLR_RGB(0xff, 0, 0)},
	{"green", CLR_RGB(0, 0xff, 0)},
	{"yellow", CLR_RGB(0xff, 0xff, 0)},
	{"blue", CLR_RGB(0, 0, 0xff)},
	{"magenta", CLR_RGB(0xff, 0, 0xff)},
	{"cyan", CLR_RGB(0, 0xff, 0xff)},
	{"white", CLR_RGB(0xff, 0xff, 0xff)},
};

/* read color component */
static int clrcomp(char *s, int len)
{
	static char *digs = "0123456789abcdef";
	int n = 0;
	int i;
	for (i = 0; i < len; i++)
		if (strchr(digs, tolower(s[i])))
			n = n * 16 + (strchr(digs, tolower(s[i])) - digs);
	return len == 1 ? n << 4 : n;
}

int clr_get(char *s)
{
	int i;
	if (s[0] == '#' && strlen(s) == 7)
		return CLR_RGB(clrcomp(s + 1, 2), clrcomp(s + 3, 2), clrcomp(s + 5, 2));
	if (s[0] == '#' && strlen(s) == 4)
		return CLR_RGB(clrcomp(s + 1, 1), clrcomp(s + 2, 1), clrcomp(s + 3, 1));
	if (isdigit(s[0]) && atoi(s) >= 0 && atoi(s) < LEN(colors))
		return colors[atoi(s)].value;
	for (i = 0; i < LEN(colors); i++)
		if (!strcmp(colors[i].name, s))
			return colors[i].value;
	return 0;
}
