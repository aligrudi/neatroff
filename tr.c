#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

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

/* skip everything until the end of line */
static void jmp_eol(void)
{
	int c;
	do {
		c = cp_next();
	} while (c >= 0 && c != '\n');
}

static void tr_ll(char **args)
{
	if (args[1])
		n_l = tr_int(args[1], n_l, 'm');
}

static void tr_vs(char **args)
{
	if (args[1])
		n_v = tr_int(args[1], n_v, 'p');
}

static void tr_pl(char **args)
{
	if (args[1])
		n_p = tr_int(args[1], n_p, 'v');
}

static void tr_nr(char **args)
{
	int id;
	if (!args[2])
		return;
	id = REG(args[1][0], args[1][1]);
	*nreg(id) = tr_int(args[2], *nreg(id), 'u');
}

static void tr_ds(char **args)
{
	if (!args[2])
		return;
	str_set(REG(args[1][0], args[1][1]), args[2]);
}

static void tr_rm(char **args)
{
	if (!args[1])
		return;
	str_rm(REG(args[1][0], args[1][1]));
}

static void tr_rn(char **args)
{
	if (!args[2])
		return;
	str_rn(REG(args[1][0], args[1][1]), REG(args[2][0], args[2][1]));
}

static char *arg_regname(char *s, int len);

static void tr_de(char **args)
{
	struct sbuf sbuf;
	char buf[4];
	int end[4] = {'.'};
	int c;
	int i;
	if (!args[1])
		return;
	if (args[2]) {
		end[0] = args[2][0];
		end[1] = args[2][1];
	}
	sbuf_init(&sbuf);
	while ((c = cp_next()) >= 0) {
		sbuf_add(&sbuf, c);
		if (c == '\n') {
			c = cp_next();
			if (c == '.') {
				arg_regname(buf, 4);
				if (buf[0] == end[0] && buf[1] == end[1]) {
					jmp_eol();
					break;
				}
				sbuf_add(&sbuf, '.');
				for (i = 0; buf[i]; i++)
					sbuf_add(&sbuf, (unsigned char) buf[i]);
			} else {
				if (c >= 0)
					sbuf_add(&sbuf, c);
			}
		}
	}
	str_set(REG(args[1][0], args[1][1]), sbuf_buf(&sbuf));
	sbuf_done(&sbuf);
}

static void tr_na(char **args)
{
	n_j = 0;
}

static char *arg_regname(char *s, int len)
{
	char *e = s + 2;
	int c;
	while ((c = cp_next()) == ' ')
		;
	while (s < e && c >= 0 && c != ' ' && c != '\n') {
		*s++ = c;
		c = cp_next();
	}
	if (c >= 0)
		cp_back(c);
	*s++ = '\0';
	return s;
}

static char *arg_normal(char *s, int len)
{
	char *e = s + len - 1;
	int quoted = 0;
	int c;
	while ((c = cp_next()) == ' ')
		;
	if (c == '"') {
		quoted = 1;
		c = cp_next();
	}
	while (s < e && c > 0 && c != '\n') {
		if (!quoted && c == ' ')
			break;
		if (quoted && c == '"') {
			c = cp_next();
			if (c != '"')
				break;
		}
		*s++ = c;
		c = cp_next();
	}
	if (c >= 0)
		cp_back(c);
	*s++ = '\0';
	return s;
}

static char *arg_string(char *s, int len)
{
	char *e = s + len - 1;
	int c;
	while ((c = cp_next()) == ' ')
		;
	if (c == '"')
		c = cp_next();
	while (s < e && c > 0 && c != '\n') {
		*s++ = c;
		c = cp_next();
	}
	*s++ = '\0';
	if (c >= 0)
		cp_back(c);
	return s;
}

/* read macro arguments */
static int mkargs(char **args, char *buf, int len)
{
	char *s = buf;
	char *e = buf + len - 1;
	int c;
	int n = 0;
	while (n < NARGS) {
		c = cp_next();
		if (c < 0 || c == '\n')
			return n;
		cp_back(c);
		args[n++] = s;
		s = arg_normal(s, e - s);
	}
	jmp_eol();
	return n;
}

/* read arguments for .ds */
static int mkargs_ds(char **args, char *buf, int len)
{
	char *s = buf;
	char *e = buf + len - 1;
	int c;
	args[0] = s;
	s = arg_regname(s, e - s);
	args[1] = s;
	s = arg_string(s, e - s);
	c = cp_next();
	if (c >= 0 && c != '\n')
		jmp_eol();
	return 2;
}

/* read arguments for commands .nr that expect a register name */
static int mkargs_reg1(char **args, char *buf, int len)
{
	char *s = buf;
	char *e = buf + len - 1;
	args[0] = s;
	s = arg_regname(s, e - s);
	return mkargs(args + 1, s, e - s) + 1;
}

static struct cmd {
	char *id;
	void (*f)(char **args);
	int (*args)(char **args, char *buf, int len);
} cmds[] = {
	{"bp", tr_bp},
	{"br", tr_br},
	{"de", tr_de, mkargs_reg1},
	{"di", tr_di},
	{"ds", tr_ds, mkargs_ds},
	{"ev", tr_ev},
	{"fp", tr_fp},
	{"ft", tr_ft},
	{"in", tr_in},
	{"ll", tr_ll},
	{"na", tr_na},
	{"nf", tr_nf},
	{"nr", tr_nr, mkargs_reg1},
	{"pl", tr_pl},
	{"ps", tr_ps},
	{"rm", tr_rm},
	{"rn", tr_rn},
	{"sp", tr_sp},
	{"vs", tr_vs},
};

int tr_next(void)
{
	int c = cp_next();
	int nl = c == '\n';
	char *args[NARGS + 3] = {NULL};
	char cmd[RLEN];
	char buf[LNLEN];
	struct cmd *req;
	while (tr_nl && (c == '.' || c == '\'')) {
		nl = 1;
		args[0] = cmd;
		cmd[0] = c;
		req = NULL;
		arg_regname(cmd + 1, sizeof(cmd) - 1);
		req = str_dget(REG(cmd[1], cmd[2]));
		if (req) {
			if (req->args)
				req->args(args + 1, buf, sizeof(buf));
			else
				mkargs(args + 1, buf, sizeof(buf));
			req->f(args);
		} else {
			mkargs(args + 1, buf, sizeof(buf));
			if (str_get(REG(cmd[1], cmd[2])))
				in_push(str_get(REG(cmd[1], cmd[2])), args + 1);
		}
		c = cp_next();
	}
	tr_nl = nl;
	return c;
}

void tr_init(void)
{
	int i;
	for (i = 0; i < LEN(cmds); i++)
		str_dset(REG(cmds[i].id[0], cmds[i].id[1]), &cmds[i]);
}
