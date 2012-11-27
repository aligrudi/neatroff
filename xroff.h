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

#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

/* number registers */
extern int nreg[];
int num_get(int id);
int num_set(int id, int n);
int tr_int(char *s, int orig, int unit);

/* string registers */
void str_set(int id, char *s);
char *str_get(int id);

/* builtin number registers; n_X for .X register */
#define REG(c1, c2)	((c1) * 256 + (c2))
#define n_d		nreg[REG('.', 'd')]
#define n_f		nreg[REG('.', 'f')]
#define n_i		nreg[REG('.', 'i')]
#define n_l		nreg[REG('.', 'l')]
#define n_o		nreg[REG('.', 'o')]
#define n_p		nreg[REG('.', 'p')]
#define n_s		nreg[REG('.', 's')]
#define n_v		nreg[REG('.', 'v')]
#define n_nl		nreg[REG('n', 'l')]
#define n_pg		nreg[REG('%', '\0')]	/* % */
#define n_f0		nreg[REG('\0', 'f')]	/* last font */
#define n_s0		nreg[REG('\0', 's')]	/* last size */
#define n_ad		nreg[REG('\0', 'a')]	/* adjustment */

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

/* rendering */
void render(void);	/* read from in.c and print the output */
void output(char *s);	/* output the given rendered line */
void ren_page(int pg);

/* troff commands */
void tr_br(char **args);
void tr_sp(char **args);
void tr_nr(char **args);
void tr_ps(char **args);
void tr_ft(char **args);
void tr_fp(char **args);
void tr_bp(char **args);

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
