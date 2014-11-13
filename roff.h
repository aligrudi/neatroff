/*
 * Most functions and variables in neatroff are prefixed with tokens
 * that indicate their purpose, such as:
 *
 * + tr_xyz: the implementation of troff request .xyz (mostly tr.c)
 * + in_xyz: input layer (in.c)
 * + cp_xyz: copy-mode interpretation layer (cp.c)
 * + ren_xyz: rendering characters into lines (ren.c)
 * + out_xyz: output layer for generating troff output (out.c)
 * + dev_xyz: output devices (dev.c)
 * + num_xyz: number registers (reg.c)
 * + str_xyz: string registers (reg.c)
 * + env_xyz: environments (reg.c)
 * + eval_xyz: integer expression evaluation (eval.c)
 * + font_xyz: fonts (font.c)
 * + sbuf_xyz: variable length string buffers (sbuf.c)
 * + dict_xyz: dictionaries (dict.c)
 * + wb_xyz: word buffers (wb.c)
 * + fmt_xyz: line formatting buffers (fmt.c)
 * + n_xyz: builtin number register xyz
 * + c_xyz: characters for requests like hc and mc
 *
 */

/* predefined array limits */
#define PATHLEN		1024	/* path length */
#define NFILES		16	/* number of input files */
#define NFONTS		32	/* number of fonts */
#define NGLYPHS		1024	/* glyphs in fonts */
#define FNLEN		32	/* font name length */
#define NMLEN		32	/* macro/register/environment/glyph name length */
#define GNLEN		NMLEN	/* glyph name length */
#define RNLEN		NMLEN	/* register/macro name */
#define ILNLEN		1000	/* line limit of input files */
#define LNLEN		4000	/* line buffer length (ren.c/out.c) */
#define NWORDS		1024	/* number of queued words in formatting buffer */
#define NLINES		32	/* number of queued lines in formatting buffer */
#define NARGS		16	/* number of macro arguments */
#define NPREV		16	/* environment stack depth */
#define NTRAPS		1024	/* number of traps per page */
#define NIES		128	/* number of nested .ie commands */
#define NTABS		16	/* number of tab stops */
#define NCMAPS		512	/* number of character translations (.tr) */
#define NSSTR		32	/* number of nested sstr_push() calls */
#define NFIELDS		32	/* number of fields */
#define MAXFRAC		100000	/* maximum value of the fractional part */
#define NCDEFS		128	/* number of character definitions (.char) */
#define NHYPHS		16384	/* hyphenation dictionary/patterns (.hw) */
#define NHYPHSWORD	16	/* number of hyphenations per word */
#define NHCODES		512	/* number of .hcode characters */
#define WORDLEN		256	/* word length (for hyph.c) */
#define NFEATS		128	/* number of features per font */
#define NGRULES		4096	/* number of gsub/gpos rules per font */

/* converting scales */
#define SC_IN		(dev_res)	/* inch in units */
#define SC_PT		(SC_IN / 72)	/* point in units */
#define SC_EM		(n_s * SC_IN / 72)

/* escape sequences */
#define ESC_Q	"bCDhHlLNoRSvwxXZ?"	/* \X'ccc' quoted escape sequences */
#define ESC_P	"*fgkmns"		/* \Xc \X(cc \X[ccc] escape sequences */

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) < (b) ? (b) : (a))
#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

/* special characters */
extern int c_ec;	/* escape character (\) */
extern int c_cc;	/* basic control character (.) */
extern int c_c2;	/* no-break control character (') */
extern char c_pc[];	/* page number character (%) */
#define c_ni	4	/* non-interpreted copy-mode escape */
#define c_hc	env_hc()/* hyphenation character */
#define c_mc	env_mc()/* margin character (.mc) */
#define c_tc	env_tc()
#define c_lc	env_lc()
#define c_bp	"\\:"	/* zero-width word break point */

/* number registers */
#define num_get(id)	(*nreg(id))
void num_set(int id, int val);
void num_inc(int id, int pos);
void num_del(int id);
char *num_str(int id);
char *num_getfmt(int id);
void num_setfmt(int id, char *fmt);
void num_setinc(int id, int val);
int *nreg(int id);
int eval(char *s, int unit);
int eval_up(char **s, int unit);
int eval_re(char *s, int orig, int unit);

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
void env_done(void);
struct fmt *env_fmt(void);
struct wb *env_wb(void);
char *env_hc(void);
char *env_mc(void);
char *env_tc(void);
char *env_lc(void);
int tab_next(int pos);
int tab_type(int pos);

/* dictionary */
struct dict {
	int *head;
	char **key;
	long *val;
	int *next;
	int size;
	int n;
	char *buf;		/* buffer for keys */
	int buflen;
	int level2;		/* use two characters for hashing */
	long notfound;		/* the value returned for missing keys */
};

void dict_init(struct dict *d, int size, long notfound, int dupkeys, int level2);
void dict_done(struct dict *d);
void dict_put(struct dict *d, char *key, long val);
long dict_get(struct dict *d, char *key);
int dict_idx(struct dict *d, char *key);
char *dict_key(struct dict *d, int idx);
long dict_val(struct dict *d, int idx);
long dict_prefix(struct dict *d, char *key, int *idx);

/* device related variables */
extern int dev_res;
extern int dev_uwid;
extern int dev_hor;
extern int dev_ver;

struct glyph {
	char id[GNLEN];		/* device-dependent glyph identifier */
	char name[GNLEN];	/* the first character mapped to this glyph */
	struct font *font;	/* glyph font */
	short wid;		/* character width */
	short llx, lly, urx, ury;	/* character bounding box */
	short type;		/* character type; ascender/descender */
};

/* output device functions */
int dev_open(char *dir, char *dev);
void dev_close(void);
int dev_mnt(int pos, char *id, char *name);
int dev_pos(char *id);
struct font *dev_font(int pos);
int dev_fontpos(struct font *fn);
struct glyph *dev_glyph(char *c, int fn);

/* font-related functions */
struct font *font_open(char *path);
void font_close(struct font *fn);
struct glyph *font_glyph(struct font *fn, char *id);
struct glyph *font_find(struct font *fn, char *name);
int font_map(struct font *fn, char *name, char *id);
int font_mapped(struct font *fn, char *name);
int font_special(struct font *fn);
int font_wid(struct font *fn, int sz, int w);
int font_gwid(struct font *fn, struct font *cfn, int sz, int w);
int font_swid(struct font *fn, int sz, int ss);
void font_setcs(struct font *fn, int cs, int ps);
int font_getcs(struct font *fn);
void font_setbd(struct font *fn, int bd);
int font_getbd(struct font *fn);
void font_setzoom(struct font *fn, int zoom);
int font_zoom(struct font *fn, int sz);
int font_feat(struct font *fn, char *name, int val);
int font_layout(struct font *fn, struct glyph **src, int nsrc, int sz,
		struct glyph **dst, int *dmap,
		int *x, int *y, int *xadv, int *yadv, int lg, int kn);

/* different layers of neatroff */
int in_next(void);		/* input layer */
int cp_next(void);		/* copy-mode layer */
int tr_next(void);		/* troff layer */

void in_push(char *s, char **args);
void in_so(char *path);		/* .so request */
void in_nx(char *path);		/* .nx request */
void in_ex(void);		/* .ex request */
void in_lf(char *path, int ln);	/* .lf request */
void in_queue(char *path);	/* queue the given input file */
char *in_arg(int i);		/* look up argument */
int in_nargs(void);		/* number of arguments */
void in_back(int c);		/* push back input character */
int in_top(void);		/* the first pushed-back character */
char *in_filename(void);	/* current filename */
int in_lnum(void);		/* current line number */

void cp_blk(int skip);		/* skip or read the next line or block */
void cp_reqbeg(void);		/* beginning of a request line */
void cp_copymode(int mode);	/* do not interpret \w and \E */
#define cp_back		in_back	/* cp.c is stateless */
int tr_nextreq(void);		/* read the next troff request */

/* variable length string buffer */
struct sbuf {
	char *s;		/* allocated buffer */
	int sz;			/* buffer size */
	int n;			/* length of the string stored in s */
};

void sbuf_init(struct sbuf *sbuf);
void sbuf_done(struct sbuf *sbuf);
char *sbuf_buf(struct sbuf *sbuf);
void sbuf_add(struct sbuf *sbuf, int c);
void sbuf_append(struct sbuf *sbuf, char *s);
void sbuf_printf(struct sbuf *sbuf, char *s, ...);
void sbuf_cut(struct sbuf *sbuf, int n);
int sbuf_len(struct sbuf *sbuf);
int sbuf_empty(struct sbuf *sbuf);

/* word buffer */
struct wb {
	struct sbuf sbuf;
	int f, s, m;		/* the last output font and size */
	int r_f, r_s, r_m;	/* current font and size; use n_f and n_s if -1 */
	int part;		/* partial input (\c) */
	int els_neg, els_pos;	/* extra line spacing */
	int h, v;		/* buffer vertical and horizontal positions */
	int ct, sb, st;		/* \w registers */
	int llx, lly, urx, ury;	/* bounding box */
	int icleft;		/* pending left italic correction */
	/* queued subword */
	char sub_c[WORDLEN][GNLEN];	/* the collected subword */
	int sub_n;		/* collected subword length */
	int sub_collect;	/* enable subword collection */
};

void wb_init(struct wb *wb);
void wb_done(struct wb *wb);
void wb_hmov(struct wb *wb, int n);
void wb_vmov(struct wb *wb, int n);
void wb_els(struct wb *wb, int els);
void wb_etc(struct wb *wb, char *x);
void wb_put(struct wb *wb, char *c);
void wb_putraw(struct wb *wb, char *c);
void wb_putexpand(struct wb *wb, char *c);
int wb_part(struct wb *wb);
void wb_setpart(struct wb *wb);
void wb_drawl(struct wb *wb, int c, int h, int v);
void wb_drawc(struct wb *wb, int c, int r);
void wb_drawe(struct wb *wb, int c, int h, int v);
void wb_drawa(struct wb *wb, int c, int h1, int v1, int h2, int v2);
void wb_drawxbeg(struct wb *wb, int c);
void wb_drawxdot(struct wb *wb, int h, int v);
void wb_drawxend(struct wb *wb);
void wb_italiccorrection(struct wb *wb);
void wb_italiccorrectionleft(struct wb *wb);
void wb_cat(struct wb *wb, struct wb *src);
void wb_catstr(struct wb *wb, char *beg, char *end);
int wb_wid(struct wb *wb);
int wb_hpos(struct wb *wb);
int wb_vpos(struct wb *wb);
int wb_empty(struct wb *wb);
int wb_eos(struct wb *wb);
void wb_wconf(struct wb *wb, int *ct, int *st, int *sb,
		int *llx, int *lly, int *urx, int *ury);
void wb_reset(struct wb *wb);
char *wb_buf(struct wb *wb);
void wb_fnszget(struct wb *wb, int *fn, int *sz, int *m);
void wb_fnszset(struct wb *wb, int fn, int sz, int m);
int wb_dashwid(struct wb *wb);
int c_isdash(char *c);

/* character translation (.tr) */
void cmap_add(char *c1, char *c2);
char *cmap_map(char *c);
/* character definition (.char) */
char *cdef_map(char *c, int fn);
int cdef_expand(struct wb *wb, char *c, int fn);

/* hyphenation flags */
#define HY_LAST		0x02	/* do not hyphenate last lines */
#define HY_FINAL2	0x04	/* do not hyphenate the final two characters */
#define HY_FIRST2	0x08	/* do not hyphenate the first two characters */

void hyphenate(char *hyphs, char *word, int flg);
void hyph_init(void);

/* adjustment types */
#define AD_C		0	/* center */
#define AD_L		1	/* adjust left margin (flag) */
#define AD_R		2	/* adjust right margin (flag) */
#define AD_B		3	/* adjust both margin (mask) */
#define AD_P		4	/* paragraph-at-once adjustment (flag) */

/* line formatting */
struct fmt *fmt_alloc(void);
void fmt_free(struct fmt *fmt);
int fmt_wid(struct fmt *fmt);
void fmt_space(struct fmt *fmt);
void fmt_suppressnl(struct fmt *fmt);
int fmt_word(struct fmt *fmt, struct wb *wb);
int fmt_newline(struct fmt *fmt);
int fmt_fillreq(struct fmt *f);
int fmt_fill(struct fmt *fmt, int br);
int fmt_morelines(struct fmt *fmt);
int fmt_morewords(struct fmt *fmt);
int fmt_nextline(struct fmt *fmt, struct sbuf *sbuf, int *w,
		int *li, int *ll, int *els_neg, int *els_pos);

/* rendering */
int render(void);				/* the main loop */
int ren_parse(struct wb *wb, char *c);
int ren_char(struct wb *wb, int (*next)(void), void (*back)(int));
int ren_wid(int (*next)(void), void (*back)(int));
void ren_tl(int (*next)(void), void (*back)(int));
void ren_hline(struct wb *wb, int l, char *c);	/* horizontal line */
void ren_hlcmd(struct wb *wb, char *arg);	/* \l */
void ren_vlcmd(struct wb *wb, char *arg);	/* \L */
void ren_bcmd(struct wb *wb, char *arg);	/* \b */
void ren_ocmd(struct wb *wb, char *arg);	/* \o */
void ren_dcmd(struct wb *wb, char *arg);	/* \D */
void ren_zcmd(struct wb *wb, char *arg);	/* \Z */

/* out.c */
void out_line(char *s);				/* output rendered line */
void out(char *s, ...);				/* output troff cmd */

/* troff commands */
void tr_ab(char **args);
void tr_bp(char **args);
void tr_br(char **args);
void tr_ce(char **args);
void tr_ch(char **args);
void tr_cl(char **args);
void tr_di(char **args);
void tr_divbeg(char **args);
void tr_divend(char **args);
void tr_divvs(char **args);
void tr_dt(char **args);
void tr_em(char **args);
void tr_ev(char **args);
void tr_fc(char **args);
void tr_fi(char **args);
void tr_fp(char **args);
void tr_fspecial(char **args);
void tr_ft(char **args);
void tr_hcode(char **args);
void tr_hpf(char **args);
void tr_hpfa(char **args);
void tr_hw(char **args);
void tr_in(char **args);
void tr_ll(char **args);
void tr_mk(char **args);
void tr_ne(char **args);
void tr_nf(char **args);
void tr_ns(char **args);
void tr_os(char **args);
void tr_pn(char **args);
void tr_ps(char **args);
void tr_rs(char **args);
void tr_rt(char **args);
void tr_sp(char **args);
void tr_sv(char **args);
void tr_ta(char **args);
void tr_ti(char **args);
void tr_wh(char **args);
void tr_popren(char **args);
void tr_transparent(char **args);

void tr_init(void);
int tr_readargs(char **args, struct sbuf *sbuf,
		int (*next)(void), void (*back)(int));

/* helpers */
void errmsg(char *msg, ...);
void errdie(char *msg);
void *xmalloc(long len);
int utf8len(int c);
int utf8next(char *s, int (*next)(void));
int utf8read(char **s, char *d);
int utf8one(char *s);
int charnext(char *c, int (*next)(void), void (*back)(int));
int charread(char **s, char *c);
int charnext_delim(char *c, int (*next)(void), void (*back)(int), char *delim);
int charread_delim(char **s, char *c, char *delim);
void charnext_str(char *d, char *c);
void quotednext(char *d, int (*next)(void), void (*back)(int));
void unquotednext(char *d, int cmd, int (*next)(void), void (*back)(int));
int escread(char **s, char *d);
/* string streams; nested next()/back() interface for string buffers */
void sstr_push(char *s);
char *sstr_pop(void);
int sstr_next(void);
void sstr_back(int c);

/* internal commands */
#define TR_DIVBEG	"\07<"	/* diversion begins */
#define TR_DIVEND	"\07>"	/* diversion ends */
#define TR_DIVVS	"\07V"	/* the amount of \n(.v inside diversions */
#define TR_POPREN	"\07P"	/* exit render_rec() */

/* mapping register, macro and environment names to indices */
#define NREGS		4096	/* maximum number of mapped names */
#define DOTMAP(c2)	(c2)	/* optimized mapping for ".x" names */

int map(char *s);		/* map name s to an index */
char *map_name(int id);		/* return the name mapped to id */

/* colors */
#define CLR_R(c)		(((c) >> 16) & 0xff)
#define CLR_G(c)		(((c) >> 8) & 0xff)
#define CLR_B(c)		((c) & 0xff)
#define CLR_RGB(r, g, b)	(((r) << 16) | ((g) << 8) | (b))

char *clr_str(int c);
int clr_get(char *s);

/* builtin number registers; n_X for .X register */
#define n_a		(*nreg(DOTMAP('a')))
#define n_cp		(*nreg(DOTMAP('C')))
#define n_d		(*nreg(DOTMAP('d')))
#define n_f		(*nreg(DOTMAP('f')))
#define n_h		(*nreg(DOTMAP('h')))
#define n_i		(*nreg(DOTMAP('i')))
#define n_it		(*nreg(map(".it")))	/* .it trap macro */
#define n_itn		(*nreg(map(".itn")))	/* .it lines left */
#define n_j		(*nreg(DOTMAP('j')))
#define n_l		(*nreg(DOTMAP('l')))
#define n_L		(*nreg(DOTMAP('L')))
#define n_n		(*nreg(DOTMAP('n')))
#define n_nI		(*nreg(map(".nI")))	/* i for .nm */
#define n_nm		(*nreg(map(".nm")))	/* .nm enabled */
#define n_nM		(*nreg(map(".nM")))	/* m for .nm */
#define n_nn		(*nreg(map(".nn")))	/* remaining .nn */
#define n_nS		(*nreg(map(".nS")))	/* s for .nm */
#define n_m		(*nreg(DOTMAP('m')))
#define n_mc		(*nreg(map(".mc")))	/* .mc enabled */
#define n_mcn		(*nreg(map(".mcn")))	/* .mc distance */
#define n_o		(*nreg(DOTMAP('o')))
#define n_p		(*nreg(DOTMAP('p')))
#define n_s		(*nreg(DOTMAP('s')))
#define n_u		(*nreg(DOTMAP('u')))
#define n_v		(*nreg(DOTMAP('v')))
#define n_ct		(*nreg(map("ct")))
#define n_dl		(*nreg(map("dl")))
#define n_dn		(*nreg(map("dn")))
#define n_ln		(*nreg(map("ln")))
#define n_nl		(*nreg(map("nl")))
#define n_sb		(*nreg(map("sb")))
#define n_st		(*nreg(map("st")))
#define n_pg		(*nreg(map("%")))	/* % */
#define n_lb		(*nreg(map(".b0")))	/* input line beg */
#define n_ce		(*nreg(map(".ce")))	/* .ce remaining */
#define n_f0		(*nreg(map(".f0")))	/* last .f */
#define n_lg		(*nreg(map(".lg")))	/* .lg mode */
#define n_hy		(*nreg(map(".hy")))	/* .hy mode */
#define n_hyp		(*nreg(map(".hyp")))	/* hyphenation penalty  */
#define n_i0		(*nreg(map(".i0")))	/* last .i */
#define n_ti		(*nreg(map(".ti")))	/* pending .ti */
#define n_kn		(*nreg(map(".kern")))	/* .kn mode */
#define n_l0		(*nreg(map(".l0")))	/* last .l */
#define n_L0		(*nreg(map(".L0")))	/* last .L */
#define n_m0		(*nreg(map(".m0")))	/* last .m */
#define n_mk		(*nreg(map(".mk")))	/* .mk internal register */
#define n_na		(*nreg(map(".na")))	/* .na mode */
#define n_ns		(*nreg(map(".ns")))	/* .ns mode */
#define n_o0		(*nreg(map(".o0")))	/* last .o */
#define n_pmll		(*nreg(map(".pmll")))	/* minimum last line (.pmll) */
#define n_ss		(*nreg(map(".ss")))	/* word space (.ss) */
#define n_sss		(*nreg(map(".sss")))	/* sentence space (.ss) */
#define n_ssh		(*nreg(map(".ssh")))	/* word space compression (.ssh) */
#define n_s0		(*nreg(map(".s0")))	/* last .s */
#define n_sv		(*nreg(map(".sv")))	/* .sv value */
#define n_lt		(*nreg(map(".lt")))	/* .lt value */
#define n_t0		(*nreg(map(".lt0")))	/* previous .lt value */
#define n_v0		(*nreg(map(".v0")))	/* last .v */
#define n_llx		(*nreg(map("bbllx")))	/* \w bounding box */
#define n_lly		(*nreg(map("bblly")))	/* \w bounding box */
#define n_urx		(*nreg(map("bburx")))	/* \w bounding box */
#define n_ury		(*nreg(map("bbury")))	/* \w bounding box */

/* functions for implementing read-only registers */
int f_nexttrap(void);	/* .t */
int f_divreg(void);	/* .z */
int f_hpos(void);	/* .k */
