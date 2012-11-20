#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define NARGS		10
#define LINEL		1024
#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

static int tr_nl = 1;

static int unit_scale(int c, int n, int mag)
{
	int mul = 1;
	int div = 1;
	switch (c) {
	case 'i':
		mul = SC_IN;
		break;
	case 'c':
		mul = SC_IN * 50;
		div = 127;
		break;
	case 'p':
		mul = SC_IN;
		div = 72;
		break;
	case 'P':
		mul = SC_IN;
		div = 6;
		break;
	case 'v':
		mul = n_v;
		break;
	case 'm':
		mul = n_s * SC_IN;
		div = 72;
		break;
	case 'n':
		mul = n_s * SC_IN;
		div = 144;
		break;
	}
	/* it may overflow */
	return n * mul / div / mag;
}

int tr_int(char *s, int orig, int unit)
{
	int n = 0;		/* the result */
	int mag = 0;		/* n should be divided by mag */
	int rel = 0;		/* n should be added to orig */
	int neg = *s == '-';	/* n should be negated */
	if (*s == '+' || *s == '-') {
		rel = 1;
		s++;
	}
	while (isdigit(*s) || *s == '.') {
		if (*s == '.') {
			mag = 1;
			s++;
			continue;
		}
		mag *= 10;
		n = n * 10 + *s++ - '0';
	}
	if (!mag)
		mag = 1;
	if (unit)
		n = unit_scale(*s ? *s : unit, n, mag);
	else
		n /= mag;
	if (neg)
		n = -n;
	return rel ? orig + n : n;
}

static void tr_ll(int argc, char **args)
{
	if (argc >= 2)
		n_l = tr_int(args[1], n_l, 'm');
}

static void tr_vs(int argc, char **args)
{
	if (argc >= 2)
		n_v = tr_int(args[1], n_v, 'p');
}

static void tr_in(int argc, char **args)
{
	if (argc >= 2)
		n_i = tr_int(args[1], n_i, 'm');
}

static void tr_readcmd(char *s)
{
	int i = 0;
	int c;
	while ((c = cp_next()) == ' ')
		;
	while (i < 2 && c > 0 && isprint(c)) {
		s[i++] = c;
		c = cp_next();
	}
	s[i] = '\0';
	if (!isprint(c))
		cp_back(c);
}

static int tr_readargs(char **args, int maxargs, char *buf, int len)
{
	char *s = buf;
	char *e = buf + len - 1;
	int c;
	int n = 0;
	while ((c = cp_next()) == ' ')
		;
	while (n < maxargs && s < e && c > 0 && c != '\n') {
		args[n++] = s;
		while (s < e && c > 0 && c != ' ' && c != '\n') {
			*s++ = c;
			c = cp_next();
		}
		*s++ = '\0';
		while (c == ' ')
			c = cp_next();
	}
	if (c != '\n')
		cp_back(c);
	return n;
}

static struct cmd {
	char *id;
	void (*f)(int argc, char **args);
} cmds[] = {
	{"br", tr_br},
	{"fp", tr_fp},
	{"ft", tr_ft},
	{"nr", tr_nr},
	{"ps", tr_ps},
	{"sp", tr_sp},
	{"vs", tr_vs},
	{"ll", tr_ll},
	{"in", tr_in},
};

int tr_next(void)
{
	int c = cp_next();
	int nl = c == '\n';
	char *args[NARGS];
	char buf[LINEL];
	char cmd[LINEL];
	int argc;
	int i;
	while (tr_nl && (c == '.' || c == '\'')) {
		nl = 1;
		args[0] = cmd;
		cmd[0] = c;
		tr_readcmd(cmd + 1);
		argc = tr_readargs(args + 1, NARGS - 1, buf, LINEL);
		for (i = 0; i < LEN(cmds); i++)
			if (!strcmp(cmd + 1, cmds[i].id))
				cmds[i].f(argc + 1, args);
		c = cp_next();
	}
	tr_nl = nl;
	return c;
}
