#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define ALIGN(n, a)	(((n) + (a) - 1) & ~((a) - 1))
#define CNTMIN		(1 << 10)
#define CNTMAX		(1 << 20)

/* iset structure to map integers to sets */
struct iset {
	int **set;
	int *sz;
	int *len;
	int cnt;
};

static void iset_extend(struct iset *iset, int cnt)
{
	iset->set = mextend(iset->set, iset->cnt, cnt, sizeof(iset->set[0]));
	iset->sz = mextend(iset->sz, iset->cnt, cnt, sizeof(iset->sz[0]));
	iset->len = mextend(iset->len, iset->cnt, cnt, sizeof(iset->len[0]));
	iset->cnt = cnt;
}

struct iset *iset_make(void)
{
	struct iset *iset = xmalloc(sizeof(*iset));
	memset(iset, 0, sizeof(*iset));
	iset_extend(iset, CNTMIN);
	return iset;
}

void iset_free(struct iset *iset)
{
	int i;
	for (i = 0; i < iset->cnt; i++)
		free(iset->set[i]);
	free(iset->set);
	free(iset->len);
	free(iset->sz);
	free(iset);
}

int *iset_get(struct iset *iset, int key)
{
	return key >= 0 && key < iset->cnt ? iset->set[key] : NULL;
}

int iset_len(struct iset *iset, int key)
{
	return key >= 0 && key < iset->cnt ? iset->len[key] : 0;
}

void iset_put(struct iset *iset, int key, int ent)
{
	if (key < 0 || key >= CNTMAX)
		return;
	if (key >= iset->cnt)
		iset_extend(iset, ALIGN(key + 1, CNTMIN));
	if (key >= 0 && key < iset->cnt && iset->len[key] + 1 >= iset->sz[key]) {
		int olen = iset->sz[key];
		int nlen = olen ? olen * 2 : 8;
		void *nset = xmalloc(nlen * sizeof(iset->set[key][0]));
		if (iset->set[key]) {
			memcpy(nset, iset->set[key],
				olen * sizeof(iset->set[key][0]));
			free(iset->set[key]);
		}
		iset->sz[key] = nlen;
		iset->set[key] = nset;
	}
	iset->set[key][iset->len[key]++] = ent;
	iset->set[key][iset->len[key]] = -1;
}

/* check entry membership */
int iset_has(struct iset *iset, int key, int ent)
{
	int i;
	if (key < 0 || key >= iset->cnt || iset->len[key] == 0)
		return 0;
	for (i = 0; i < iset->len[key]; i++)
		if (iset->set[key][i] == ent)
			return 1;
	return 0;
}
