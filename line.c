#include <stdlib.h>
#include <string.h>
#include "xroff.h"

/* horizontal and vertical line characters */
static char *hs[] = {"_", "\\_", "\\-", "\\(ru", "\\(ul", "\\(rn", NULL};
static char *vs[] = {"\\(bv", "\\(br", "|", NULL};

static int cwid(char *c)
{
	struct glyph *g = dev_glyph(c, n_f);
	return charwid(g ? g->wid : SC_DW, n_s);
}

static int lchar(char *c, char **cs)
{
	while (*cs)
		if (!strcmp(*cs++, c))
			return 1;
	return 0;
}

static void vmov(struct adj *adj, int w)
{
	adj_put(adj, w, "\\v'%du'", w);
}

static void hmov(struct adj *adj, int w)
{
	adj_put(adj, w, "\\h'%du'", w);
}

void ren_hline(struct adj *adj, char *arg)
{
	char *lc = "\\(ru";
	int w, l, n, i, rem;
	l = eval_up(&arg, 0, 'm');
	if (!l)
		return;
	if (arg[0] == '\\' && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (*arg)
		lc = arg;
	w = cwid(lc);
	/* negative length; moving backwards */
	if (l < 0) {
		hmov(adj, l);
		l = -l;
	}
	n = l / w;
	rem = l % w;
	/* length less than character width */
	if (l < w) {
		n = 1;
		rem = 0;
		hmov(adj, -(w - l) / 2);
	}
	/* the initial gap */
	if (rem) {
		if (lchar(lc, hs)) {
			adj_put(adj, w, "%s", lc);
			hmov(adj, rem - w);
		} else {
			hmov(adj, rem);
		}
	}
	for (i = 0; i < n; i++)
		adj_put(adj, w, lc);
	/* moving back */
	if (l < w)
		hmov(adj, -(w - l + 1) / 2);
}

void ren_vline(struct adj *adj, char *arg)
{
	char *lc = "\\(br";
	int w, l, n, i, rem, hw, neg;
	l = eval_up(&arg, 0, 'm');
	if (!l)
		return;
	neg = l < 0;
	if (arg[0] == '\\' && arg[1] == '&')	/* \& can be used as a separator */
		arg += 2;
	if (*arg)
		lc = arg;
	w = n_s * SC_PT;	/* character height */
	hw = cwid(lc);		/* character width */
	/* negative length; moving backwards */
	if (l < 0) {
		vmov(adj, l);
		l = -l;
	}
	n = l / w;
	rem = l % w;
	/* length less than character width */
	if (l < w) {
		n = 1;
		rem = 0;
		vmov(adj, -w + l / 2);
	}
	/* the initial gap */
	if (rem) {
		if (lchar(lc, vs)) {
			vmov(adj, w);
			adj_put(adj, hw, "%s", lc);
			hmov(adj, -hw);
			vmov(adj, rem - w);
		} else {
			vmov(adj, rem);
		}
	}
	for (i = 0; i < n; i++) {
		vmov(adj, w);
		adj_put(adj, hw, lc);
		hmov(adj, -hw);
	}
	/* moving back */
	if (l < w)
		vmov(adj, l / 2);
	if (neg)
		vmov(adj, -l);
	hmov(adj, hw);
}
