/* copy-mode character interpretation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

static int cp_blkdep;		/* input block depth (text in \{ and \}) */
static int cp_cpmode;		/* disable the interpretation of \w and \E */
static int cp_reqdep;		/* the block depth of current request line */

/* just like cp_next(), but remove c_ni characters */
static int cp_noninext(void)
{
	int c = cp_next();
	while (c == c_ni)
		c = cp_next();
	return c;
}

/* return 1 if \*[] includes a space */
static int cparg(char *d, int len)
{
	int c = cp_noninext();
	int i = 0;
	if (c == '(') {
		i += utf8next(d + i, cp_noninext);
		i += utf8next(d + i, cp_noninext);
	} else if (!n_cp && c == '[') {
		c = cp_noninext();
		while (c >= 0 && c != ']' && c != ' ') {
			if (i + 1 < len)
				d[i++] = c;
			c = cp_noninext();
		}
		d[i] = '\0';
		return c == ' ';
	} else {
		cp_back(c);
		utf8next(d, cp_noninext);
	}
	return 0;
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
	int c = cp_noninext();
	char *s;
	if (c != '-' && c != '+')
		cp_back(c);
	id = regid();
	if (c == '-' || c == '+')
		num_inc(id, c == '+');
	if ((s = num_str(id)) != NULL)
		in_push(s, NULL);
}

/* interpolate \*(xy */
static void cp_str(void)
{
	char reg[NMLEN];
	char *args[NARGS + 2] = {reg};
	char *buf = NULL;
	if (cparg(reg, sizeof(reg))) {
		buf = tr_args(args + 1, ']', cp_noninext, cp_back);
		cp_noninext();
	}
	if (str_get(map(reg)))
		in_push(str_get(map(reg)), args);
	else if (!n_cp)
		tr_req(map(reg), args);
	free(buf);
}

/* interpolate \g(xy */
static void cp_numfmt(void)
{
	in_push(num_getfmt(regid()), NULL);
}

/* interpolate \$*, \$@, and \$^ */
static void cp_args(int quote, int escape)
{
	struct sbuf sb;
	char *s;
	int i;
	sbuf_init(&sb);
	for (i = 1; i < in_nargs(); i++) {
		sbuf_append(&sb, i > 1 ? " " : "");
		sbuf_append(&sb, quote ? "\"" : "");
		s = in_arg(i);
		while (*s) {
			sbuf_append(&sb, escape && *s == '"' ? "\"" : "");
			sbuf_add(&sb, (unsigned char) *s++);
		}
		sbuf_append(&sb, quote ? "\"" : "");
	}
	in_push(sbuf_buf(&sb), NULL);
	sbuf_done(&sb);
}

/* interpolate \$1 */
static void cp_arg(void)
{
	char argname[NMLEN];
	char *arg = NULL;
	int argnum;
	cparg(argname, sizeof(argname));
	if (!strcmp("@", argname)) {
		cp_args(1, 0);
		return;
	}
	if (!strcmp("*", argname)) {
		cp_args(0, 0);
		return;
	}
	if (!strcmp("^", argname)) {
		cp_args(1, 1);
		return;
	}
	argnum = atoi(argname);
	if (argnum >= 0 && argnum < NARGS)
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
	char *arg = quotednext(cp_noninext, cp_back);
	char *s = arg;
	while (*s && *s != ' ')
		s++;
	if (!*s) {
		free(arg);
		return;
	}
	*s++ = '\0';
	num_set(map(arg), eval_re(s, num_get(map(arg)), 'u'));
	free(arg);
}

/* conditional interpolation as \?'cond@expr1@expr2@' */
static void cp_cond(void)
{
	char delim[GNLEN], cs[GNLEN];
	char *r, *s;
	char *s1, *s2;
	int n;
	char *arg = quotednext(cp_noninext, cp_back);
	s = arg;
	n = eval_up(&s, '\0');
	if (charread(&s, delim) < 0) {
		free(arg);
		return;
	}
	if (!strcmp(delim, "\\&") && charread(&s, delim) < 0) {
		free(arg);
		return;
	}
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
	free(arg);
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
		/* replace \{ and \} with a space if not in copy mode */
		if (c == '}' && !cp_cpmode) {
			cp_blkdep--;
			return ' ';
		}
		if (c == '{' && !cp_cpmode) {
			cp_blkdep++;
			return ' ';
		}
		in_back(c);
		return c_ec;
	}
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
		} else if (c == '#') {
			while (c >= 0 && c != '\n')
				c = cp_raw();
			if (c >= 0)
				c = cp_next();
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
		} else if (c == '?') {
			cp_cond();
			c = cp_next();
		} else if (c == 'R' && !cp_cpmode) {
			cp_numdef();
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
		while (c == ' ')
			c = cp_next();
		/* push back if the space is not inserted due to \{ and \} */
		if (c != ' ')
			cp_back(c);
	}
}

void cp_copymode(int mode)
{
	cp_cpmode = mode;
}

/* beginning of a request; save current cp_blkdep */
void cp_reqbeg(void)
{
	cp_reqdep = cp_blkdep;
}
