#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

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
	if (mag > MAXFRAC) {
		n /= mag / MAXFRAC;
		mag /= mag / MAXFRAC;
	}
	n = readunit(*s && strchr(SCHAR, *s) ? *s++ : defunit, n);
	*_s = s;
	return n / (mag > 0 ? mag : 1);
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
int eval_up(char **s, int unit)
{
	defunit = unit;
	if (unit == 'v')
		abspos = -n_d;
	if (unit == 'm')
		abspos = n_lb - f_hpos();
	return evalexpr(s);
}

/* evaluate s relative to its previous value */
int eval_re(char *s, int orig, int unit)
{
	int n;
	int rel = 0;		/* n should be added to orig */
	if (*s == '+' || *s == '-') {
		rel = *s == '+' ? 1 : -1;
		s++;
	}
	n = eval_up(&s, unit);
	if (rel)
		return rel > 0 ? orig + n : orig - n;
	return n;
}

/* evaluate s */
int eval(char *s, int unit)
{
	return eval_up(&s, unit);
}
