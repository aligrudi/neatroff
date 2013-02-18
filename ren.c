#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define ADJ_LL		(n_l - n_i)	/* effective line length */
#define ADJ_MODE	(n_u ? n_ad : ADJ_N)

/* diversion */
struct div {
	int f, s, f0, s0;		/* backup variables */
};

static struct adj *adj;			/* line buffer */
static int ren_backed = -1;		/* pushed back character */
static int ren_div;			/* current diversion */
static struct sbuf out_div;		/* current diversion output */
static struct div cur_div;

static int ren_next(void)
{
	int c = ren_backed >= 0 ? ren_backed : tr_next();
	ren_backed = -1;
	return c;
}

static void ren_back(int c)
{
	ren_backed = c;
}

static int nextchar(char *s)
{
	int c = ren_next();
	int l = utf8len(c);
	int i;
	if (c < 0)
		return 0;
	s[0] = c;
	for (i = 1; i < l; i++)
		s[i] = ren_next();
	s[l] = '\0';
	return l;
}

static void ren_ne(int n)
{
	if (n_nl + n > n_p && !ren_div)
		ren_page(n_pg + 1);
}

void tr_di(char **args)
{
	if (args[1]) {
		sbuf_init(&out_div);
		ren_div = REG(args[1][0], args[1][1]);
		n_d = 0;
	} else if (ren_div) {
		sbuf_putnl(&out_div);
		str_set(ren_div, sbuf_buf(&out_div));
		sbuf_done(&out_div);
		ren_div = 0;
	}
}

/* begin outputting diverted line */
static void div_beg(void)
{
	cur_div.f = n_f;
	cur_div.s = n_s;
	cur_div.f0 = n_f0;
	cur_div.s0 = n_s0;
}

/* end outputting diverted line */
static void div_end(void)
{
	n_f = cur_div.f;
	n_s = cur_div.s;
	n_f0 = cur_div.f0;
	n_s0 = cur_div.s0;
}

/* vertical motion before rendering lines */
static void down(int n)
{
	char cmd[32];
	n_d += n;
	if (ren_div) {
		sbuf_putnl(&out_div);
		sprintf(cmd, ".sp %du\n", n);
		sbuf_append(&out_div, cmd);
	} else {
		n_nl = n_d;
		if (n_nl <= n_p)
			OUT("v%d\n", n);
	}
	ren_ne(0);
}

static void out_line(char *out)
{
	char cmd[32];
	if (ren_div) {
		if (!sbuf_empty(&out_div))
			down(n_v);
		sprintf(cmd, "\\h'%d'", n_i);
		sbuf_append(&out_div, DIV_BEG);
		sbuf_append(&out_div, cmd);
		sbuf_append(&out_div, out);
		sbuf_append(&out_div, DIV_END);
	} else {
		down(n_v);
		OUT("H%d\n", n_o + n_i);
		output(out);
	}
}

static void ren_br(int sp, int force)
{
	char out[LNLEN];
	if (!adj_empty(adj, ADJ_MODE)) {
		adj_fill(adj, force ? ADJ_N : ADJ_MODE, ADJ_LL, out);
		ren_ne(n_v);
		out_line(out);
		ren_ne(n_v);
	}
	if (sp)
		down(sp);
}

void tr_br(char **args)
{
	ren_br(0, 1);
}

void tr_sp(char **args)
{
	int sp = 0;
	if (args[1])
		sp = tr_int(args[1], 0, 'v');
	ren_br(sp, 1);
}

void ren_page(int pg)
{
	n_nl = -1;
	n_d = 0;
	n_pg = pg;
	OUT("p%d\n", pg);
	OUT("V%d\n", 0);
}

void tr_bp(char **args)
{
	if (!ren_div) {
		ren_br(0, 1);
		ren_page(args[1] ? tr_int(args[1], n_pg, 'v') : n_pg + 1);
	}
}

static void ren_ps(char *s)
{
	int ps = !*s || !strcmp("0", s) ? n_s0 : tr_int(s, n_s, '\0');
	n_s0 = n_s;
	n_s = ps;
}

void tr_ps(char **args)
{
	if (args[1])
		ren_ps(args[1]);
}

void tr_in(char **args)
{
	ren_br(0, 1);
	if (args[1])
		n_i = tr_int(args[1], n_i, 'm');
}

static void ren_ft(char *s)
{
	int fn = !*s || !strcmp("P", s) ? n_f0 : dev_font(s);
	if (fn >= 0) {
		n_f0 = n_f;
		n_f = fn;
	}
}

void tr_ft(char **args)
{
	if (args[1])
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
	ren_br(0, 1);
	n_u = 0;
}

static void escarg_ren(char *d, int cmd)
{
	int c, q;
	if (strchr(ESC_P, cmd)) {
		c = ren_next();
		if (cmd == 's' && (c == '-' || c == '+')) {
			*d++ = c;
			c = ren_next();
		}
		if (c == '(') {
			*d++ = ren_next();
			*d++ = ren_next();
		} else {
			*d++ = c;
			if (cmd == 's' && c >= '1' && c <= '3') {
				c = ren_next();
				if (isdigit(c))
					*d++ = c;
				else
					ren_back(c);
			}
		}
	}
	if (strchr(ESC_Q, cmd)) {
		q = ren_next();
		while (1) {
			c = ren_next();
			if (c == q || c < 0)
				break;
			*d++ = c;
		}
	}
	if (cmd == 'z')
		*d++ = ren_next();
	*d = '\0';
}

void render(void)
{
	char c[GNLEN * 2];
	char arg[ILNLEN];
	struct glyph *g;
	int r_s = n_s;
	int r_f = n_f;
	int esc = 0;
	adj = adj_alloc();
	ren_br(0, 1);
	while (nextchar(c) > 0) {
		if (c[0] == ' ' || c[0] == '\n')
			adj_put(adj, charwid(dev_spacewid(), n_s), c);
		while (adj_full(adj, ADJ_MODE, ADJ_LL))
			ren_br(0, 0);
		if (c[0] == ' ' || c[0] == '\n')
			continue;
		esc = 0;
		if (c[0] == '\\') {
			esc = 1;
			nextchar(c);
			/* rendered lines inside diversions */
			if (c[0] == DIV_BEG[1]) {
				nextchar(c);
				if (c[0] == DIV_BEG[2])
					div_beg();
				if (c[0] == DIV_END[2])
					div_end();
				continue;
			}
			if (c[0] == '(') {
				int l = nextchar(c);
				l += nextchar(c + l);
				c[l] = '\0';
			} else if (strchr("sf", c[0])) {
				escarg_ren(arg, c[0]);
				if (c[0] == 'f')
					ren_ft(arg);
				if (c[0] == 's')
					ren_ps(arg);
				continue;
			}
		}
		if (r_s != n_s) {
			adj_swid(adj, charwid(dev_spacewid(), n_s));
			adj_put(adj, 0, "\\s(%02d", n_s);
			r_s = n_s;
		}
		if (r_f != n_f) {
			adj_put(adj, 0, "\\f(%02d", n_f);
			r_f = n_f;
		}
		if (utf8len(c[0]) == strlen(c))
			sprintf(arg, "%s%s", esc ? "\\" : "", c);
		else
			sprintf(arg, "\\(%s", c);
		g = dev_glyph(c, n_f);
		adj_put(adj, charwid(g ? g->wid : dev_spacewid(), n_s), arg);
	}
	ren_br(0, 1);
	adj_free(adj);
}
