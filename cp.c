/* copy-mode character interpretation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static int cp_blkdep;		/* input block depth (text in \{ and \}) */
static int cp_cpmode;		/* disable the interpretation \w and \E */
static int cp_reqln;		/* a request line; replace \{ with an space */
static int cp_reqdep;		/* the block depth of current request line */

static void cparg(char *d, int len)
{
	int c = cp_next();
	int i = 0;
	if (c == '(') {
		d[i++] = cp_next();
		d[i++] = cp_next();
	} else if (!n_cp && c == '[') {
		c = cp_next();
		while (i < NMLEN - 1 && c >= 0 && c != ']') {
			d[i++] = c;
			c = cp_next();
		}
	} else {
		d[i++] = c;
	}
	d[i] = '\0';
}

static int regid(void)
{
	char regname[NMLEN];
	cparg(regname, sizeof(regname));
	return map(regname);
}

/* interpolate \n(xy */
static void cp_num(void)
{
	int id;
	int c = cp_next();
	if (c != '-' && c != '+')
		cp_back(c);
	id = regid();
	if (c == '-' || c == '+')
		num_inc(id, c == '+');
	if (num_str(id))
		in_push(num_str(id), NULL);
}

/* interpolate \*(xy */
static void cp_str(void)
{
	char arg[ILNLEN];
	struct sbuf sbuf;
	char *args[NARGS] = {NULL};
	cparg(arg, sizeof(arg));
	if (strchr(arg, ' ')) {
		sbuf_init(&sbuf);
		sstr_push(strchr(arg, ' ') + 1);
		tr_readargs(args, &sbuf, sstr_next, sstr_back);
		sstr_pop();
		*strchr(arg, ' ') = '\0';
		if (str_get(map(arg)))
			in_push(str_get(map(arg)), args);
		sbuf_done(&sbuf);
	} else {
		if (str_get(map(arg)))
			in_push(str_get(map(arg)), NULL);
	}
}

/* interpolate \g(xy */
static void cp_numfmt(void)
{
	in_push(num_getfmt(regid()), NULL);
}

/* interpolate \$1 */
static void cp_arg(void)
{
	char argname[NMLEN];
	char *arg = NULL;
	int argnum;
	cparg(argname, sizeof(argname));
	argnum = atoi(argname);
	if (argnum > 0 && argnum < NARGS + 1)
		arg = in_arg(argnum);
	if (arg)
		in_push(arg, NULL);
}

/* interpolate \w'xyz' */
static void cp_width(void)
{
	char wid[16];
	sprintf(wid, "%d", ren_wid(cp_next, cp_back));
	in_push(wid, NULL);
}

/* define a register as \R'xyz expr' */
static void cp_numdef(void)
{
	char arg[ILNLEN];
	char *s;
	quotednext(arg, cp_next, cp_back);
	s = arg;
	while (*s && *s != ' ')
		s++;
	if (!*s)
		return;
	*s++ = '\0';
	num_set(map(arg), eval_re(s, num_get(map(arg)), 'u'));
}

/* conditional interpolation as \?'cond@expr1@expr2@' */
static void cp_cond(void)
{
	char arg[ILNLEN];
	char delim[GNLEN], cs[GNLEN];
	char *r, *s = arg;
	char *s1, *s2;
	int n;
	quotednext(arg, cp_next, cp_back);
	n = eval_up(&s, '\0');
	if (charread(&s, delim) < 0)
		return;
	if (!strcmp(delim, "\\&") && charread(&s, delim) < 0)
		return;
	s1 = s;
	r = s;
	while (charread_delim(&s, cs, delim) >= 0)
		r = s;
	*r = '\0';
	s2 = s;
	r = s;
	while (charread_delim(&s, cs, delim) >= 0)
		r = s;
	*r = '\0';
	in_push(n > 0 ? s1 : s2, NULL);
}

static int cp_raw(void)
{
	int c;
	if (in_top() >= 0)
		return in_next();
	do {
		c = in_next();
	} while (c == c_ni);
	if (c == c_ec) {
		do {
			c = in_next();
		} while (c == c_ni);
		if (c == '\n')
			return cp_raw();
		if (c == '.')
			return '.';
		if (c == '\\') {
			in_back('\\');
			return c_ni;
		}
		if (c == 't') {
			in_back('\t');
			return c_ni;
		}
		if (c == 'a') {
			in_back('');
			return c_ni;
		}
		if (c == '}' && !cp_cpmode) {
			cp_blkdep--;
			return cp_raw();
		}
		if (c == '{' && !cp_cpmode) {
			cp_blkdep++;
			return cp_reqln ? ' ' : cp_raw();
		}
		in_back(c);
		return c_ec;
	}
	if (c == '\n')
		cp_reqln = 0;
	return c;
}

int cp_next(void)
{
	int c;
	if (in_top() >= 0)
		return in_next();
	c = cp_raw();
	if (c == c_ec) {
		c = cp_raw();
		if (c == 'E' && !cp_cpmode)
			c = cp_next();
		if (c == '"') {
			while (c >= 0 && c != '\n')
				c = cp_raw();
		} else if (c == 'w' && !cp_cpmode) {
			cp_width();
			c = cp_next();
		} else if (c == 'n') {
			cp_num();
			c = cp_next();
		} else if (c == '*') {
			cp_str();
			c = cp_next();
		} else if (c == 'g') {
			cp_numfmt();
			c = cp_next();
		} else if (c == '$') {
			cp_arg();
			c = cp_next();
		} else if (c == 'R' && !cp_cpmode) {
			cp_numdef();
			c = cp_next();
		} else if (c == '?' && !cp_cpmode) {
			cp_cond();
			c = cp_next();
		} else {
			cp_back(c);
			c = c_ec;
		}
	}
	return c;
}

void cp_blk(int skip)
{
	if (skip) {
		int c = cp_raw();
		while (c >= 0 && (c != '\n' || cp_blkdep > cp_reqdep))
			c = cp_raw();
	} else {
		int c = cp_next();
		while ((c == ' ' || c == '\t') && cp_blkdep <= cp_reqdep)
			c = cp_next();
		/* push back if the space is not inserted because of cp_reqln */
		if (c != ' ' && c != '\t')
			cp_back(c);
	}
}

void cp_copymode(int mode)
{
	cp_cpmode = mode;
}

/* beginning of a request line; replace \{ with a space until an EOL */
void cp_reqline(void)
{
	cp_reqln = 1;
	cp_reqdep = cp_blkdep;
}
