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
