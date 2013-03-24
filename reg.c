#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include "xroff.h"

#define NREGS		(1 << 16)

int nreg[NREGS];
char *sreg[NREGS];

int num_get(int id)
{
	return nreg[id];
}

int num_set(int id, int n)
{
	int o = nreg[id];
	nreg[id] = n;
	return o;
}

void tr_nr(char **args)
{
	int id;
	if (!args[2])
		return;
	id = REG(args[1][0], args[1][1]);
	nreg[id] = tr_int(args[2], nreg[id], 'u');
}

void str_set(int id, char *s)
{
	int len = strlen(s) + 1;
	if (sreg[id])
		free(sreg[id]);
	sreg[id] = malloc(len);
	strcpy(sreg[id], s);
}

char *str_get(int id)
{
	return sreg[id];
}

static struct env envs[3];
struct env *env;

void env_init(void)
{
	int i;
	for (i = 0; i < LEN(envs); i++) {
		struct env *e = &envs[i];
		e->f = 1;
		e->i = 0;
		e->j = 1;
		e->l = SC_IN * 65 / 10;
		e->s = 10;
		e->u = 1;
		e->v = 12 * SC_PT;
		e->s0 = e->s;
		e->f0 = e->f;
		e->adj = adj_alloc();
	}
	env = &envs[0];
}

void env_free(void)
{
	int i;
	for (i = 0; i < LEN(envs); i++)
		adj_free(envs[i].adj);
}

static int oenv[NPREV];
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
	env = &envs[id];
}
