#include <stdio.h>
#include <stdlib.h>
#include "xroff.h"

#define CPBUF		4

static int cp_backed = 0;
static int cp_buf[CPBUF];

static int regid(void)
{
	int c1;
	int c2 = 0;
	c1 = cp_next();
	if (c1 == '(') {
		c1 = cp_next();
		c2 = cp_next();
	}
	return REG(c1, c2);
}

static void cp_num(void)
{
	char buf[32];
	sprintf(buf, "%d", num_get(regid()));
	in_push(buf, NULL);
}

static void cp_str(void)
{
	char *buf = str_get(regid());
	if (buf)
		in_push(buf, NULL);
}

static void cp_arg(void)
{
	int c;
	char *arg;
	c = cp_next();
	if (c >= '1' && c <= '9')
		arg = in_arg(c - '0');
	if (arg)
		in_push(arg, NULL);
}

int cp_next(void)
{
	int c;
	if (cp_backed)
		return cp_buf[--cp_backed];
	c = in_next();
	if (c == '\\') {
		c = in_next();
		if (c == 'n') {
			cp_num();
			c = in_next();
		} else if (c == '*') {
			cp_str();
			c = in_next();
		} else if (c == '$') {
			cp_arg();
			c = in_next();
		} else {
			cp_back(c);
			c = '\\';
		}
	}
	return c;
}

void cp_back(int c)
{
	if (cp_backed < CPBUF)
		cp_buf[cp_backed++] = c;
}
