#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define SZEXT	1024

struct dict {
	int *head;
	int *next;
	char **key;
	int *val;
	int size;
	int n;
	int tabsz;		/* hash table size */
	int dupkeys;		/* duplicate keys if set */
};

static void dict_extend(struct dict *d, int size)
{
	d->next = mextend(d->next, d->size, size, sizeof(d->next[0]));
	d->key = mextend(d->key, d->size, size, sizeof(d->key[0]));
	d->val = mextend(d->val, d->size, size, sizeof(d->val[0]));
	d->size = size;
}

struct dict *dict_make(int tabsz, int dupkeys)
{
	struct dict *d = malloc(sizeof(*d));
	memset(d, 0, sizeof(*d));
	d->n = 1;
	d->head = calloc(tabsz, sizeof(d->head[0]));
	d->tabsz = tabsz;
	d->dupkeys = dupkeys;
	dict_extend(d, 1024);
	return d;
}

void dict_free(struct dict *d)
{
	int i;
	if (d->dupkeys) {
		for (i = 0; i < d->n; i++)
			free(d->key[i]);
	}
	free(d->head);
	free(d->next);
	free(d->val);
	free(d->key);
	free(d);
}

static int dict_hash(struct dict *d, char *key)
{
	unsigned hash = (unsigned char) *key++;
	while (*key)
		hash = (hash << 5) + hash + (unsigned char) *key++;
	return hash % d->tabsz;
}

void dict_put(struct dict *d, char *key, int val)
{
	int idx;
	int hash = dict_hash(d, key);
	if (d->n >= d->size)
		dict_extend(d, d->n + SZEXT);
	if (d->dupkeys) {
		int len = strlen(key) + 1;
		char *dup = malloc(len);
		memcpy(dup, key, len);
		key = dup;
	}
	idx = d->n++;
	d->key[idx] = key;
	d->val[idx] = val;
	d->next[idx] = d->head[hash];
	d->head[hash] = idx;
}

/* return the index of key in d */
int dict_idx(struct dict *d, char *key)
{
	int idx = d->head[dict_hash(d, key)];
	while (idx > 0 && strcmp(key, d->key[idx]))
		idx = d->next[idx];
	return idx > 0 ? idx : -1;
}

char *dict_key(struct dict *d, int idx)
{
	return d->key[idx];
}

int dict_get(struct dict *d, char *key)
{
	int idx = dict_idx(d, key);
	return idx >= 0 ? d->val[idx] : -1;
}

/* for finding entries with the given prefix */
struct pref {
	struct iset *map;
	char **key;
	int *val;
	int size;
	int n;
};

static void pref_extend(struct pref *d, int size)
{
	d->key = mextend(d->key, d->size, size, sizeof(d->key[0]));
	d->val = mextend(d->val, d->size, size, sizeof(d->val[0]));
	d->size = size;
}

struct pref *pref_make(void)
{
	struct pref *d = malloc(sizeof(*d));
	memset(d, 0, sizeof(*d));
	d->n = 1;
	d->map = iset_make();
	pref_extend(d, SZEXT);
	return d;
}

void pref_free(struct pref *d)
{
	free(d->val);
	free(d->key);
	iset_free(d->map);
	free(d);
}

static int pref_hash(struct pref *d, char *key)
{
	return ((((unsigned char) key[0]) << 5) | (unsigned char) key[1]) % 0x3ff;
}

void pref_put(struct pref *d, char *key, int val)
{
	int idx;
	if (d->n >= d->size)
		pref_extend(d, d->n + SZEXT);
	idx = d->n++;
	d->key[idx] = key;
	d->val[idx] = val;
	iset_put(d->map, pref_hash(d, key), idx);
}

/* match a prefix of key; in the first call, *pos should be -1 */
int pref_prefix(struct pref *d, char *key, int *pos)
{
	int *r = iset_get(d->map, pref_hash(d, key));
	while (r && r[++*pos] >= 0) {
		int idx = r[*pos];
		int plen = strlen(d->key[idx]);
		if (!strncmp(d->key[idx], key, plen))
			return d->val[idx];
	}
	return -1;
}
