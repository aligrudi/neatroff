#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define DHASH(d, s)	((d)->level2 ? (((unsigned char) (s)[1]) << 8) | (unsigned char) (s)[0] : (unsigned char) s[0])

/*
 * initialise a dictionary
 *
 * notfound: the value returned for missing keys.
 * dupkeys: if nonzero, store a copy of keys inserted via dict_put().
 * level2: use two characters for hasing
 */
void dict_init(struct dict *d, int size, long notfound, int dupkeys, int level2)
{
	int headsize = (level2 ? 256 * 256 : 256) * sizeof(d->head[0]);
	memset(d, 0, sizeof(*d));
	d->size = size;
	d->n = 1;
	d->level2 = level2;
	d->notfound = notfound;
	d->key = xmalloc(size * sizeof(d->key[0]));
	d->val = xmalloc(size * sizeof(d->val[0]));
	d->next = xmalloc(size * sizeof(d->next[0]));
	d->head = xmalloc(headsize);
	memset(d->head, 0, headsize);
	if (dupkeys)
		d->buf = xmalloc(size * NMLEN);
}

void dict_done(struct dict *d)
{
	free(d->val);
	free(d->key);
	free(d->next);
	free(d->buf);
	free(d->head);
}

void dict_put(struct dict *d, char *key, long val)
{
	int idx;
	if (d->n >= d->size)
		return;
	if (d->buf) {
		int len = strlen(key) + 1;
		if (d->buflen + len >= d->size * NMLEN)
			return;
		memcpy(d->buf + d->buflen, key, len);
		key = d->buf + d->buflen;
		d->buflen += len;
	}
	idx = d->n++;
	d->key[idx] = key;
	d->val[idx] = val;
	d->next[idx] = d->head[DHASH(d, key)];
	d->head[DHASH(d, key)] = idx;
}

/* return the index of key in d */
int dict_idx(struct dict *d, char *key)
{
	int idx = d->head[DHASH(d, key)];
	while (idx > 0) {
		if (!strcmp(d->key[idx], key))
			return idx;
		idx = d->next[idx];
	}
	return -1;
}

char *dict_key(struct dict *d, int idx)
{
	return d->key[idx];
}

long dict_val(struct dict *d, int idx)
{
	return d->val[idx];
}

long dict_get(struct dict *d, char *key)
{
	int idx = dict_idx(d, key);
	return idx >= 0 ? d->val[idx] : d->notfound;
}

/* match a prefix of key; in the first call, *idx should be -1 */
long dict_prefix(struct dict *d, char *key, int *idx)
{
	int plen;
	if (!*idx)
		return d->notfound;
	if (*idx < 0)
		*idx = d->head[DHASH(d, key)];
	else
		*idx = d->next[*idx];
	while (*idx > 0) {
		plen = strlen(d->key[*idx]);
		if (!strncmp(d->key[*idx], key, plen))
			return d->val[*idx];
		*idx = d->next[*idx];
	}
	return d->notfound;
}
