/* rendering lines and managing traps */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define NOPAGE		0x40000000	/* undefined bp_next */
#define cfmt		env_fmt()	/* current formatter */
#define cwb		env_wb()	/* current word buffer */

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
static int ren_divvs;		/* the amount of .v in diversions */
static int trap_em = -1;	/* end macro */

static int ren_nl;		/* just after a newline */
static int ren_partial;		/* reading an input line in render_rec() */
static int ren_unbuf[8];	/* ren_back() buffer */
static int ren_un;
static int ren_aborted;		/* .ab executed */

static int bp_first = 1;	/* prior to the first page */
static int bp_next = NOPAGE;	/* next page number */
static int bp_ejected;		/* current ejected page */
static int bp_final;		/* 1: executing em, 2: the final page, 3: the 2nd final page */
static int ren_level;		/* the depth of render_rec() calls */

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

int f_divreg(void)
{
	return cdiv ? cdiv->reg : -1;
}

int f_hpos(void)
{
	return fmt_wid(cfmt) + wb_wid(cwb);
}

void tr_divbeg(char **args)
{
	odiv_beg();
	ren_div++;
}

void tr_divend(char **args)
{
	if (ren_div <= 0)
		errdie("neatroff: diversion stack empty\n");
	odiv_end();
	ren_div--;
}

void tr_divvs(char **args)
{
	ren_divvs = eval(args[1], 'u');
}

void tr_transparent(char **args)
{
	if (cdiv)
		sbuf_printf(&cdiv->sbuf, "%s\n", args[1]);
	else
		out("%s\n", args[1]);
}

static int trap_reg(int pos);
static int trap_pos(int pos);
static void trap_exec(int reg);

static void ren_page(int force)
{
	if (!force && bp_final >= 2)
		return;
	n_nl = 0;
	n_d = 0;
	n_h = 0;
	n_pg = bp_next != NOPAGE ? bp_next : n_pg + 1;
	bp_next = NOPAGE;
	n_PG += 1;
	out("p%d\n", n_PG);
	out("V%d\n", 0);
	if (trap_pos(-1) == 0)
		trap_exec(trap_reg(-1));
}

static int ren_first(void)
{
	if (bp_first && !cdiv) {
		bp_first = 0;
		ren_page(1);
		return 0;
	}
	return 1;
}

/* when nodiv, do not append .sp to diversions */
static void ren_sp(int n, int nodiv)
{
	int linevs = !n;	/* the vertical spacing before a line */
	ren_first();
	if (!n && ren_div && ren_divvs && !n_u)
		n = ren_divvs;	/* .v at the time of diversion */
	ren_divvs = 0;
	n_ns = 0;
	n_d += n ? n : n_v;
	if (n_d > n_h)
		n_h = n_d;
	if (cdiv && !nodiv) {
		if (linevs)
			sbuf_printf(&cdiv->sbuf, "%c%s %du\n", c_cc, TR_DIVVS, n_v);
		else
			sbuf_printf(&cdiv->sbuf, "%csp %du\n", c_cc, n ? n : n_v);
	}
	if (!cdiv)
		n_nl = n_d;
}

static int render_rec(int level);

static void trap_exec(int reg)
{
	char cmd[16];
	int partial = ren_partial && (!ren_un || ren_unbuf[0] != '\n');
	if (str_get(reg)) {
		sprintf(cmd, "%c%s %d\n", c_cc, TR_POPREN, ren_level);
		in_push(cmd, NULL);
		in_push(str_get(reg), NULL);
		if (partial)
			in_push("\n", NULL);
		render_rec(++ren_level);
		/* executed the trap while in the middle of an input line */
		if (partial)
			fmt_suppressnl(cfmt);
	}
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
		ren_page(0);
		return 1;
	}
	return 0;
}

/* return 1 if triggered a trap */
static int down(int n)
{
	if (ren_traps(n_d, n_d + (n ? n : n_v), 1))
		return 1;
	ren_sp(MAX(n, -n_d), 0);
	return ren_pagelimit(0);
}

/* line adjustment */
static int ren_ljust(struct sbuf *spre, int w, int ad, int li, int lI, int ll)
{
	int ljust = li;
	int llen = ll - lI - li;
	n_n = w;
	if ((ad & AD_B) == AD_C)
		ljust += llen > w ? (llen - w) / 2 : 0;
	if ((ad & AD_B) == AD_R)
		ljust += llen - w;
	if (ljust)
		sbuf_printf(spre, "%ch'%du'", c_ec, ljust);
	if (cdiv && cdiv->dl < w + ljust)
		cdiv->dl = w + ljust;
	return ljust;
}

/* append the line to the current diversion or send it to out.c */
static void ren_out(char *beg, char *mid, char *end)
{
	if (cdiv) {
		sbuf_append(&cdiv->sbuf, beg);
		sbuf_append(&cdiv->sbuf, mid);
		sbuf_append(&cdiv->sbuf, end);
		sbuf_append(&cdiv->sbuf, "\n");
	} else {
		out("H%d\n", n_o);
		out("V%d\n", n_d);
		out_line(beg);
		out_line(mid);
		out_line(end);
	}
}

static void ren_dir(struct sbuf *sbuf)
{
	struct sbuf fixed;
	sbuf_init(&fixed);
	dir_fix(&fixed, sbuf_buf(sbuf));
	sbuf_done(sbuf);
	sbuf_init(sbuf);
	sbuf_append(sbuf, sbuf_buf(&fixed));
	sbuf_done(&fixed);
}

static int zwid(void)
{
	struct glyph *g = dev_glyph("0", n_f);
	return g ? font_gwid(g->font, dev_font(n_f), n_s, g->wid) : 0;
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
	sbuf_append(spre, wb_buf(&wb));
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
	sbuf_append(sbuf, wb_buf(&wb));
	wb_done(&wb);
}

/* process a line and print it with ren_out() */
static int ren_line(char *line, int w, int ad, int body,
		int li, int lI, int ll, int els_neg, int els_pos)
{
	struct sbuf sbeg, send, sbuf;
	int prev_d, lspc, ljust;
	ren_first();
	sbuf_init(&sbeg);
	sbuf_init(&send);
	sbuf_init(&sbuf);
	sbuf_append(&sbuf, line);
	lspc = MAX(1, n_L) * n_v;	/* line space, ignoreing \x */
	prev_d = n_d;
	if (!n_ns || line[0] || els_neg || els_pos) {
		if (els_neg)
			ren_sp(-els_neg, 1);
		ren_sp(0, 0);
		if (line[0] && n_nm && body)
			ren_lnum(&sbeg);
		if (!ren_div && dir_do)
			ren_dir(&sbuf);
		ljust = ren_ljust(&sbeg, w, ad, li, lI, ll);
		if (line[0] && body && n_mc)
			ren_mc(&send, w, ljust);
		ren_out(sbuf_buf(&sbeg), sbuf_buf(&sbuf), sbuf_buf(&send));
		n_ns = 0;
		if (els_pos)
			ren_sp(els_pos, 1);
	}
	sbuf_done(&sbeg);
	sbuf_done(&send);
	sbuf_done(&sbuf);
	n_a = els_pos;
	if (detect_traps(prev_d, n_d) || detect_pagelimit(lspc - n_v)) {
		if (!ren_pagelimit(lspc - n_v))
			ren_traps(prev_d, n_d, 0);
		return 1;
	}
	if (lspc - n_v && down(lspc - n_v))
		return 1;
	return 0;
}

/* read a line from fmt and send it to ren_line() */
static int ren_passline(struct fmt *fmt)
{
	char *buf;
	int ll, li, lI, els_neg, els_pos, w, ret;
	int ad = n_j;
	ren_first();
	if (!fmt_morewords(fmt))
		return 0;
	buf = fmt_nextline(fmt, &w, &li, &lI, &ll, &els_neg, &els_pos);
	if ((n_cp && !n_u) || n_na)
		ad = AD_L;
	else if ((ad & AD_B) == AD_B)
		ad = n_td > 0 ? AD_R : AD_L;
	if (n_ce)
		ad = AD_C;
	ret = ren_line(buf, w, ad, 1, li, lI, ll, els_neg, els_pos);
	free(buf);
	return ret;
}

/* output formatted lines in fmt */
static int ren_fmtpop(struct fmt *fmt)
{
	int ret = 0;
	while (fmt_morelines(fmt))
		ret = ren_passline(fmt);
	return ret;
}

/* format and output all lines in fmt */
static void ren_fmtpopall(struct fmt *fmt)
{
	while (fmt_fill(fmt, 0))
		ren_fmtpop(fmt);
	ren_fmtpop(fmt);
}

/* pass the given word buffer to the current line buffer (cfmt) */
static void ren_fmtword(struct wb *wb)
{
	while (fmt_word(cfmt, wb))
		ren_fmtpop(cfmt);
	wb_reset(wb);
}

/* output current line; returns 1 if triggered a trap */
static int ren_br(void)
{
	ren_first();
	ren_fmtword(cwb);
	while (fmt_fill(cfmt, 1))
		ren_fmtpop(cfmt);
	return ren_fmtpop(cfmt);
}

void tr_br(char **args)
{
	if (args[0][0] == c_cc)
		ren_br();
	else
		ren_fmtpopall(cfmt);	/* output the completed lines */
}

void tr_sp(char **args)
{
	int traps = 0;
	int n;
	if (args[0][0] == c_cc)
		traps = ren_br();
	n = args[1] ? eval(args[1], 'v') : n_v;
	if (n && (!n_ns || ren_div) && !traps)
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
	if (!ren_first())
		return;
	if (!ren_traps(n_d, n_d + n - 1, 1))
		ren_pagelimit(n);
}

static void ren_ejectpage(int br)
{
	ren_first();
	bp_ejected = n_PG;
	if (br)
		ren_br();
	while (n_PG == bp_ejected && !cdiv) {
		if (detect_traps(n_d, n_p)) {
			ren_traps(n_d, n_p, 1);
		} else {
			bp_ejected = 0;
			ren_page(0);
		}
	}
}

void tr_bp(char **args)
{
	if (!cdiv && (args[1] || !n_ns)) {
		if (args[1])
			bp_next = eval_re(args[1], n_pg, 0);
		ren_ejectpage(args[0][0] == c_cc);
	}
}

void tr_pn(char **args)
{
	if (args[1])
		bp_next = eval_re(args[1], n_pg, 0);
}

static void ren_ps(char *s)
{
	int ps = !s || !*s || !strcmp("0", s) ?
		n_s0 * SC_PT : eval_re(s, n_s * SC_PT, 'p');
	n_s0 = n_s;
	n_s = MAX(1, (ps + SC_PT / 2) / SC_PT);
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
}

void tr_in(char **args)
{
	int in = args[1] ? eval_re(args[1], n_i, 'm') : n_i0;
	if (args[0][0] == c_cc)
		ren_br();
	n_i0 = n_i;
	n_i = MAX(0, in);
	n_ti = -1;
}

void tr_ti(char **args)
{
	if (args[0][0] == c_cc)
		ren_br();
	if (args[1])
		n_ti = eval_re(args[1], n_i, 'm');
}

void tr_l2r(char **args)
{
	dir_do = 1;
	if (args[0][0] == c_cc)
		ren_br();
	n_td = 0;
	n_cd = 0;
}

void tr_r2l(char **args)
{
	dir_do = 1;
	if (args[0][0] == c_cc)
		ren_br();
	n_td = 1;
	n_cd = 1;
}

void tr_in2(char **args)
{
	int I = args[1] ? eval_re(args[1], n_I, 'm') : n_I0;
	if (args[0][0] == c_cc)
		ren_br();
	n_I0 = n_I;
	n_I = MAX(0, I);
	n_tI = -1;
}

void tr_ti2(char **args)
{
	if (args[0][0] == c_cc)
		ren_br();
	if (args[1])
		n_tI = eval_re(args[1], n_I, 'm');
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
	int pos;
	if (!args[2])
		return;
	pos = isdigit((unsigned char) args[1][0]) ? atoi(args[1]) : -1;
	if (dev_mnt(pos, args[2], args[3] ? args[3] : args[2]) < 0)
		errmsg("neatroff: failed to mount <%s>\n", args[2]);
}

void tr_nf(char **args)
{
	if (args[0][0] == c_cc)
		ren_br();
	n_u = 0;
}

void tr_fi(char **args)
{
	if (args[0][0] == c_cc)
		ren_br();
	n_u = 1;
}

void tr_ce(char **args)
{
	if (args[0][0] == c_cc)
		ren_br();
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
		wb_hmov(wb, font_swid(dev_font(n_f), n_s, n_ss));
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
	case 'j':
		wb_setcost(wb, eval(arg, 0));
		break;
	case 'k':
		num_set(map(arg), wb == cwb ? f_hpos() - n_lb : wb_wid(wb));
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
		if (wb == cwb)
			while (fmt_fillreq(cfmt))
				ren_fmtpop(cfmt);
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
	case 'Z':
		ren_zcmd(wb, arg);
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
	case '/':
		wb_italiccorrection(wb);
		break;
	case ',':
		wb_italiccorrectionleft(wb);
		break;
	case '<':
	case '>':
		n_cd = c == '<';
		wb_flushdir(wb);
		break;
	}
}

static void ren_field(struct wb *wb, int (*next)(void), void (*back)(int));
static void ren_tab(struct wb *wb, char *tc, int (*next)(void), void (*back)(int));

/* insert a character, escape sequence, field or etc into wb */
static void ren_put(struct wb *wb, char *c, int (*next)(void), void (*back)(int))
{
	int w;
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
		if (strchr(" bCcDdefHhjkLlmNoprSsuvXxZz0^|!{}&/,<>", c[1])) {
			char *arg = NULL;
			if (strchr(ESC_P, c[1]))
				arg = unquotednext(c[1], next, back);
			if (strchr(ESC_Q, c[1]))
				arg = quotednext(next, back);
			if (c[1] == 'e') {
				snprintf(c, GNLEN, "%c%c", c_ec, c_ec);
			} else if (c[1] == 'N') {
				snprintf(c, GNLEN, "GID=%s", arg);
			} else {
				ren_cmd(wb, c[1], arg);
				free(arg);
				return;
			}
			free(arg);
		}
	}
	if (ren_div) {
		wb_putraw(wb, c);
		return;
	}
	if (cdef_map(c, n_f))		/* .char characters */
		wb_putexpand(wb, c);
	else
		wb_put(wb, c);
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

/* like ren_char(); return 1 if d1 was read and 2 if d2 was read */
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
	wb_wconf(&wb, &n_ct, &n_st, &n_sb, &n_llx, &n_lly, &n_urx, &n_ury);
	wb_done(&wb);
	return n;
}

/* return 1 if d1 was read and 2 if d2 was read */
static int ren_until(struct wb *wb, int (*next)(void), void (*back)(int),
			char *d1, char *d2)
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

/* like ren_until(); map src to dst */
static int ren_untilmap(struct wb *wb, int (*next)(void), void (*back)(int),
			char *end, char *src, char *dst)
{
	int ret;
	while ((ret = ren_until(wb, next, back, src, end)) == 1) {
		sstr_push(dst);
		ren_until(wb, sstr_next, sstr_back, end, NULL);
		sstr_pop();
	}
	return 0;
}

static void wb_cpy(struct wb *dst, struct wb *src, int left)
{
	wb_hmov(dst, left - wb_wid(dst));
	wb_cat(dst, src);
}

void ren_tl(int (*next)(void), void (*back)(int))
{
	struct wb wb, wb2;
	char *pgnum;
	char delim[GNLEN];
	ren_first();
	pgnum = num_str(map("%"));
	wb_init(&wb);
	wb_init(&wb2);
	charnext(delim, next, back);
	if (!strcmp("\n", delim))
		back('\n');
	/* the left-adjusted string */
	ren_untilmap(&wb2, next, back, delim, c_pc, pgnum);
	wb_cpy(&wb, &wb2, 0);
	/* the centered string */
	ren_untilmap(&wb2, next, back, delim, c_pc, pgnum);
	wb_cpy(&wb, &wb2, (n_lt - wb_wid(&wb2)) / 2);
	/* the right-adjusted string */
	ren_untilmap(&wb2, next, back, delim, c_pc, pgnum);
	wb_cpy(&wb, &wb2, n_lt - wb_wid(&wb2));
	/* flushing the line */
	ren_line(wb_buf(&wb), wb_wid(&wb), AD_L, 0,
			0, 0, n_lt, wb.els_neg, wb.els_pos);
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
		if (ren_until(&wbs[n++], next, back, c_fb, c_fa) != 1)
			break;
	}
	left = wb == cwb ? f_hpos() : wb_wid(wb);
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
	int pos = wb == cwb ? f_hpos() : wb_wid(wb);
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

/* parse characters and troff requests of s and append them to wb */
int ren_parse(struct wb *wb, char *s)
{
	int c;
	odiv_beg();
	sstr_push(s);
	c = sstr_next();
	while (c >= 0) {
		sstr_back(c);
		if (ren_char(wb, sstr_next, sstr_back))
			break;
		c = sstr_next();
	}
	sstr_pop();
	odiv_end();
	return 0;
}

/* cause nested render_rec() to exit */
void tr_popren(char **args)
{
	ren_level = args[1] ? atoi(args[1]) : 0;
}

#define FMT_PAR()	(n_u && !n_na && !n_ce && (n_j & AD_P) == AD_P)

/* read characters from tr.c and pass the rendered lines to out.c */
static int render_rec(int level)
{
	int ren_div_saved = ren_div;
	int c;
	ren_div = 0;
	while (ren_level >= level) {
		while (!ren_un && !tr_nextreq())
			if (ren_level < level)
				break;
		if (ren_level < level)
			break;
		if (ren_aborted)
			return 1;
		c = ren_next();
		if (c < 0) {
			if (bp_final >= 2)
				break;
			if (bp_final == 0) {
				bp_final = 1;
				if (trap_em >= 0)
					trap_exec(trap_em);
			} else {
				bp_final = 2;
				ren_ejectpage(1);
			}
		}
		if (c >= 0)
			ren_partial = c != '\n';
		/* add cwb (the current word) to cfmt */
		if (c == ' ' || c == '\n') {
			if (!wb_part(cwb)) {	/* not after a \c */
				ren_fmtword(cwb);
				if (c == '\n')
					while (fmt_newline(cfmt))
						ren_fmtpop(cfmt);
				if (!FMT_PAR())
					ren_fmtpopall(cfmt);
				if (c == ' ')
					fmt_space(cfmt);
			}
		}
		/* flush the line if necessary */
		if (c == ' ' || c == '\n' || c < 0)
			ren_fmtpop(cfmt);
		if (c == '\n' || ren_nl)	/* end or start of input line */
			n_lb = f_hpos();
		if (c == '\n' && n_it && --n_itn == 0)
			trap_exec(n_it);
		if (c == '\n' && !wb_part(cwb))
			n_ce = MAX(0, n_ce - 1);
		if (c != ' ' && c >= 0) {
			ren_back(c);
			ren_char(cwb, ren_next, ren_back);
		}
		if (c >= 0)
			ren_nl = c == '\n';
	}
	/* restore ren_div after processing traps */
	ren_div = ren_div_saved;
	return 0;
}

/* render input words */
int render(void)
{
	n_nl = -1;
	while (!tr_nextreq())
		;
	ren_first();			/* transition to the first page */
	render_rec(0);
	bp_final = 3;
	if (fmt_morewords(cfmt))
		ren_page(1);
	ren_br();
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
	if (id < 0)		/* find an unused position in treg[] */
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
	if (id >= 0) {
		if (args[2])
			tpos[id] = eval(args[2], 'v');
		else
			treg[id] = -1;
	}
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
