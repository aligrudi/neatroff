/* mapping register/macro names to indices */
#include <stdio.h>
#include <string.h>
#include "roff.h"

#define MAPBEG		256	/* the entries reserved for .x names */

/* register, macro, or environments names */
static char keys[NREGS][GNLEN];
static int nkeys = 1;
/* per starting character name lists */
static int key_head[256];
static int key_next[NREGS];

/* return the index of s in keys[]; insert it if not in keys[] */
static int key_get(char *s)
{
	int head = (unsigned char) s[0];
	int i = key_head[head];
	if (*s == '\0')
		return 0;
	while (i > 0) {
		if (keys[i][1] == s[1] && !strcmp(keys[i], s))
			return i;
		i = key_next[i];
	}
	if (nkeys >= NREGS - MAPBEG)
		errdie("neatroff: out of register names (NREGS)\n");
	i = nkeys++;
	strcpy(keys[i], s);
	key_next[i] = key_head[head];
	key_head[head] = i;
	return i;
}

/* map register names to [0..NREGS] */
int map(char *s)
{
	if (s[0] == '.' && s[1] && !s[2])	/* ".x" is mapped to 'x' */
		return (unsigned char) s[1];
	return MAPBEG + key_get(s);
}

/* return the name mapped to id; returns a static buffer */
char *map_name(int id)
{
	static char map_buf[NMLEN];
	if (id >= MAPBEG)
		return keys[id - MAPBEG];
	map_buf[0] = '.';
	map_buf[1] = id;
	map_buf[2] = '\0';
	return map_buf;
}
