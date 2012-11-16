#include <stdlib.h>
#include "xroff.h"

#define NREGS		(1 << 16)

int nreg[NREGS];

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

void tr_nr(int argc, char **args)
{
	int id;
	if (argc < 3)
		return;
	id = N_ID(args[1][0], args[1][1]);
	nreg[id] = atoi(args[2]);
}
