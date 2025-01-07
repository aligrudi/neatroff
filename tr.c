/* built-in troff requests */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static int tr_nl = 1;		/* just read a newline */
static int tr_bm = -1;		/* blank line macro */
static int tr_sm = -1;		/* leading space macro */
char c_pc[GNLEN] = "%";		/* page number character */
int c_ec = '\\';		/* escape character */
int c_cc = '.';			/* control character */
int c_c2 = '\'';		/* no-break control character */

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
	if (args[3])
		num_setinc(id, eval(args[3], 'u'));
}

static void tr_rr(char **args)
{
	int i;
	for (i = 1; args[i]; i++)
		num_del(map(args[i]));
}

static void tr_af(char **args)
{
	if (args[2])
		num_setfmt(map(args[1]), args[2]);
}

static void tr_ds(char **args)
{
	str_set(map(args[1]), args[2] ? args[2] : "");
}

static void tr_as(char **args)
{
	int reg;
	char *s1, *s2, *s;
	reg = map(args[1]);
	s1 = str_get(reg) ? str_get(reg) : "";
	s2 = args[2] ? args[2] : "";
	s = xmalloc(strlen(s1) + strlen(s2) + 1);
	strcpy(s, s1);
	strcat(s, s2);
	str_set(reg, s);
	free(s);
}

static void tr_rm(char **args)
{
	int i;
	for (i = 1; args[i]; i++)
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

/* read a string argument of a macro */
static char *read_string(void)
{
	struct sbuf sbuf;
	int c;
	int empty;
	sbuf_init(&sbuf);
	cp_copymode(1);
	while ((c = cp_next()) == ' ')
		;
	empty = c <= 0 || c == '\n';
	if (c == '"')
		c = cp_next();
	while (c > 0 && c != '\n') {
		if (c != c_ni)
			sbuf_add(&sbuf, c);
		c = cp_next();
	}
	if (c >= 0)
		cp_back(c);
	cp_copymode(0);
	if (empty) {
		sbuf_done(&sbuf);
		return NULL;
	}
	return sbuf_out(&sbuf);
}

/* read a space separated macro argument; if two, read at most two characters */
static char *read_name(int two)
{
	struct sbuf sbuf;
	int c = cp_next();
	int i = 0;
	sbuf_init(&sbuf);
	while (c == ' ' || c == '\t' || c == c_ni)
		c = cp_next();
	while (c > 0 && c != ' ' && c != '\t' && c != '\n' && (!two || i < 2)) {
		if (c != c_ni) {
			sbuf_add(&sbuf, c);
			i++;
		}
		c = cp_next();
	}
	if (c >= 0)
		cp_back(c);
	return sbuf_out(&sbuf);
}

static void macrobody(struct sbuf *sbuf, char *end)
{
	int first = 1;
	int c;
	char *req = NULL;
	cp_back('\n');
	cp_copymode(1);
	while ((c = cp_next()) >= 0) {
		if (sbuf && !first)
			sbuf_add(sbuf, c);
		first = 0;
		if (c == '\n') {
			if ((c = cp_next()) != c_cc) {
				cp_back(c);
				continue;
			}
			req = read_name(n_cp);
			if (!strcmp(end, req)) {
				in_push(end, NULL);
				cp_back(c_cc);
				break;
			}
			if (sbuf) {
				sbuf_add(sbuf, c_cc);
				sbuf_append(sbuf, req);
			}
			free(req);
			req = NULL;
		}
	}
	free(req);
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
	if (!n_cp && args[3])	/* parse the arguments as request argv[3] */
		str_dset(id, str_dget(map(args[3])));
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
		if (c == c_ni)
			continue;
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
	cp_reqbeg();
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
	} else if (c == ' ') {
		ret = 0;
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
	case 'k':
		return AD_B | AD_K;
	}
	return def;
}

static void tr_ad(char **args)
{
	char *s = args[1];
	n_na = 0;
	if (!s)
		return;
	if (isdigit((unsigned char) s[0]))
		n_j = atoi(s) & 15;
	else
		n_j = s[0] == 'p' ? AD_P | adjmode(s[1], AD_B) : adjmode(s[0], n_j);
}

static void tr_tm(char **args)
{
	fprintf(stderr, "%s\n", args[1] ? args[1] : "");
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

static void tr_shift(char **args)
{
	int n = args[1] ? atoi(args[1]) : 1;
	while (n-- >= 1)
		in_shift();
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
	char *s = args[1];
	if (!s || charread(&s, c_pc) < 0)
		strcpy(c_pc, "");
}

static void tr_tl(char **args)
{
	int c;
	do {
		c = cp_next();
	} while (c >= 0 && (c == ' ' || c == '\t'));
	cp_back(c);
	ren_tl(cp_next, cp_back);
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
	c_cc = args[1] ? args[1][0] : '.';
}

static void tr_c2(char **args)
{
	c_c2 = args[1] ? args[1][0] : '\'';
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

/* sentence ending and their transparent characters */
static char eos_sent[NCHARS][GNLEN] = { ".", "?", "!", };
static int eos_sentcnt = 3;
static char eos_tran[NCHARS][GNLEN] = { "'", "\"", ")", "]", "*", };
static int eos_trancnt = 5;

static void tr_eos(char **args)
{
	eos_sentcnt = 0;
	eos_trancnt = 0;
	if (args[1]) {
		char *s = args[1];
		while (s && charread(&s, eos_sent[eos_sentcnt]) >= 0)
			if (eos_sentcnt < NCHARS - 1)
				eos_sentcnt++;
	}
	if (args[2]) {
		char *s = args[2];
		while (s && charread(&s, eos_tran[eos_trancnt]) >= 0)
			if (eos_trancnt < NCHARS - 1)
				eos_trancnt++;
	}
}

int c_eossent(char *s)
{
	int i;
	for (i = 0; i < eos_sentcnt; i++)
		if (!strcmp(eos_sent[i], s))
			return 1;
	return 0;
}

int c_eostran(char *s)
{
	int i;
	for (i = 0; i < eos_trancnt; i++)
		if (!strcmp(eos_tran[i], s))
			return 1;
	return 0;
}

/* hyphenation dashes and hyphenation inhibiting character */
static char hy_dash[NCHARS][GNLEN] = { "\\:", "-", "em", "en", "\\-", "--", "hy", };
static int hy_dashcnt = 7;
static char hy_stop[NCHARS][GNLEN] = { "\\%", };
static int hy_stopcnt = 1;

static void tr_nh(char **args)
{
	n_hy = 0;
}

static void tr_hy(char **args)
{
	n_hy = args[1] ? eval_re(args[1], n_hy, '\0') : 1;
}

static void tr_hlm(char **args)
{
	n_hlm = args[1] ? eval_re(args[1], n_hlm, '\0') : 0;
}

static void tr_hycost(char **args)
{
	n_hycost = args[1] ? eval_re(args[1], n_hycost, '\0') : 0;
	n_hycost2 = args[2] ? eval_re(args[2], n_hycost2, '\0') : 0;
	n_hycost3 = args[3] ? eval_re(args[3], n_hycost3, '\0') : 0;
}

static void tr_hydash(char **args)
{
	hy_dashcnt = 0;
	if (args[1]) {
		char *s = args[1];
		while (s && charread(&s, hy_dash[hy_dashcnt]) >= 0)
			if (hy_dashcnt < NCHARS - 1)
				hy_dashcnt++;
	}
}

static void tr_hystop(char **args)
{
	hy_stopcnt = 0;
	if (args[1]) {
		char *s = args[1];
		while (s && charread(&s, hy_stop[hy_stopcnt]) >= 0)
			if (hy_stopcnt < NCHARS - 1)
				hy_stopcnt++;
	}
}

int c_hydash(char *s)
{
	int i;
	for (i = 0; i < hy_dashcnt; i++)
		if (!strcmp(hy_dash[i], s))
			return 1;
	return 0;
}

int c_hystop(char *s)
{
	int i;
	for (i = 0; i < hy_stopcnt; i++)
		if (!strcmp(hy_stop[i], s))
			return 1;
	return 0;
}

int c_hymark(char *s)
{
	return !strcmp(c_bp, s) || !strcmp(c_hc, s);
}

static void tr_pmll(char **args)
{
	n_pmll = args[1] ? eval_re(args[1], n_pmll, '\0') : 0;
	n_pmllcost = args[2] ? eval_re(args[2], n_pmllcost, '\0') : 100;
}

static void tr_lg(char **args)
{
	if (args[1])
		n_lg = eval(args[1], '\0');
}

static void tr_kn(char **args)
{
	if (args[1])
		n_kn = eval(args[1], '\0');
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

static void tr_tkf(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	if (fn && args[5])
		font_track(fn, eval(args[2], 0), eval(args[3], 0),
				eval(args[4], 0), eval(args[5], 0));
}

static void tr_ff(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	int i;
	for (i = 2; args[i]; i++)
		if (fn && args[i][0] && args[i][1])
			font_feat(fn, args[i] + 1, args[i][0] == '+');
}

static void tr_ffsc(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	if (fn)
		font_scrp(fn, args[2]);
	if (fn)
		font_lang(fn, args[3]);
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
	if (args[2] && isdigit((unsigned char) args[2][0]))
		n_nM = MAX(1, eval(args[2], 0));
	if (args[3] && isdigit((unsigned char) args[3][0]))
		n_nS = MAX(0, eval(args[3], 0));
	if (args[4] && isdigit((unsigned char) args[4][0]))
		n_nI = MAX(0, eval(args[4], 0));
}

static void tr_nn(char **args)
{
	n_nn = args[1] ? eval(args[1], 0) : 1;
}

static void tr_bd(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	if (!args[1] || !strcmp("S", args[1]))
		return;
	if (fn)
		font_setbd(fn, args[2] ? eval(args[2], 'u') : 0);
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
static struct dict *cmap;		/* character mapping */
static char cmap_src[NCMAPS][GNLEN];	/* source character */
static char cmap_dst[NCMAPS][GNLEN];	/* character mapping */
static int cmap_n;			/* number of translated character */

void cmap_add(char *c1, char *c2)
{
	int i = dict_get(cmap, c1);
	if (i >= 0) {
		strcpy(cmap_dst[i], c2);
	} else if (cmap_n < NCMAPS) {
		strcpy(cmap_src[cmap_n], c1);
		strcpy(cmap_dst[cmap_n], c2);
		dict_put(cmap, cmap_src[cmap_n], cmap_n);
		cmap_n++;
	}
}

char *cmap_map(char *c)
{
	int i = dict_get(cmap, c);
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

static void cdef_remove(char *fn, char *cs)
{
	char c[GNLEN];
	int i;
	int fp = fn ? dev_pos(fn) : -1;
	if (!cs || charread(&cs, c) < 0)
		return;
	for (i = 0; i < cdef_n; i++) {
		if (!strcmp(cdef_src[i], c)) {
			if (!fn || (fp > 0 && cdef_fn[i] == fp)) {
				free(cdef_dst[i]);
				cdef_dst[i] = NULL;
				cdef_src[i][0] = '\0';
			}
		}
	}
}

static void cdef_add(char *fn, char *cs, char *def)
{
	char c[GNLEN];
	int i;
	if (!def || charread(&cs, c) < 0)
		return;
	i = cdef_find(c, fn ? dev_pos(fn) : -1);
	if (i < 0) {
		for (i = 0; i < cdef_n; i++)
			if (!cdef_dst[i])
				break;
		if (i == cdef_n && cdef_n < NCDEFS)
			cdef_n++;
	}
	if (i >= 0 && i < cdef_n) {
		snprintf(cdef_src[i], sizeof(cdef_src[i]), "%s", c);
		cdef_dst[i] = xmalloc(strlen(def) + 1);
		strcpy(cdef_dst[i], def);
		cdef_fn[i] = fn ? dev_pos(fn) : 0;
	}
}

static void tr_rchar(char **args)
{
	int i;
	for (i = 1; args[i]; i++)
		cdef_remove(NULL, args[i]);
}

static void tr_char(char **args)
{
	if (args[2])
		cdef_add(NULL, args[1], args[2]);
	else
		cdef_remove(NULL, args[1]);
}

static void tr_ochar(char **args)
{
	if (args[3])
		cdef_add(args[1], args[2], args[3]);
	else
		cdef_remove(args[1], args[2]);
}

static void tr_fmap(char **args)
{
	struct font *fn = args[1] ? dev_font(dev_pos(args[1])) : NULL;
	if (fn && args[2])
		font_map(fn, args[2], args[3]);
}

static void tr_blm(char **args)
{
	tr_bm = args[1] ? map(args[1]) : -1;
}

static void tr_lsm(char **args)
{
	tr_sm = args[1] ? map(args[1]) : -1;
}

static void tr_co(char **args)
{
	char *src = args[1];
	char *dst = args[2];
	if (src && dst && str_get(map(src)))
		str_set(map(dst), str_get(map(src)));
}

static void tr_coa(char **args)
{
	char *src = args[1];
	char *dst = args[2];
	if (src && dst && str_get(map(src))) {
		struct sbuf sb;
		sbuf_init(&sb);
		if (str_get(map(dst)))
			sbuf_append(&sb, str_get(map(dst)));
		sbuf_append(&sb, str_get(map(src)));
		str_set(map(dst), sbuf_buf(&sb));
		sbuf_done(&sb);
	}
}

static void tr_coo(char **args)
{
	char *reg = args[1];
	char *path = args[2];
	FILE *fp;
	if (!reg || !reg[0] || !path || !path[0])
		return;
	if ((fp = fopen(path, "w"))) {
		if (str_get(map(reg)))
			fputs(str_get(map(reg)), fp);
		fclose(fp);
	}
}

static void tr_coi(char **args)
{
	char *reg = args[1];
	char *path = args[2];
	char buf[1024];
	FILE *fp;
	if (!reg || !reg[0] || !path || !path[0])
		return;
	if ((fp = fopen(path, "r"))) {
		struct sbuf sb;
		sbuf_init(&sb);
		while (fgets(buf, sizeof(buf), fp))
			sbuf_append(&sb, buf);
		str_set(map(reg), sbuf_buf(&sb));
		sbuf_done(&sb);
		fclose(fp);
	}
}

static void tr_dv(char **args)
{
	if (args[1])
		out_x(args[1]);
}

/* read a single macro argument */
static int macroarg(struct sbuf *sbuf, int brk, int (*next)(void), void (*back)(int))
{
	int quoted = 0;
	int c;
	c = next();
	while (c == ' ')
		c = next();
	if (c == '\n' || c == brk)
		back(c);
	if (c < 0 || c == '\n' || c == brk)
		return 1;
	if (c == '"') {
		quoted = 1;
		c = next();
	}
	while (c >= 0 && c != '\n' && (quoted || c != brk)) {
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

/* split the arguments in sbuf, after calling one of mkargs_*() */
static void chopargs(struct sbuf *sbuf, char **args)
{
	char *s = sbuf_buf(sbuf);
	char *e = s + sbuf_len(sbuf);
	int n = 0;
	while (n < NARGS && s && s < e) {
		args[n++] = s;
		if ((s = memchr(s, '\0', e - s)))
			s++;
	}
}

/* read macro arguments; free the returned pointer when done */
char *tr_args(char **args, int brk, int (*next)(void), void (*back)(int))
{
	struct sbuf sbuf;
	sbuf_init(&sbuf);
	while (!macroarg(&sbuf, brk, next, back))
		;
	chopargs(&sbuf, args);
	return sbuf_out(&sbuf);
}

/* read regular macro arguments */
static void mkargs_macro(struct sbuf *sbuf)
{
	cp_copymode(1);
	while (!macroarg(sbuf, -1, cp_next, cp_back))
		;
	jmp_eol();
	cp_copymode(0);
}

/* read request arguments; trims tabs too */
static void mkargs_req(struct sbuf *sbuf)
{
	int n = 0;
	int c;
	c = cp_next();
	while (n < NARGS) {
		int ok = 0;
		while (c == ' ' || c == '\t')
			c = cp_next();
		while (c >= 0 && c != '\n' && c != ' ' && c != '\t') {
			if (c != c_ni)
				sbuf_add(sbuf, c);
			c = cp_next();
			ok = 1;
		}
		if (ok) {
			n++;
			sbuf_add(sbuf, 0);
		}
		if (c == '\n')
			cp_back(c);
		if (c < 0 || c == '\n')
			break;
	}
	jmp_eol();
}

/* read arguments for .ds and .char */
static void mkargs_ds(struct sbuf *sbuf)
{
	char *s = read_name(n_cp);
	sbuf_append(sbuf, s);
	sbuf_add(sbuf, 0);
	free(s);
	s = read_string();
	if (s) {
		sbuf_append(sbuf, s);
		sbuf_add(sbuf, 0);
		free(s);
	}
	jmp_eol();
}

/* read arguments for .ochar */
static void mkargs_ochar(struct sbuf *sbuf)
{
	char *s = read_name(0);
	sbuf_append(sbuf, s);
	sbuf_add(sbuf, 0);
	free(s);
	mkargs_ds(sbuf);
}

/* read arguments for .nr */
static void mkargs_reg1(struct sbuf *sbuf)
{
	char *s = read_name(n_cp);
	sbuf_append(sbuf, s);
	sbuf_add(sbuf, 0);
	free(s);
	mkargs_req(sbuf);
}

/* do not read any arguments; for .if, .ie and .el */
static void mkargs_null(struct sbuf *sbuf)
{
}

/* read the whole line for .tm */
static void mkargs_eol(struct sbuf *sbuf)
{
	int c;
	cp_copymode(1);
	c = cp_next();
	while (c == ' ')
		c = cp_next();
	while (c >= 0 && c != '\n') {
		if (c != c_ni)
			sbuf_add(sbuf, c);
		c = cp_next();
	}
	cp_copymode(0);
}

static struct cmd {
	char *id;
	void (*f)(char **args);
	void (*args)(struct sbuf *sbuf);
} cmds[] = {
	{TR_DIVBEG, tr_divbeg},
	{TR_DIVEND, tr_divend},
	{TR_DIVVS, tr_divvs},
	{TR_POPREN, tr_popren},
	{">>", tr_l2r},
	{"<<", tr_r2l},
	{"ab", tr_ab, mkargs_eol},
	{"ad", tr_ad},
	{"af", tr_af},
	{"am", tr_de, mkargs_reg1},
	{"as", tr_as, mkargs_ds},
	{"bd", tr_bd},
	{"blm", tr_blm},
	{"bp", tr_bp},
	{"br", tr_br},
	{"c2", tr_c2},
	{"cc", tr_cc},
	{"ce", tr_ce},
	{"ch", tr_ch},
	{"char", tr_char, mkargs_ds},
	{"chop", tr_chop, mkargs_reg1},
	{"cl", tr_cl},
	{"co", tr_co},
	{"co+", tr_coa},
	{"co<", tr_coi, mkargs_ds},
	{"co>", tr_coo, mkargs_ds},
	{"cp", tr_cp},
	{"cs", tr_cs},
	{"da", tr_di},
	{"de", tr_de, mkargs_reg1},
	{"di", tr_di},
	{"ds", tr_ds, mkargs_ds},
	{"dt", tr_dt},
	{"dv", tr_dv, mkargs_eol},
	{"ec", tr_ec},
	{"el", tr_el, mkargs_null},
	{"em", tr_em},
	{"eo", tr_eo},
	{"eos", tr_eos},
	{"ev", tr_ev},
	{"evc", tr_evc},
	{"ex", tr_ex},
	{"fc", tr_fc},
	{"ff", tr_ff},
	{"fi", tr_fi},
	{"fl", tr_br},
	{"fmap", tr_fmap},
	{"fp", tr_fp},
	{"ffsc", tr_ffsc},
	{"fspecial", tr_fspecial},
	{"ft", tr_ft},
	{"fzoom", tr_fzoom},
	{"hc", tr_hc},
	{"hcode", tr_hcode},
	{"hlm", tr_hlm},
	{"hpf", tr_hpf},
	{"hpfa", tr_hpfa},
	{"hy", tr_hy},
	{"hycost", tr_hycost},
	{"hydash", tr_hydash},
	{"hystop", tr_hystop},
	{"hw", tr_hw},
	{"ie", tr_if, mkargs_null},
	{"if", tr_if, mkargs_null},
	{"ig", tr_ig},
	{"in", tr_in},
	{"in2", tr_in2},
	{"it", tr_it},
	{"kn", tr_kn},
	{"lc", tr_lc},
	{"lf", tr_lf},
	{"lg", tr_lg},
	{"ll", tr_ll},
	{"ls", tr_ls},
	{"lsm", tr_lsm},
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
	{"ochar", tr_ochar, mkargs_ochar},
	{"os", tr_os},
	{"pc", tr_pc},
	{"pl", tr_pl},
	{"pmll", tr_pmll},
	{"pn", tr_pn},
	{"po", tr_po},
	{"ps", tr_ps},
	{"rchar", tr_rchar},
	{"rm", tr_rm},
	{"rn", tr_rn},
	{"rr", tr_rr},
	{"rs", tr_rs},
	{"rt", tr_rt},
	{"shift", tr_shift},
	{"so", tr_so},
	{"sp", tr_sp},
	{"ss", tr_ss},
	{"ssh", tr_ssh},
	{"sv", tr_sv},
	{"sy", tr_sy, mkargs_eol},
	{"ta", tr_ta},
	{"tc", tr_tc},
	{"ti", tr_ti},
	{"ti2", tr_ti2},
	{"tkf", tr_tkf},
	{"tl", tr_tl, mkargs_null},
	{"tm", tr_tm, mkargs_eol},
	{"tr", tr_tr, mkargs_eol},
	{"vs", tr_vs},
	{"wh", tr_wh},
};

static char *dotted(char *name, int dot)
{
	char *out = xmalloc(strlen(name) + 2);
	out[0] = dot;
	strcpy(out + 1, name);
	return out;
}

/* execute a built-in request */
void tr_req(int reg, char **args)
{
	struct cmd *req = str_dget(reg);
	if (req)
		req->f(args);
}

/* interpolate a macro for tr_nextreq() */
static void tr_nextreq_exec(char *mac, char *arg0, int readargs)
{
	char *args[NARGS + 3] = {arg0};
	struct cmd *req = str_dget(map(mac));
	char *str = str_get(map(mac));
	struct sbuf sbuf;
	sbuf_init(&sbuf);
	if (readargs) {
		if (req && req->args)
			req->args(&sbuf);
		if (req && !req->args)
			mkargs_req(&sbuf);
		if (!req)
			mkargs_macro(&sbuf);
		chopargs(&sbuf, args + 1);
	}
	if (str)
		in_push(str, args);
	if (!str && req)
		req->f(args);
	sbuf_done(&sbuf);
}

/* read the next troff request; return zero if a request was executed. */
int tr_nextreq(void)
{
	char *mac;
	char *arg0 = NULL;
	int c;
	if (!tr_nl)
		return 1;
	c = cp_next();
	/* transparent line indicator */
	if (c == c_ec) {
		int c2 = cp_next();
		if (c2 == '!') {
			char *args[NARGS + 3] = {"\\!"};
			struct sbuf sbuf;
			sbuf_init(&sbuf);
			cp_copymode(1);
			mkargs_eol(&sbuf);
			cp_copymode(0);
			chopargs(&sbuf, args + 1);
			tr_transparent(args);
			sbuf_done(&sbuf);
			return 0;
		}
		cp_back(c2);
	}
	/* not a request, a blank line, or a line with leading spaces */
	if (c < 0 || (c != c_cc && c != c_c2 &&
			(c != '\n' || tr_bm < 0) &&
			(c != ' ' || tr_sm < 0))) {
		cp_back(c);
		return 1;
	}
	cp_reqbeg();
	if (c == '\n') {		/* blank line macro */
		mac = malloc(strlen(map_name(tr_bm)) + 1);
		strcpy(mac, map_name(tr_bm));
		arg0 = dotted(mac, '.');
		tr_nextreq_exec(mac, arg0, 0);
	} else if (c == ' ') {		/* leading space macro */
		int i;
		mac = malloc(strlen(map_name(tr_sm)) + 1);
		strcpy(mac, map_name(tr_sm));
		for (i = 0; c == ' '; i++)
			c = cp_next();
		cp_back(c);
		n_lsn = i;
		arg0 = dotted(mac, '.');
		tr_nextreq_exec(mac, arg0, 0);
	} else {
		mac = read_name(n_cp);
		arg0 = dotted(mac, c);
		tr_nextreq_exec(mac, arg0, 1);
	}
	free(arg0);
	free(mac);
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
	cmap = dict_make(-1, 0, 2);
}

void tr_done(void)
{
	int i;
	for (i = 0; i < cdef_n; i++)
		free(cdef_dst[i]);
	dict_free(cmap);
}
