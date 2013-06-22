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
	int prev_d;		/* previous \(.d value */
	int prev_h;		/* previous \(.h value */
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

static int bp_first = 1;	/* prior to the first page */
static int bp_next = 1;		/* next page number */
static int bp_count;		/* number of pages so far */
static int bp_ejected;		/* current ejected page */
static int bp_final;		/* 1: the final page, 2: the 2nd final page */

static int c_fa;		/* field delimiter */
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
		cdiv->reg = REG(args[1][0], args[1][1]);
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
	if (!force && bp_final)
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

static void down(int n)
{
	if (!ren_traps(n_d, n_d + (n ? n : n_v), 1)) {
		ren_sp(n, 0);
		ren_pagelimit(0);
	}
}

/* flush the given line and send it to out.c */
static void ren_line(char *s, int w, int ad, int ll, int li, int lt)
{
	int ljust = lt >= 0 ? lt : li;
	int llen = ll - ljust;
	n_n = w;
	if (ad == AD_C)
		ljust += llen > w ? (llen - w) / 2 : 0;
	if (ad == AD_R)
		ljust += llen - w;
	if (cdiv) {
		if (cdiv->dl < w)
			cdiv->dl = w;
		if (ljust)
			sbuf_printf(&cdiv->sbuf, "%ch'%du'", c_ec, ljust);
		sbuf_append(&cdiv->sbuf, s);
	} else {
		out("H%d\n", n_o + ljust);
		out("V%d\n", n_d);
		out_line(s);
	}
}

static void ren_transparent(char *s)
{
	if (cdiv)
		sbuf_printf(&cdiv->sbuf, "%s\n", s);
	else
		out("%s\n", s);
}

/* return 1 if triggered a trap */
static int ren_bradj(struct adj *adj, int fill, int ad)
{
	char cmd[16];
	struct sbuf sbuf;
	int ll, li, lt, els_neg, els_pos;
	int w, prev_d, lspc;
	ren_first();
	if (!adj_empty(adj, fill)) {
		sbuf_init(&sbuf);
		w = adj_fill(adj, ad == AD_B, fill, n_hy, &sbuf,
				&ll, &li, &lt, &els_neg, &els_pos);
		prev_d = n_d;
		if (els_neg)
			ren_sp(-els_neg, 1);
		if (!n_ns || !sbuf_empty(&sbuf) || els_neg || els_pos) {
			ren_sp(0, 0);
			ren_line(sbuf_buf(&sbuf), w, ad, ll, li, lt);
			n_ns = 0;
		}
		sbuf_done(&sbuf);
		if (els_pos)
			ren_sp(els_pos, 1);
		n_a = els_pos;
		lspc = MAX(1, n_L) * n_v - n_v;
		if (detect_traps(prev_d, n_d) || detect_pagelimit(lspc)) {
			sprintf(cmd, "%c&", c_ec);
			if (!ren_cnl)	/* prevent unwanted newlines */
				in_push(cmd, NULL);
			if (!ren_traps(prev_d, n_d, 0))
				ren_pagelimit(lspc);
			return 1;
		}
		if (lspc)
			down(lspc);
	}
	return 0;
}

/* return 1 if triggered a trap */
static int ren_br(int force)
{
	return ren_bradj(cadj, !force && !n_ce && n_u,
			n_ce ? AD_C : (n_u && !n_na && (n_j != AD_B || !force) ? n_j : AD_L));
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
		num_set(REG(args[1][0], args[1][1]), n_d);
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
	if (id == bp_ejected && !cdiv) {
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
	int fn = !s || !*s || !strcmp("P", s) ? n_f0 : dev_font(s);
	if (fn >= 0) {
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
		errmsg("troff: failed to mount %s\n", args[2]);
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
	if (args[1]) {
		c_fa = args[1][0];
		strcpy(c_fb, args[2] ? args[2] : " ");
	} else {
		c_fa = -1;
		c_fb[0] = '\0';
	}
}

static void escarg_ren(char *d, int cmd, int (*next)(void), void (*back)(int))
{
	char delim[GNLEN];
	int c;
	if (strchr(ESC_P, cmd)) {
		c = next();
		if (cmd == 's' && (c == '-' || c == '+')) {
			*d++ = c;
			c = next();
		}
		if (c == '(') {
			*d++ = next();
			*d++ = next();
		} else {
			*d++ = c;
			if (cmd == 's' && c >= '1' && c <= '3') {
				c = next();
				if (isdigit(c))
					*d++ = c;
				else
					back(c);
			}
		}
	}
	if (strchr(ESC_Q, cmd)) {
		schar_read(delim, next);
		while (schar_jump(delim, next, back)) {
			if ((c = next()) < 0)
				break;
			*d++ = c;
		}
	}
	*d = '\0';
}

static int nextchar(char *s, int (*next)(void))
{
	int c = next();
	int l = utf8len(c);
	int i;
	if (c < 0)
		return 0;
	s[0] = c;
	for (i = 1; i < l; i++)
		s[i] = next();
	s[l] = '\0';
	return l;
}

static void ren_cmd(struct wb *wb, int c, char *arg)
{
	struct glyph *g;
	switch (c) {
	case ' ':
		wb_hmov(wb, charwid(dev_spacewid(), n_s));
		break;
	case 'b':
		ren_bracket(wb, arg);
		break;
	case 'c':
		wb_setpart(wb);
		break;
	case 'D':
		ren_draw(wb, arg);
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
		num_set(REG(arg[0], arg[1]),
			RENWB(wb) ? f_hpos() - n_lb : wb_wid(wb));
		break;
	case 'L':
		ren_vline(wb, arg);
		break;
	case 'l':
		ren_hline(wb, arg);
		break;
	case 'o':
		ren_over(wb, arg);
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
		g = dev_glyph("0", n_f);
		wb_hmov(wb, charwid(g ? g->wid : SC_DW, n_s));
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

/* read one character and place it inside wb buffer */
void ren_char(struct wb *wb, int (*next)(void), void (*back)(int))
{
	char c[GNLEN * 4];
	char arg[ILNLEN];
	char *s;
	int w, n;
	nextchar(c, next);
	if (c[0] == ' ' || c[0] == '\n') {
		wb_put(wb, c);
		return;
	}
	if (c[0] == '\t' || c[0] == '') {
		n = RENWB(wb) ? f_hpos() : wb_wid(wb);
		wb_hmov(wb, tab_next(n) - n);
		return;
	}
	if (c[0] == c_fa) {
		ren_field(wb, next, back);
		return;
	}
	if (c[0] == c_ec) {
		nextchar(c + 1, next);
		if (c[1] == '(') {
			int l = nextchar(c + 2, next);
			l += nextchar(c + 2 + l, next);
			c[2 + l] = '\0';
		} else if (c[1] == 'z') {
			w = wb_wid(wb);
			ren_char(wb, next, back);
			wb_hmov(wb, w - wb_wid(wb));
			return;
		} else if (c[1] == '!') {
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
		} else if (strchr(" bCcDdfhkLloprsuvXxz0^|{}&", c[1])) {
			escarg_ren(arg, c[1], next, back);
			if (c[1] != 'C') {
				ren_cmd(wb, c[1], arg);
				return;
			}
			strcpy(c, arg);
		}
	}
	if (c[0] == c_ni)
		nextchar(c + 1, next);
	wb_put(wb, c);
}

/* read the argument of \w and push its width */
int ren_wid(int (*next)(void), void (*back)(int))
{
	char delim[GNLEN];
	int c, n;
	struct wb wb;
	wb_init(&wb);
	schar_read(delim, next);
	odiv_beg();
	c = next();
	while (c >= 0 && c != '\n') {
		back(c);
		if (!schar_jump(delim, next, back))
			break;
		ren_char(&wb, next, back);
		c = next();
	}
	odiv_end();
	n = wb_wid(&wb);
	wb_wconf(&wb, &n_ct, &n_st, &n_sb);
	wb_done(&wb);
	return n;
}

/* return 1 if the ending character (ec) was read */
static int ren_until(struct wb *wb, char *delim, int ec,
			int (*next)(void), void (*back)(int))
{
	int c;
	c = next();
	while (c >= 0 && c != '\n' && c != ec) {
		back(c);
		if (!schar_jump(delim, next, back))
			break;
		ren_char(wb, next, back);
		c = next();
	}
	if (c == '\n')
		back(c);
	return c == ec;
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
	schar_read(delim, next);
	/* the left-adjusted string */
	ren_until(&wb2, delim, '\n', next, back);
	wb_cpy(&wb, &wb2, 0);
	/* the centered string */
	ren_until(&wb2, delim, '\n', next, back);
	wb_cpy(&wb, &wb2, (n_lt - wb_wid(&wb2)) / 2);
	/* the right-adjusted string */
	ren_until(&wb2, delim, '\n', next, back);
	wb_cpy(&wb, &wb2, n_lt - wb_wid(&wb2));
	/* flushing the line */
	adj_ll(adj, n_lt);
	adj_wb(adj, &wb);
	adj_nl(adj);
	ren_bradj(adj, 0, AD_L);
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
		if (ren_until(&wbs[n++], c_fb, c_fa, next, back))
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

/* read characters from in.c and pass rendered lines to out.c */
void render(void)
{
	struct wb *wb = &ren_wb;
	int fillreq;
	int c;
	n_nl = -1;
	wb_init(wb);
	tr_first();
	ren_first();			/* transition to the first page */
	c = ren_next();
	while (1) {
		if (c < 0) {
			if (bp_final)
				break;
			bp_final = 1;
			push_eject();
			push_br();
			if (trap_em >= 0)
				trap_exec(trap_em);
			c = ren_next();
			continue;
		}
		ren_cnl = c == '\n';
		fillreq = 0;
		/* add wb (the current word) to cadj */
		if (c == ' ' || c == '\n') {
			adj_swid(cadj, charwid(dev_spacewid(), n_s));
			if (!wb_part(wb)) {	/* not after a \c */
				adj_wb(cadj, wb);
				fillreq = ren_fillreq;
				ren_fillreq = 0;
				if (c == '\n')
					adj_nl(cadj);
				else
					adj_sp(cadj);
			}
		}
		while ((fillreq && !n_ce && n_u) || adj_full(cadj, !n_ce && n_u)) {
			ren_br(0);
			fillreq = 0;
		}
		if (c == '\n' || ren_nl)	/* end or start of input line */
			n_lb = f_hpos();
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
	bp_final = 2;
	if (!adj_empty(cadj, 0))
		ren_page(bp_next, 1);
	ren_br(1);
	wb_done(wb);
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
	reg = REG(args[2][0], args[2][1]);
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
	reg = REG(args[1][0], args[1][1]);
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
		cdiv->treg = REG(args[2][0], args[2][1]);
	} else {
		cdiv->treg = -1;
	}
}

void tr_em(char **args)
{
	trap_em = args[1] ? REG(args[1][0], args[1][1]) : -1;
}

static int trap_pos(int pos)
{
	int ret = trap_first(pos);
	if (bp_final > 1)
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
