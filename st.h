/* See LICENSE for license details. */

#ifndef ST_H
#define ST_H

#include <stdint.h>
#include <stddef.h>
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

typedef uint_least32_t Rune;

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

enum selection_snap {
	SELECTION_SNAP_WORD = 1,
	SELECTION_SNAP_LINE = 2
};

#define Glyph Glyph_
typedef struct {
	Rune rune;           /* character code */
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

void die(const char *, ...) __attribute__((noreturn));
void redraw(void);
void draw(void);

void user_scroll_down(const Arg *);
void user_scroll_up(const Arg *);
void externalpipe(const Arg *);
void user_print_screen(const Arg *);
void user_print_sel(const Arg *);
void user_send_break(const Arg *);
void user_toggle_printer(const Arg *);

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

int64 utf8_encode(Rune, char *);

void *xmalloc(int64);
void xfree(void *);
void *xrealloc(void *, int64);
char *xstrdup(const char *);

int32 isboxdraw(Rune);
uint16 boxdrawindex(const Glyph *);
#ifdef XFT_VERSION
/* only exposed to main.c, otherwise we'll need Xft.h for the types */
void boxdraw_xinit(Display *, Colormap, XftDraw *, Visual *);
void drawboxes(int32, int32, int32, int32, XftColor *, XftColor *, const XftGlyphFontSpec *, int32);
#endif

/* config.def.h globals */
extern char *CONF_UTMP;
extern char *scroll;
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

typedef XftDraw *Draw;
typedef XftColor Color;
typedef XftGlyphFontSpec GlyphFontSpec;

/* Purely graphic info */
typedef struct {
    int32 tty_width, tty_height; /* tty width and height */
    int32 w, h;                  /* window width and height */
    int32 hborderpx, vborderpx;
    int32 ch;     /* char height */
    int32 cw;     /* char width  */
    int32 mode;   /* window state/mode flags */
    int32 cursor; /* cursor style */
} TermWindow;

typedef struct {
    Display *display;
    Colormap cmap;
    Window win;
    Drawable buffer;
    GlyphFontSpec *specbuf; /* font spec buffer used for rendering */
    Atom xembed, wmdeletewin, netwmname, netwmiconname, netwmpid;
    struct {
        XIM xim;
        XIC xic;
        XPoint spot;
        XVaNestedList spotlist;
    } ime;
    Draw draw;
    Visual *vis;
    XSetWindowAttributes attrs;
    int32 scr;
    int32 isfixed; /* is fixed geometry? */
    int32 depth;   /* bit depth */
    int32 l, t;    /* left and top offset */
    int32 gm;      /* geometry mask */
} XWindow;

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

static DrawingContext draw_context;

#endif /* ST_H */
