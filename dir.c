#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roff.h"

#define DBUFLEN		(1 << 19)

static char dbuf[DBUFLEN + 1];	/* text in n_td direction */
static int dbuflen;
static char rbuf[DBUFLEN + 1];	/* text in reverse direction */
static int rbuflen;
static int dir_cd;		/* current direction */

static int dir_copy(char *d, int dlen, char *s, int slen, int dir)
{
	if (dir > 0)
		memcpy(d + DBUFLEN - dlen - slen, s, slen);
	else
		memcpy(d + dlen, s, slen);
	return dlen + slen;
}

static void dir_flush(void)
{
	char *s = rbuf + (n_td > 0 ? 0 : DBUFLEN - rbuflen);
	dbuflen = dir_copy(dbuf, dbuflen, s, rbuflen, n_td);
	rbuflen = 0;
}

static void dir_append(char *s)
{
	int dir = dir_cd > 0;
	if (dir == n_td && rbuflen)
		dir_flush();
	if (dir == n_td)
		dbuflen = dir_copy(dbuf, dbuflen, s, strlen(s), dir);
	else
		rbuflen = dir_copy(rbuf, rbuflen, s, strlen(s), dir);
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
	char cmd[ILNLEN];
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
	if (rbuflen)
		dir_flush();
	r = n_td > 0 ? dbuf + DBUFLEN - dbuflen : dbuf;
	r[dbuflen] = '\0';
	dbuflen = 0;
	sbuf_append(sbuf, r);
}
