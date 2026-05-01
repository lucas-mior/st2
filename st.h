/* See LICENSE for license details. */

#ifndef ST_H
#define ST_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>

typedef unsigned char uchar;
typedef unsigned long ulong;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef size_t usize;
typedef ssize_t isize;

/* Arbitrary sizes */
#define UTF_INVALID 0xFFFD
#define UTF_SIZ 4
#define ESC_BUF_SIZ (128*UTF_SIZ)
#define ESC_ARG_SIZ 16
#define STR_BUF_SIZ ESC_BUF_SIZ
#define STR_ARG_SIZ ESC_ARG_SIZ
#define HISTORY_SIZE 2000
#define RESIZE_BUFFER 1000

/* macros */
#define SIZEOF(X) (int64)sizeof(X)
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LENGTH(a)			(int32)(SIZEOF(a) / SIZEOF(*a))
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define ATTRCMP(a, b)		((a).mode != (b).mode || (a).fg != (b).fg || \
				(a).bg != (b).bg)
#define TIMEDIFF(t1, t2)	((float)(t1.tv_sec-t2.tv_sec)*1000 + \
				(float)(t1.tv_nsec-t2.tv_nsec)/1E6f)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define TRUECOLOR(r,g,b)	(1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)		(1 << 24 & (x))

#define error(...) fprintf(stderr, __VA_ARGS__)

/* X modifiers */
#define XK_ANY_MOD UINT_MAX
#define XK_NO_MOD 0
#define XK_SWITCH_MOD (1 << 13 | 1 << 14)

enum GlyphAttribute {
	ATTR_NULL       = 0,
	ATTR_SET        = 1 << 0,
	ATTR_BOLD       = 1 << 1,
	ATTR_FAINT      = 1 << 2,
	ATTR_ITALIC     = 1 << 3,
	ATTR_UNDERLINE  = 1 << 4,
	ATTR_BLINK      = 1 << 5,
	ATTR_REVERSE    = 1 << 6,
	ATTR_INVISIBLE  = 1 << 7,
	ATTR_STRUCK     = 1 << 8,
	ATTR_WRAP       = 1 << 9,
	ATTR_WIDE       = 1 << 10,
	ATTR_WDUMMY     = 1 << 11,
	ATTR_SELECTED   = 1 << 12,
	ATTR_BOXDRAW    = 1 << 13,
	ATTR_SIXEL      = 1 << 16,
	ATTR_BOLD_FAINT = ATTR_BOLD | ATTR_FAINT,
};

enum SelectionMode {
	SELECTION_IDLE = 0,
	SELECTION_EMPTY = 1,
	SELECTION_READY = 2
};

enum SelectionType {
	SELECTION_REGULAR = 1,
	SELECTION_RECTANGULAR = 2
};

enum SelectionSnap {
	SELECTION_SNAP_WORD = 1,
	SELECTION_SNAP_LINE = 2
};

#define Glyph Glyph_
typedef struct {
	uint32 rune;           /* character code */
	uint16 mode;      /* attribute flags */
	uint16 padding;
	int32 fg;      /* foreground  */
	int32 bg;      /* background  */
} Glyph;

typedef union {
	int32 i;
	uint32 ui;
	float f;
	const void *v;
	const char *s;
} Arg;

typedef struct {
    uint32 mod;
    uint32 button;
    void (*func)(const Arg *);
    const Arg arg;
    uint32 release;
} MouseShortcut;

typedef struct ImageList {
	struct ImageList *next, *prev;
	unsigned char *pixels;
	void *pixmap;
	void *clipmask;
	int width;
	int height;
	int x;
	int y;
	int reflow_y;
	int cols;
	int cw;
	int ch;
	int transparent;
} ImageList;

enum term_mode {
    TERM_MODE_WRAP         = 1 << 0,
    TERM_MODE_INSERT       = 1 << 1,
    TERM_MODE_ALTSCREEN    = 1 << 2,
    TERM_MODE_CRLF         = 1 << 3,
    TERM_MODE_ECHO         = 1 << 4,
    TERM_MODE_PRINT        = 1 << 5,
    TERM_MODE_UTF8         = 1 << 6,
	TERM_MODE_SIXEL        = 1 << 7,
	TERM_MODE_SIXEL_CUR_RT = 1 << 8,
	TERM_MODE_SIXEL_SDM    = 1 << 9
};

enum scroll_mode {
    SCROLL_RESIZE = -1,
    SCROLL_NOSAVEHIST = 0,
    SCROLL_SAVEHIST = 1
};

enum cursor_movement {
    CURSOR_SAVE,
    CURSOR_LOAD
};

enum cursor_state {
    CURSOR_DEFAULT = 0,
    CURSOR_WRAPNEXT = 1,
    CURSOR_ORIGIN = 2
};

enum charset {
    CS_GRAPHIC0,
    CS_GRAPHIC1,
    CS_UK,
    CS_USA,
    CS_MULTI,
    CS_GER,
    CS_FIN
};

enum escape_state {
    ESC_START = 1,
    ESC_CSI = 2,
    ESC_STR = 4, /* DCS, OSC, PM, APC */
    ESC_ALTCHARSET = 8,
    ESC_STR_END = 16, /* a final string was encountered */
    ESC_TEST = 32,    /* Enter in test mode */
    ESC_UTF8 = 64,
    ESC_SIXEL = 128,  /* Sixel data stream active */
};

typedef struct {
    Glyph attr; /* current char attributes */
    int32 x;
    int32 y;
    char state;
} TCursor;

typedef struct {
    int32 mode;
    int32 type;
    int32 snap;
    /*
     * Selection variables:
     * nb – normalized coordinates of the beginning of the selection
     * ne – normalized coordinates of the end of the selection
     * ob – original coordinates of the beginning of the selection
     * oe – original coordinates of the end of the selection
     */
    struct {
        int32 x;
        int32 y;
    } nb, ne, ob, oe;

    int32 alt;
} Selection;

/* Internal representation of the screen */
static struct {
    int32 nrows;               /* nb row */
    int32 ncols;               /* nb col */
    Glyph **line;              /* screen */
    Glyph *hist[HISTORY_SIZE]; /* history buffer */
    int32 i_hist;              /* history index */
    int32 n_hist;              /* nb history available */
    int32 lines_scrolled_up;   /* scroll back */
    int32 wrap_char_width[2];  /* used in updating WRAPNEXT when resizing */
    bool *dirty;               /* dirtyness of lines */
    TCursor cursor;            /* cursor */
    int32 old_cursor_x;        /* old cursor col */
    int32 old_cursor_y;        /* old cursor row */
    int32 top_scroll_limit;    /* top    scroll limit */
    int32 bot_scroll_limit;    /* bottom scroll limit */
    int32 mode;                /* terminal mode flags */
    int32 esc;                 /* escape state flags */
    char translation_table[4]; /* charset table translation */
    int32 charset;             /* current charset */
    int32 icharset;            /* selection_is_selected charset for sequence */
    int32 *tabs;
    uint32 last_char; /* last printed char outside of sequence, 0 if control */
	ImageList *images;
	ImageList *images_alt;
} term;

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
typedef struct {
    char buffer[ESC_BUF_SIZ]; /* raw string */
    int64 len;                /* raw string length */
    char priv;
    int32 arg[ESC_ARG_SIZ];
    int32 narg; /* nb of args */
    char mode[2];
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct {
    char type;    /* ESC type ... */
    char *buffer; /* allocated raw string */
    uint64 siz;   /* allocation size */
    uint64 len;   /* raw string length */
    char *args[STR_ARG_SIZ];
    int32 narg; /* nb of args */
} STREscape;

void die(const char *, ...) __attribute__((noreturn));
void redraw(void);
void draw(void);

void dcshandle(void);
void user_scroll_down(const Arg *);
void user_scroll_up(const Arg *);
void externalpipe(const Arg *);
void user_print_screen(const Arg *);
void user_print_sel(const Arg *);
void user_send_break(const Arg *);
void user_toggle_printer(const Arg *);

static void tdeleteimages(void);
static inline void tsetsixelattr(Glyph *line, int x1, int x2);

int32 term_attr_set(int32);
int32 tisaltscreen(void);
void term_resize(int32, int32);
void term_set_dirt_attr(int32);
void tty_hangup(void);
int32 tty_new(const char *, char *, const char *, char **);
int64 tty_read(void);
void tty_resize(int32, int32);
void tty_write(const char *, int64, int32);

void reset_title(void);

void selection_clear(void);
void selection_start(int32, int32, int32);
void selection_extend(int32, int32, int32, int32);
int32 selection_is_selected(int32, int32);
char *selection_get(void);

int64 utf8_encode(uint32, char *);

void *xmalloc(int64);
void xfree(void *);
void *xrealloc(void *, int64);
char *xstrdup(const char *);

int32 isboxdraw(uint32);
uint16 boxdrawindex(const Glyph *);
#ifdef XFT_VERSION
/* only exposed to main.c, otherwise we'll need Xft.h for the types */
void boxdraw_xinit(Display *, Colormap, XftDraw *, Visual *);
void drawboxes(int32, int32, int32, int32, XftColor *, XftColor *, const XftGlyphFontSpec *, int32);
#endif

static void exec_shell(char *, char **) __attribute__((noreturn));
static void stty(char **);
static void handler_sigchld(int32);
static void tty_write_raw(const char *, int64);

static void control_seq_intro_dump(void);
static void control_seq_intro_handle(void);
static void control_seq_intro_parse(void);
static void control_seq_intro_reset(void);
static void osc_color_response(int32, int32, int32);
static int32 eschandle(uchar);
static void string_handle(void);

static void term_printer(char *, int64);
static void term_dump_sel(void);
static void term_dump_line(int32);
static void term_dump(void);
static void term_clear_region(int32, int32, int32, int32, int32);
static void term_cursor(int32);
static void term_clear_glyph(Glyph *, int32);
static void term_reset_cursor(void);
static void term_delete_char(int32);
static void term_delete_line(int32);
static void term_insert_blank(int32);
static void term_insert_blank_line(int32);
static int32 term_line_len(Glyph *len);
static int32 term_is_wrapped(Glyph *line);
static char *term_get_glyphs(char *, const Glyph *, const Glyph *);
static void term_move_to(int32, int32);
static void term_move_abs_to(int32, int32);
static void term_new_line(int32);
static void term_put_tab(int32);
static void term_putc(uint32);
static void term_reset(void);
static void term_scroll_up(int32, int32, int32, int32);
static void term_scroll_down(int32, int32);
static void term_reflow(int32, int32);
static void reflow_scroll_down(int32);
static void term_resize_def(int32, int32);
static void term_resize_alt(int32, int32);
static void term_set_attr(const int32 *, int32);
static void term_set_char(uint32, const Glyph *, int32, int32);
static void term_set_dirt(int32, int32);
static void term_swap_screen(void);
static void term_load_def_screen(int32, int32);
static void term_load_alt_screen(int32, int32);
static void term_set_mode(int32, int32, const int32 *, int32);
static int32 term_write(const char *, int32, int32);
static void term_full_dirt(void);
static void term_control_code(uchar);
static void term_dec_test(char);
static void term_def_utf8(char);
static int32_t term_def_color(const int32 *, int32 *, int32);
static void term_def_tran(char);
static void term_str_sequence(uchar);

static void selection_normalize(void);
static void selection_scroll(int32, int32, int32);
static void selection_move(int32);
static void selection_remove(void);
static int32 selection_is_selected4(int32, int32, int32, int32);
static void SelectionSnap(int32 *, int32 *, int32);

static int64 utf8_decode(const char *, uint32 *, int64);
static uint32 utf8_decode_byte(char, int64 *);
static char utf8_encode_byte(uint32, int64);
static int64 utf8_validate(uint32 *, int64);

static char *base64_decode(const char *);
static char base64_decode_getc(const char **);

static int64 xwrite(int32, const char *, int64);

/* config.def.h globals */
extern char *CONF_UTMP;
extern char *CONF_STTY_ARGS;
extern char *CONF_VTIDEN;
extern wchar_t *CONF_WORD_DELIMITERS;
extern int32 CONF_ALLOW_ALT_SCREEN;
extern int32 CONF_ALLOW_WINDOW_OPS;
extern char *CONF_TERM_NAME;
extern int32 CONF_TAB_NSPACES;
extern int32 CONF_COLOR_INDEX_FONT;
extern int32 CONF_COLOR_BG;
extern int32 CONF_COLOR_INDEX_CURSOR;
extern const int32 CONF_BOXDRAW, CONF_BOXDRAW_BOLD, CONF_BOXDRAW_BRAILLE;
extern float CONF_ALPHA;

/* function definitions used in config.def.h */
void user_clipboard_copy(const Arg *);
void user_clipboard_paste(const Arg *);
void user_toggle_numlock(const Arg *);
void user_selection_paste(const Arg *);
void user_change_alpha(const Arg *);
void user_zoom(const Arg *);
void zoom_abs(const Arg *);
void user_zoom_reset(const Arg *);
void user_tty_send(const Arg *);

typedef struct {
    uint32 mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Shortcut;

typedef struct {
    KeySym k;
    uint32 mask;
    char *s;
    /* three-valued logic variables: 0 indifferent, 1 on, -1 off */
    char appkey;    /* application keypad */
    char appcursor; /* application cursor */
} Key;

enum win_mode {
	WIN_MODE_VISIBLE     = 1 << 0,
	WIN_MODE_FOCUSED     = 1 << 1,
	WIN_MODE_APPKEYPAD   = 1 << 2,
	WIN_MODE_MOUSEBTN    = 1 << 3,
	WIN_MODE_MOUSEMOTION = 1 << 4,
	WIN_MODE_REVERSE     = 1 << 5,
	WIN_MODE_KBDLOCK     = 1 << 6,
	WIN_MODE_HIDE        = 1 << 7,
	WIN_MODE_APPCURSOR   = 1 << 8,
	WIN_MODE_MOUSESGR    = 1 << 9,
	WIN_MODE_8BIT        = 1 << 10,
	WIN_MODE_BLINK       = 1 << 11,
	WIN_MODE_FBLINK      = 1 << 12,
	WIN_MODE_FOCUS       = 1 << 13,
	WIN_MODE_MOUSEX10    = 1 << 14,
	WIN_MODE_MOUSEMANY   = 1 << 15,
	WIN_MODE_BRCKTPASTE  = 1 << 16,
	WIN_MODE_NUMLOCK     = 1 << 17,
	WIN_MODE_MOUSE       = WIN_MODE_MOUSEBTN|WIN_MODE_MOUSEMOTION|WIN_MODE_MOUSEX10\
	                  |WIN_MODE_MOUSEMANY,
};

void x_bell(void);
void x_draw_cursor(int32, int32, Glyph, int32, int32, Glyph);
void x_draw_line(Glyph *, int32, int32, int32);
void x_finish_draw(void);
void x_load_cols(void);
int32 x_set_color_name(int32, const char *);
void x_set_icon_title(char *);
void x_set_title(char *);
int32 x_set_cursor(int32);
void x_set_mode(int32, uint32);
void x_set_pointer_motion(int32);
int32 x_start_draw(void);
void x_xim_spot(int32, int32);

void selection_set(char *, Time);

typedef XftColor Color;
typedef XftGlyphFontSpec GlyphFontSpec;

static struct {
    int32 tty_width, tty_height; /* tty width and height */
    int32 w, h;                  /* window width and height */
    int32 hborderpx, vborderpx;
    int32 ch;     /* char height */
    int32 cw;     /* char width  */
    int32 mode;   /* window state/mode flags */
    int32 cursor; /* cursor style */
} term_window;

static struct {
    Display *display;
    Colormap color_map;
    Window win;
    Drawable drawable;
    GlyphFontSpec *specbuf; /* font spec buffer used for rendering */
    Atom xembed;
	Atom wm_delete_win;
	Atom net_wm_name;
	Atom net_wm_iconname;
	Atom net_wm_pid;
    struct {
        XIM xim;
        XIC xic;
        XPoint point;
        XVaNestedList spotlist;
    } ime;
    XftDraw *xft_draw;
    Visual *visual;
    XSetWindowAttributes attrs;
    int32 screen;
    int32 is_fixed; /* is fixed geometry? */
    int32 depth;   /* bit depth */
    int32 left_offset;
	int32 top_offset;    /* left and top offset */
    int32 geo_mask;      /* geometry mask */
} x_window;

#define Font Font_
typedef struct {
    int32 height;
    int32 width;
    int32 ascent;
    int32 descent;
    int32 badslant;
    int32 badweight;
    int16 lbearing;
    int16 rbearing;
    XftFont *match;
    FcFontSet *set;
    FcPattern *pattern;
} Font;

typedef struct {
    Color *color;
    int32 collen;
    Font font, bfont, ifont, ibfont;
    GC graphics;
} DrawingContext;

typedef struct {
    Atom xtarget;
    char *primary, *clipboard;
    struct timespec tclick1;
    struct timespec tclick2;
} XSelection;
static XSelection xsel;

static DrawingContext draw_context;

#include "sixel.h"
sixel_state_t sixel_st;

#endif /* ST_H */
