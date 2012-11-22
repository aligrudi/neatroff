#include <stdio.h>
#include "xroff.h"

static int cp_backed = -1;

static int regid(void)
{
	int c1;
	int c2 = 0;
	c1 = cp_next();
	if (c1 == '(') {
		c1 = cp_next();
		c2 = cp_next();
	}
	return N_ID(c1, c2);
}

static void cp_num(void)
{
	char buf[32];
	sprintf(buf, "%d", num_get(regid()));
	in_push(buf);
}

static void cp_str(void)
{
	char *buf = str_get(regid());
	if (buf)
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
		} else if (c == '*') {
			cp_str();;
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
