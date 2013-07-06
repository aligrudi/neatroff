#include <stdio.h>
#include <string.h>
#include "roff.h"

static char keys[NREGS][GNLEN];
static int nkeys;

/* map register names to [0..NREGS * 2) */
int map(char *s)
{
	int i;
	if (n_cp || !s[1] || !s[2])
		return REG(s[0], s[1]);
	for (i = 0; i < nkeys; i++)
		if (keys[i][0] == s[0] && !strcmp(keys[i], s))
			return NREGS + i;
	strcpy(keys[nkeys++], s);
	return NREGS + nkeys - 1;
}

/* returns a static buffer */
char *map_name(int id)
{
	static char map_buf[NMLEN];
	if (id >= NREGS)
		return keys[id - NREGS];
	map_buf[0] = (id >> 8) & 0xff;
	map_buf[1] = id & 0xff;
	map_buf[2] = '\0';
	return map_buf;
}
