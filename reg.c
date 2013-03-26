#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define NREGS		(1 << 16)
#define NENVS		(1 << 5)

struct env {
	int eregs[NENVS];	/* environment-specific number registers */
	struct adj *adj;	/* per environment line buffer */
};

static int nregs[NREGS];	/* global number registers */
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
	REG('.', 's'),
	REG('.', 'u'),
	REG('.', 'v'),
	REG(0, 'f'),
	REG(0, 's'),
};

/* return the address of a number register */
int *nreg(int id)
{
	if (eregs_idx[id])
		return &env->eregs[eregs_idx[id]];
	return &nregs[id];
}

/* the contents of a number register (returns a static buffer) */
char *num_get(int id)
{
	static char numbuf[128];
	sprintf(numbuf, "%d", *nreg(id));
	return numbuf;
}

void str_set(int id, char *s)
{
	int len = strlen(s) + 1;
	if (sregs[id])
		free(sregs[id]);
	sregs[id] = malloc(len);
	strcpy(sregs[id], s);
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
	env = &envs[id];
	if (!env->adj) {
		env->adj = adj_alloc();
		n_f = 1;
		n_i = 0;
		n_j = 1;
		n_l = SC_IN * 65 / 10;
		n_s = 10;
		n_u = 1;
		n_v = 12 * SC_PT;
		n_s0 = n_s;
		n_f0 = n_f;
	}
}

void env_init(void)
{
	int i;
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
