#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "roff.h"

int utf8len(int c)
{
	if (c > 0 && c <= 0x7f)
		return 1;
	if (c >= 0xfc)
		return 6;
	if (c >= 0xf8)
		return 5;
	if (c >= 0xf0)
		return 4;
	if (c >= 0xe0)
		return 3;
	if (c >= 0xc0)
		return 2;
	return c != 0;
}

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
			argnext(c, 'C', next, back);
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
	if (c[0] == c_ec || c[0] == c_ni || !c[1] || utf8len(c[0]) == strlen(c)) {
		strcpy(d, c);
		return;
	}
	if (!c[2] && utf8len(c[0]) == 1)
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

/* read the argument of a troff escape sequence */
void argnext(char *d, int cmd, int (*next)(void), void (*back)(int))
{
	char delim[GNLEN], cs[GNLEN];
	int c;
	if (strchr(ESC_P, cmd)) {
		c = next();
		if (cmd == 's' && (c == '-' || c == '+')) {
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
	}
	if (strchr(ESC_Q, cmd)) {
		charnext(delim, next, back);
		while (charnext_delim(cs, next, back, delim) >= 0) {
			charnext_str(d, cs);
			d = strchr(d, '\0');
		}
	}
	*d = '\0';
}

/* this is called only for internal neatroff strings */
void argread(char **sp, char *d, int cmd)
{
	char *s = *sp;
	int q;
	if (strchr(ESC_P, cmd)) {
		if (cmd == 's' && (*s == '-' || *s == '+'))
			*d++ = *s++;
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
			if (cmd == 's' && s[-1] >= '1' && s[-1] <= '3')
				if (isdigit(*s))
					*d++ = *s++;
		}
	}
	if (strchr(ESC_Q, cmd)) {
		q = *s++;
		while (*s && *s != q)
			*d++ = *s++;
		if (*s == q)
			s++;
	}
	if (cmd == 'z')
		*d++ = *s++;
	*d = '\0';
	*sp = s;
}

/*
 * read a glyph or an escape sequence
 *
 * This functions reads from s either an output troff request
 * (only the ones emitted by wb.c) or a glyph name and updates
 * s.  The return value is the name of the troff request (the
 * argument is copied into d) or zero for glyph names (it is
 * copied into d).  Returns -1 when the end of s is reached.
 */
int escread(char **s, char *d)
{
	char *r = d;
	if (!**s)
		return -1;
	utf8read(s, d);
	if (d[0] == c_ec) {
		utf8read(s, d + 1);
		if (d[1] == '(') {
			utf8read(s, d);
			utf8read(s, d + strlen(d));
		} else if (!n_cp && d[1] == '[') {
			while (**s && **s != ']')
				*r++ = *(*s)++;
			if (**s == ']')
				(*s)++;
		} else if (strchr("CDfhmsvXx", d[1])) {
			int c = d[1];
			argread(s, d, d[1]);
			return c == 'C' ? 0 : c;
		}
	}
	if (d[0] == c_ni)
		utf8read(s, d + 1);
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
 *   charnext(c, sstr_next, sstr_prev);
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
