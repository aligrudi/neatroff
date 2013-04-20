#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "xroff.h"

#define SCHAR	"icpPvmnu"	/* scale indicators */

static int defunit = 0;		/* default scale indicator */
static int abspos = 0;		/* absolute position like |1i */

static int readunit(int c, int n)
{
	switch (c) {
	case 'i':
		return n * SC_IN;
	case 'c':
		return n * SC_IN * 50 / 127;
	case 'p':
		return n * SC_IN / 72;
	case 'P':
		return n * SC_IN / 6;
	case 'v':
		return n * n_v;
	case 'm':
		return n * n_s * SC_IN / 72;
	case 'n':
		return n * n_s * SC_IN / 144;
	case 'u':
		return n;
	}
	return n;
}

static int evalnum(char **_s)
{
	char *s = *_s;
	int n = 0;		/* the result */
	int mag = 0;		/* n should be divided by mag */
	while (isdigit(*s) || *s == '.') {
		if (*s == '.') {
			mag = 1;
			s++;
			continue;
		}
		mag *= 10;
		n = n * 10 + *s++ - '0';
	}
	n = readunit(*s && strchr(SCHAR, *s) ? *s++ : defunit, n);
	*_s = s;
	return n / (mag > 0 ? mag : 1);		/* this may overflow */
}

static int evaljmp(char **s, int c)
{
	if (**s == c) {
		(*s)++;
		return 0;
	}
	return 1;
}

static int evalisnum(char **s)
{
	return **s == '.' || isdigit(**s);
}

static char **wid_s;

static int wid_next(void)
{
	return (unsigned char) *(*wid_s)++;
}

static void wid_back(int c)
{
	(*wid_s)--;
}

static int evalexpr(char **s);
static int evalatom(char **s);

static int evalatom(char **s)
{
	int ret;
	if (evalisnum(s))
		return evalnum(s);
	if (!evaljmp(s, '-'))
		return -evalatom(s);
	if (!evaljmp(s, '+'))
		return evalatom(s);
	if (!evaljmp(s, '|'))
		return abspos + evalatom(s);
	if (!evaljmp(s, '(')) {
		ret = evalexpr(s);
		evaljmp(s, ')');
		return ret;
	}
	if ((*s)[0] == '\\' && (*s)[1] == 'w') {
		*s += 2;
		wid_s = s;
		ret = ren_wid(wid_next, wid_back);
		readunit(**s && strchr(SCHAR, **s) ? *(*s)++ : defunit, ret);
		return ret;
	}
	return 0;
}

static int evalexpr(char **s)
{
	int ret = evalatom(s);
	while (**s) {
		if (!evaljmp(s, '+'))
			ret += evalatom(s);
		else if (!evaljmp(s, '-'))
			ret -= evalatom(s);
		else if (!evaljmp(s, '/'))
			ret /= evalatom(s);
		else if (!evaljmp(s, '*'))
			ret *= evalatom(s);
		else if (!evaljmp(s, '%'))
			ret %= evalatom(s);
		else if (!evaljmp(s, '<'))
			ret = !evaljmp(s, '=') ? ret <= evalatom(s) : ret < evalatom(s);
		else if (!evaljmp(s, '>'))
			ret = !evaljmp(s, '=') ? ret >= evalatom(s) : ret > evalatom(s);
		else if (!evaljmp(s, '=') + !evaljmp(s, '='))
			ret = ret == evalatom(s);
		else if (!evaljmp(s, '&'))
			ret = ret && evalatom(s);
		else if (!evaljmp(s, ':'))
			ret = ret || evalatom(s);
		else
			break;
	}
	return ret;
}

/* evaluate *s and update s to point to the last character read */
int eval_up(char **s, int orig, int unit)
{
	int n;
	int rel = 0;		/* n should be added to orig */
	if (**s == '+' || **s == '-') {
		rel = **s == '+' ? 1 : -1;
		(*s)++;
	}
	defunit = unit;
	if (unit == 'v')
		abspos = -n_d;
	if (unit == 'm')
		abspos = n_lb - f_hpos();
	n = evalexpr(s);
	if (rel)
		return rel > 0 ? orig + n : orig - n;
	return n;
}

/* evaluate s */
int eval(char *s, int orig, int unit)
{
	return eval_up(&s, orig, unit);
}
