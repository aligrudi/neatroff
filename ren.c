#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define cadj		env_adj()		/* line buffer */
#define RENWB(wb)	((wb) == &ren_wb)	/* is ren_wb */

/* diversions */
struct div {
	struct sbuf sbuf;	/* diversion output */
	int reg;		/* diversion register */
	int tpos;		/* diversion trap position */
	int treg;		/* diversion trap register */
	int dl;			/* diversion width */
	int prev_d;		/* previous \n(.d value */
	int prev_h;		/* previous \n(.h value */
	int prev_mk;		/* previous .mk internal register */
	int prev_ns;		/* previous .ns value */
};
static struct div divs[NPREV];	/* diversion stack */
static struct div *cdiv;	/* current diversion */
static int ren_div;		/* rendering a diversion */
static int trap_em = -1;	/* end macro */

static struct wb ren_wb;	/* the main ren.c word buffer */
static int ren_nl;		/* just after newline */
static int ren_cnl;		/* current char is a newline */
static int ren_unbuf[8];	/* ren_back() buffer */
static int ren_un;
static int ren_fillreq;		/* \p request */
static int ren_aborted;		/* .ab executed */

static int bp_first = 1;	/* prior to the first page */
static int bp_next = 1;		/* next page number */
static int bp_count;		/* number of pages so far */
static int bp_ejected;		/* current ejected page */
static int bp_final;		/* 1: executing em, 2: the final page, 3: the 2nd final page */

static char c_fa[GNLEN];	/* field delimiter */
static char c_fb[GNLEN];	/* field padding */

static int ren_next(void)
{
	return ren_un > 0 ? ren_unbuf[--ren_un] : tr_next();
}

static void ren_back(int c)
{
	ren_unbuf[ren_un++] = c;
}

void tr_di(char **args)
{
	if (args[1]) {
		cdiv = cdiv ? cdiv + 1 : divs;
		memset(cdiv, 0, sizeof(*cdiv));
		sbuf_init(&cdiv->sbuf);
		cdiv->reg = map(args[1]);
		cdiv->treg = -1;
		if (args[0][2] == 'a' && str_get(cdiv->reg))	/* .da */
			sbuf_append(&cdiv->sbuf, str_get(cdiv->reg));
		sbuf_printf(&cdiv->sbuf, "%c%s\n", c_cc, TR_DIVBEG);
		cdiv->prev_d = n_d;
		cdiv->prev_h = n_h;
		cdiv->prev_mk = n_mk;
		cdiv->prev_ns = n_ns;
		n_d = 0;
		n_h = 0;
		n_mk = 0;
		n_ns = 0;
	} else if (cdiv) {
		sbuf_putnl(&cdiv->sbuf);
		sbuf_printf(&cdiv->sbuf, "%c%s\n", c_cc, TR_DIVEND);
		str_set(cdiv->reg, sbuf_buf(&cdiv->sbuf));
		sbuf_done(&cdiv->sbuf);
		n_dl = cdiv->dl;
		n_dn = n_d;
		n_d = cdiv->prev_d;
		n_h = cdiv->prev_h;
		n_mk = cdiv->prev_mk;
		n_ns = cdiv->prev_ns;
		cdiv = cdiv > divs ? cdiv - 1 : NULL;
	}
}

int charwid_base(int fn, int sz, int wid)
{
	/* the original troff rounds the widths up */
	return (wid * sz + dev_uwid / 2) / dev_uwid;
}

int charwid(int fn, int sz, int wid)
{
	if (dev_getcs(fn))
		return dev_getcs(n_f) * SC_EM / 36;
	return charwid_base(fn, sz, wid) +
		(dev_getbd(fn) ? dev_getbd(fn) - 1 : 0);
}

int spacewid(int fn, int sz)
{
	return charwid(fn, sz, (dev_font(fn)->spacewid * n_ss + 6) / 12);
}

int f_divreg(void)
{
	return cdiv ? cdiv->reg : -1;
}

int f_hpos(void)
{
	return adj_wid(cadj) + wb_wid(&ren_wb);
}

void tr_divbeg(char **args)
{
	odiv_beg();
	ren_div++;
}

void tr_divend(char **args)
{
	odiv_end();
	ren_div--;
}

static int trap_reg(int pos);
static int trap_pos(int pos);
static void trap_exec(int reg);

static void ren_page(int pg, int force)
{
	if (!force && bp_final >= 2)
		return;
	n_nl = 0;
	n_d = 0;
	n_h = 0;
	n_pg = pg;
	bp_next = n_pg + 1;
	bp_count++;
	out("p%d\n", pg);
	out("V%d\n", 0);
	if (trap_pos(-1) == 0)
		trap_exec(trap_reg(-1));
}

static void ren_first(void)
{
	if (bp_first && !cdiv) {
		bp_first = 0;
		ren_page(bp_next, 1);
	}
}

/* when nodiv, do not append .sp to diversions */
static void ren_sp(int n, int nodiv)
{
	ren_first();
	/* ignore .sp without arguments when reading diversions */
	if (!n && ren_div && !n_u)
		return;
	n_d += n ? n : n_v;
	if (n_d > n_h)
		n_h = n_d;
	if (cdiv && !nodiv) {
		sbuf_putnl(&cdiv->sbuf);
		sbuf_printf(&cdiv->sbuf, "%csp %du\n", c_cc, n ? n : n_v);
	} else {
		n_nl = n_d;
	}
}

static void trap_exec(int reg)
{
	if (str_get(reg))
		in_pushnl(str_get(reg), NULL);
}

static int detect_traps(int beg, int end)
{
	int pos = trap_pos(beg);
	return pos >= 0 && (cdiv || pos < n_p) && pos <= end;
}

/* return 1 if executed a trap */
static int ren_traps(int beg, int end, int dosp)
{
	int pos = trap_pos(beg);
	if (detect_traps(beg, end)) {
		if (dosp && pos > beg)
			ren_sp(pos - beg, 0);
		trap_exec(trap_reg(beg));
		return 1;
	}
	return 0;
}

static int detect_pagelimit(int ne)
{
	return !cdiv && n_nl + ne >= n_p;
}

/* start a new page if needed */
static int ren_pagelimit(int ne)
{
	if (detect_pagelimit(ne)) {
		ren_page(bp_next, 0);
		return 1;
	}
	return 0;
}

/* return 1 if triggered a trap */
static int down(int n)
{
	if (ren_traps(n_d, n_d + (n ? n : n_v), 1))
		return 1;
	ren_sp(n, 0);
	return ren_pagelimit(0);
}

/* line adjustment */
static int ren_ljust(struct sbuf *spre, int w, int ad, int ll, int li, int lt)
{
	int ljust = lt >= 0 ? lt : li;
	int llen = ll - ljust;
	n_n = w;
	if (ad == AD_C)
		ljust += llen > w ? (llen - w) / 2 : 0;
	if (ad == AD_R)
		ljust += llen - w;
	if (ljust)
		sbuf_printf(spre, "%ch'%du'", c_ec, ljust);
	if (cdiv && cdiv->dl < w + ljust)
		cdiv->dl = w + ljust;
	return ljust;
}

/* append the line to the current diversion or send it to out.c */
static void ren_line(struct sbuf *spre, struct sbuf *sbuf)
{
	if (cdiv) {
		if (!sbuf_empty(spre))
			sbuf_append(&cdiv->sbuf, sbuf_buf(spre));
		sbuf_append(&cdiv->sbuf, sbuf_buf(sbuf));
	} else {
		out("H%d\n", n_o);
		out("V%d\n", n_d);
		if (!sbuf_empty(spre))
			out_line(sbuf_buf(spre));
		out_line(sbuf_buf(sbuf));
	}
}

static void ren_transparent(char *s)
{
	if (cdiv)
		sbuf_printf(&cdiv->sbuf, "%s\n", s);
	else
		out("%s\n", s);
}

static int zwid(void)
{
	struct glyph *g = dev_glyph("0", n_f);
	return charwid(n_f, n_s, g ? g->wid : SC_DW);
}

/* append the line number to the output line */
static void ren_lnum(struct sbuf *spre)
{
	char num[16] = "";
	char dig[16] = "";
	struct wb wb;
	int i = 0;
	wb_init(&wb);
	if (n_nn <= 0 && (n_ln % n_nM) == 0)
		sprintf(num, "%d", n_ln);
	wb_hmov(&wb, n_nI * zwid());
	if (strlen(num) < 3)
		wb_hmov(&wb, (3 - strlen(num)) * zwid());
	while (num[i]) {
		dig[0] = num[i++];
		wb_put(&wb, dig);
	}
	wb_hmov(&wb, n_nS * zwid());
	sbuf_append(spre, sbuf_buf(&wb.sbuf));
	wb_done(&wb);
	if (n_nn > 0)
		n_nn--;
	else
		n_ln++;
}

/* append margin character */
static void ren_mc(struct sbuf *sbuf, int w, int ljust)
{
	struct wb wb;
	wb_init(&wb);
	if (w + ljust < n_l + n_mcn)
		wb_hmov(&wb, n_l + n_mcn - w - ljust);
	wb_putexpand(&wb, c_mc);
	sbuf_append(sbuf, sbuf_buf(&wb.sbuf));
	wb_done(&wb);
}

/* output current line; returns 1 if triggered a trap */
static int ren_bradj(struct adj *adj, int fill, int ad, int body)
{
	char cmd[16];
	struct sbuf sbuf, spre;
	int ll, li, lt, els_neg, els_pos;
	int w, hyph, prev_d, lspc, ljust;
	ren_first();
	if (!adj_empty(adj, fill)) {
		sbuf_init(&sbuf);
		sbuf_init(&spre);
		hyph = n_hy;
		lspc = MAX(1, n_L) * n_v;	/* line space, ignoreing \x */
		if (n_hy & HY_LAST && (detect_traps(n_d, n_d + lspc) ||
					detect_pagelimit(lspc)))
			hyph = 0;		/* disable for last lines */
		w = adj_fill(adj, ad == AD_B, fill, hyph, &sbuf,
				&ll, &li, &lt, &els_neg, &els_pos);
		prev_d = n_d;
		if (els_neg)
			ren_sp(-els_neg, 1);
		if (!n_ns || !sbuf_empty(&sbuf) || els_neg || els_pos) {
			ren_sp(0, 0);
			if (!sbuf_empty(&sbuf) && n_nm && body)
				ren_lnum(&spre);
			ljust = ren_ljust(&spre, w, ad, ll, li, lt);
			if (!sbuf_empty(&sbuf) && body && n_mc)
				ren_mc(&sbuf, w, ljust);
			ren_line(&spre, &sbuf);
			n_ns = 0;
		}
		sbuf_done(&spre);
		sbuf_done(&sbuf);
		if (els_pos)
			ren_sp(els_pos, 1);
		n_a = els_pos;
		if (detect_traps(prev_d, n_d) || detect_pagelimit(lspc - n_v)) {
			sprintf(cmd, "%c&", c_ec);
			if (!ren_cnl)		/* prevent unwanted newlines */
				in_push(cmd, NULL);
			if (!ren_traps(prev_d, n_d, 0))
				ren_pagelimit(lspc - n_v);
			return 1;
		}
		if (lspc - n_v && down(lspc - n_v))
			return 1;
	}
	return 0;
}

/* output current line; returns 1 if triggered a trap */
static int ren_br(int force)
{
	int ad = n_j;
	if (!n_u || n_na || (n_j == AD_B && force))
		ad = AD_L;
	if (n_ce)
		ad = AD_C;
	return ren_bradj(cadj, !force && !n_ce && n_u, ad, 1);
}

void tr_br(char **args)
{
	if (args[0][0] == c_cc)
		ren_br(1);
}

void tr_sp(char **args)
{
	int traps = 0;
	int n = args[1] ? eval(args[1], 'v') : n_v;
	if (args[0][0] == c_cc)
		traps = ren_br(1);
	if (n && !n_ns && !traps)
		down(n);
}

void tr_sv(char **args)
{
	int n = eval(args[1], 'v');
	n_sv = 0;
	if (n_d + n < f_nexttrap())
		down(n);
	else
		n_sv = n;
}

void tr_ns(char **args)
{
	n_ns = 1;
}

void tr_rs(char **args)
{
	n_ns = 0;
}

void tr_os(char **args)
{
	if (n_sv)
		down(n_sv);
	n_sv = 0;
}

void tr_mk(char **args)
{
	if (args[1])
		num_set(map(args[1]), n_d);
	else
		n_mk = n_d;
}

void tr_rt(char **args)
{
	int n = args[1] ? eval_re(args[1], n_d, 'v') : n_mk;
	if (n >= 0 && n < n_d)
		ren_sp(n - n_d, 0);
}

void tr_ne(char **args)
{
	int n = args[1] ? eval(args[1], 'v') : n_v;
	if (!ren_traps(n_d, n_d + n - 1, 1))
		ren_pagelimit(n);
}

static void push_eject(void)
{
	char buf[32];
	bp_ejected = bp_count;
	sprintf(buf, "%c%s %d\n", c_cc, TR_EJECT, bp_ejected);
	in_pushnl(buf, NULL);
}

static void push_br(void)
{
	char br[8] = {c_cc, 'b', 'r', '\n'};
	in_pushnl(br, NULL);
}

static void ren_eject(int id)
{
	if (id == bp_ejected && id == bp_count && !cdiv) {
		if (detect_traps(n_d, n_p)) {
			push_eject();
			ren_traps(n_d, n_p, 1);
		} else {
			bp_ejected = 0;
			ren_page(bp_next, 0);
		}
	}
}

void tr_eject(char **args)
{
	ren_eject(atoi(args[1]));
}

void tr_bp(char **args)
{
	if (!cdiv && (args[1] || !n_ns)) {
		if (bp_ejected != bp_count)
			push_eject();
		if (args[0][0] == c_cc)
			push_br();
		if (args[1])
			bp_next = eval_re(args[1], n_pg, 0);
	}
}

void tr_pn(char **args)
{
	if (args[1])
		bp_next = eval_re(args[1], n_pg, 0);
}

static void ren_ps(char *s)
{
	int ps = !s || !*s || !strcmp("0", s) ? n_s0 : eval_re(s, n_s, 0);
	n_s0 = n_s;
	n_s = MAX(1, ps);
}

void tr_ps(char **args)
{
	ren_ps(args[1]);
}

void tr_ll(char **args)
{
	int ll = args[1] ? eval_re(args[1], n_l, 'm') : n_l0;
	n_l0 = n_l;
	n_l = MAX(0, ll);
	adj_ll(cadj, n_l);
}

void tr_in(char **args)
{
	int in = args[1] ? eval_re(args[1], n_i, 'm') : n_i0;
	if (args[0][0] == c_cc)
		ren_br(1);
	n_i0 = n_i;
	n_i = MAX(0, in);
	adj_in(cadj, n_i);
	adj_ti(cadj, -1);
}

void tr_ti(char **args)
{
	if (args[0][0] == c_cc)
		ren_br(1);
	if (args[1])
		adj_ti(cadj, eval_re(args[1], n_i, 'm'));
}

static void ren_ft(char *s)
{
	int fn = !s || !*s || !strcmp("P", s) ? n_f0 : dev_pos(s);
	if (fn < 0) {
		errmsg("neatroff: failed to mount <%s>\n", s);
	} else {
		n_f0 = n_f;
		n_f = fn;
	}
}

void tr_ft(char **args)
{
	ren_ft(args[1]);
}

void tr_fp(char **args)
{
	if (!args[2])
		return;
	if (dev_mnt(atoi(args[1]), args[2], args[3] ? args[3] : args[2]) < 0)
		errmsg("neatroff: failed to mount <%s>\n", args[2]);
}

void tr_nf(char **args)
{
	if (args[0][0] == c_cc)
		ren_br(1);
	n_u = 0;
}

void tr_fi(char **args)
{
	if (args[0][0] == c_cc)
		ren_br(1);
	n_u = 1;
}

void tr_ce(char **args)
{
	if (args[0][0] == c_cc)
		ren_br(1);
	n_ce = args[1] ? atoi(args[1]) : 1;
}

void tr_fc(char **args)
{
	char *fa = args[1];
	char *fb = args[2];
	if (fa && charread(&fa, c_fa) >= 0) {
		if (!fb || charread(&fb, c_fb) < 0)
			strcpy(c_fb, " ");
	} else {
		c_fa[0] = '\0';
		c_fb[0] = '\0';
	}
}

static void ren_cl(char *s)
{
	int m = !s || !*s ? n_m0 : clr_get(s);
	n_m0 = n_m;
	n_m = m;
}

void tr_cl(char **args)
{
	ren_cl(args[1]);
}

void tr_ab(char **args)
{
	fprintf(stderr, "%s\n", args[1]);
	ren_aborted = 1;
}

static void ren_cmd(struct wb *wb, int c, char *arg)
{
	switch (c) {
	case ' ':
		wb_hmov(wb, spacewid(n_f, n_s));
		break;
	case 'b':
		ren_bcmd(wb, arg);
		break;
	case 'c':
		wb_setpart(wb);
		break;
	case 'D':
		ren_dcmd(wb, arg);
		break;
	case 'd':
		wb_vmov(wb, SC_EM / 2);
		break;
	case 'f':
		ren_ft(arg);
		break;
	case 'h':
		wb_hmov(wb, eval(arg, 'm'));
		break;
	case 'k':
		num_set(map(arg), RENWB(wb) ? f_hpos() - n_lb : wb_wid(wb));
		break;
	case 'L':
		ren_vlcmd(wb, arg);
		break;
	case 'l':
		ren_hlcmd(wb, arg);
		break;
	case 'm':
		ren_cl(arg);
		break;
	case 'o':
		ren_ocmd(wb, arg);
		break;
	case 'p':
		if (RENWB(wb))
			ren_fillreq = 1;
		break;
	case 'r':
		wb_vmov(wb, -SC_EM);
		break;
	case 's':
		ren_ps(arg);
		break;
	case 'u':
		wb_vmov(wb, -SC_EM / 2);
		break;
	case 'v':
		wb_vmov(wb, eval(arg, 'v'));
		break;
	case 'X':
		wb_etc(wb, arg);
		break;
	case 'x':
		wb_els(wb, eval(arg, 'v'));
		break;
	case '0':
		wb_hmov(wb, zwid());
		break;
	case '|':
		wb_hmov(wb, SC_EM / 6);
		break;
	case '&':
		wb_hmov(wb, 0);
		break;
	case '^':
		wb_hmov(wb, SC_EM / 12);
		break;
	case '{':
	case '}':
		break;
	}
}

static void ren_field(struct wb *wb, int (*next)(void), void (*back)(int));
static void ren_tab(struct wb *wb, char *tc, int (*next)(void), void (*back)(int));

/* insert a character, escape sequence, field or etc into wb */
static void ren_put(struct wb *wb, char *c, int (*next)(void), void (*back)(int))
{
	char arg[ILNLEN];
	struct glyph *g;
	char *s;
	int w, n;
	if (c[0] == ' ' || c[0] == '\n') {
		wb_put(wb, c);
		return;
	}
	if (c[0] == '\t' || c[0] == '') {
		ren_tab(wb, c[0] == '\t' ? c_tc : c_lc, next, back);
		return;
	}
	if (c_fa[0] && !strcmp(c_fa, c)) {
		ren_field(wb, next, back);
		return;
	}
	if (c[0] == c_ec) {
		if (c[1] == 'z') {
			w = wb_wid(wb);
			ren_char(wb, next, back);
			wb_hmov(wb, w - wb_wid(wb));
			return;
		}
		if (c[1] == '!') {
			if (ren_nl && next == ren_next) {
				s = arg;
				n = next();
				while (n >= 0 && n != '\n') {
					*s++ = n;
					n = next();
				}
				*s = '\0';
				ren_transparent(arg);
			}
			return;
		}
		if (strchr(" bCcDdfHhkLlmNoprSsuvXxz0^|{}&", c[1])) {
			argnext(arg, c[1], next, back);
			if (c[1] == 'S' || c[1] == 'H')
				return;			/* not implemented */
			if (c[1] != 'N') {
				ren_cmd(wb, c[1], arg);
				return;
			}
			g = dev_glyph_byid(arg, n_f);
			strcpy(c, g ? g->name : "cnull");
		}
	}
	if (!n_lg || ren_div || wb_lig(wb, c)) {
		if (n_kn && !ren_div)
			wb_kern(wb, c);
		wb_putexpand(wb, c);
	}
}

/* read one character and place it inside wb buffer */
int ren_char(struct wb *wb, int (*next)(void), void (*back)(int))
{
	char c[GNLEN * 4];
	if (charnext(c, next, back) < 0)
		return -1;
	ren_put(wb, c, next, back);
	return 0;
}

/* like ren_char(); return 1 if d1 was read and d2 if d2 was read */
static int ren_chardel(struct wb *wb, int (*next)(void), void (*back)(int),
			char *d1, char *d2)
{
	char c[GNLEN * 4];
	if (charnext(c, next, back) < 0)
		return -1;
	if (d1 && !strcmp(d1, c))
		return 1;
	if (d2 && !strcmp(d2, c))
		return 2;
	ren_put(wb, c, next, back);
	return 0;
}

/* read the argument of \w and push its width */
int ren_wid(int (*next)(void), void (*back)(int))
{
	char delim[GNLEN];
	int c, n;
	struct wb wb;
	wb_init(&wb);
	charnext(delim, next, back);
	odiv_beg();
	c = next();
	while (c >= 0 && c != '\n') {
		back(c);
		if (ren_chardel(&wb, next, back, delim, NULL))
			break;
		c = next();
	}
	odiv_end();
	n = wb_wid(&wb);
	wb_wconf(&wb, &n_ct, &n_st, &n_sb);
	wb_done(&wb);
	return n;
}

/* return 1 if d1 was read and 2 if d2 was read */
static int ren_until(struct wb *wb, char *d1, char *d2,
			int (*next)(void), void (*back)(int))
{
	int c, ret;
	c = next();
	while (c >= 0 && c != '\n') {
		back(c);
		ret = ren_chardel(wb, next, back, d1, d2);
		if (ret)
			return ret;
		c = next();
	}
	if (c == '\n')
		back(c);
	return 0;
}

static void wb_cpy(struct wb *dst, struct wb *src, int left)
{
	wb_hmov(dst, left - wb_wid(dst));
	wb_cat(dst, src);
}

void ren_tl(int (*next)(void), void (*back)(int))
{
	struct adj *adj;
	struct wb wb, wb2;
	char delim[GNLEN];
	adj = adj_alloc();
	wb_init(&wb);
	wb_init(&wb2);
	charnext(delim, next, back);
	/* the left-adjusted string */
	ren_until(&wb2, delim, NULL, next, back);
	wb_cpy(&wb, &wb2, 0);
	/* the centered string */
	ren_until(&wb2, delim, NULL, next, back);
	wb_cpy(&wb, &wb2, (n_lt - wb_wid(&wb2)) / 2);
	/* the right-adjusted string */
	ren_until(&wb2, delim, NULL, next, back);
	wb_cpy(&wb, &wb2, n_lt - wb_wid(&wb2));
	/* flushing the line */
	adj_ll(adj, n_lt);
	adj_wb(adj, &wb);
	adj_nl(adj);
	ren_bradj(adj, 0, AD_L, 0);
	adj_free(adj);
	wb_done(&wb2);
	wb_done(&wb);
}

static void ren_field(struct wb *wb, int (*next)(void), void (*back)(int))
{
	struct wb wbs[NFIELDS];
	int i, n = 0;
	int wid = 0;
	int left, right, cur_left;
	int pad, rem;
	while (n < LEN(wbs)) {
		wb_init(&wbs[n]);
		if (ren_until(&wbs[n++], c_fb, c_fa, next, back) != 1)
			break;
	}
	left = RENWB(wb) ? f_hpos() : wb_wid(wb);
	right = tab_next(left);
	for (i = 0; i < n; i++)
		wid += wb_wid(&wbs[i]);
	pad = (right - left - wid) / (n > 1 ? n - 1 : 1);
	rem = (right - left - wid) % (n > 1 ? n - 1 : 1);
	for (i = 0; i < n; i++) {
		if (i == 0)
			cur_left = left;
		else if (i == n - 1)
			cur_left = right - wb_wid(&wbs[i]);
		else
			cur_left = wb_wid(wb) + pad + (i + rem >= n);
		wb_cpy(wb, &wbs[i], cur_left);
		wb_done(&wbs[i]);
	}
}

static void ren_tab(struct wb *wb, char *tc, int (*next)(void), void (*back)(int))
{
	struct wb t;
	int pos = RENWB(wb) ? f_hpos() : wb_wid(wb);
	int ins = tab_next(pos);	/* insertion position */
	int typ = tab_type(pos);	/* tab type */
	int c;
	wb_init(&t);
	if (typ == 'R' || typ == 'C') {
		c = next();
		while (c >= 0 && c != '\n' && c != '\t' && c != '') {
			back(c);
			ren_char(&t, next, back);
			c = next();
		}
		back(c);
	}
	if (typ == 'C')
		ins -= wb_wid(&t) / 2;
	if (typ == 'R')
		ins -= wb_wid(&t);
	if (!tc[0] || ins <= pos)
		wb_hmov(wb, ins - pos);
	else
		ren_hline(wb, ins - pos, tc);
	wb_cat(wb, &t);
	wb_done(&t);
}

static int ren_expanding;	/* expanding the definition of a character */

/* expand the given defined character */
int ren_expand(struct wb *wb, char *n)
{
	char *s = chdef_map(n);
	int c;
	if (!s || ren_expanding)
		return 1;
	ren_expanding = 1;
	odiv_beg();
	sstr_push(s);
	c = sstr_next();
	while (c >= 0) {
		sstr_back(c);
		if (ren_chardel(wb, sstr_next, sstr_back, NULL, NULL))
			break;
		c = sstr_next();
	}
	sstr_pop();
	odiv_end();
	ren_expanding = 0;
	return 0;
}

/* read characters from in.c and pass rendered lines to out.c */
int render(void)
{
	struct wb *wb = &ren_wb;
	int c;
	n_nl = -1;
	wb_init(wb);
	tr_first();
	ren_first();			/* transition to the first page */
	c = ren_next();
	while (1) {
		if (ren_aborted)
			return 1;
		if (c < 0) {
			if (bp_final >= 2)
				break;
			if (bp_final == 0 && trap_em >= 0) {
				trap_exec(trap_em);
				bp_final = 1;
			} else {
				bp_final = 2;
				push_eject();
				push_br();
			}
			c = ren_next();
			continue;
		}
		ren_cnl = c == '\n';
		/* add wb (the current word) to cadj */
		if (c == ' ' || c == '\n') {
			adj_swid(cadj, spacewid(n_f, n_s));
			if (!wb_part(wb)) {	/* not after a \c */
				adj_wb(cadj, wb);
				if (c == '\n')
					adj_nl(cadj);
				else
					adj_sp(cadj);
			}
		}
		/* flush the line if necessary */
		if (c == ' ' || c == '\n') {
			while ((ren_fillreq && !wb_part(wb) && !n_ce && n_u) ||
						adj_full(cadj, !n_ce && n_u)) {
				ren_br(0);
				ren_fillreq = 0;
			}
		}
		if (c == '\n' || ren_nl)	/* end or start of input line */
			n_lb = f_hpos();
		if (c == '\n' && n_it && --n_itn == 0)
			trap_exec(n_it);
		if (c == '\n' && !wb_part(wb))
			n_ce = MAX(0, n_ce - 1);
		if (c != ' ') {
			ren_back(c);
			ren_char(wb, ren_next, ren_back);
			if (c != '\n' && wb_empty(wb))
				adj_nonl(cadj);
		}
		ren_nl = c == '\n';
		c = ren_next();
	}
	bp_final = 3;
	if (!adj_empty(cadj, 0))
		ren_page(bp_next, 1);
	ren_br(1);
	wb_done(wb);
	return 0;
}

/* trap handling */

#define tposval(i)		(tpos[i] < 0 ? n_p + tpos[i] : tpos[i])

static int tpos[NTRAPS];	/* trap positions */
static int treg[NTRAPS];	/* trap registers */
static int ntraps;

static int trap_first(int pos)
{
	int best = -1;
	int i;
	for (i = 0; i < ntraps; i++)
		if (treg[i] >= 0 && tposval(i) > pos)
			if (best < 0 || tposval(i) < tposval(best))
				best = i;
	return best;
}

static int trap_byreg(int reg)
{
	int i;
	for (i = 0; i < ntraps; i++)
		if (treg[i] == reg)
			return i;
	return -1;
}

static int trap_bypos(int reg, int pos)
{
	int i;
	for (i = 0; i < ntraps; i++)
		if (treg[i] >= 0 && tposval(i) == pos)
			if (reg == -1 || treg[i] == reg)
				return i;
	return -1;
}

void tr_wh(char **args)
{
	int reg, pos, id;
	if (!args[1])
		return;
	pos = eval(args[1], 'v');
	id = trap_bypos(-1, pos);
	if (!args[2]) {
		if (id >= 0)
			treg[id] = -1;
		return;
	}
	reg = map(args[2]);
	if (id < 0)
		id = trap_byreg(-1);
	if (id < 0)
		id = ntraps++;
	tpos[id] = pos;
	treg[id] = reg;
}

void tr_ch(char **args)
{
	int reg;
	int id;
	if (!args[1])
		return;
	reg = map(args[1]);
	id = trap_byreg(reg);
	if (id >= 0)
		tpos[id] = args[2] ? eval(args[2], 'v') : -1;
}

void tr_dt(char **args)
{
	if (!cdiv)
		return;
	if (args[2]) {
		cdiv->tpos = eval(args[1], 'v');
		cdiv->treg = map(args[2]);
	} else {
		cdiv->treg = -1;
	}
}

void tr_em(char **args)
{
	trap_em = args[1] ? map(args[1]) : -1;
}

static int trap_pos(int pos)
{
	int ret = trap_first(pos);
	if (bp_final >= 3)
		return -1;
	if (cdiv)
		return cdiv->treg && cdiv->tpos > pos ? cdiv->tpos : -1;
	return ret >= 0 ? tposval(ret) : -1;
}

static int trap_reg(int pos)
{
	int ret = trap_first(pos);
	if (cdiv)
		return cdiv->treg && cdiv->tpos > pos ? cdiv->treg : -1;
	return ret >= 0 ? treg[ret] : -1;
}

int f_nexttrap(void)
{
	int pos = trap_pos(n_d);
	if (cdiv)
		return pos >= 0 ? pos : 0x7fffffff;
	return (pos >= 0 && pos < n_p ? pos : n_p) - n_d;
}
