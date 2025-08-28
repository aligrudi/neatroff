/* registers and environments */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "roff.h"

#define NENVS		64	/* number of environment registers */

struct env {
	int eregs[NENVS];	/* environment-specific number registers */
	int tabs[NTABS];	/* tab stops */
	char tabs_type[NTABS];	/* type of tabs: L, C, R */
	struct fmt *fmt;	/* per environment line formatting buffer */
	struct wb wb;		/* per environment partial word */
	char tc[GNLEN];		/* tab character (.tc) */
	char lc[GNLEN];		/* leader character (.lc) */
	char hc[GNLEN];		/* hyphenation character (.hc) */
	char mc[GNLEN];		/* margin character (.mc) */
};

static int nregs[NREGS];	/* global number registers */
static int nregs_inc[NREGS];	/* number register auto-increment size */
static int nregs_fmt[NREGS];	/* number register format */
static char *sregs[NREGS];	/* global string registers */
static void *sregs_dat[NREGS];	/* builtin function data */
static struct env *envs[NREGS];	/* environments */
static struct env *env;		/* current environment */
static int env_id;		/* current environment id */
static int eregs_idx[NREGS];	/* register environment index in eregs[] */

static char *eregs[] = {	/* environment-specific number registers */
	"ln", ".f", ".i", ".j", ".l",
	".L", ".nI", ".nm", ".nM", ".nn",
	".nS", ".m", ".s", ".u", ".v",
	".it", ".itn", ".mc", ".mcn",
	".ce", ".na", ".f0", ".i0", ".l0",
	".hy", ".hycost", ".hycost2", ".hycost3", ".hlm",
	".L0", ".m0", ".n0", ".s0", ".ss", ".ssh", ".sss", ".pmll", ".pmllcost",
	".ti", ".lt", ".lt0", ".v0",
	".I", ".I0", ".tI", ".td", ".cd",
};

/* return the address of a number register */
int *nreg(int id)
{
	if (eregs_idx[id])
		return &env->eregs[eregs_idx[id]];
	return &nregs[id];
}

static char *directory(char *path)
{
	static char dst[PATHLEN];
	char *s = strrchr(path, '/');
	if (!s)
		return ".";
	if (path == s)
		return "/";
	memcpy(dst, path, s - path);
	dst[s - path] = '\0';
	return dst;
}

static char *num_tabs(void)
{
	static char tabs[16 * NTABS];
	int i;
	char *s = tabs;
	for (i = 0; i < NTABS && env->tabs_type[i]; i++)
		s += sprintf(s, "%du%c ", env->tabs[i], env->tabs_type[i]);
	return tabs;
}

static int num_fmt(char *s, int n, int fmt);

/* the contents of a number register (returns a static buffer) */
char *num_str(int id)
{
	static char numbuf[128];
	char *s = map_name(id);
	if (!nregs_fmt[id])
		nregs_fmt[id] = '0';
	numbuf[0] = '\0';
	if (s[0] == '.' && !s[2]) {
		switch (s[1]) {
		case 'b':
			sprintf(numbuf, "%d", font_getbd(dev_font(n_f)));
			return numbuf;
		case 'c':
			sprintf(numbuf, "%d", in_lnum());
			return numbuf;
		case 'k':
			sprintf(numbuf, "%d", f_hpos());
			return numbuf;
		case 'm':
			sprintf(numbuf, "#%02x%02x%02x",
				CLR_R(n_m), CLR_G(n_m), CLR_B(n_m));
			return numbuf;
		case 't':
			sprintf(numbuf, "%d", f_nexttrap());
			return numbuf;
		case 'z':
			if (f_divreg() >= 0)
				snprintf(numbuf, sizeof(numbuf), "%s", map_name(f_divreg()));
			return numbuf;
		case 'F':
			snprintf(numbuf, sizeof(numbuf), "%s", in_filename());
			return numbuf;
		case 'D':
			snprintf(numbuf, sizeof(numbuf), "%s", directory(in_filename()));
			return numbuf;
		case '$':
			sprintf(numbuf, "%d", in_nargs() - 1);
			return numbuf;
		}
	}
	if (s[0] == '.' && !strcmp(".neat", s))
		return "1";
	if (s[0] == '.' && s[1] == 'e' && s[2] == 'v' && !s[3])
		return map_name(env_id);
	if (s[0] == '$' && s[1] == '$' && !s[2]) {
		sprintf(numbuf, "%d", getpid());
		return numbuf;
	}
	if (s[0] == 'y' && s[1] == 'r' && !s[2]) {
		sprintf(numbuf, "%02d", *nreg(id));
		return numbuf;
	}
	if (s[0] == '.' && !strcmp(".tabs", s))
		return num_tabs();
	if (!nregs_fmt[id] || num_fmt(numbuf, *nreg(id), nregs_fmt[id]))
		sprintf(numbuf, "%d", *nreg(id));
	return numbuf;
}

void num_set(int id, int val)
{
	if (!nregs_fmt[id])
		nregs_fmt[id] = '0';
	*nreg(id) = val;
}

void num_setinc(int id, int val)
{
	nregs_inc[id] = val;
}

void num_inc(int id, int pos)
{
	*nreg(id) += pos > 0 ? nregs_inc[id] : -nregs_inc[id];
}

void num_del(int id)
{
	*nreg(id) = 0;
	nregs_inc[id] = 0;
	nregs_fmt[id] = 0;
}

void str_set(int id, char *s)
{
	int len = strlen(s) + 1;
	if (sregs[id])
		free(sregs[id]);
	sregs[id] = xmalloc(len);
	memcpy(sregs[id], s, len);
	sregs_dat[id] = NULL;
}

char *str_get(int id)
{
	return sregs[id];
}

void *str_dget(int id)
{
	return sregs_dat[id];
}

void str_dset(int id, void *d)
{
	sregs_dat[id] = d;
}

void str_rm(int id)
{
	if (sregs[id])
		free(sregs[id]);
	sregs[id] = NULL;
	sregs_dat[id] = NULL;
}

void str_rn(int src, int dst)
{
	if (!sregs[src] && !sregs_dat[src])
		return;
	str_rm(dst);
	sregs[dst] = sregs[src];
	sregs_dat[dst] = sregs_dat[src];
	sregs[src] = NULL;
	sregs_dat[src] = NULL;
}

static struct env *env_alloc(void)
{
	struct env *env = xmalloc(sizeof(*env));
	memset(env, 0, sizeof(*env));
	wb_init(&env->wb);
	env->fmt = fmt_alloc();
	return env;
}

static void env_free(struct env *env)
{
	fmt_free(env->fmt);
	wb_done(&env->wb);
	free(env);
}

static void env_set(int id)
{
	int i;
	env = envs[id];
	env_id = id;
	if (!env) {
		envs[id] = env_alloc();
		env = envs[id];
		n_f = 1;
		n_i = 0;
		n_I = 0;
		n_j = AD_B;
		n_l = SC_IN * 65 / 10;
		n_L = 1;
		n_s = 10;
		n_u = 1;
		n_v = 12 * SC_PT;
		n_s0 = n_s;
		n_v0 = n_v;
		n_f0 = n_f;
		n_l0 = n_l;
		n_L0 = n_L;
		n_na = 0;
		n_lt = SC_IN * 65 / 10;
		n_hy = 1;
		n_ss = 12;
		n_sss = 12;
		n_nM = 1;
		n_nS = 1;
		strcpy(env->hc, "\\%");
		strcpy(env->lc, ".");
		for (i = 0; i < NTABS; i++) {
			env->tabs[i] = i * SC_IN / 2;
			env->tabs_type[i] = 'L';
		}
	}
}

static void init_time(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	num_set(map("dw"), tm->tm_wday + 1);
	num_set(map("dy"), tm->tm_mday);
	num_set(map("mo"), tm->tm_mon + 1);
	num_set(map("yr"), tm->tm_year % 100);
	num_set(map(".yr"), 1900 + tm->tm_year);
}

static void init_globals(void)
{
	n_o = SC_IN;
	n_o0 = n_o;
	n_p = SC_IN * 11;
	n_lg = 1;
	n_kn = 1;
	num_set(map(".H"), 1);
	num_set(map(".V"), 1);
}

void env_init(void)
{
	int i;
	init_time();
	init_globals();
	for (i = 0; i < LEN(eregs); i++)
		eregs_idx[map(eregs[i])] = i + 1;
	env_set(map("0"));
}

void env_done(void)
{
	int i;
	for (i = 0; i < LEN(envs); i++)
		if (envs[i])
			env_free(envs[i]);
	for (i = 0; i < LEN(sregs); i++)
		free(sregs[i]);
}

static int oenv[NPREV];		/* environment stack */
static int nenv;

void tr_ev(char **args)
{
	int id = -1;
	if (args[1])
		id = map(args[1]);
	else
		id = nenv ? oenv[--nenv] : -1;
	if (id < 0)
		return;
	if (args[1] && env && nenv < NPREV)
		oenv[nenv++] = env_id;
	env_set(id);
}

void tr_evc(char **args)
{
	struct env *src;
	int id = -1;
	if (args[1])
		id = map(args[1]);
	if (id < 0 || !envs[id] || id == env_id)
		return;
	src = envs[id];
	memcpy(env->eregs, src->eregs, sizeof(env->eregs));
	memcpy(env->tabs, src->tabs, sizeof(env->tabs));
	memcpy(env->tabs_type, src->tabs_type, sizeof(env->tabs_type));
	memcpy(env->tc, src->tc, sizeof(env->tc));
	memcpy(env->lc, src->lc, sizeof(env->lc));
	memcpy(env->hc, src->hc, sizeof(env->hc));
	memcpy(env->mc, src->mc, sizeof(env->mc));
}

struct fmt *env_fmt(void)
{
	return env->fmt;
}

struct wb *env_wb(void)
{
	return &env->wb;
}

char *env_hc(void)
{
	return env->hc;
}

char *env_mc(void)
{
	return env->mc;
}

char *env_tc(void)
{
	return env->tc;
}

char *env_lc(void)
{
	return env->lc;
}

/* saving and restoring registers around diverted lines */
struct odiv {
	int f, s, m, f0, s0, m0, cd;
};

static struct odiv odivs[NPREV];	/* state before diverted text */
static int nodivs;

/* begin outputting diverted line */
void odiv_beg(void)
{
	struct odiv *o = &odivs[nodivs++];
	o->f = n_f;
	o->s = n_s;
	o->m = n_m;
	o->f0 = n_f0;
	o->s0 = n_s0;
	o->m0 = n_m0;
	o->cd = n_cd;
}

/* end outputting diverted line */
void odiv_end(void)
{
	struct odiv *o = &odivs[--nodivs];
	n_f = o->f;
	n_s = o->s;
	n_m = o->m;
	n_f0 = o->f0;
	n_s0 = o->s0;
	n_m0 = o->m0;
	n_cd = o->cd;
}

void tr_ta(char **args)
{
	int i;
	int c;
	for (i = 0; i < NTABS; i++) {
		if (i + 1 < NARGS && args[i + 1]) {
			char *a = args[i + 1];
			env->tabs[i] = eval_re(a, i > 0 ? env->tabs[i - 1] : 0, 'm');
			c = a[0] ? (unsigned char) strchr(a, '\0')[-1] : 0;
			env->tabs_type[i] = strchr("LRC", c) ? c : 'L';
		} else {
			env->tabs[i] = 0;
			env->tabs_type[i] = 0;
		}
	}
}

static int tab_idx(int pos)
{
	int i;
	for (i = 0; i < LEN(env->tabs); i++)
		if (env->tabs[i] > pos)
			return i;
	return -1;
}

int tab_next(int pos)
{
	int i = tab_idx(pos);
	return i >= 0 ? env->tabs[i] : pos;
}

int tab_type(int pos)
{
	int i = tab_idx(pos);
	return i >= 0 && env->tabs_type[i] ? env->tabs_type[i] : 'L';
}

/* number register format (.af) */
#define NF_LSH		8		/* number format length shifts */
#define NF_FMT		0x00ff		/* number format mask */

/* the format of a number register (returns a static buffer) */
char *num_getfmt(int id)
{
	static char fmtbuf[128];
	char *s = fmtbuf;
	int fmt = nregs_fmt[id] & NF_FMT;
	int i;
	if (fmt == '0' || fmt == 'x' || fmt == 'X') {
		i = nregs_fmt[id] >> NF_LSH;
		while (i-- > 1)
			*s++ = '0';
		*s++ = fmt;
	} else if (nregs_fmt[id]) {
		*s++ = fmt;
	}
	*s = '\0';
	return fmtbuf;
}

void num_setfmt(int id, char *s)
{
	int i = 0;
	if (strchr("iIaA", s[0])) {
		nregs_fmt[id] = s[0];
	} else {
		while (isdigit((unsigned char) s[i]))
			i++;
		if (s[i] == 'x' || s[i] == 'X')
			nregs_fmt[id] = s[i] | ((i + 1) << NF_LSH);
		else
			nregs_fmt[id] = '0' | (i << NF_LSH);
	}
}

static void nf_reverse(char *s)
{
	char r[128];
	int i, l;
	strcpy(r, s);
	l = strlen(r);
	for (i = 0; i < l; i++)
		s[i] = r[l - i - 1];
}

static void nf_roman(char *s, int n, char *I, char *V)
{
	int i;
	if (!n)
		return;
	if (n % 5 == 4) {
		*s++ = n % 10 == 9 ? I[1] : V[0];
		*s++ = I[0];
	} else {
		for (i = 0; i < n % 5; i++)
			*s++ = I[0];
		if (n % 10 >= 5)
			*s++ = V[0];
	}
	*s = '\0';
	nf_roman(s, n / 10, I + 1, V + 1);
}

static void nf_alpha(char *s, int n, int a)
{
	while (n) {
		*s++ = a + ((n - 1) % 26);
		n = (n - 1) / 26;
	}
	*s = '\0';
}

/* returns nonzero on failure */
static int num_fmt(char *s, int n, int fmt)
{
	int type = fmt & NF_FMT;
	if (n < 0) {
		n = -n;
		*s++ = '-';
	}
	if ((type == 'i' || type == 'I') && n > 0 && n < 40000) {
		if (type == 'i')
			nf_roman(s, n, "ixcmz", "vldw");
		else
			nf_roman(s, n, "IXCMZ", "VLDW");
		nf_reverse(s);
		return 0;
	}
	if ((type == 'a' || type == 'A') && n > 0) {
		nf_alpha(s, n, type);
		nf_reverse(s);
		return 0;
	}
	if (type == '0' || type == 'x' || type == 'X') {
		char pat[16];
		sprintf(pat, "%%0%d%c", fmt >> NF_LSH, type == '0' ? 'd' : type);
		sprintf(s, pat, n);
		return 0;
	}
	return 1;
}
