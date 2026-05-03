#ifndef ST_H
#define ST_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>
#include <sys/types.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>

#include "util.c"

#define XEMBED_FOCUS_IN 4
#define XEMBED_FOCUS_OUT 5

#define TERM_WINDOW_IS_SET(flag) ((term_window.mode & (flag)) != 0)
#define TRUE_RED(x) (uint16)(((x) & 0xff0000) >> 8)
#define TRUE_GREEN(x) (uint16)(((x) & 0xff00))
#define TRUE_BLUE(x) (uint16)(((x) & 0xff) << 8)

#define UTF_INVALID 0xFFFD
#define UTF_SIZ 4
#define ESC_BUF_SIZ (128*UTF_SIZ)
#define ESC_ARG_SIZ 16
#define STR_BUF_SIZ ESC_BUF_SIZ
#define STR_ARG_SIZ ESC_ARG_SIZ
#define HISTORY_SIZE 2000
#define RESIZE_BUFFER 1000

#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define ATTRCMP(a, b)		(((a).mode != (b).mode) \
                           || (a).fg   != (b).fg    \
                           || (a).bg   != (b).bg)
#define TIMEDIFF(t1, t2)	((float)(t1.tv_sec - t2.tv_sec)*1000 + \
                             (float)(t1.tv_nsec - t2.tv_nsec)/1E6f)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define TRUECOLOR(r,g,b)	(1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)		(1 << 24 & (x))

/* X modifiers */
#define XK_ANY_MOD UINT_MAX
#define XK_NO_MOD 0
#define XK_SWITCH_MOD (1 << 13 | 1 << 14)

#define TERM_MODE_IS_SET(flag) !(!(term.mode & (flag)))
#define IS_CONTROL_C0(c) (BETWEEN(c, 0, 0x1f) || (c) == 0x7f)
#define IS_CONTROL_C1(c) (BETWEEN(c, 0x80, 0x9f))
#define IS_CONTROl(c) (IS_CONTROL_C0(c) || IS_CONTROL_C1(c))
#define IS_DELIM(u) (u && wcschr(CONF_WORD_DELIMITERS, (wchar_t)u))

#define TERM_LINE(y)                                                                                 \
    ((y) < term.lines_scrolled_up                                                                    \
         ? term.hist[(term.i_hist + (y) - term.lines_scrolled_up + 1 + HISTORY_SIZE) % HISTORY_SIZE] \
         : term.lines[(y) - term.lines_scrolled_up])

#define TERM_LINE_ABS(y)                                                    \
    ((y) < 0                                                                \
	     ? term.hist[(term.i_hist + (y) + 1 + HISTORY_SIZE) % HISTORY_SIZE] \
		 : term.lines[(y)])

#define TERM_LINE_HIST(y)                                                   \
    ((y) <= HISTORY_SIZE - term.nrows + 2                                   \
	     ? term.hist[(y)]                                                   \
         : term.lines[(y - HISTORY_SIZE + term.nrows - 3)])

#define UPDATE_WRAP_NEXT(alt, col)                                          \
    do {                                                                    \
        if ((term.cursor.state & CURSOR_WRAPNEXT)                           \
            && term.cursor.x + term.wrap_char_width[alt] < col) {           \
            term.cursor.x += term.wrap_char_width[alt];                     \
            term.cursor.state &= ~CURSOR_WRAPNEXT;                          \
        }                                                                   \
    } while (0)

#define ENUM_NAME GlyphAttribute
#define ENUM_PREFIX_ ATTR_ 
#define ENUM_BITFLAGS 1
#define ENUM_FIELDS \
	X(SET) \
	X(BOLD)       \
	X(FAINT)      \
	X(ITALIC)     \
	X(UNDERLINE)  \
	X(BLINK)      \
	X(REVERSE)    \
	X(INVISIBLE)  \
	X(STRUCK)     \
	X(WRAP)       \
	X(WIDE)       \
	X(WDUMMY)     \
	X(SELECTED)   \
	X(BOXDRAW)    \
	X(SIXEL)      \
	X(BOLD_FAINT, ATTR_BOLD | ATTR_FAINT)
#include "xenums.c"

enum SelectionSnap {
	SELECTION_SNAP_WORD = 1,
	SELECTION_SNAP_LINE = 2
};

enum SelectionType {
	SELECTION_NORMAL = 1,
	SELECTION_RECTANGULAR = 2
};

#define ENUM_NAME TermMode
#define ENUM_PREFIX_ TERM_MODE_
#define ENUM_BITFLAGS 1
#define ENUM_FIELDS \
    X(WRAP) \
    X(INSERT) \
    X(ALTSCREEN) \
    X(CRLF) \
    X(ECHOO) \
    X(PRINT) \
    X(UTF8) \
	X(SIXEL) \
	X(SIXEL_CUR_RT) \
	X(SIXEL_SDM)
#include "cbase/xenums.c"

typedef struct StGlyph {
	uint32 rune;           /* character code */
	enum GlyphAttribute mode;      /* attribute flags */
	uint16 padding;
	int32 fg;      /* foreground  */
	int32 bg;      /* background  */
} StGlyph;

union Arg {
	int32 i;
	uint32 ui;
	float f;
	void *v;
	char *s;
};

typedef struct MouseShortcut {
    uint32 mod;
    uint32 button;
    void (*func)(union Arg *);
    union Arg arg;
    uint32 release;
} MouseShortcut;

typedef struct ImageList {
	struct ImageList *next;
	struct ImageList *prev;
	uchar *pixels;
	void *pixmap;
	void *clipmask;
	int32 width;
	int32 height;
	int32 x;
	int32 y;
	int32 reflow_y;
	int32 cols;
	int32 cw;
	int32 ch;
	int32 transparent;
} ImageList;

enum ScrollMode {
    SCROLL_RESIZE = -1,
    SCROLL_NOSAVEHIST = 0,
    SCROLL_SAVEHIST = 1
};

enum CursorMovement {
    CURSOR_SAVE,
    CURSOR_LOAD
};

#define ENUM_NAME CursorState
#define ENUM_PREFIX_ CURSOR_
#define ENUM_BITFLAGS 0
#define ENUM_FIELDS \
    X(DEFAULT) \
    X(WRAPNEXT) \
    X(ORIGIN)
#include "cbase/xenums.c"

enum charset {
    CS_GRAPHIC0,
    CS_GRAPHIC1,
    CS_UK,
    CS_USA,
    CS_MULTI,
    CS_GER,
    CS_FIN
};

#define ENUM_NAME EscapeState
#define ENUM_PREFIX_ ESC_
#define ENUM_BITFLAGS 1
#define ENUM_FIELDS \
    X(START) \
    X(CSI) \
    X(STR) \
    X(ALTCHARSET) \
    X(STR_END) \
    X(TEST) \
    X(UTF8) \
    X(SIXEL) \
    X(DCS)
#include "cbase/xenums.c"

typedef struct TCursor {
    StGlyph attr; /* current char attributes */
    int32 x;
    int32 y;
    enum CursorState state;
} TCursor;

/* Internal representation of the screen */
static struct {
    int32 nrows;
    int32 ncols;
    StGlyph **lines;             /* screen */
    StGlyph *hist[HISTORY_SIZE]; /* history buffer */
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
    enum TermMode mode;        /* terminal mode flags */
    enum EscapeState esc;      /* escape state flags */
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
typedef struct CSIEscape {
    char buffer[ESC_BUF_SIZ]; /* raw string */
    int64 len;                /* raw string length */
    char priv;
    int32 arg[ESC_ARG_SIZ];
    int32 narg; /* nb of args */
    char mode[2];
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct STREscape {
    char type;    /* ESC type ... */
    char *buffer; /* allocated raw string */
    int32 siz;    /* allocation size */
    int32 len;    /* raw string length */
    char *args[STR_ARG_SIZ];
    int32 nargs;
} STREscape;

typedef struct StFont {
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
} StFont;

static void redraw(void);
static int32 x_get_color(int32 x, uint *r, uint *g, uint *b);
static void term_delete_images(void);

static void tty_hangup(void);
static void tty_write(char *, int64, int32);

static void reset_title(void);

static void exec_shell(char *, char **) __attribute__((noreturn));
static void tty_write_raw(char *, int64);

static char *term_get_glyphs(char *, StGlyph *, StGlyph *);
static inline void term_set_sixel_attr(StGlyph *line, int32 x1, int32 x2);
static int32 term_is_wrapped(StGlyph *line);
static int32 term_line_len(StGlyph *len);
static int32 term_write(char *, int32, int32);
static void term_clear_glyph(StGlyph *, int32);
static void term_clear_region(int32, int32, int32, int32, int32);
static void term_delete_char(int32);
static void term_delete_line(int32);
static void term_dump(void);
static void term_dump_line(int32);
static void term_dump_sel(void);
static void term_full_dirt(void);
static void term_insert_blank(int32);
static void term_insert_blank_line(int32);
static void term_load_alt_screen(int32, int32);
static void term_load_def_screen(int32, int32);
static void term_move_abs_to(int32, int32);
static void term_move_to(int32, int32);
static void term_new_line(int32);
static void term_printer(char *, int64);
static void term_put_tab(int32);
static void term_putc(uint32);
static void term_reflow(int32, int32);
static void term_reset(void);
static void term_resize_alt(int32, int32);
static void term_resize_def(int32, int32);
static void term_scroll_down(int32, int32);
static void term_scroll_up(int32, int32, int32, enum ScrollMode);
static void term_set_char(uint32, StGlyph *, int32, int32);
static void term_set_dirt(int32, int32);
static void term_swap_screen(void);

static int32 xevent_col(XEvent *);
static int32 xevent_row(XEvent *);

static int32 x_make_glyph_font_specs(XftGlyphFontSpec *, StGlyph *,
		                             int32, int32, int32);
static void x_draw_glyph_font_specs(XftGlyphFontSpec *, StGlyph,
		                            int32, int32, int32);
static void x_draw_glyph(StGlyph, int32, int32);
static void x_clear(int32, int32, int32, int32);
static int32 x_geom_mask_to_gravity(int32);
static int32 x_im_open(Display *);
static void x_im_instantiate(Display *, XPointer, XPointer);
static void x_im_destroy(XIM, XPointer, XPointer);
static int32 x_ic_destroy(XIC, XPointer, XPointer);
static void cresize(int32, int32);
static void x_resize(int32, int32);
static void x_hints(void);
static int32 x_load_color(int32, char *, XftColor *);
static int32 x_load_font(StFont *, FcPattern *);
static void x_load_fonts(char *, float);
static int32 xloadsparefont(FcPattern *, int32);
static void x_load_spare_fonts(void);
static void x_unload_font(StFont *);
static void x_unload_fonts(void);
static void x_set_urgency(int32);

static void zoom_abs(union Arg *);

static void user_clipboard_copy(union Arg *);
static void user_clipboard_paste(union Arg *);
static void user_toggle_numlock(union Arg *);
static void user_selection_paste(union Arg *);
static void user_change_alpha(union Arg *);
static void user_zoom(union Arg *);
static void user_zoom_reset(union Arg *);
static void user_tty_send(union Arg *);
static void user_scroll_down(union Arg *);
static void user_scroll_up(union Arg *);
static void user_external_pipe(union Arg *);
static void user_print_screen(union Arg *);
static void user_print_sel(union Arg *);
static void user_send_break(union Arg *);
static void user_toggle_printer(union Arg *);
static void user_vim_select(union Arg *arg);

static void x_bell(void);
static void x_draw_cursor(int32, int32, StGlyph, int32, int32, StGlyph);
static void x_draw_line(StGlyph *, int32, int32, int32);
static void x_load_cols(void);
static int32 x_set_color_name(int32, char *);
static void x_set_icon_title(char *);
static void x_set_title(char *);
static int32 x_set_cursor(int32);
static void x_set_mode(int32, uint32);
static void x_set_pointer_motion(int32);
static int32 x_start_draw(void);
static void x_xim_spot(int32, int32);

static void selection_set(char *, Time);
static int64 xwrite(int32 fd, char *s, int64 len);

typedef struct Shortcut {
    uint32 mod;
    KeySym keysym;
    void (*func)(union Arg *);
    union Arg arg;
} Shortcut;

typedef struct Key {
    KeySym k;
    uint32 mask;
    char *s;
    /* three-valued logic variables: 0 indifferent, 1 on, -1 off */
    char appkey;    /* application keypad */
    char appcursor; /* application cursor */
} Key;

#define ENUM_NAME WinMode
#define ENUM_PREFIX_ WIN_MODE_
#define ENUM_BITFLAGS 1
#define ENUM_FIELDS \
	X(VISIBLE) \
	X(FOCUSED) \
	X(APPKEYPAD) \
	X(MOUSEBTN) \
	X(MOUSEMOTION) \
	X(REVERSE) \
	X(KBDLOCK) \
	X(HIDE) \
	X(APPCURSOR) \
	X(MOUSESGR) \
	X(8BIT) \
	X(BLINK) \
	X(FBLINK) \
	X(FOCUS) \
	X(MOUSEX10) \
	X(MOUSEMANY) \
	X(BRCKTPASTE) \
	X(NUMLOCK) \
	X(MOUSE, WIN_MODE_MOUSEBTN|WIN_MODE_MOUSEMOTION|WIN_MODE_MOUSEX10|WIN_MODE_MOUSEMANY)
#include "xenums.c"

static struct {
    int32 tty_width;
	int32 tty_height;
    int32 w;
	int32 h;
    int32 hborderpx;
	int32 vborderpx;
    int32 ch;     /* char height */
    int32 cw;     /* char width  */
    enum WinMode mode;   /* window state/mode flags */
    int32 cursor; /* cursor style */
} term_window;

static struct {
    Display *display;
    Colormap color_map;
    Window win;
    Drawable drawable;
    XftGlyphFontSpec *specbuf; /* font spec buffer used for rendering */
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
    int32 is_fixed;
    int32 depth;
    int32 left_offset;
	int32 top_offset;
    int32 geo_mask;
} x_window;

static struct {
    XftColor *colors;
    int32 colors_len;
    StFont font;
	StFont bfont;
	StFont ifont;
	StFont ibfont;
    GC graphics;
} draw_context;

static struct {
    Atom xtarget;
    char *primary;
	char *clipboard;
    struct timespec tclick1;
    struct timespec tclick2;
} xsel;

#include "sixel.h"
static SixelState sixel_st;

static float usedfontsize = 0;
static float defaultfontsize = 0;
static pid_t pid;
static int32 io_fd = 1;
static int32 command_fd;
static uint32 buttons; /* bit field of pressed buttons */

static char *opt_class = NULL;
static char **opt_cmd = NULL;
static char *opt_embed = NULL;
static char *opt_font = NULL;
static char *opt_iofile = NULL;
static char *opt_line = NULL;
static char *opt_name = NULL;
static char *opt_title = NULL;

/* StFont Ring Cache */
enum {
    FRC_NORMAL,
    FRC_ITALIC,
    FRC_BOLD,
    FRC_ITALICBOLD
};

typedef struct FontCache {
    XftFont *font;
    int32 flags;
    uint32 unicodep;
} FontCache;

/* Fontcache is an array now. A new font will be appended to the array. */
static FontCache *frc = NULL;
static int32 frclen = 0;
static int32 frccap = 0;
static char *usedfont = NULL;

#endif /* ST_H */
