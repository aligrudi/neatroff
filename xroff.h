/* converting scales */
#define SC_IN		(dev_res)	/* inch in units */
#define SC_PT		(SC_IN / 72)	/* point in units */

/* predefined array limits */
#define PATHLEN		1024	/* path length */
#define NFONTS		32	/* number of fonts */
#define FNLEN		32	/* font name length */
#define NGLYPHS		512	/* glyphs in fonts */
#define GNLEN		32	/* glyph name length */
#define ILNLEN		256	/* line limit of input files */
#define LNLEN		4000	/* line buffer length (ren.c/out.c) */
#define NWORDS		1000	/* number of words in line buffer */
#define NARGS		9	/* number of macro arguments */
#define RLEN		4	/* register/macro name */
#define NPREV		16	/* environment stack depth */
#define NTRAPS		1024	/* number of traps per page */
#define NIES		128	/* number of nested .ie commands */

/* escape sequences */
#define ESC_Q	"bCDhHlLNoSvwxX"	/* quoted escape sequences */
#define ESC_P	"*fgkns"		/* 1 or 2-char escape sequences */

#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

/* number registers */
char *num_get(int id);
int *nreg(int id);
int eval(char *s, int orig, int unit);

/* string registers */
void str_set(int id, char *s);
void str_dset(int id, void *d);
char *str_get(int id);
void *str_dget(int id);
void str_rm(int id);
void str_rn(int src, int dst);

/* saving and restoring registers before and after printing diverted lines */
void odiv_beg(void);
void odiv_end(void);

/* enviroments */
void env_init(void);
void env_free(void);
struct adj *env_adj(void);

/* device related variables */
extern int dev_res;
extern int dev_uwid;
extern int dev_hor;
extern int dev_ver;

struct glyph {
	char name[FNLEN];	/* name of the glyph */
	char id[FNLEN];		/* device-dependent glyph identifier */
	struct font *font;	/* glyph font */
	int wid;		/* character width */
	int type;		/* character type; ascender/descender */
};

struct font {
	char name[FNLEN];
	struct glyph glyphs[NGLYPHS];
	int nglyphs;
	int spacewid;
	int special;
	char c[NGLYPHS][FNLEN];		/* character names in charset */
	struct glyph *g[NGLYPHS];	/* character glyphs in charset */
	int n;				/* number of characters in charset */
};

/* troff output function */
#define OUT	printf

/* output device functions */
int dev_open(char *path);
void dev_close(void);
int dev_mnt(int pos, char *id, char *name);
int dev_font(char *id);
int charwid(int wid, int sz);

/* font-related functions */
struct font *font_open(char *path);
void font_close(struct font *fn);
struct glyph *font_glyph(struct font *fn, char *id);
struct glyph *font_find(struct font *fn, char *name);

/* glyph handling functions */
struct glyph *dev_glyph(char *c, int fn);
struct glyph *dev_glyph_byid(char *id, int fn);
int dev_spacewid(void);

/* different layers of neatroff */
int in_next(void);	/* input layer */
int cp_next(void);	/* copy-mode layer */
int tr_next(void);	/* troff layer */
void in_push(char *s, char **args);
char *in_arg(int i);
void in_back(int c);
void cp_back(int c);
void cp_skip(void);	/* skip current input line or block */

/* rendering */
void render(void);	/* read from in.c and print the output */
void output(char *s);	/* output the given rendered line */
void ren_page(int pg);

/* troff commands */
void tr_bp(char **args);
void tr_br(char **args);
void tr_ch(char **args);
void tr_di(char **args);
void tr_divbeg(char **args);
void tr_divend(char **args);
void tr_dt(char **args);
void tr_ev(char **args);
void tr_fi(char **args);
void tr_fp(char **args);
void tr_ft(char **args);
void tr_in(char **args);
void tr_ne(char **args);
void tr_nf(char **args);
void tr_ps(char **args);
void tr_sp(char **args);
void tr_wh(char **args);

void tr_init(void);

/* helpers */
void errmsg(char *msg, ...);
int utf8len(int c);

/* variable length string buffer */
struct sbuf {
	char *s;
	int sz;
	int n;
};

void sbuf_init(struct sbuf *sbuf);
void sbuf_done(struct sbuf *sbuf);
char *sbuf_buf(struct sbuf *sbuf);
void sbuf_add(struct sbuf *sbuf, int c);
void sbuf_append(struct sbuf *sbuf, char *s);
void sbuf_putnl(struct sbuf *sbuf);
int sbuf_empty(struct sbuf *sbuf);

/* diversions */
#define DIV_BEG		".&<"
#define DIV_END		".&>"

/* adjustment */
#define ADJ_L		0
#define ADJ_B		1
#define ADJ_N		2		/* no adjustment (.nf) */

struct adj *adj_alloc(void);
void adj_free(struct adj *adj);
int adj_fill(struct adj *adj, int mode, int linelen, char *dst);
void adj_put(struct adj *adj, int wid, char *s, ...);
void adj_swid(struct adj *adj, int swid);
int adj_full(struct adj *adj, int mode, int linelen);
int adj_empty(struct adj *adj, int mode);
int adj_wid(struct adj *adj);

/* builtin number registers; n_X for .X register */
#define REG(c1, c2)	((c1) * 256 + (c2))
#define n_d		(*nreg(REG('.', 'd')))
#define n_f		(*nreg(REG('.', 'f')))
#define n_i		(*nreg(REG('.', 'i')))
#define n_j		(*nreg(REG('.', 'j')))
#define n_k		(*nreg(REG('.', 'k')))
#define n_l		(*nreg(REG('.', 'l')))
#define n_n		(*nreg(REG('.', 'n')))
#define n_o		(*nreg(REG('.', 'o')))
#define n_p		(*nreg(REG('.', 'p')))
#define n_s		(*nreg(REG('.', 's')))
#define n_u		(*nreg(REG('.', 'u')))
#define n_v		(*nreg(REG('.', 'v')))
#define n_ct		(*nreg(REG('c', 't')))
#define n_dl		(*nreg(REG('d', 'l')))
#define n_dn		(*nreg(REG('d', 'n')))
#define n_nl		(*nreg(REG('n', 'l')))
#define n_pg		(*nreg(REG('%', '\0')))	/* % */
#define n_f0		(*nreg(REG(0, 'f')))	/* last font */
#define n_s0		(*nreg(REG(0, 's')))	/* last size */

/* functions for implementing read-only registers */
int f_nexttrap(void);	/* .t */
int f_divreg(void);	/* .z */
int f_hpos(void);	/* .k */
