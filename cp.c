#include <stdio.h>
#include "xroff.h"

static int cp_backed = -1;

static void cp_num(void)
{
	int c1;
	int c2 = 0;
	char buf[32];
	c1 = cp_next();
	if (c1 == '(') {
		c1 = cp_next();
		c2 = cp_next();
	}
	sprintf(buf, "%d", num_get(N_ID(c1, c2)));
	in_push(buf);
}

int cp_next(void)
{
	int c = cp_backed >= 0 ? cp_backed : in_next();
	cp_backed = -1;
	if (c == '\\') {
		c = in_next();
		if (c == 'n') {
			cp_num();
			c = in_next();
		} else {
			in_back(c);
			c = '\\';
		}
	}
	return c;
}

void cp_back(int c)
{
	cp_backed = c;
}
