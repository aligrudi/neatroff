#include <stdio.h>
#include <stdlib.h>
#include "xroff.h"

#define CPBUF		4

static int cp_buf[CPBUF];	/* pushed character stack */
static int cp_backed;		/* number of pushed characters */
static int cp_nblk;		/* input block depth (text in \{ and \}) */
static int cp_sblk[NIES];	/* skip \} escape at this depth, if set */

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
	int id;
	int c = cp_next();
	if (c != '-' && c != '+')
		cp_back(c);
	id = regid();
	if (c == '-' || c == '+')
		num_get(id, c == '+' ? 1 : -1);
	if (num_str(id))
		in_push(num_str(id), NULL);
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
	char *arg = NULL;
	c = cp_next();
	if (c >= '1' && c <= '9')
		arg = in_arg(c - '0');
	if (arg)
		in_push(arg, NULL);
}

static int cp_raw(void)
{
	int c;
	if (cp_backed)
		return cp_buf[--cp_backed];
	c = in_next();
	if (c == '\\') {
		c = in_next();
		if (c == '\n')
			return in_next();
		if (c == '.')
			return '.';
		if (c == '{' && cp_nblk < LEN(cp_sblk))
			cp_sblk[cp_nblk++] = 0;
		if (c == '}' && cp_nblk > 0)
			if (cp_sblk[--cp_nblk])
				return cp_raw();
		cp_back(c);
		return '\\';
	}
	return c;
}

int cp_next(void)
{
	int c;
	if (cp_backed)
		return cp_buf[--cp_backed];
	c = cp_raw();
	if (c == '\\') {
		c = cp_raw();
		if (c == '"') {
			while (c >= 0 && c != '\n')
				c = cp_raw();
		} else if (c == 'n') {
			cp_num();
			c = cp_raw();
		} else if (c == '*') {
			cp_str();
			c = cp_raw();
		} else if (c == '$') {
			cp_arg();
			c = cp_raw();
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

static int cp_top(void)
{
	return cp_backed ? cp_buf[cp_backed - 1] : -1;
}

void cp_blk(int skip)
{
	int c;
	int nblk = cp_nblk;
	do {
		c = cp_raw();
	} while (c == ' ' || c == '\t');
	if (c == '\\' && cp_top() == '{') {	/* a troff \{ \} block */
		if (skip) {
			while (skip && cp_nblk > nblk && c >= 0)
				c = cp_raw();
		} else {
			cp_sblk[nblk] = 1;
			cp_raw();
		}
	} else {
		if (!skip)
			cp_back(c);
	}
	while (skip && c != '\n')	/* skip until the end of the line */
		c = cp_raw();
}
