/* built-in troff requests */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static int tr_nl = 1;		/* just read a newline */
static int c_pc = '%';		/* page number character */
int c_ec = '\\';
int c_cc = '.';
int c_c2 = '\'';

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
	int ls = args[1] ? eval_re(args[1], n_L, 0) : n_L0;
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
	id = map(args[1]);
	num_set(id, eval_re(args[2], num_get(id, 0), 'u'));
	num_inc(id, args[3] ? eval(args[3], 'u') : 0);
}

static void tr_rr(char **args)
{
	int i;
	for (i = 1; i <= NARGS; i++)
		if (args[i])
			num_del(map(args[i]));
}

static void tr_af(char **args)
{
	if (args[2])
		num_setfmt(map(args[1]), args[2]);
}

static void tr_ds(char **args)
{
	if (args[2])
		str_set(map(args[1]), args[2]);
}

static void tr_as(char **args)
{
	int reg;
	char *s1, *s2, *s;
	if (!args[2])
		return;
	reg = map(args[1]);
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
			str_rm(map(args[i]));
}

static void tr_rn(char **args)
{
	if (!args[2])
		return;
	str_rn(map(args[1]), map(args[2]));
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
	char buf[NMLEN];
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
				arg_regname(buf, sizeof(buf));
				if ((n_cp && end[0] == buf[0] && end[1] == buf[1]) ||
							!strcmp(end, buf)) {
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
	id = map(args[1]);
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

/* read into sbuf until stop; if stop is NULL, stop at whitespace */
static int read_until(struct sbuf *sbuf, char *stop)
{
	char cs[GNLEN], cs2[GNLEN];
	int c;
	while ((c = cp_next()) >= 0) {
		cp_back(c);
		if (c == '\n')
			return 1;
		if (!stop && (c == ' ' || c == '\t'))
			return 0;
		charnext(cs, cp_next, cp_back);
		if (stop && !strcmp(stop, cs))
			return 0;
		charnext_str(cs2, cs);
		sbuf_append(sbuf, cs2);
	}
	return 1;
}

/* evaluate .if strcmp (i.e. 'str'str') */
static int if_strcmp(void)
{
	char delim[GNLEN];
	struct sbuf s1, s2;
	int ret;
	charnext(delim, cp_next, cp_back);
	sbuf_init(&s1);
	sbuf_init(&s2);
	read_until(&s1, delim);
	read_until(&s2, delim);
	ret = !strcmp(sbuf_buf(&s1), sbuf_buf(&s2));
	sbuf_done(&s1);
	sbuf_done(&s2);
	return ret;
}

/* evaluate .if condition letters */
static int if_cond(void)
{
	switch (cp_next()) {
	case 'o':
		return n_pg % 2;
	case 'e':
		return !(n_pg % 2);
	case 't':
		return 1;
	case 'n':
		return 0;
	}
	return 0;
}

/* evaluate .if condition */
static int if_eval(void)
{
	struct sbuf sbuf;
	int ret;
	sbuf_init(&sbuf);
	if (!read_until(&sbuf, NULL))
		cp_back(' ');
	ret = eval(sbuf_buf(&sbuf), '\0') > 0;
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
	cp_back(c);
	if (strchr("oetn", c)) {
		ret = if_cond();
	} else if (!isdigit(c) && !strchr("-+*/%<=>&:.|()", c)) {
		ret = if_strcmp();
	} else {
		ret = if_eval();
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
		in_so(args[1]);
}

static void tr_nx(char **args)
{
	in_nx(args[1]);
}

static void tr_ex(char **args)
{
	in_ex();
}

static void tr_sy(char **args)
{
	system(args[1]);
}

static void tr_lt(char **args)
{
	int lt = args[1] ? eval_re(args[1], n_lt, 'm') : n_t0;
	n_t0 = n_t0;
	n_lt = MAX(0, lt);
}

static void tr_pc(char **args)
{
	c_pc = args[1] ? args[1][0] : -1;
}

static int tl_next(void)
{
	int c = cp_next();
	if (c >= 0 && c == c_pc) {
		in_push(num_str(map("%")), NULL);
		c = cp_next();
	}
	return c;
}

static void tr_tl(char **args)
{
	int c;
	do {
		c = cp_next();
	} while (c >= 0 && (c == ' ' || c == '\t'));
	cp_back(c);
	ren_tl(tl_next, cp_back);
	do {
		c = cp_next();
	} while (c >= 0 && c != '\n');
}

static void tr_ec(char **args)
{
	c_ec = args[1] ? args[1][0] : '\\';
}

static void tr_cc(char **args)
{
	c_ec = args[1] ? args[1][0] : '.';
}

static void tr_c2(char **args)
{
	c_ec = args[1] ? args[1][0] : '\'';
}

static void tr_eo(char **args)
{
	c_ec = -1;
}

static void tr_hc(char **args)
{
	char *s = args[1];
	if (!s || charread(&s, c_hc) < 0)
		strcpy(c_hc, "\\%");
}

static void tr_nh(char **args)
{
	n_hy = 0;
}

static void tr_hy(char **args)
{
	n_hy = args[1] ? atoi(args[1]) : 1;
}

static void tr_lg(char **args)
{
	if (args[1])
		n_lg = atoi(args[1]);
}

static void tr_kn(char **args)
{
	if (args[1])
		n_kn = atoi(args[1]);
}

static void tr_cp(char **args)
{
	if (args[1])
		n_cp = atoi(args[1]);
}

static void tr_ss(char **args)
{
	if (args[1])
		n_ss = eval_re(args[1], n_ss, 0);
}

static void tr_cs(char **args)
{
	if (!args[1])
		return;
	dev_setcs(dev_pos(args[1]), args[2] ? eval(args[2], 0) : 0);
}

static void tr_nm(char **args)
{
	if (!args[1]) {
		n_nm = 0;
		return;
	}
	n_nm = 1;
	n_ln = eval_re(args[1], n_ln, 0);
	n_ln = MAX(0, n_ln);
	if (args[2] && isdigit(args[2][0]))
		n_nM = MAX(1, eval(args[2], 0));
	if (args[3] && isdigit(args[3][0]))
		n_nS = MAX(0, eval(args[3], 0));
	if (args[4] && isdigit(args[4][0]))
		n_nI = MAX(0, eval(args[4], 0));
}

static void tr_nn(char **args)
{
	n_nn = args[1] ? eval(args[1], 0) : 1;
}

static void tr_bd(char **args)
{
	if (!args[1] || !strcmp("S", args[1]))
		return;
	dev_setbd(dev_pos(args[1]), args[2] ? eval(args[2], 'u') : 0);
}

static void tr_it(char **args)
{
	if (args[2]) {
		n_it = map(args[2]);
		n_itn = eval(args[1], 0);
	} else {
		n_it = 0;
	}
}

static void tr_mc(char **args)
{
	char *s = args[1];
	if (s && charread(&s, c_mc) >= 0) {
		n_mc = 1;
		n_mcn = args[2] ? eval(args[2], 'm') : SC_EM;
	} else {
		n_mc = 0;
	}
}

static void tr_tc(char **args)
{
	char *s = args[1];
	if (!s || charread(&s, c_tc) < 0)
		strcpy(c_tc, "");
}

static void tr_lc(char **args)
{
	char *s = args[1];
	if (!s || charread(&s, c_lc) < 0)
		strcpy(c_lc, "");
}

static void tr_lf(char **args)
{
	if (args[1])
		in_lf(args[2], eval(args[1], 0));
}

static void tr_chop(char **args)
{
	struct sbuf sbuf;
	int id;
	if (args[1])
		in_lf(args[2], eval(args[1], 0));
	id = map(args[1]);
	if (str_get(id)) {
		sbuf_init(&sbuf);
		sbuf_append(&sbuf, str_get(id));
		if (!sbuf_empty(&sbuf)) {
			sbuf_cut(&sbuf, sbuf_len(&sbuf) - 1);
			str_set(id, sbuf_buf(&sbuf));
		}
		sbuf_done(&sbuf);
	}
}

/* character translation (.tr) */
static char cmap_src[NCMAPS][GNLEN];	/* source character */
static char cmap_dst[NCMAPS][GNLEN];	/* character mapping */
static int cmap_n;			/* number of translated character */

static int tr_find(char *c)
{
	int i;
	for (i = 0; i < cmap_n; i++)
		if (!strcmp(c, cmap_src[i]))
			return i;
	return -1;
}

void cmap_add(char *c1, char *c2)
{
	int i = tr_find(c1);
	if (i < 0 && cmap_n < NCMAPS)
		i = cmap_n++;
	if (i >= 0) {
		strcpy(cmap_src[i], c1);
		strcpy(cmap_dst[i], c2);
	}
}

char *cmap_map(char *c)
{
	int i = tr_find(c);
	return i >= 0 ? cmap_dst[i] : c;
}

static void tr_tr(char **args)
{
	char *s = args[1];
	char c1[GNLEN], c2[GNLEN];
	while (s && charread(&s, c1) >= 0) {
		if (charread(&s, c2) < 0)
			strcpy(c2, " ");
		cmap_add(c1, c2);
	}
}

/* character definition (.char) */
static char cdef_src[NCDEFS][GNLEN];	/* source character */
static char *cdef_dst[NCDEFS];		/* character definition */
static int cdef_fn[NCDEFS];		/* owning font */
static int cdef_n;			/* number of defined characters */
static int cdef_expanding;		/* inside cdef_expand() call */

static int cdef_find(char *c, int fn)
{
	int i;
	for (i = 0; i < cdef_n; i++)
		if (!strcmp(cdef_src[i], c) && (!cdef_fn[i] || cdef_fn[i] == fn))
			return i;
	return -1;
}

/* return the definition of the given character */
char *cdef_map(char *c, int fn)
{
	int i = cdef_find(c, fn);
	return !cdef_expanding && i >= 0 ? cdef_dst[i] : NULL;
}

int cdef_expand(struct wb *wb, char *s, int fn)
{
	char *d = cdef_map(s, fn);
	if (!d)
		return 1;
	cdef_expanding = 1;
	ren_parse(wb, d);
	cdef_expanding = 0;
	return 0;
}

static void cdef_add(char *fn, char *cs, char *def)
{
	char c[GNLEN];
	int i;
	if (!def || charread(&cs, c) < 0)
		return;
	i = cdef_find(c, -1);
	if (i < 0 && cdef_n < NCDEFS)
		i = cdef_n++;
	if (i >= 0) {
		strncpy(cdef_src[i], c, sizeof(cdef_src[i]) - 1);
		cdef_dst[i] = malloc(strlen(def) + 1);
		strcpy(cdef_dst[i], def);
		cdef_fn[i] = fn ? dev_pos(fn) : 0;
	}
}

static void cdef_remove(char *cs)
{
	char c[GNLEN];
	int i;
	if (!cs || charread(&cs, c) < 0)
		return;
	for (i = 0; i < cdef_n; i++) {
		if (!strcmp(cdef_src[i], c)) {
			free(cdef_dst[i]);
			cdef_dst[i] = NULL;
			cdef_src[i][0] = '\0';
		}
	}
}

static void tr_char(char **args)
{
	cdef_add(NULL, args[1], args[2]);
}

static void tr_rchar(char **args)
{
	int i;
	for (i = 1; i <= NARGS; i++)
		if (args[i])
			cdef_remove(args[i]);
}

static void tr_ochar(char **args)
{
	cdef_add(args[1], args[2], args[3]);
}

static void tr_fmap(char **args)
{
	struct font *fn;
	if (!args[2])
		return;
	fn = dev_font(dev_pos(args[1]));
	if (fn)
		font_map(fn, args[2], args[3] ? font_glyph(fn, args[3]) : NULL);
}

static char *arg_regname(char *s, int len)
{
	char *e = n_cp ? s + 2 : s + len;
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
	cp_wid(0);
	s = arg_string(s, e - s);
	cp_wid(1);
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
	{TR_DIVBEG, tr_divbeg},
	{TR_DIVEND, tr_divend},
	{TR_EJECT, tr_eject},
	{"ab", tr_ab, mkargs_eol},
	{"ad", tr_ad},
	{"af", tr_af},
	{"am", tr_de, mkargs_reg1},
	{"as", tr_as, mkargs_ds},
	{"bd", tr_bd},
	{"bp", tr_bp},
	{"br", tr_br},
	{"c2", tr_c2},
	{"cc", tr_cc},
	{"ochar", tr_ochar},
	{"ce", tr_ce},
	{"ch", tr_ch},
	{"char", tr_char, mkargs_ds},
	{"chop", tr_chop, mkargs_reg1},
	{"cl", tr_cl},
	{"cp", tr_cp},
	{"cs", tr_cs},
	{"da", tr_di},
	{"de", tr_de, mkargs_reg1},
	{"di", tr_di},
	{"ds", tr_ds, mkargs_ds},
	{"dt", tr_dt},
	{"ec", tr_ec},
	{"el", tr_el, mkargs_null},
	{"em", tr_em},
	{"eo", tr_eo},
	{"ev", tr_ev},
	{"ex", tr_ex},
	{"fc", tr_fc},
	{"fi", tr_fi},
	{"fmap", tr_fmap},
	{"fp", tr_fp},
	{"fspecial", tr_fspecial},
	{"ft", tr_ft},
	{"hc", tr_hc},
	{"hy", tr_hy},
	{"hw", tr_hw},
	{"ie", tr_if, mkargs_null},
	{"if", tr_if, mkargs_null},
	{"ig", tr_ig},
	{"in", tr_in},
	{"it", tr_it},
	{"kn", tr_kn},
	{"lc", tr_lc},
	{"lf", tr_lf},
	{"lg", tr_lg},
	{"ll", tr_ll},
	{"ls", tr_ls},
	{"lt", tr_lt},
	{"mc", tr_mc},
	{"mk", tr_mk},
	{"na", tr_na},
	{"ne", tr_ne},
	{"nf", tr_nf},
	{"nh", tr_nh},
	{"nm", tr_nm},
	{"nn", tr_nn},
	{"nr", tr_nr, mkargs_reg1},
	{"ns", tr_ns},
	{"nx", tr_nx},
	{"os", tr_os},
	{"pc", tr_pc},
	{"pl", tr_pl},
	{"pn", tr_pn},
	{"po", tr_po},
	{"ps", tr_ps},
	{"rchar", tr_rchar, mkargs_ds},
	{"rm", tr_rm},
	{"rn", tr_rn},
	{"rr", tr_rr},
	{"rs", tr_rs},
	{"rt", tr_rt},
	{"so", tr_so},
	{"sp", tr_sp},
	{"ss", tr_ss},
	{"sv", tr_sv},
	{"sy", tr_sy, mkargs_eol},
	{"ta", tr_ta},
	{"tc", tr_tc},
	{"ti", tr_ti},
	{"tl", tr_tl, mkargs_null},
	{"tm", tr_tm, mkargs_eol},
	{"tr", tr_tr, mkargs_eol},
	{"vs", tr_vs},
	{"wh", tr_wh},
};

int tr_next(void)
{
	int c = cp_next();
	int nl = c == '\n';
	char *args[NARGS + 3] = {NULL};
	char cmd[RNLEN];
	char buf[LNLEN];
	struct cmd *req;
	while (tr_nl && c >= 0 && (c == c_cc || c == c_c2)) {
		nl = 1;
		memset(args, 0, sizeof(args));
		args[0] = cmd;
		cmd[0] = c;
		req = NULL;
		arg_regname(cmd + 1, sizeof(cmd) - 1);
		req = str_dget(map(cmd + 1));
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
			if (str_get(map(cmd + 1)))
				in_push(str_get(map(cmd + 1)), args + 1);
		}
		c = cp_next();
		nl = c == '\n';
	}
	tr_nl = c < 0 || nl;
	return c;
}

void tr_init(void)
{
	int i;
	for (i = 0; i < LEN(cmds); i++)
		str_dset(map(cmds[i].id), &cmds[i]);
}

void tr_first(void)
{
	cp_back(tr_next());
	tr_nl = 1;
}
