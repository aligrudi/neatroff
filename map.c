/* mapping register/macro names to indices */
#include <stdio.h>
#include <string.h>
#include "roff.h"

#define MAPBEG		256	/* the entries reserved for .x names */

/* register, macro, or environments names */
static struct dict *mapdict;

/* map register names to [0..NREGS] */
int map(char *s)
{
	int i;
	if (!s[0])
		return 0;
	if (s[0] == '.' && s[1] && !s[2])	/* ".x" is mapped to 'x' */
		return (unsigned char) s[1];
	if (!mapdict)
		mapdict = dict_make(-1, 1, 2);
	i = dict_idx(mapdict, s);
	if (i < 0) {
		dict_put(mapdict, s, 0);
		i = dict_idx(mapdict, s);
		if (MAPBEG + i >= NREGS)
			errdie("neatroff: increase NREGS\n");
	}
	return MAPBEG + i;
}

/* return the name mapped to id; returns a static buffer */
char *map_name(int id)
{
	static char map_buf[NMLEN];
	if (id >= MAPBEG)
		return dict_key(mapdict, id - MAPBEG);
	map_buf[0] = '.';
	map_buf[1] = id;
	map_buf[2] = '\0';
	return map_buf;
}

void map_done(void)
{
	if (mapdict)
		dict_free(mapdict);
}
