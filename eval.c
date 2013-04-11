#include <ctype.h>
#include <stdio.h>
#include "xroff.h"

static int defunit = 0;		/* default scale indicator */

static int readunit(int c, int *mul, int *div)
{
	*mul = 1;
	*div = 1;
	switch (c) {
	case 'i':
		*mul = SC_IN;
		return 0;
	case 'c':
		*mul = SC_IN * 50;
		*div = 127;
		return 0;
	case 'p':
		*mul = SC_IN;
		*div = 72;
		return 0;
	case 'P':
		*mul = SC_IN;
		*div = 6;
		return 0;
	case 'v':
		*mul = n_v;
		return 0;
	case 'm':
		*mul = n_s * SC_IN;
		*div = 72;
		return 0;
	case 'n':
		*mul = n_s * SC_IN;
		*div = 144;
		return 0;
	case 'u':
		return 0;
	}
	return 1;
}

static int evalexpr(char **s);

static int evalnum(char **_s)
{
	char *s = *_s;
	int n = 0;		/* the result */
	int mag = 0;		/* n should be divided by mag */
	int mul, div;
	while (isdigit(*s) || *s == '.') {
		if (*s == '.') {
			mag = 1;
			s++;
			continue;
		}
		mag *= 10;
		n = n * 10 + *s++ - '0';
	}
	if (!readunit(*s, &mul, &div))
		s++;
	else
		readunit(defunit, &mul, &div);
	*_s = s;
	/* this may overflow */
	return n * mul / div / (mag > 0 ? mag : 1);
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

static int evalatom(char **s)
{
	if (!evaljmp(s, '-'))
		return -evalatom(s);
	if (!evaljmp(s, '+'))
		return evalatom(s);
	if (evalisnum(s))
		return evalnum(s);
	if (!evaljmp(s, '(')) {
		int ret = evalexpr(s);
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

int eval(char *s, int orig, int unit)
{
	int n;
	int rel = 0;		/* n should be added to orig */
	if (*s == '+' || *s == '-') {
		rel = *s == '+' ? 1 : -1;
		s++;
	}
	defunit = unit;
	n = evalexpr(&s);
	if (rel)
		return rel > 0 ? orig + n : orig - n;
	return n;
}
