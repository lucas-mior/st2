/* See LICENSE for license details. */

#ifndef ST_H
#define ST_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <X11/X.h>

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

/* X modifiers */
#define XK_ANY_MOD UINT_MAX
#define XK_NO_MOD 0
#define XK_SWITCH_MOD (1 << 13 | 1 << 14)

enum glyph_attribute {
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

enum selection_mode {
	SELECTION_IDLE = 0,
	SELECTION_EMPTY = 1,
	SELECTION_READY = 2
};

enum selection_type {
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

typedef Glyph *Line;

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
void term_new(int32, int32);
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
void selection_init(void);
void selection_start(int32, int32, int32);
void selection_extend(int32, int32, int32, int32);
int32 selected(int32, int32);
char *get_sel(void);

int64 utf8_encode(Rune, char *);

void *xmalloc(int64);
void *xrealloc(void *, int64);
char *xstrdup(const char *);

int32 isboxdraw(Rune);
uint16 boxdrawindex(const Glyph *);
#ifdef XFT_VERSION
/* only exposed to x.c, otherwise we'll need Xft.h for the types */
void boxdraw_xinit(Display *, Colormap, XftDraw *, Visual *);
void drawboxes(int32, int32, int32, int32, XftColor *, XftColor *, const XftGlyphFontSpec *, int32);
#endif

/* config.def.h globals */
extern char *utmp;
extern char *scroll;
extern char *stty_args;
extern char *vtiden;
extern wchar_t *WORD_DELIMITERS;
extern int32 ALLOW_ALT_SCREEN;
extern int32 ALLOW_WINDOW_OPS;
extern char *TERM_NAME;
extern int32 TAB_NSPACES;
extern int32 default_foreground;
extern int32 default_background;
extern int32 default_cursor;
extern const int32 CONF_BOXDRAW, CONF_BOXDRAW_BOLD, CONF_BOXDRAW_BRAILLE;
extern float alpha;

/* function definitions used in config.def.h */
void user_clipboard_copy(const Arg *);
void user_clipboard_paste(const Arg *);
void user_toggle_numlock(const Arg *);
void user_selection_paste(const Arg *);
void user_change_alpha(const Arg *);
void user_zoom(const Arg *);
void zoom_abs(const Arg *);
void user_zoom_reset(const Arg *);
void tty_send(const Arg *);

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

#endif /* ST_H */
