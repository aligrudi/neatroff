#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define CNTMIN		(1 << 10)

struct dict {
	struct iset *map;
	char **key;
	int *val;
	int size;
	int n;
	int notfound;		/* the value returned for missing keys */
	int hashlen;		/* the number of characters used for hashing */
	int dupkeys;		/* duplicate keys if set */
};

static void dict_extend(struct dict *d, int size)
{
	d->key = mextend(d->key, d->size, size, sizeof(d->key[0]));
	d->val = mextend(d->val, d->size, size, sizeof(d->val[0]));
	d->size = size;
}

/*
 * initialise a dictionary
 *
 * notfound: the value returned for missing keys.
 * dupkeys: if nonzero, store a copy of keys inserted via dict_put().
 * hashlen: the number of characters used for hashing
 */
struct dict *dict_make(int notfound, int dupkeys, int hashlen)
{
	struct dict *d = xmalloc(sizeof(*d));
	memset(d, 0, sizeof(*d));
	d->n = 1;
	d->hashlen = hashlen ? hashlen : 32;
	d->dupkeys = dupkeys;
	d->notfound = notfound;
	d->map = iset_make();
	dict_extend(d, CNTMIN);
	return d;
}

void dict_free(struct dict *d)
{
	int i;
	if (d->dupkeys)
		for (i = 0; i < d->size; i++)
			free(d->key[i]);
	free(d->val);
	free(d->key);
	iset_free(d->map);
	free(d);
}

static int dict_hash(struct dict *d, char *key)
{
	unsigned long hash = (unsigned char) *key++;
	int i = d->hashlen;
	while (--i > 0 && *key)
		hash = (hash << 5) + hash + (unsigned char) *key++;
	return hash & 0x3ff;
}

void dict_put(struct dict *d, char *key, int val)
{
	int idx;
	if (d->n >= d->size)
		dict_extend(d, d->n + CNTMIN);
	if (d->dupkeys) {
		int len = strlen(key) + 1;
		char *dup = xmalloc(len);
		memcpy(dup, key, len);
		key = dup;
	}
	idx = d->n++;
	d->key[idx] = key;
	d->val[idx] = val;
	iset_put(d->map, dict_hash(d, key), idx);
}

/* return the index of key in d */
int dict_idx(struct dict *d, char *key)
{
	int h = dict_hash(d, key);
	int *b = iset_get(d->map, h);
	int *r = b + iset_len(d->map, h);
	while (b && --r >= b)
		if (!strcmp(d->key[*r], key))
			return *r;
	return -1;
}

char *dict_key(struct dict *d, int idx)
{
	return d->key[idx];
}

int dict_val(struct dict *d, int idx)
{
	return d->val[idx];
}

int dict_get(struct dict *d, char *key)
{
	int idx = dict_idx(d, key);
	return idx >= 0 ? d->val[idx] : d->notfound;
}

/* match a prefix of key; in the first call, *idx should be -1 */
int dict_prefix(struct dict *d, char *key, int *pos)
{
	int *r = iset_get(d->map, dict_hash(d, key));
	while (r && r[++*pos] >= 0) {
		int idx = r[*pos];
		int plen = strlen(d->key[idx]);
		if (!strncmp(d->key[idx], key, plen))
			return d->val[idx];
	}
	return d->notfound;
}
