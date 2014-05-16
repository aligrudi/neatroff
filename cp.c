/* copy-mode character interpretation */
#include <stdio.h>
#include <stdlib.h>
#include "roff.h"

static int cp_nblk;		/* input block depth (text in \{ and \}) */
static int cp_sblk[NIES];	/* skip \} escape at this depth, if set */
static int cp_cpmode;		/* disable the interpretation \w and \E */

static void cparg(char *d)
{
	int c = cp_next();
	int i = 0;
	if (c == '(') {
		d[i++] = cp_next();
		d[i++] = cp_next();
	} else if (!n_cp && c == '[') {
		c = cp_next();
		while (i < NMLEN - 1 && c >= 0 && c != ']') {
			d[i++] = c;
			c = cp_next();
		}
	} else {
		d[i++] = c;
	}
	d[i] = '\0';
}

static int regid(void)
{
	char regname[NMLEN];
	cparg(regname);
	return map(regname);
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

static void cp_numfmt(void)
{
	in_push(num_getfmt(regid()), NULL);
}

static void cp_arg(void)
{
	char argname[NMLEN];
	char *arg = NULL;
	int argnum;
	cparg(argname);
	argnum = atoi(argname);
	if (argnum > 0 && argnum < NARGS + 1)
		arg = in_arg(argnum);
	if (arg)
		in_push(arg, NULL);
}

static void cp_width(void)
{
	char wid[16];
	sprintf(wid, "%d", ren_wid(cp_next, cp_back));
	in_push(wid, NULL);
}

static void cp_numdef(void)
{
	char arg[ILNLEN];
	char *s;
	argnext(arg, 'R', cp_next, cp_back);
	s = arg;
	while (*s && *s != ' ')
		s++;
	if (!*s)
		return;
	*s++ = '\0';
	num_set(map(arg), eval_re(s, num_get(map(arg), 0), 'u'));
}

static int cp_raw(void)
{
	int c;
	if (in_top() >= 0)
		return in_next();
	do {
		c = in_next();
	} while (c == c_ni);
	if (c == c_ec) {
		do {
			c = in_next();
		} while (c == c_ni);
		if (c == '\n')
			return cp_raw();
		if (c == '.')
			return '.';
		if (c == '\\') {
			in_back('\\');
			return c_ni;
		}
		if (c == 't') {
			in_back('\t');
			return c_ni;
		}
		if (c == 'a') {
			in_back('');
			return c_ni;
		}
		if (c == '{' && cp_nblk < LEN(cp_sblk))
			cp_sblk[cp_nblk++] = 0;
		if (c == '}' && cp_nblk > 0)
			if (cp_sblk[--cp_nblk])
				return cp_raw();
		in_back(c);
		return c_ec;
	}
	return c;
}

int cp_next(void)
{
	int c;
	if (in_top() >= 0)
		return in_next();
	c = cp_raw();
	if (c == c_ec) {
		c = cp_raw();
		if (c == 'E' && !cp_cpmode)
			c = cp_next();
		if (c == '"') {
			while (c >= 0 && c != '\n')
				c = cp_raw();
		} else if (c == 'w' && !cp_cpmode) {
			cp_width();
			c = cp_next();
		} else if (c == 'n') {
			cp_num();
			c = cp_next();
		} else if (c == '*') {
			cp_str();
			c = cp_next();
		} else if (c == 'g') {
			cp_numfmt();
			c = cp_next();
		} else if (c == '$') {
			cp_arg();
			c = cp_next();
		} else if (c == 'R' && !cp_cpmode) {
			cp_numdef();
			c = cp_next();
		} else {
			cp_back(c);
			c = c_ec;
		}
	}
	return c;
}

void cp_blk(int skip)
{
	int c;
	int nblk = cp_nblk;
	do {
		c = skip ? cp_raw() : cp_next();
	} while (c == ' ' || c == '\t');
	if (skip) {
		while (c >= 0 && (c != '\n' || cp_nblk > nblk))
			c = cp_raw();
	} else {
		if (c == c_ec && in_top() == '{') {	/* a troff \{ \} block */
			cp_sblk[nblk] = 1;
			cp_raw();
		} else {
			cp_back(c);
		}
	}
}

void cp_copymode(int mode)
{
	cp_cpmode = mode;
}
