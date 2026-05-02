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
#define TERM_LINE(y)                                                                                \
    ((y) < term.lines_scrolled_up                                                                   \
         ? term.hist[(term.i_hist + (y) - term.lines_scrolled_up + 1 + HISTORY_SIZE)                \
                     % HISTORY_SIZE]                                                                \
         : term.line[(y) - term.lines_scrolled_up])

#define TERM_LINE_ABS(y)                                                                            \
    ((y) < 0 ? term.hist[(term.i_hist + (y) + 1 + HISTORY_SIZE) % HISTORY_SIZE] : term.line[(y)])
#define TERM_LINE_HIST(y)                                                                           \
    ((y) <= HISTORY_SIZE - term.nrows + 2 ? term.hist[(y)]                                          \
                                          : term.line[(y - HISTORY_SIZE + term.nrows - 3)])

#define UPDATE_WRAP_NEXT(alt, col)                                                                  \
    do {                                                                                            \
        if ((term.cursor.state & CURSOR_WRAPNEXT)                                                   \
            && term.cursor.x + term.wrap_char_width[alt] < col) {                                   \
            term.cursor.x += term.wrap_char_width[alt];                                             \
            term.cursor.state &= ~CURSOR_WRAPNEXT;                                                  \
        }                                                                                           \
    } while (0)

enum GlyphAttribute {
	ATTR_NULL        = 0,
	ATTR_SET         = 1 << 0,
	ATTR_BOLD        = 1 << 1,
	ATTR_FAINT       = 1 << 2,
	ATTR_ITALIC      = 1 << 3,
	ATTR_UNDERLINE   = 1 << 4,
	ATTR_BLINK       = 1 << 5,
	ATTR_REVERSE     = 1 << 6,
	ATTR_INVISIBLE   = 1 << 7,
	ATTR_STRUCK      = 1 << 8,
	ATTR_WRAP        = 1 << 9,
	ATTR_WIDE        = 1 << 10,
	ATTR_WDUMMY      = 1 << 11,
	ATTR_SELECTED    = 1 << 12,
	ATTR_BOXDRAW     = 1 << 13,
	ATTR_SIXEL       = 1 << 16,
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

#define ENUM_NAME TermMode
#define ENUM_PREFIX_ TERM_MODE_
#define ENUM_BITFLAGS 1
#define ENUM_FIELDS \
    X(WRAP,         1 << 0) \
    X(INSERT,       1 << 1) \
    X(ALTSCREEN,    1 << 2) \
    X(CRLF,         1 << 3) \
    X(ECHOO,        1 << 4) \
    X(PRINT,        1 << 5) \
    X(UTF8,         1 << 6) \
	X(SIXEL,        1 << 7) \
	X(SIXEL_CUR_RT, 1 << 8) \
	X(SIXEL_SDM,    1 << 9)
#include "cbase/xenums.c"

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
	void *v;
	char *s;
} Arg;

typedef struct {
    uint32 mod;
    uint32 button;
    void (*func)(Arg *);
    Arg arg;
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

enum scroll_mode {
    SCROLL_RESIZE = -1,
    SCROLL_NOSAVEHIST = 0,
    SCROLL_SAVEHIST = 1
};

enum cursor_movement {
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

typedef struct {
    Glyph attr; /* current char attributes */
    int32 x;
    int32 y;
    enum CursorState state;
} TCursor;

/* Internal representation of the screen */
static struct {
    int32 nrows;
    int32 ncols;
    Glyph **line;               /* screen */
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
static void draw(void);

static int32 x_get_color(int32 x, uint *r, uint *g, uint *b);

static void tdeleteimages(void);
static inline void term_set_sixel_attr(Glyph *line, int32 x1, int32 x2);

static int32 term_attr_set(int32);
static void term_resize(int32, int32);
static void term_set_dirt_attr(int32);
static void tty_hangup(void);
static int32 tty_new(char *, char *, char *, char **);
static int64 tty_read(void);
static void tty_resize(int32, int32);
static void tty_write(char *, int64, int32);

static void reset_title(void);

static void selection_clear(void);
static void selection_start(int32, int32, int32);
static void selection_extend(int32, int32, int32, int32);
static int32 selection_is_selected(int32, int32);
static char *selection_get(void);

static int64 utf8_encode(uint32, char *);

static void xfree(void *);

static int32 isboxdraw(uint32);
static uint16 boxdrawindex(Glyph *);
#ifdef XFT_VERSION
/* only exposed to main.c, otherwise we'll need Xft.h for the types */
static void boxdraw_xinit(Display *, Colormap, XftDraw *, Visual *);
static void drawboxes(int32, int32, int32, int32, XftColor *, XftColor *, XftGlyphFontSpec *, int32);
#endif

static void exec_shell(char *, char **) __attribute__((noreturn));
static void stty(char **);
static void handler_sigchld(int32);
static void tty_write_raw(char *, int64);

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
static void term_delete_char(int32);
static void term_delete_line(int32);
static void term_insert_blank(int32);
static void term_insert_blank_line(int32);
static int32 term_line_len(Glyph *len);
static int32 term_is_wrapped(Glyph *line);
static char *term_get_glyphs(char *, Glyph *, Glyph *);
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
static void term_set_attr(int32 *, int32);
static void term_set_char(uint32, Glyph *, int32, int32);
static void term_set_dirt(int32, int32);
static void term_swap_screen(void);
static void term_load_def_screen(int32, int32);
static void term_load_alt_screen(int32, int32);
static void term_set_mode(int32, int32, int32 *, int32);
static int32 term_write(char *, int32, int32);
static void term_full_dirt(void);
static void term_control_code(uchar);
static void term_dec_test(char);
static void term_def_utf8(char);
static int32_t term_def_color(int32 *, int32 *, int32);
static void term_def_tran(char);
static void term_str_sequence(uchar);

static void selection_normalize(void);
static void selection_scroll(int32, int32, int32);
static void selection_move(int32);
static void selection_remove(void);
static int32 selection_is_selected4(int32, int32, int32, int32);
static void SelectionSnap(int32 *, int32 *, int32);

static int64 utf8_decode(char *, uint32 *, int64);
static uint32 utf8_decode_byte(char, int64 *);
static char utf8_encode_byte(uint32, int64);
static int64 utf8_validate(uint32 *, int64);

static void mouse_select(XEvent *, int32);
static void mouse_report(XEvent *);
static int32 match_mask_state(uint32, uint32);
static uint32 button_mask(uint32);
static int32 mouse_action(XEvent *, uint32);

static int32 xevent_col(XEvent *);
static int32 xevent_row(XEvent *);

static int32 x_make_glyph_font_specs(XftGlyphFontSpec *, Glyph *,
		                             int32, int32, int32);
static void x_draw_glyph_font_specs(XftGlyphFontSpec *, Glyph,
		                            int32, int32, int32);
static void x_draw_glyph(Glyph, int32, int32);
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

static void zoom_abs(Arg *);

static void user_clipboard_copy(Arg *);
static void user_clipboard_paste(Arg *);
static void user_toggle_numlock(Arg *);
static void user_selection_paste(Arg *);
static void user_change_alpha(Arg *);
static void user_zoom(Arg *);
static void user_zoom_reset(Arg *);
static void user_tty_send(Arg *);
static void user_scroll_down(Arg *);
static void user_scroll_up(Arg *);
static void externalpipe(Arg *);
static void user_print_screen(Arg *);
static void user_print_sel(Arg *);
static void user_send_break(Arg *);
static void user_toggle_printer(Arg *);

typedef struct {
    uint32 mod;
    KeySym keysym;
    void (*func)(Arg *);
    Arg arg;
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
	WIN_MODE_MOUSE       = WIN_MODE_MOUSEBTN
                    |WIN_MODE_MOUSEMOTION
                    |WIN_MODE_MOUSEX10
                    |WIN_MODE_MOUSEMANY,
};

static void x_bell(void);
static void x_draw_cursor(int32, int32, Glyph, int32, int32, Glyph);
static void x_draw_line(Glyph *, int32, int32, int32);
static void x_finish_draw(void);
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

static struct {
    int32 tty_width;
	int32 tty_height;
    int32 w;
	int32 h;
    int32 hborderpx;
	int32 vborderpx;
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
    int32 is_fixed; /* is fixed geometry? */
    int32 depth;   /* bit depth */
    int32 left_offset;
	int32 top_offset;    /* left and top offset */
    int32 geo_mask;      /* geometry mask */
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

typedef struct {
    XftFont *font;
    int32 flags;
    uint32 unicodep;
} Fontcache;

/* Fontcache is an array now. A new font will be appended to the array. */
static Fontcache *frc = NULL;
static int32 frclen = 0;
static int32 frccap = 0;
static char *usedfont = NULL;

#endif /* ST_H */
