#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

static int tr_nl = 1;

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
		n_l = eval(args[1], n_l, 'm');
}

static void tr_vs(char **args)
{
	if (args[1])
		n_v = eval(args[1], n_v, 'p');
}

static void tr_pl(char **args)
{
	if (args[1])
		n_p = eval(args[1], n_p, 'v');
}

static void tr_nr(char **args)
{
	int id;
	if (!args[2])
		return;
	id = REG(args[1][0], args[1][1]);
	*nreg(id) = eval(args[2], *nreg(id), 'u');
}

static void tr_ds(char **args)
{
	if (!args[2])
		return;
	str_set(REG(args[1][0], args[1][1]), args[2]);
}

static void tr_as(char **args)
{
	int reg;
	char *s1, *s2, *s;
	if (!args[2])
		return;
	reg = REG(args[1][0], args[1][1]);
	s1 = str_get(reg) ? str_get(reg) : "";
	s2 = args[2];
	s = malloc(strlen(s1) + strlen(s2) + 1);
	strcpy(s, s1);
	strcat(s, s2);
	str_set(reg, s);
	free(s);
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
	int id, c, i;
	if (!args[1])
		return;
	if (args[2]) {
		end[0] = args[2][0];
		end[1] = args[2][1];
	}
	id = REG(args[1][0], args[1][1]);
	sbuf_init(&sbuf);
	if (args[0][1] == 'a' && args[0][2] == 'm' && str_get(id))
		sbuf_append(&sbuf, str_get(id));
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
	str_set(id, sbuf_buf(&sbuf));
	sbuf_done(&sbuf);
}

/* read into sbuf until stop */
static void read_until(struct sbuf *sbuf, int stop)
{
	int c;
	while ((c = cp_next()) >= 0) {
		if (c == stop)
			return;
		if (c == '\n') {
			cp_back(c);
			return;
		}
		sbuf_add(sbuf, c);
	}
}

/* evaluate .if strcmp (i.e. 'str'str') */
static int if_strcmp(void)
{
	struct sbuf s1, s2;
	int ret;
	sbuf_init(&s1);
	sbuf_init(&s2);
	read_until(&s1, '\'');
	read_until(&s2, '\'');
	ret = !strcmp(sbuf_buf(&s1), sbuf_buf(&s2));
	sbuf_done(&s1);
	sbuf_done(&s2);
	return ret;
}

/* evaluate .if condition */
static int if_cond(void)
{
	struct sbuf sbuf;
	char *s;
	int ret;
	sbuf_init(&sbuf);
	read_until(&sbuf, ' ');
	s = sbuf_buf(&sbuf);
	if (s[0] == 'o' && s[1] == '\0')
		ret = n_pg % 2;
	else if (s[0] == 'e' && s[1] == '\0')
		ret = !(n_pg % 2);
	else if (s[0] == 't' && s[1] == '\0')
		ret = 1;
	else if (s[0] == 'n' && s[1] == '\0')
		ret = 0;
	else
		ret = eval(s, 0, '\0') > 0;
	sbuf_done(&sbuf);
	return ret;
}

/* execute or skip the line or block following .if */
static void if_blk(int doexec)
{
	int c;
	if (doexec) {
		do {
			c = cp_next();
		} while (c == ' ');
		cp_back(c);
	} else {
		cp_skip();
	}
}

static int ie_cond[NIES];	/* .ie condition stack */
static int ie_depth;

static void tr_if(char **args)
{
	int neg = 0;
	int ret;
	int c;
	do {
		c = cp_next();
	} while (c == ' ' || c == '\t');
	if (c == '!') {
		neg = 1;
		c = cp_next();
	}
	if (c == '\'') {
		ret = if_strcmp();
	} else {
		cp_back(c);
		ret = if_cond();
	}
	if (args[0][1] == 'i' && args[0][2] == 'e')	/* .ie command */
		if (ie_depth < NIES)
			ie_cond[ie_depth++] = ret != neg;
	if_blk(ret != neg);
}

static void tr_el(char **args)
{
	if_blk(ie_depth > 0 ? !ie_cond[--ie_depth] : 0);
}

static void tr_na(char **args)
{
	n_na = 1;
}

static void tr_ad(char **args)
{
	n_na = 0;
	if (!args[1])
		return;
	switch (args[1][0]) {
	case '0' + AD_L:
	case 'l':
		n_j = AD_L;
		break;
	case '0' + AD_R:
	case 'r':
		n_j = AD_R;
		break;
	case '0' + AD_C:
	case 'c':
		n_j = AD_C;
		break;
	case '0' + AD_B:
	case 'b':
	case 'n':
		n_j = AD_B;
		break;
	}
}

static void tr_tm(char **args)
{
	fprintf(stderr, "%s\n", args[1]);
}

static void tr_so(char **args)
{
	if (args[1])
		in_source(args[1]);
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

/* do not read arguments; for .if, .ie and .el */
static int mkargs_null(char **args, char *buf, int len)
{
	return 0;
}

/* read the whole line for .tm */
static int mkargs_eol(char **args, char *buf, int len)
{
	char *s = buf;
	char *e = buf + len - 1;
	int c;
	args[0] = s;
	c = cp_next();
	while (c == ' ')
		c = cp_next();
	while (s < e && c >= 0 && c != '\n') {
		*s++ = c;
		c = cp_next();
	}
	*s = '\0';
	return 1;
}

static struct cmd {
	char *id;
	void (*f)(char **args);
	int (*args)(char **args, char *buf, int len);
} cmds[] = {
	{DIV_BEG + 1, tr_divbeg},
	{DIV_END + 1, tr_divend},
	{"ad", tr_ad},
	{"am", tr_de, mkargs_reg1},
	{"as", tr_as, mkargs_ds},
	{"bp", tr_bp},
	{"br", tr_br},
	{"ch", tr_ch},
	{"da", tr_di},
	{"de", tr_de, mkargs_reg1},
	{"di", tr_di},
	{"ds", tr_ds, mkargs_ds},
	{"dt", tr_dt},
	{"el", tr_el, mkargs_null},
	{"ev", tr_ev},
	{"fi", tr_fi},
	{"fp", tr_fp},
	{"ft", tr_ft},
	{"ie", tr_if, mkargs_null},
	{"if", tr_if, mkargs_null},
	{"in", tr_in},
	{"ll", tr_ll},
	{"na", tr_na},
	{"ne", tr_ne},
	{"nf", tr_nf},
	{"nr", tr_nr, mkargs_reg1},
	{"pl", tr_pl},
	{"ps", tr_ps},
	{"rm", tr_rm},
	{"rn", tr_rn},
	{"so", tr_so},
	{"sp", tr_sp},
	{"ti", tr_ti},
	{"tm", tr_tm, mkargs_eol},
	{"vs", tr_vs},
	{"wh", tr_wh},
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
		memset(args, 0, sizeof(args));
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
