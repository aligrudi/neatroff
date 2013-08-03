#include <stdio.h>
#include <string.h>
#include "roff.h"

/* register, macro, or environments names with more than two characters */
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
	while (i > 0) {
		if (!strcmp(keys[i], s))
			return i;
		i = key_next[i];
	}
	i = nkeys++;
	strcpy(keys[i], s);
	key_next[i] = key_head[head];
	key_head[head] = i;
	return i;
}

/* map register names to [0..NREGS * 2) */
int map(char *s)
{
	if (n_cp || !s[1] || !s[2])
		return REG(s[0], s[1]);
	return NREGS + key_get(s);
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
