#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define NARGS		10
#define LINEL		1024
#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

static int tr_nl = 1;

static void tr_ft(int argc, char **args)
{
	int fn;
	if (argc < 2)
		return;
	fn = dev_font(args[1]);
	if (fn >= 0)
		n_f = fn;
}

static void tr_ps(int argc, char **args)
{
	if (argc >= 2)
		n_s = atoi(args[1]);
}

static void tr_vs(int argc, char **args)
{
	if (argc >= 2)
		n_v = atoi(args[1]) * SC_PT;
}

static void tr_ll(int argc, char **args)
{
	if (argc >= 2)
		n_v = atoi(args[1]) * SC_PT;
}

static void tr_in(int argc, char **args)
{
	if (argc >= 2)
		n_v = atoi(args[1]) * SC_PT;
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
	{"ft", tr_ft},
	{"nr", tr_nr},
	{"ps", tr_ps},
	{"sp", tr_sp},
	{"vs", tr_vs},
	{"vs", tr_ll},
	{"vs", tr_in},
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
