/* reading characters and escapes */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

/* return the length of a utf-8 character based on its first byte */
int utf8len(int c)
{
	if (~c & 0x80)
		return c > 0;
	if (~c & 0x20)
		return 2;
	if (~c & 0x10)
		return 3;
	if (~c & 0x08)
		return 4;
	if (~c & 0x04)
		return 5;
	if (~c & 0x02)
		return 6;
	return 1;
}

/* return nonzero if s is a single utf-8 character */
int utf8one(char *s)
{
	return !s[utf8len((unsigned char) *s)];
}

/* read a utf-8 character from s and copy it to d */
int utf8read(char **s, char *d)
{
	int l = utf8len((unsigned char) **s);
	int i;
	for (i = 0; i < l; i++)
		d[i] = (*s)[i];
	d[l] = '\0';
	*s += l;
	return l;
}

/* read a utf-8 character with next() and copy it to s */
int utf8next(char *s, int (*next)(void))
{
	int c = next();
	int l = utf8len(c);
	int i;
	if (c < 0)
		return 0;
	s[0] = c;
	for (i = 1; i < l; i++)
		s[i] = next();
	s[l] = '\0';
	return l;
}

/* read quoted arguments of escape sequences (ESC_Q) */
void quotednext(char *d, int (*next)(void), void (*back)(int))
{
	char delim[GNLEN], cs[GNLEN];
	charnext(delim, next, back);
	while (charnext_delim(cs, next, back, delim) >= 0) {
		charnext_str(d, cs);
		d = strchr(d, '\0');
	}
}

/* read unquoted arguments of escape sequences (ESC_P) */
void unquotednext(char *d, int cmd, int (*next)(void), void (*back)(int))
{
	int c = next();
	if (cmd == 's' && (c == '-' || c == '+')) {
		cmd = c;
		*d++ = c;
		c = next();
	}
	if (c == '(') {
		*d++ = next();
		*d++ = next();
	} else if (!n_cp && c == '[') {
		c = next();
		while (c > 0 && c != '\n' && c != ']') {
			*d++ = c;
			c = next();
		}
	} else {
		*d++ = c;
		if (cmd == 's' && c >= '1' && c <= '3') {
			c = next();
			if (isdigit(c))
				*d++ = c;
			else
				back(c);
		}
	}
	*d = '\0';
}

/*
 * read the next character or escape sequence (x, \x, \(xy, \[xyz], \C'xyz')
 *
 * character	returned	contents of c
 * x		'\0'		x
 * \4x		c_ni		\4x
 * \\x		'\\'		\\x
 * \\(xy	'('		xy
 * \\[xyz]	'['		xyz
 * \\C'xyz'	'C'		xyz
 */
int charnext(char *c, int (*next)(void), void (*back)(int))
{
	int l, n;
	if (!utf8next(c, next))
		return -1;
	if (c[0] == c_ni) {
		utf8next(c + 1, next);
		return c_ni;
	}
	if (c[0] == c_ec) {
		utf8next(c + 1, next);
		if (c[1] == '(') {
			l = utf8next(c, next);
			l += utf8next(c + l, next);
			return '(';
		} else if (!n_cp && c[1] == '[') {
			l = 0;
			n = next();
			while (n >= 0 && n != '\n' && n != ']' && l < GNLEN - 1) {
				c[l++] = n;
				n = next();
			}
			c[l] = '\0';
			return '[';
		} else if (c[1] == 'C') {
			quotednext(c, next, back);
			return 'C';
		}
		return '\\';
	}
	return '\0';
}

/* like nextchar(), but return -1 if delim was read */
int charnext_delim(char *c, int (*next)(void), void (*back)(int), char *delim)
{
	int t = charnext(c, next, back);
	return strcmp(c, delim) ? t : -1;
}

/* convert back the character read from nextchar() (e.g. xy -> \\(xy) */
void charnext_str(char *d, char *c)
{
	int c0 = (unsigned char) c[0];
	if (c0 == c_ec || c0 == c_ni || !c[1] || utf8one(c)) {
		strcpy(d, c);
		return;
	}
	if (!c[2] && utf8len(c0) == 1)
		sprintf(d, "%c(%s", c_ec, c);
	else
		sprintf(d, "%cC'%s'", c_ec, c);
}

/* like charnext() for string buffers */
int charread(char **s, char *c)
{
	int ret;
	sstr_push(*s);
	ret = charnext(c, sstr_next, sstr_back);
	*s = sstr_pop();
	return ret;
}

/* like charnext_delim() for string buffers */
int charread_delim(char **s, char *c, char *delim)
{
	int ret;
	sstr_push(*s);
	ret = charnext_delim(c, sstr_next, sstr_back, delim);
	*s = sstr_pop();
	return ret;
}

/* read quoted arguments; this is called only for internal neatroff strings */
static void quotedread(char **sp, char *d)
{
	char *s = *sp;
	int q = *s++;
	while (*s && *s != q)
		*d++ = *s++;
	if (*s == q)
		s++;
	*d = '\0';
	*sp = s;
}

/* read unquoted arguments; this is called only for internal neatroff strings */
static void unquotedread(char **sp, char *d)
{
	char *s = *sp;
	if (*s == '(') {
		s++;
		*d++ = *s++;
		*d++ = *s++;
	} else if (!n_cp && *s == '[') {
		s++;
		while (*s && *s != ']')
			*d++ = *s++;
		if (*s == ']')
			s++;
	} else {
		*d++ = *s++;
	}
	*d = '\0';
	*sp = s;
}

/*
 * read a glyph or an escape sequence
 *
 * This function reads from s either an output troff request
 * (only the ones emitted by wb.c) or a glyph name and updates
 * s.  The return value is the name of the troff request (the
 * argument is copied into d) or zero for glyph names (it is
 * copied into d).  Returns -1 when the end of s is reached.
 * Note that to d, a pointer to a static array is assigned.
 */
int escread(char **s, char **d)
{
	static char buf[1 << 12];
	char *r;
	if (!**s)
		return -1;
	r = buf;
	*d = buf;
	utf8read(s, r);
	if (r[0] == c_ec) {
		utf8read(s, r + 1);
		if (r[1] == '(') {
			utf8read(s, r);
			utf8read(s, r + strlen(r));
		} else if (!n_cp && r[1] == '[') {
			while (**s && **s != ']')
				*r++ = *(*s)++;
			*r = '\0';
			if (**s == ']')
				(*s)++;
		} else if (strchr("CDfhmsvXx", r[1])) {
			int c = r[1];
			r[0] = '\0';
			if (strchr(ESC_P, c))
				unquotedread(s, r);
			if (strchr(ESC_Q, c))
				quotedread(s, r);
			return c == 'C' ? 0 : c;
		}
	} else if (r[0] == c_ni) {
		utf8read(s, r + 1);
	}
	return 0;
}

/*
 * string streams: provide next()/back() interface for string buffers
 *
 * Functions like charnext() require a next()/back() interface
 * for reading input streams.  In order to provide this interface
 * for string buffers, the following functions can be used:
 *
 *   sstr_push(s);
 *   charnext(c, sstr_next, sstr_back);
 *   sstr_pop();
 *
 * The calls to sstr_push()/sstr_pop() may be nested.
 */
static char *sstr_bufs[NSSTR];	/* buffer stack */
static int sstr_n;		/* numbers of items in sstr_bufs[] */
static char *sstr_s;		/* current buffer */

void sstr_push(char *s)
{
	sstr_bufs[sstr_n++] = sstr_s;
	sstr_s = s;
}

char *sstr_pop(void)
{
	char *ret = sstr_s;
	sstr_s = sstr_bufs[--sstr_n];
	return ret;
}

int sstr_next(void)
{
	return *sstr_s ? (unsigned char) *sstr_s++ : -1;
}

void sstr_back(int c)
{
	sstr_s--;
}
