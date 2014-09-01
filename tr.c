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
	num_set(id, eval_re(args[2], num_get(id), 'u'));
	num_setinc(id, args[3] ? eval(args[3], 'u') : 0);
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
	s = xmalloc(strlen(s1) + strlen(s2) + 1);
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

static void read_regname(char *s)
{
	int c = cp_next();
	int n = n_cp ? 2 : NMLEN - 1;
	while (c == ' ' || c == '\t')
		c = cp_next();
	while (c >= 0 && c != ' ' && c != '\t' && c != '\n' && --n >= 0) {
		*s++ = c;
		c = cp_next();
	}
	if (c >= 0)
		cp_back(c);
	*s = '\0';
}

static void macrobody(struct sbuf *sbuf, char *end)
{
	char buf[NMLEN];
	int i, c;
	int first = 1;
	cp_back('\n');
	cp_copymode(1);
	while ((c = cp_next()) >= 0) {
		if (sbuf && !first)
			sbuf_add(sbuf, c);
		first = 0;
		if (c == '\n') {
			if ((c = cp_next()) != '.') {
				cp_back(c);
				continue;
			}
			read_regname(buf);
			if ((n_cp && end[0] == buf[0] && end[1] == buf[1]) ||
						!strcmp(end, buf)) {
				jmp_eol();
				break;
			}
			if (sbuf) {
				sbuf_add(sbuf, '.');
				for (i = 0; buf[i]; i++)
					sbuf_add(sbuf, (unsigned char) buf[i]);
			}
		}
	}
	cp_copymode(0);
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
static int read_until(struct sbuf *sbuf, char *stop,
			int (*next)(void), void (*back)(int))
{
	char cs[GNLEN], cs2[GNLEN];
	int c;
	while ((c = next()) >= 0) {
		back(c);
		if (c == '\n')
			return 1;
		if (!stop && (c == ' ' || c == '\t'))
			return 0;
		charnext(cs, next, back);
		if (stop && !strcmp(stop, cs))
			return 0;
		charnext_str(cs2, cs);
		sbuf_append(sbuf, cs2);
	}
	return 1;
}

/* evaluate .if strcmp (i.e. 'str'str') */
static int if_strcmp(int (*next)(void), void (*back)(int))
{
	char delim[GNLEN];
	struct sbuf s1, s2;
	int ret;
	charnext(delim, next, back);
	sbuf_init(&s1);
	sbuf_init(&s2);
	read_until(&s1, delim, next, back);
	read_until(&s2, delim, next, back);
	ret = !strcmp(sbuf_buf(&s1), sbuf_buf(&s2));
	sbuf_done(&s1);
	sbuf_done(&s2);
	return ret;
}

/* evaluate .if condition letters */
static int if_cond(int (*next)(void), void (*back)(int))
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
static int if_eval(int (*next)(void), void (*back)(int))
{
	struct sbuf sbuf;
	int ret;
	sbuf_init(&sbuf);
	read_until(&sbuf, NULL, next, back);
	ret = eval(sbuf_buf(&sbuf), '\0') > 0;
	sbuf_done(&sbuf);
	return ret;
}

static int eval_if(int (*next)(void), void (*back)(int))
{
	int neg = 0;
	int ret;
	int c;
	do {
		c = next();
	} while (c == ' ' || c == '\t');
	if (c == '!') {
		neg = 1;
		c = next();
	}
	back(c);
	if (strchr("oetn", c)) {
		ret = if_cond(next, back);
	} else if (!isdigit(c) && !strchr("-+*/%<=>&:.|()", c)) {
		ret = if_strcmp(next, back);
	} else {
		ret = if_eval(next, back);
	}
	return ret != neg;
}

static int ie_cond[NIES];	/* .ie condition stack */
static int ie_depth;

static void tr_if(char **args)
{
	int c = eval_if(cp_next, cp_back);
	if (args[0][1] == 'i' && args[0][2] == 'e')	/* .ie command */
		if (ie_depth < NIES)
			ie_cond[ie_depth++] = c;
	cp_blk(!c);
}

static void tr_el(char **args)
{
	cp_blk(ie_depth > 0 ? ie_cond[--ie_depth] : 1);
}

static void tr_na(char **args)
{
	n_na = 1;
}

static int adjmode(int c, int def)
{
	switch (c) {
	case 'l':
		return AD_L;
	case 'r':
		return AD_R;
	case 'c':
		return AD_C;
	case 'b':
	case 'n':
		return AD_B;
	}
	return def;
}

static void tr_ad(char **args)
{
	char *s = args[1];
	n_na = 0;
	if (!s)
		return;
	if (isdigit(s[0]))
		n_j = atoi(s) & 15;
	else
		n_j = s[0] == 'p' ? AD_P | adjmode(s[1], AD_B) : adjmode(s[0], n_j);
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

static void tr_hyp(char **args)
{
	n_hyp = args[1] ? atoi(args[1]) : 1;
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
	if (args[1]) {
		n_ss = eval_re(args[1], n_ss, 0);
		n_sss = args[2] ? eval_re(args[2], n_sss, 0) : n_ss;
	}
}

static void tr_ssh(char **args)
{
	n_ssh = args[1] ? eval_re(args[1], n_ssh, 0) : 0;
}

static void tr_cs(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	if (fn)
		font_setcs(fn, args[2] ? eval(args[2], 0) : 0,
				args[3] ? eval(args[3], 0) : 0);
}

static void tr_fzoom(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	if (fn)
		font_setzoom(fn, args[2] ? eval(args[2], 0) : 0);
}

static void tr_ff(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	int i;
	for (i = 2; i <= NARGS; i++)
		if (fn && args[i] && args[i][0] && args[i][1])
			font_feat(fn, args[i] + 1, args[i][0] == '+');
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
	font_setbd(dev_font(dev_pos(args[1])), args[2] ? eval(args[2], 'u') : 0);
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
static struct dict cmap;		/* character mapping */
static char cmap_src[NCMAPS][GNLEN];	/* source character */
static char cmap_dst[NCMAPS][GNLEN];	/* character mapping */
static int cmap_n;			/* number of translated character */

void cmap_add(char *c1, char *c2)
{
	int i = dict_get(&cmap, c1);
	if (i >= 0) {
		strcpy(cmap_dst[i], c2);
	} else if (cmap_n < NCMAPS) {
		strcpy(cmap_src[cmap_n], c1);
		strcpy(cmap_dst[cmap_n], c2);
		dict_put(&cmap, cmap_src[cmap_n], cmap_n);
		cmap_n++;
	}
}

char *cmap_map(char *c)
{
	int i = dict_get(&cmap, c);
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
		if ((!cdef_fn[i] || cdef_fn[i] == fn) && !strcmp(cdef_src[i], c))
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
		cdef_dst[i] = xmalloc(strlen(def) + 1);
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
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	if (fn && args[2])
		font_map(fn, args[2], args[3]);
}

static void arg_regname(struct sbuf *sbuf)
{
	char reg[NMLEN];
	read_regname(reg);
	sbuf_append(sbuf, reg);
	sbuf_add(sbuf, 0);
}

static void arg_string(struct sbuf *sbuf)
{
	int c;
	while ((c = cp_next()) == ' ')
		;
	if (c == '"')
		c = cp_next();
	while (c > 0 && c != '\n') {
		sbuf_add(sbuf, c);
		c = cp_next();
	}
	sbuf_add(sbuf, 0);
	if (c >= 0)
		cp_back(c);
}

static int mkargs_arg(struct sbuf *sbuf, int (*next)(void), void (*back)(int))
{
	int quoted = 0;
	int c;
	c = next();
	while (c == ' ')
		c = next();
	if (c == '\n')
		back(c);
	if (c < 0 || c == '\n')
		return 1;
	if (c == '"') {
		quoted = 1;
		c = next();
	}
	while (c >= 0 && c != '\n') {
		if (!quoted && c == ' ')
			break;
		if (quoted && c == '"') {
			c = next();
			if (c != '"')
				break;
		}
		if (c == c_ec) {
			sbuf_add(sbuf, c);
			c = next();
		}
		sbuf_add(sbuf, c);
		c = next();
	}
	sbuf_add(sbuf, 0);
	if (c >= 0)
		back(c);
	return 0;
}

/* read macro arguments */
int tr_readargs(char **args, struct sbuf *sbuf, int (*next)(void), void (*back)(int))
{
	int idx[NARGS];
	int i, n = 0;
	while (n < NARGS) {
		idx[n] = sbuf_len(sbuf);
		if (mkargs_arg(sbuf, next, back))
			break;
		n++;
	}
	for (i = 0; i < n; i++)
		args[i] = sbuf_buf(sbuf) + idx[i];
	return n;
}

/* read macro arguments; trims tabs if rmtabs is nonzero */
static int mkargs(char **args, struct sbuf *sbuf)
{
	int n = tr_readargs(args, sbuf, cp_next, cp_back);
	jmp_eol();
	return n;
}

/* read request arguments; trims tabs too */
static int mkargs_req(char **args, struct sbuf *sbuf)
{
	int idx[NARGS];
	int i, n = 0;
	int c;
	c = cp_next();
	while (n < NARGS) {
		idx[n] = sbuf_len(sbuf);
		while (c == ' ' || c == '\t')
			c = cp_next();
		while (c >= 0 && c != '\n' && c != ' ' && c != '\t') {
			sbuf_add(sbuf, c);
			c = cp_next();
		}
		if (sbuf_len(sbuf) > idx[n])
			n++;
		sbuf_add(sbuf, 0);
		if (c == '\n')
			cp_back(c);
		if (c < 0 || c == '\n')
			break;
	}
	for (i = 0; i < n; i++)
		args[i] = sbuf_buf(sbuf) + idx[i];
	jmp_eol();
	return n;
}

/* read arguments for .ds */
static int mkargs_ds(char **args, struct sbuf *sbuf)
{
	int idx[NARGS];
	int i, n = 0;
	idx[n++] = sbuf_len(sbuf);
	arg_regname(sbuf);
	idx[n++] = sbuf_len(sbuf);
	cp_copymode(1);
	arg_string(sbuf);
	cp_copymode(0);
	jmp_eol();
	for (i = 0; i < n; i++)
		args[i] = sbuf_buf(sbuf) + idx[i];
	return n;
}

/* read arguments for commands .nr that expect a register name */
static int mkargs_reg1(char **args, struct sbuf *sbuf)
{
	int n;
	int idx0 = sbuf_len(sbuf);
	arg_regname(sbuf);
	n = mkargs_req(args + 1, sbuf) + 1;
	args[0] = sbuf_buf(sbuf) + idx0;
	return n;
}

/* do not read arguments; for .if, .ie and .el */
static int mkargs_null(char **args, struct sbuf *sbuf)
{
	return 0;
}

/* read the whole line for .tm */
static int mkargs_eol(char **args, struct sbuf *sbuf)
{
	int idx0 = sbuf_len(sbuf);
	int c;
	c = cp_next();
	while (c == ' ')
		c = cp_next();
	while (c >= 0 && c != '\n') {
		sbuf_add(sbuf, c);
		c = cp_next();
	}
	args[0] = sbuf_buf(sbuf) + idx0;
	return 1;
}

static struct cmd {
	char *id;
	void (*f)(char **args);
	int (*args)(char **args, struct sbuf *sbuf);
} cmds[] = {
	{TR_DIVBEG, tr_divbeg},
	{TR_DIVEND, tr_divend},
	{TR_POPREN, tr_popren},
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
	{"ff", tr_ff},
	{"fi", tr_fi},
	{"fmap", tr_fmap},
	{"fp", tr_fp},
	{"fspecial", tr_fspecial},
	{"ft", tr_ft},
	{"fzoom", tr_fzoom},
	{"hc", tr_hc},
	{"hcode", tr_hcode},
	{"hpf", tr_hpf},
	{"hpfa", tr_hpfa},
	{"hy", tr_hy},
	{"hyp", tr_hyp},
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
	{"ssh", tr_ssh},
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

/* read the next troff request; return zero if a request was executed. */
int tr_nextreq(void)
{
	char *args[NARGS + 3] = {NULL};
	char cmd[RNLEN + 1];
	struct cmd *req;
	struct sbuf sbuf;
	int c;
	if (!tr_nl)
		return 1;
	c = cp_next();
	if (c < 0 || (c != c_cc && c != c_c2)) {
		cp_back(c);
		return 1;
	}
	memset(args, 0, sizeof(args));
	args[0] = cmd;
	cmd[0] = c;
	req = NULL;
	cp_reqline();
	read_regname(cmd + 1);
	sbuf_init(&sbuf);
	req = str_dget(map(cmd + 1));
	if (req) {
		if (req->args)
			req->args(args + 1, &sbuf);
		else
			mkargs_req(args + 1, &sbuf);
		req->f(args);
	} else {
		cp_copymode(1);
		mkargs(args + 1, &sbuf);
		cp_copymode(0);
		if (str_get(map(cmd + 1)))
			in_push(str_get(map(cmd + 1)), args + 1);
	}
	sbuf_done(&sbuf);
	return 0;
}

int tr_next(void)
{
	int c;
	while (!tr_nextreq())
		;
	c = cp_next();
	tr_nl = c == '\n' || c < 0;
	return c;
}

void tr_init(void)
{
	int i;
	for (i = 0; i < LEN(cmds); i++)
		str_dset(map(cmds[i].id), &cmds[i]);
	dict_init(&cmap, NCMAPS, -1, 0, 0);
}
