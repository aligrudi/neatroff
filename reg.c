#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xroff.h"

#define NREGS		(1 << 16)
#define NENVS		(1 << 5)

struct env {
	int eregs[NENVS];	/* environment-specific number registers */
	int tabs[NTABS];	/* tab stops */
	struct adj *adj;	/* per environment line buffer */
	char hc[GNLEN];		/* hyphenation character */
};

static int nregs[NREGS];	/* global number registers */
static int nregs_inc[NREGS];	/* number register auto-increment size */
static char *sregs[NREGS];	/* global string registers */
static void *sregs_dat[NREGS];	/* builtin function data */
static struct env envs[3];	/* environments */
static struct env *env;		/* current enviroment */
static int eregs_idx[NREGS];	/* register environment index in eregs[] */

static int eregs[] = {		/* environment-specific number registers */
	REG('.', 'f'),
	REG('.', 'i'),
	REG('.', 'j'),
	REG('.', 'l'),
	REG('.', 'L'),
	REG('.', 's'),
	REG('.', 'u'),
	REG('.', 'v'),
	REG(0, 'c'),
	REG(0, 'f'),
	REG(0, 'h'),
	REG(0, 'i'),
	REG(0, 'l'),
	REG(0, 'L'),
	REG(0, 'n'),
	REG(0, 's'),
	REG(0, 't'),
	REG(0, 'T'),
	REG(0, 'v'),
};

/* return the address of a number register */
int *nreg(int id)
{
	if (eregs_idx[id])
		return &env->eregs[eregs_idx[id]];
	return &nregs[id];
}

static void reg_name(char *s, int id)
{
	s[0] = (id >> 8) & 0xff;
	s[1] = id & 0xff;
	s[3] = '\0';
}

/* the contents of a number register (returns a static buffer) */
char *num_str(int id)
{
	static char numbuf[128];
	numbuf[0] = '\0';
	switch (id) {
	case REG('.', 'k'):
		sprintf(numbuf, "%d", f_hpos());
		break;
	case REG('.', 't'):
		sprintf(numbuf, "%d", f_nexttrap());
		break;
	case REG('.', 'z'):
		if (f_divreg() >= 0)
			reg_name(numbuf, f_divreg());
		break;
	case REG('.', 'F'):
		sprintf(numbuf, "%s", in_filename());
		break;
	case REG('.', '$'):
		sprintf(numbuf, "%d", in_nargs());
		break;
	case REG('y', 'r'):
		sprintf(numbuf, "%02d", nregs[id]);
		break;
	default:
		sprintf(numbuf, "%d", *nreg(id));
	}
	return numbuf;
}

void num_set(int id, int val)
{
	*nreg(id) = val;
}

void num_inc(int id, int val)
{
	nregs_inc[id] = val;
}

void num_del(int id)
{
	*nreg(id) = 0;
	nregs_inc[id] = 0;
}

int num_get(int id, int inc)
{
	if (inc)
		*nreg(id) += inc > 0 ? nregs_inc[id] : -nregs_inc[id];
	return *nreg(id);
}

void str_set(int id, char *s)
{
	int len = strlen(s) + 1;
	if (sregs[id])
		free(sregs[id]);
	sregs[id] = malloc(len);
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
	str_rm(dst);
	sregs[dst] = sregs[src];
	sregs_dat[dst] = sregs_dat[src];
	sregs[src] = NULL;
	sregs_dat[src] = NULL;
}

static void env_set(int id)
{
	int i;
	env = &envs[id];
	if (!env->adj) {
		env->adj = adj_alloc();
		n_f = 1;
		n_i = 0;
		n_j = AD_B;
		n_l = SC_IN * 65 / 10;
		n_L = 1;
		n_s = 10;
		n_u = 1;
		n_v = 12 * SC_PT;
		n_s0 = n_s;
		n_f0 = n_f;
		n_na = 0;
		n_lt = SC_IN * 65 / 10;
		n_hy = 1;
		strcpy(env->hc, "\\%");
		adj_ll(env->adj, n_l);
		adj_in(env->adj, n_i);
		for (i = 0; i < NTABS; i++)
			env->tabs[i] = i * SC_IN / 2;
	}
}

static void init_time(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	nregs[REG('d', 'w')] = tm->tm_wday + 1;
	nregs[REG('d', 'y')] = tm->tm_mday;
	nregs[REG('m', 'o')] = tm->tm_mon + 1;
	nregs[REG('y', 'r')] = tm->tm_year % 100;
}

void env_init(void)
{
	int i;
	init_time();
	for (i = 0; i < LEN(eregs); i++)
		eregs_idx[eregs[i]] = i + 1;
	env_set(0);
}

void env_free(void)
{
	int i;
	for (i = 0; i < LEN(envs); i++)
		if (envs[i].adj)
			adj_free(envs[i].adj);
}

static int oenv[NPREV];		/* environment stack */
static int nenv;

void tr_ev(char **args)
{
	int id = args[1] ? atoi(args[1]) : -1;
	if (id < 0 && nenv)
		id = oenv[--nenv];
	if (id >= LEN(envs) || id < 0)
		return;
	if (args[1] && env && nenv < NPREV)
		oenv[nenv++] = env - envs;
	env_set(id);
}

struct adj *env_adj(void)
{
	return env->adj;
}

char *env_hc(void)
{
	return env->hc;
}

/* saving and restoring registers around diverted lines */
struct odiv {
	int f, s, f0, s0;
};

static struct odiv odivs[NPREV];	/* state before diverted text */
static int nodivs;

/* begin outputting diverted line */
void odiv_beg(void)
{
	struct odiv *o = &odivs[nodivs++];
	o->f = n_f;
	o->s = n_s;
	o->f0 = n_f0;
	o->s0 = n_s0;
}

/* end outputting diverted line */
void odiv_end(void)
{
	struct odiv *o = &odivs[--nodivs];
	n_f = o->f;
	n_s = o->s;
	n_f0 = o->f0;
	n_s0 = o->s0;
}

void tr_ta(char **args)
{
	int i;
	for (i = 0; i < NARGS && args[i]; i++)
		env->tabs[i] = eval_re(args[i], i > 0 ? env->tabs[i - 1] : 0, 'm');
}

int tab_next(int pos)
{
	int i;
	for (i = 0; i < LEN(env->tabs); i++)
		if (env->tabs[i] > pos)
			return env->tabs[i];
	return pos;
}
