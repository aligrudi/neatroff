/* hyphenation */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"
#include "hyen.h"

#define HYPATLEN	(NHYPHS * 16)	/* hyphenation pattern length */

/* the hyphenation dictionary (.hw) */

static char hwword[HYPATLEN];	/* buffer for .hw words */
static char hwhyph[HYPATLEN];	/* buffer for .hw hyphenations */
static int hwword_len;		/* used hwword[] length */
static struct dict hwdict;	/* map words to their index in hwoff[] */
static int hwoff[NHYPHS];	/* the offset of this word in hwword[] */
static int hwlen[NHYPHS];	/* the length of the word */
static int hw_n;		/* number of words in hw_*[] lists */

/* functions for the hyphenation dictionary */

static void hw_add(char *word)
{
	char *s = word;
	char *d = hwword + hwword_len;
	int c, i;
	if (hw_n == LEN(hwoff) || hwword_len + 128 > sizeof(hwword))
		return;
	i = hw_n++;
	while ((c = *s++)) {
		if (c == '-')
			hwhyph[d - hwword] = 1;
		else
			*d++ = c;
	}
	*d++ = '\0';
	hwoff[i] = hwword_len;
	hwword_len = d - hwword;
	hwlen[i] = hwword_len - hwoff[i] - 1;
	dict_put(&hwdict, hwword + hwoff[i], i);
}

/* copy lower-cased s to d */
static void hw_strcpy(char *d, char *s)
{
	while (*s) {
		if (*s & 0x80)
			*d++ = *s++;
		else
			*d++ = tolower(*s++);
	}
	*d = '\0';
}

static char *hw_lookup(char *s)
{
	char word[ILNLEN];
	int i, idx = -1;
	hw_strcpy(word, s);
	i = dict_prefix(&hwdict, s, &idx);
	return i >= 0 ? hwhyph + hwoff[i] : NULL;
}

void tr_hw(char **args)
{
	int i;
	for (i = 1; i < NARGS && args[i]; i++)
		hw_add(args[i]);
}

/* the tex hyphenation algorithm */

static int hyinit;		/* hyphenation data initialized */
static char hypats[HYPATLEN];	/* the patterns */
static char hynums[HYPATLEN];	/* numbers in the patterns */
static int hypats_len;
static struct dict hydict;	/* map patterns to their index in hyoff[] */
static int hyoff[NHYPHS];	/* the offset of this pattern in hypats[] */
static int hy_n;		/* number of words in hy_*[] lists */

/* make s lower-case and replace its non-alphabetic characters with . */
static void hy_strcpy(char *d, char *s)
{
	int c;
	*d++ = '.';
	while ((c = (unsigned char) *s++))
		*d++ = c & 0x80 ? c : (isalpha(c) ? tolower(c) : '.');
	*d++ = '.';
	*d = '\0';
}

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
		plen = strlen(p);
		for (j = 0; j < plen; j++)
			if (n[j] < np[j])
				n[j] = np[j];
	}
}

/* mark the hyphenation points of word in hyph */
static void hy_dohyph(char *hyph, char *word, int flg)
{
	char n[ILNLEN] = {0};
	char w[ILNLEN];
	int c[ILNLEN];	/* start of the i-th character in w */
	int nc = 0;
	int i, wlen;
	hy_strcpy(w, word);
	wlen = strlen(w);
	for (i = 0; i < wlen - 1; i += utf8len((unsigned int) w[i]))
		c[nc++] = i;
	for (i = 0; i < nc - 1; i++)
		hy_find(w + c[i], n + c[i]);
	memset(hyph, 0, wlen * sizeof(hyph[0]));
	for (i = 3; i < nc - 2; i++)
		if (n[i] % 2 && w[c[i - 1]] != '.' && w[c[i - 2]] != '.' && w[c[i + 1]] != '.')
			hyph[c[i - 1]] = (~flg & HY_FINAL2 || w[c[i + 2]] != '.') &&
				(~flg & HY_FIRST2 || w[c[i - 3]] != '.');
}

/* insert pattern s into hypats[] and hynums[] */
static void hy_ins(char *s)
{
	char *p = hypats + hypats_len;
	char *n = hynums + hypats_len;
	int i = 0, idx;
	if (hy_n >= NHYPHS || hypats_len + 64 >= sizeof(hypats))
		return;
	idx = hy_n++;
	while (*s) {
		if (*s >= '0' && *s <= '9')
			n[i] = *s++ - '0';
		else
			p[i++] = *s++;
	}
	p[i] = '\0';
	hyoff[idx] = hypats_len;
	dict_put(&hydict, hypats + hyoff[idx], idx);
	hypats_len += i + 1;
}

static void hyph_readpatterns(char *s)
{
	char word[ILNLEN];
	char *d;
	while (*s) {
		d = word;
		while (*s && !isspace((unsigned char) *s))
			*d++ = *s++;
		*d = '\0';
		hy_ins(word);
		while (*s && isspace((unsigned char) *s))
			s++;
	}
}

static void hyph_readexceptions(char *s)
{
	char word[ILNLEN];
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
	char *r;
	if (!hyinit) {
		hyinit = 1;
		hyph_readpatterns(en_patterns);
		hyph_readexceptions(en_exceptions);
	}
	r = hw_lookup(word);
	if (r)
		memcpy(hyph, r, strlen(word) + 1);
	else
		hy_dohyph(hyph, word, flg);
}

void tr_hpfa(char **args)
{
	char tok[ILNLEN];
	FILE *filp;
	/* reading patterns */
	if (args[1]) {
		hyinit = 1;
		filp = fopen(args[1], "r");
		while (fscanf(filp, "%s", tok) == 1)
			hy_ins(tok);
		fclose(filp);
	}
	/* reading exceptions */
	if (args[2]) {
		filp = fopen(args[1], "r");
		while (fscanf(filp, "%s", tok) == 1)
			hw_add(tok);
		fclose(filp);
	}
}

void hyph_init(void)
{
	dict_init(&hwdict, NHYPHS, -1, 0, 1);
	dict_init(&hydict, NHYPHS, -1, 0, 1);
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
	/* reading */
	hyph_init();
	tr_hpfa(args);
}
