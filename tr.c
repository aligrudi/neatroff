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

static void tr_vs(char **args)
{
	int vs = args[1] ? eval_re(args[1], n_v, 'p') : n_v0;
	n_v0 = n_v;
	n_v = MAX(0, vs);
}

static void tr_ls(char **args)
{
	int ls = args[1] ? eval_re(args[1], n_L, 'v') : n_L0;
	n_L0 = n_L;
	n_L = MAX(1, ls);
}

static void tr_pl(char **args)
{
	int n = eval_re(args[1] ? args[1] : "11i", n_p, 'v');
	n_p = MAX(0, n);
}

static void tr_nr(char **args)
{
	int id;
	if (!args[2])
		return;
	id = REG(args[1][0], args[1][1]);
	num_set(id, eval_re(args[2], num_get(id, 0), 'u'));
	num_inc(id, args[3] ? eval(args[3], 'u') : 0);
}

static void tr_rr(char **args)
{
	int i;
	for (i = 1; i <= NARGS; i++)
		if (args[i])
			num_del(REG(args[i][0], args[i][1]));
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
	int i;
	for (i = 1; i <= NARGS; i++)
		if (args[i])
			str_rm(REG(args[i][0], args[i][1]));
}

static void tr_rn(char **args)
{
	if (!args[2])
		return;
	str_rn(REG(args[1][0], args[1][1]), REG(args[2][0], args[2][1]));
}

static void tr_po(char **args)
{
	int po = args[1] ? eval_re(args[1], n_o, 'm') : n_o0;
	n_o0 = n_o;
	n_o = MAX(0, po);
}

static char *arg_regname(char *s, int len);

static void macrobody(struct sbuf *sbuf, char *end)
{
	char buf[4];
	int i, c;
	int first = 1;
	cp_back('\n');
	cp_wid(0);		/* copy-mode; disable \w handling */
	while ((c = cp_next()) >= 0) {
		if (sbuf && !first)
			sbuf_add(sbuf, c);
		first = 0;
		if (c == '\n') {
			c = cp_next();
			if (c == '.') {
				arg_regname(buf, 4);
				if (buf[0] == end[0] && buf[1] == end[1]) {
					jmp_eol();
					break;
				}
				if (!sbuf)
					continue;
				sbuf_add(sbuf, '.');
				for (i = 0; buf[i]; i++)
					sbuf_add(sbuf, (unsigned char) buf[i]);
				continue;
			}
			if (sbuf && c >= 0)
				sbuf_add(sbuf, c);
		}
	}
	cp_wid(1);
}

static void tr_de(char **args)
{
	struct sbuf sbuf;
	int id;
	if (!args[1])
		return;
	id = REG(args[1][0], args[1][1]);
	sbuf_init(&sbuf);
	if (args[0][1] == 'a' && args[0][2] == 'm' && str_get(id))
		sbuf_append(&sbuf, str_get(id));
	macrobody(&sbuf, args[2] ? args[2] : ".");
	str_set(id, sbuf_buf(&sbuf));
	sbuf_done(&sbuf);
}

static void tr_ig(char **args)
{
	macrobody(NULL, args[1] ? args[1] : ".");
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
		ret = eval(s, '\0') > 0;
	sbuf_done(&sbuf);
	return ret;
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
	cp_blk(ret == neg);
}

static void tr_el(char **args)
{
	cp_blk(ie_depth > 0 ? ie_cond[--ie_depth] : 1);
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
	int c = cp_next();
	while (c == ' ' || c == '\t')
		c = cp_next();
	while (s < e && c >= 0 && c != ' ' && c != '\t' && c != '\n') {
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
	c = cp_next();
	while (c == ' ')
		c = cp_next();
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

/* read macro arguments; trims tabs if rmtabs is nonzero */
static int mkargs(char **args, char *buf, int len)
{
	char *s = buf;
	char *e = buf + len - 1;
	int c;
	int n = 0;
	while (n < NARGS) {
		char *r = s;
		c = cp_next();
		if (c < 0 || c == '\n')
			return n;
		cp_back(c);
		s = arg_normal(s, e - s);
		if (*r != '\0')
			args[n++] = r;
	}
	jmp_eol();
	return n;
}

/* read request arguments; trims tabs too */
static int mkargs_req(char **args, char *buf, int len)
{
	char *r, *s = buf;
	char *e = buf + len - 1;
	int c;
	int n = 0;
	c = cp_next();
	while (n < NARGS && s < e) {
		r = s;
		while (c == ' ' || c == '\t')
			c = cp_next();
		while (c >= 0 && c != '\n' && c != ' ' && c != '\t' && s < e) {
			*s++ = c;
			c = cp_next();
		}
		*s++ = '\0';
		if (*r != '\0')
			args[n++] = r;
		if (c < 0 || c == '\n')
			return n;
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
	return mkargs_req(args + 1, s, e - s) + 1;
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
	{"ig", tr_ig},
	{"in", tr_in},
	{"ll", tr_ll},
	{"ls", tr_ls},
	{"mk", tr_mk},
	{"na", tr_na},
	{"ne", tr_ne},
	{"nf", tr_nf},
	{"nr", tr_nr, mkargs_reg1},
	{"ns", tr_ns},
	{"os", tr_os},
	{"pl", tr_pl},
	{"pn", tr_pn},
	{"po", tr_po},
	{"ps", tr_ps},
	{"rm", tr_rm},
	{"rn", tr_rn},
	{"rr", tr_rr},
	{"rs", tr_rs},
	{"rt", tr_rt},
	{"so", tr_so},
	{"sp", tr_sp},
	{"sv", tr_sv},
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
				mkargs_req(args + 1, buf, sizeof(buf));
			req->f(args);
		} else {
			cp_wid(0);
			mkargs(args + 1, buf, sizeof(buf));
			cp_wid(1);
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

void tr_first(void)
{
	cp_back(tr_next());
}
