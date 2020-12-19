/* output text direction */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

int dir_do;			/* enable text direction processing */

static char *dbuf;		/* text in n_td direction */
static int dbuf_sz, dbuf_n;	/* dbuf[] size and length */
static char *rbuf;		/* text in (1 - n_td) direction */
static int rbuf_sz, rbuf_n;	/* rbuf[] size and length */
static int dir_cd;		/* current direction */

/* append s to the start (dir == 0) or end (dir == 1) of d */
static void dir_copy(char **d, int *d_n, int *d_sz, char *s, int s_n, int dir)
{
	while (*d_n + s_n + 1 > *d_sz) {
		int sz = *d_sz ? *d_sz * 2 : 512;
		char *n = malloc(sz + 1);
		if (*d_sz)
			memcpy(dir ? n + *d_sz : n, *d, *d_sz);
		free(*d);
		*d_sz = sz;
		*d = n;
	}
	if (dir > 0)
		memcpy(*d + *d_sz - *d_n - s_n, s, s_n);
	else
		memcpy(*d + *d_n, s, s_n);
	*d_n += s_n;
}

/* copy rbuf (the text in reverse direction) to dbuf */
static void dir_flush(void)
{
	char *s = rbuf + (n_td > 0 ? 0 : rbuf_sz - rbuf_n);
	dir_copy(&dbuf, &dbuf_n, &dbuf_sz, s, rbuf_n, n_td);
	rbuf_n = 0;
}

/* append s to dbuf or rbuf based on the current text direction */
static void dir_append(char *s)
{
	int dir = dir_cd > 0;
	if (dir == n_td && rbuf_n)
		dir_flush();
	if (dir == n_td)
		dir_copy(&dbuf, &dbuf_n, &dbuf_sz, s, strlen(s), dir);
	else
		dir_copy(&rbuf, &rbuf_n, &rbuf_sz, s, strlen(s), dir);
}

static void setfont(int f)
{
	char cmd[32];
	sprintf(cmd, "%cf(%02d", c_ec, f);
	if (f >= 0)
		dir_append(cmd);
}

static void setsize(int s)
{
	char cmd[32];
	sprintf(cmd, s <= 99 ? "%cs(%02d" : "%cs[%d]", c_ec, s);
	if (s >= 0)
		dir_append(cmd);
}

static void setcolor(int m)
{
	char cmd[32];
	sprintf(cmd, "%cm[%s]", c_ec, clr_str(m));
	if (m >= 0)
		dir_append(cmd);
}

void dir_fix(struct sbuf *sbuf, char *src)
{
	char cmd[1024];
	char *prev_s = src;
	char *r, *c;
	int f = -1, s = -1, m = -1;
	int t, n;
	dir_cd = n_td;
	while ((t = escread(&src, &c)) >= 0) {
		cmd[0] = '\0';
		switch (t) {
		case 0:
		case 'D':
		case 'h':
		case 'v':
		case 'x':
			memcpy(cmd, prev_s, src - prev_s);
			cmd[src - prev_s] = '\0';
			dir_append(cmd);
			break;
		case 'f':
			n = atoi(c);
			if (f != n) {
				setfont(f);
				f = n;
				setfont(f);
			}
			break;
		case 'm':
			n = clr_get(c);
			if (m != n) {
				setcolor(m);
				m = n;
				setcolor(m);
			}
			break;
		case 's':
			n = atoi(c);
			if (s != n) {
				setsize(s);
				s = n;
				setsize(s);
			}
			break;
		case 'X':
			sprintf(cmd, "%c%c%s", c_ec, t, c);
			dir_append(cmd);
			break;
		case '<':
			setcolor(m);
			setfont(f);
			setsize(s);
			dir_cd = 1;
			setsize(s);
			setfont(f);
			setcolor(m);
			break;
		case '>':
			setcolor(m);
			setfont(f);
			setsize(s);
			dir_cd = 0;
			setsize(s);
			setfont(f);
			setcolor(m);
			break;
		}
		prev_s = src;
	}
	setcolor(m);
	setfont(f);
	setsize(s);
	dir_flush();
	r = n_td > 0 ? dbuf + dbuf_sz - dbuf_n : dbuf;
	r[dbuf_n] = '\0';
	dbuf_n = 0;
	sbuf_append(sbuf, r);
}

void dir_done(void)
{
	free(rbuf);
	free(dbuf);
}
