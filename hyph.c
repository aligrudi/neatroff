/* hyphenation */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"
#include "hyen.h"

#define HYPATLEN	(NHYPHS * 16)	/* hyphenation pattern length */

static void hcode_strcpy(char *d, char *s, int *map, int dots);

/* the hyphenation dictionary (.hw) */

static char hwword[HYPATLEN];	/* buffer for .hw words */
static char hwhyph[HYPATLEN];	/* buffer for .hw hyphenations */
static int hwword_len;		/* used hwword[] length */
static struct dict hwdict;	/* map words to their index in hwoff[] */
static int hwoff[NHYPHS];	/* the offset of words in hwword[] */
static int hw_n;		/* the number of dictionary words */

/* insert word s into hwword[] and hwhyph[] */
static void hw_add(char *s)
{
	char *p = hwword + hwword_len;
	char *n = hwhyph + hwword_len;
	int len = strlen(s) + 1;
	int i = 0, c;
	if (hw_n == NHYPHS || hwword_len + len > sizeof(hwword))
		return;
	memset(n, 0, len);
	while ((c = (unsigned char) *s++)) {
		if (c == '-')
			n[i] = 1;
		else
			p[i++] = c;
	}
	p[i] = '\0';
	hwoff[hw_n] = hwword_len;
	dict_put(&hwdict, hwword + hwoff[hw_n], hw_n);
	hwword_len += i + 1;
	hw_n++;
}

static int hw_lookup(char *word, char *hyph)
{
	char word2[WORDLEN] = {0};
	char *hyph2;
	int map[WORDLEN] = {0};
	int i, j, idx = -1;
	hcode_strcpy(word2, word, map, 0);
	i = dict_prefix(&hwdict, word2, &idx);
	if (i < 0)
		return 1;
	hyph2 = hwhyph + hwoff[i];
	for (j = 0; word2[j]; j++)
		if (hyph2[j])
			hyph[map[j]] = hyph2[j];
	return 0;
}

void tr_hw(char **args)
{
	int i;
	for (i = 1; i < NARGS && args[i]; i++)
		hw_add(args[i]);
}

/* the tex hyphenation algorithm */

static int hyinit;		/* hyphenation data initialized */
static char hypats[HYPATLEN];	/* hyphenation patterns */
static char hynums[HYPATLEN];	/* hyphenation pattern numbers */
static int hypats_len;		/* used hypats[] and hynums[] length */
static struct dict hydict;	/* map patterns to their index in hyoff[] */
static int hyoff[NHYPHS];	/* the offset of this pattern in hypats[] */
static int hy_n;		/* the number of patterns */

/* find the patterns matching s and update hyphenation values in n */
static void hy_find(char *s, char *n)
{
	int plen;
	char *p, *np;
	int i, j;
	int idx = -1;
	while ((i = dict_prefix(&hydict, s, &idx)) >= 0) {
		p = hypats + hyoff[i];
		np = hynums + (p - hypats);
		plen = strlen(p) + 1;
		for (j = 0; j < plen; j++)
			if (n[j] < np[j])
				n[j] = np[j];
	}
}

/* mark the hyphenation points of word in hyph */
static void hy_dohyph(char *hyph, char *word, int flg)
{
	char n[WORDLEN] = {0};
	char w[WORDLEN] = {0};
	int c[WORDLEN];			/* start of the i-th character in w */
	int wmap[WORDLEN] = {0};	/* word[wmap[i]] is w[i] */
	int nc = 0;
	int i, wlen;
	hcode_strcpy(w, word, wmap, 1);
	wlen = strlen(w);
	for (i = 0; i < wlen - 1; i += utf8len((unsigned int) w[i]))
		c[nc++] = i;
	for (i = 0; i < nc - 1; i++)
		hy_find(w + c[i], n + c[i]);
	memset(hyph, 0, wlen * sizeof(hyph[0]));
	for (i = 3; i < nc - 2; i++)
		if (n[i] % 2 && w[c[i - 1]] != '.' && w[c[i - 2]] != '.' && w[c[i + 1]] != '.')
			hyph[wmap[c[i]]] = (~flg & HY_FINAL2 || w[c[i + 2]] != '.') &&
				(~flg & HY_FIRST2 || w[c[i - 3]] != '.');
}

/* insert pattern s into hypats[] and hynums[] */
static void hy_add(char *s)
{
	char *p = hypats + hypats_len;
	char *n = hynums + hypats_len;
	int len = strlen(s) + 1;
	int i = 0, c;
	if (hy_n >= NHYPHS || hypats_len + len >= sizeof(hypats))
		return;
	memset(n, 0, len);
	while ((c = (unsigned char) *s++)) {
		if (c >= '0' && c <= '9')
			n[i] = c - '0';
		else
			p[i++] = c;
	}
	p[i] = '\0';
	hyoff[hy_n] = hypats_len;
	dict_put(&hydict, hypats + hyoff[hy_n], hy_n);
	hypats_len += i + 1;
	hy_n++;
}

/* .hcode request */
static struct dict hcodedict;
static char hcodesrc[NHCODES][GNLEN];
static char hcodedst[NHCODES][GNLEN];
static int hcode_n;

/* replace the character in s after .hcode mapping; returns s's new length */
static int hcode_mapchar(char *s)
{
	int i = dict_get(&hcodedict, s);
	if (i >= 0)
		strcpy(s, hcodedst[i]);
	else if (isalpha((unsigned char) *s))
		*s = tolower(*s);
	return strlen(s);
}

/* copy s to d after .hcode mappings; s[map[j]] corresponds to d[j] */
static void hcode_strcpy(char *d, char *s, int *map, int dots)
{
	int di = 0, si = 0, len;
	if (dots)
		d[di++] = '.';
	while (s[si]) {
		len = utf8len((unsigned char) s[si]);
		map[di] = si;
		memcpy(d + di, s + si, len);
		si += len;
		di += hcode_mapchar(d + di);
	}
	if (dots)
		d[di++] = '.';
	d[di] = '\0';
}

void hcode_add(char *c1, char *c2)
{
	int i = dict_get(&hcodedict, c1);
	if (i >= 0) {
		strcpy(hcodedst[i], c2);
	} else if (hcode_n < NHCODES) {
		strcpy(hcodesrc[hcode_n], c1);
		strcpy(hcodedst[hcode_n], c2);
		dict_put(&hcodedict, hcodesrc[hcode_n], hcode_n);
		hcode_n++;
	}
}

void tr_hcode(char **args)
{
	char c1[GNLEN], c2[GNLEN];
	char *s = args[1];
	while (s && utf8read(&s, c1) && utf8read(&s, c2))
		hcode_add(c1, c2);
}

static void hyph_readpatterns(char *s)
{
	char word[WORDLEN];
	char *d;
	while (*s) {
		d = word;
		while (*s && !isspace((unsigned char) *s))
			*d++ = *s++;
		*d = '\0';
		hy_add(word);
		while (*s && isspace((unsigned char) *s))
			s++;
	}
}

static void hyph_readexceptions(char *s)
{
	char word[WORDLEN];
	char *d;
	while (*s) {
		d = word;
		while (*s && !isspace((unsigned char) *s))
			*d++ = *s++;
		*d = '\0';
		hw_add(word);
		while (*s && isspace((unsigned char) *s))
			s++;
	}
}

void hyphenate(char *hyph, char *word, int flg)
{
	if (!hyinit) {
		hyinit = 1;
		hyph_readpatterns(en_patterns);
		hyph_readexceptions(en_exceptions);
	}
	if (hw_lookup(word, hyph))
		hy_dohyph(hyph, word, flg);
}

void tr_hpfa(char **args)
{
	char tok[ILNLEN], c1[ILNLEN], c2[ILNLEN];
	FILE *filp;
	hyinit = 1;
	/* load english hyphenation patterns with no arguments */
	if (!args[1]) {
		hyph_readpatterns(en_patterns);
		hyph_readexceptions(en_exceptions);
	}
	/* reading patterns */
	if (args[1] && (filp = fopen(args[1], "r"))) {
		while (fscanf(filp, "%s", tok) == 1)
			if (strlen(tok) < WORDLEN)
				hy_add(tok);
		fclose(filp);
	}
	/* reading exceptions */
	if (args[2] && (filp = fopen(args[2], "r"))) {
		while (fscanf(filp, "%s", tok) == 1)
			if (strlen(tok) < WORDLEN)
				hw_add(tok);
		fclose(filp);
	}
	/* reading hcode mappings */
	if (args[3] && (filp = fopen(args[3], "r"))) {
		while (fscanf(filp, "%s", tok) == 1) {
			char *s = tok;
			if (utf8read(&s, c1) && utf8read(&s, c2))
				hcode_add(c2, c1);	/* inverting */
		}
		fclose(filp);
	}
}

void hyph_init(void)
{
	dict_init(&hwdict, NHYPHS, -1, 0, 1);
	dict_init(&hydict, NHYPHS, -1, 0, 1);
	dict_init(&hcodedict, NHYPHS, -1, 0, 1);
}

void tr_hpf(char **args)
{
	/* reseting the patterns */
	hypats_len = 0;
	hy_n = 0;
	dict_done(&hydict);
	/* reseting the dictionary */
	hwword_len = 0;
	hw_n = 0;
	dict_done(&hwdict);
	/* reseting hcode mappings */
	hcode_n = 0;
	dict_done(&hcodedict);
	/* reading */
	hyph_init();
	tr_hpfa(args);
}
