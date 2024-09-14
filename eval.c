/* evaluation of integer expressions */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

#define SCHAR	"icpPvmnu"	/* scale indicators */

typedef long long eval_t;

static int defunit = 0;		/* default scale indicator */
static int abspos = 0;		/* absolute position like |1i */

static eval_t readunit(int c, eval_t n)
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

static eval_t evalnum(char **_s)
{
	char *s = *_s;
	eval_t n = 0;		/* the result */
	eval_t mag = 0;		/* n should be divided by mag */
	while (isdigit((unsigned char) *s) || *s == '.') {
		if (mag == MAXFRAC || (mag > 0 && n > 200000000u)) {
			s++;
			continue;
		}
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
	return **s == '.' || isdigit((unsigned char) **s);
}

static eval_t evalexpr(char **s);
static eval_t evalatom(char **s);

static eval_t evalatom(char **s)
{
	if (evalisnum(s))
		return evalnum(s);
	if (!evaljmp(s, '-'))
		return -evalatom(s);
	if (!evaljmp(s, '+'))
		return evalatom(s);
	if (!evaljmp(s, '|'))
		return abspos + evalatom(s);
	if (!evaljmp(s, '(')) {
		eval_t ret = evalexpr(s);
		evaljmp(s, ')');
		return ret;
	}
	return 0;
}

static int nonzero(int n)
{
	if (!n)
		errdie("neatroff: divide by zero\n");
	return n;
}

static eval_t evalexpr(char **s)
{
	eval_t ret = evalatom(s);
	while (**s) {
		if (!evaljmp(s, '+'))
			ret += evalatom(s);
		else if (!evaljmp(s, '-'))
			ret -= evalatom(s);
		else if (!evaljmp(s, '/'))
			ret /= nonzero(evalatom(s));
		else if (!evaljmp(s, '*'))
			ret *= evalatom(s);
		else if (!evaljmp(s, '%'))
			ret %= nonzero(evalatom(s));
		else if (!evaljmp(s, '<'))
			ret = !evaljmp(s, '=') ? ret <= evalatom(s) : ret < evalatom(s);
		else if (!evaljmp(s, '>'))
			ret = !evaljmp(s, '=') ? ret >= evalatom(s) : ret > evalatom(s);
		else if (!evaljmp(s, '=') + !evaljmp(s, '='))
			ret = ret == evalatom(s);
		else if (!evaljmp(s, '&'))
			ret = ret > 0 && evalatom(s) > 0;
		else if (!evaljmp(s, ':'))
			ret = ret > 0 || evalatom(s) > 0;
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
		abspos = -n_d - ren_vpos();
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
