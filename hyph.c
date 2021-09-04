/* hyphenation */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"
#include "hyen.h"

#define HYPATLEN	(NHYPHS * 16)	/* hyphenation pattern length */

static void hcode_strcpy(char *d, char *s, int *map, int dots);
static int hcode_mapchar(char *s);

/* the hyphenation dictionary (.hw) */

static char hwword[HYPATLEN];	/* buffer for .hw words */
static char hwhyph[HYPATLEN];	/* buffer for .hw hyphenations */
static int hwword_len;		/* used hwword[] length */
static struct dict *hwdict;	/* map words to their index in hwoff[] */
static int hwoff[NHYPHS];	/* the offset of words in hwword[] */
static int hw_n;		/* the number of dictionary words */

/* read a single character from s into d; return the number of characters read */
static int hy_cget(char *d, char *s)
{
	int i = 0;
	if (s[0] != '\\')
		return utf8read(&s, d);
	if (s[1] == '[') {
		s += 2;
		while (*s && *s != ']' && i < GNLEN - 1)
			d[i++] = *s++;
		d[i] = '\0';
		return *s ? i + 3 : i + 2;
	}
	if (s[1] == '(') {
		s += 2;
		i += utf8read(&s, d + i);
		i += utf8read(&s, d + i);
		return 2 + i;
	}
	if (s[1] == 'C') {
		int q = s[2];
		s += 3;
		while (*s && *s != q && i < GNLEN - 1)
			d[i++] = *s++;
		d[i] = '\0';
		return *s ? i + 4 : i + 3;
	}
	*d++ = *s++;
	return 1 + utf8read(&s, d);
}

/* append character s to d; return the number of characters written */
int hy_cput(char *d, char *s)
{
	if (!s[0] || !s[1] || utf8one(s))
		strcpy(d, s);
	else if (s[0] == '\\')
		strcpy(d, s);
	else if (!s[2])
		snprintf(d, GNLEN, "\\[%s]", s);
	return strlen(d);
}

/* insert word s into hwword[] and hwhyph[] */
static void hw_add(char *s)
{
	char *p = hwword + hwword_len;
	char *n = hwhyph + hwword_len;
	int len = strlen(s) + 1;
	int i = 0, c;
	if (hw_n == NHYPHS || (size_t)(hwword_len + len) > sizeof(hwword))
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
	dict_put(hwdict, hwword + hwoff[hw_n], hw_n);
	hwword_len += i + 1;
	hw_n++;
}

static int hw_lookup(char *word, char *hyph)
{
	char word2[WORDLEN] = {0};
	char *hyph2;
	int map[WORDLEN] = {0};
	int off = 0;
	int i, j, idx = -1;
	hcode_strcpy(word2, word, map, 0);
	while (word2[off] == '.')	/* skip unknown characters at the front */
		off++;
	i = dict_prefix(hwdict, word2 + off, &idx);
	if (i < 0)
		return 1;
	hyph2 = hwhyph + hwoff[i];
	for (j = 0; word2[j + off]; j++)
		if (hyph2[j])
			hyph[map[j + off]] = hyph2[j];
	return 0;
}

void tr_hw(char **args)
{
	char word[WORDLEN];
	char *c;
	int i;
	for (i = 1; i < NARGS && args[i]; i++) {
		char *s = args[i];
		char *d = word;
		while (d - word < WORDLEN - GNLEN && !escread(&s, &c)) {
			if (strcmp("-", c))
				hcode_mapchar(c);
			d += hy_cput(d, c);
		}
		hw_add(word);
	}
}

/* the tex hyphenation algorithm */

static int hyinit;		/* hyphenation data initialized */
static char hypats[HYPATLEN];	/* hyphenation patterns */
static char hynums[HYPATLEN];	/* hyphenation pattern numbers */
static int hypats_len;		/* used hypats[] and hynums[] length */
static struct dict *hydict;	/* map patterns to their index in hyoff[] */
static int hyoff[NHYPHS];	/* the offset of this pattern in hypats[] */
static int hy_n;		/* the number of patterns */

/* find the patterns matching s and update hyphenation values in n */
static void hy_find(char *s, char *n)
{
	int plen;
	char *p, *np;
	int i, j;
	int idx = -1;
	while ((i = dict_prefix(hydict, s, &idx)) >= 0) {
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
	char w[WORDLEN] = {0};		/* cleaned-up word[]; "Abc" -> ".abc." */
	char n[WORDLEN] = {0};		/* the hyphenation value for w[] */
	int c[WORDLEN];			/* start of the i-th character in w */
	int wmap[WORDLEN] = {0};	/* w[i] corresponds to word[wmap[i]] */
	char ch[GNLEN];
	int nc = 0;
	int i, wlen;
	hcode_strcpy(w, word, wmap, 1);
	wlen = strlen(w);
	for (i = 0; i < wlen - 1; i += hy_cget(ch, w + i))
		c[nc++] = i;
	for (i = 0; i < nc - 1; i++)
		hy_find(w + c[i], n + c[i]);
	memset(hyph, 0, wlen * sizeof(hyph[0]));
	for (i = 3; i < nc - 2; i++)
		if (n[c[i]] % 2 && w[c[i - 1]] != '.' && w[c[i]] != '.' &&
				w[c[i - 2]] != '.' && w[c[i + 1]] != '.' &&
				(~flg & HY_FINAL2 || w[c[i + 2]] != '.') &&
				(~flg & HY_FIRST2 || w[c[i - 3]] != '.'))
			hyph[wmap[c[i]]] = 1;
}

/* insert pattern s into hypats[] and hynums[] */
static void hy_add(char *s)
{
	char *p = hypats + hypats_len;
	char *n = hynums + hypats_len;
	int len = strlen(s) + 1;
	int i = 0, c;
	if (hy_n >= NHYPHS || (size_t)(hypats_len + len) >= sizeof(hypats))
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
	dict_put(hydict, hypats + hyoff[hy_n], hy_n);
	hypats_len += i + 1;
	hy_n++;
}

/* .hcode request */
static struct dict *hcodedict;
static char hcodesrc[NHCODES][GNLEN];
static char hcodedst[NHCODES][GNLEN];
static int hcode_n;

/* replace the character in s after .hcode mapping; returns s's new length */
static int hcode_mapchar(char *s)
{
	int i = dict_get(hcodedict, s);
	if (i >= 0)
		strcpy(s, hcodedst[i]);
	else if (!s[1])
		*s = isalpha((unsigned char) *s) ? tolower((unsigned char) *s) : '.';
	return strlen(s);
}

/* copy s to d after .hcode mappings; s[map[j]] corresponds to d[j] */
static void hcode_strcpy(char *d, char *s, int *map, int dots)
{
	char c[GNLEN];
	int di = 0, si = 0;
	if (dots)
		d[di++] = '.';
	while (di < WORDLEN - GNLEN && s[si]) {
		map[di] = si;
		si += hy_cget(c, s + si);
		hcode_mapchar(c);
		di += hy_cput(d + di, c);
	}
	if (dots)
		d[di++] = '.';
	d[di] = '\0';
}

static void hcode_add(char *c1, char *c2)
{
	int i = dict_get(hcodedict, c1);
	if (i >= 0) {
		strcpy(hcodedst[i], c2);
	} else if (hcode_n < NHCODES) {
		strcpy(hcodesrc[hcode_n], c1);
		strcpy(hcodedst[hcode_n], c2);
		dict_put(hcodedict, hcodesrc[hcode_n], hcode_n);
		hcode_n++;
	}
}

void tr_hcode(char **args)
{
	char c1[GNLEN], c2[GNLEN];
	char *s = args[1];
	while (s && charread(&s, c1) >= 0 && charread(&s, c2) >= 0)
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

/* lowercase-uppercase character mapping */
static char *hycase[][2] = {
	{"a", "A"}, {"á", "Á"}, {"à", "À"}, {"ă", "Ă"}, {"â", "Â"},
	{"ǎ", "Ǎ"}, {"å", "Å"}, {"ä", "Ä"}, {"ã", "Ã"}, {"ą", "Ą"},
	{"ā", "Ā"}, {"æ", "Æ"}, {"ǽ", "Ǽ"}, {"b", "B"}, {"c", "C"},
	{"ć", "Ć"}, {"ĉ", "Ĉ"}, {"č", "Č"}, {"ç", "Ç"}, {"d", "D"},
	{"ď", "Ď"}, {"đ", "Đ"}, {"ḍ", "Ḍ"}, {"ð", "Ð"}, {"e", "E"},
	{"é", "É"}, {"è", "È"}, {"ê", "Ê"}, {"ě", "Ě"}, {"ë", "Ë"},
	{"ė", "Ė"}, {"ę", "Ę"}, {"ē", "Ē"}, {"f", "F"}, {"g", "G"},
	{"ğ", "Ğ"}, {"ĝ", "Ĝ"}, {"ģ", "Ģ"}, {"h", "H"}, {"ĥ", "Ĥ"},
	{"ḥ", "Ḥ"}, {"ḫ", "Ḫ"}, {"i", "I"}, {"ı", "I"}, {"í", "Í"},
	{"ì", "Ì"}, {"î", "Î"}, {"ǐ", "Ǐ"}, {"ï", "Ï"}, {"į", "Į"},
	{"ī", "Ī"}, {"j", "J"}, {"ĵ", "Ĵ"}, {"k", "K"}, {"ķ", "Ķ"},
	{"l", "L"}, {"ľ", "Ľ"}, {"ł", "Ł"}, {"ļ", "Ļ"}, {"ḷ", "Ḷ"},
	{"m", "M"}, {"ṁ", "Ṁ"}, {"ṃ", "Ṃ"}, {"n", "N"}, {"ń", "Ń"},
	{"ň", "Ň"}, {"ñ", "Ñ"}, {"ṅ", "Ṅ"}, {"ņ", "Ņ"}, {"ṇ", "Ṇ"},
	{"œ", "Œ"}, {"o", "O"}, {"ó", "Ó"}, {"ò", "Ò"}, {"ô", "Ô"},
	{"ǒ", "Ǒ"}, {"ö", "Ö"}, {"ő", "Ő"}, {"õ", "Õ"}, {"ø", "Ø"},
	{"ō", "Ō"}, {"p", "P"}, {"q", "Q"}, {"r", "R"}, {"ŕ", "Ŕ"},
	{"ř", "Ř"}, {"s", "S"}, {"ś", "Ś"}, {"ŝ", "Ŝ"}, {"š", "Š"},
	{"ş", "Ş"}, {"ṣ", "Ṣ"}, {"t", "T"}, {"ť", "Ť"}, {"ț", "Ț"},
	{"ṭ", "Ṭ"}, {"u", "U"}, {"ú", "Ú"}, {"ù", "Ù"}, {"ŭ", "Ŭ"},
	{"û", "Û"}, {"ǔ", "Ǔ"}, {"ů", "Ů"}, {"ü", "Ü"}, {"ǘ", "Ǘ"},
	{"ǜ", "Ǜ"}, {"ǚ", "Ǚ"}, {"ǖ", "Ǖ"}, {"ű", "Ű"}, {"ų", "Ų"},
	{"ū", "Ū"}, {"v", "V"}, {"w", "W"}, {"x", "X"}, {"y", "Y"},
	{"ý", "Ý"}, {"z", "Z"}, {"ź", "Ź"}, {"ž", "Ž"}, {"ż", "Ż"},
	{"þ", "Þ"}, {"α", "Α"}, {"ά", "Ά"}, {"β", "Β"}, {"ϐ", "Β"},
	{"γ", "Γ"}, {"δ", "Δ"}, {"ϫ", "Ϫ"}, {"ε", "Ε"}, {"έ", "Έ"},
	{"ζ", "Ζ"}, {"ϩ", "Ϩ"}, {"η", "Η"}, {"ή", "Ή"}, {"θ", "Θ"},
	{"ι", "Ι"}, {"ί", "Ί"}, {"ϊ", "Ϊ"}, {"κ", "Κ"}, {"ϧ", "Ϧ"},
	{"λ", "Λ"}, {"μ", "Μ"}, {"ν", "Ν"}, {"ξ", "Ξ"}, {"ο", "Ο"},
	{"ό", "Ό"}, {"π", "Π"}, {"ρ", "Ρ"}, {"ϲ", "Ϲ"}, {"σ", "Σ"},
	{"ς", "Σ"}, {"ϭ", "Ϭ"}, {"τ", "Τ"}, {"ϯ", "Ϯ"}, {"υ", "Υ"},
	{"ύ", "Ύ"}, {"ϋ", "Ϋ"}, {"φ", "Φ"}, {"ϥ", "Ϥ"}, {"χ", "Χ"},
	{"ψ", "Ψ"}, {"ϣ", "Ϣ"}, {"ω", "Ω"}, {"ώ", "Ώ"}, {"а", "А"},
	{"ӓ", "Ӓ"}, {"б", "Б"}, {"в", "В"}, {"г", "Г"}, {"ґ", "Ґ"},
	{"д", "Д"}, {"ђ", "Ђ"}, {"е", "Е"}, {"ѐ", "Ѐ"}, {"є", "Є"},
	{"ё", "Ё"}, {"ж", "Ж"}, {"з", "З"}, {"ѕ", "Ѕ"}, {"и", "И"},
	{"ѝ", "Ѝ"}, {"ӥ", "Ӥ"}, {"і", "І"}, {"ї", "Ї"}, {"й", "Й"},
	{"ј", "Ј"}, {"к", "К"}, {"л", "Л"}, {"љ", "Љ"}, {"м", "М"},
	{"н", "Н"}, {"њ", "Њ"}, {"ᲂ", "О"}, {"о", "О"}, {"ӧ", "Ӧ"},
	{"ө", "Ө"}, {"п", "П"}, {"р", "Р"}, {"с", "С"}, {"т", "Т"},
	{"ћ", "Ћ"}, {"у", "У"}, {"ӱ", "Ӱ"}, {"ү", "Ү"}, {"ў", "Ў"},
	{"ф", "Ф"}, {"х", "Х"}, {"ц", "Ц"}, {"ч", "Ч"}, {"џ", "Џ"},
	{"ш", "Ш"}, {"щ", "Щ"}, {"ᲆ", "Ъ"}, {"ъ", "Ъ"}, {"ы", "Ы"},
	{"ӹ", "Ӹ"}, {"ь", "Ь"}, {"э", "Э"}, {"ӭ", "Ӭ"}, {"ю", "Ю"},
	{"я", "Я"}, {"ա", "Ա"}, {"բ", "Բ"}, {"գ", "Գ"}, {"դ", "Դ"},
	{"ե", "Ե"}, {"զ", "Զ"}, {"է", "Է"}, {"ը", "Ը"}, {"թ", "Թ"},
	{"ժ", "Ժ"}, {"ի", "Ի"}, {"լ", "Լ"}, {"խ", "Խ"}, {"ծ", "Ծ"},
	{"կ", "Կ"}, {"հ", "Հ"}, {"ձ", "Ձ"}, {"ղ", "Ղ"}, {"ճ", "Ճ"},
	{"մ", "Մ"}, {"յ", "Յ"}, {"ն", "Ն"}, {"շ", "Շ"}, {"ո", "Ո"},
	{"չ", "Չ"}, {"պ", "Պ"}, {"ջ", "Ջ"}, {"ռ", "Ռ"}, {"ս", "Ս"},
	{"վ", "Վ"}, {"տ", "Տ"}, {"ր", "Ր"}, {"ց", "Ց"}, {"փ", "Փ"},
	{"ք", "Ք"}, {"օ", "Օ"},
};

void tr_hpfa(char **args)
{
	char tok[128], c1[GNLEN], c2[GNLEN];
	FILE *filp;
	hyinit = 1;
	/* load english hyphenation patterns with no arguments */
	if (!args[1]) {
		hyph_readpatterns(en_patterns);
		hyph_readexceptions(en_exceptions);
	}
	/* reading patterns */
	if (args[1] && (filp = fopen(args[1], "r"))) {
		while (fscanf(filp, "%128s", tok) == 1)
			if (strlen(tok) < WORDLEN)
				hy_add(tok);
		fclose(filp);
	}
	/* reading exceptions */
	if (args[2] && (filp = fopen(args[2], "r"))) {
		while (fscanf(filp, "%128s", tok) == 1)
			if (strlen(tok) < WORDLEN)
				hw_add(tok);
		fclose(filp);
	}
	/* reading hcode mappings */
	if (args[3] && (filp = fopen(args[3], "r"))) {
		while (fscanf(filp, "%128s", tok) == 1) {
			char *s = tok;
			if (utf8read(&s, c1) && utf8read(&s, c2) && !*s)
				hcode_add(c2, c1);	/* inverting */
		}
		fclose(filp);
	}
	/* lowercase-uppercase character hcode mappings */
	if (args[3] && !strcmp("-", args[3])) {
		size_t i;
		for (i = 0; i < LEN(hycase); i++)
			hcode_add(hycase[i][1], hycase[i][0]);
	}
}

void hyph_init(void)
{
	hwdict = dict_make(-1, 0, 2);
	hydict = dict_make(-1, 0, 2);
	hcodedict = dict_make(-1, 0, 1);
}

void hyph_done(void)
{
	if (hwdict)
		dict_free(hwdict);
	if (hydict)
		dict_free(hydict);
	if (hcodedict)
		dict_free(hcodedict);
}

void tr_hpf(char **args)
{
	/* reseting the patterns */
	hypats_len = 0;
	hy_n = 0;
	dict_free(hydict);
	/* reseting the dictionary */
	hwword_len = 0;
	hw_n = 0;
	dict_free(hwdict);
	/* reseting hcode mappings */
	hcode_n = 0;
	dict_free(hcodedict);
	/* reading */
	hyph_init();
	tr_hpfa(args);
}
