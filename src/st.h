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
#include <hb.h>
#include <hb-ft.h>
#include <time.h>

#include "util.c"

#define XEMBED_FOCUS_IN 4
#define XEMBED_FOCUS_OUT 5

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
#define MAX_NROWS 5000
#define MAX_NCOLS 5000
#define FONT_SPEC_BUF_SIZE 8

#define LIMIT(x, a, b)        (x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)        (((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)        (a) = (a) ? (a) : (b)
#define ATTRCMP(a, b)        (((a).mode != (b).mode) \
                           || (a).fg   != (b).fg    \
                           || (a).bg   != (b).bg)
#define MODBIT(x, set, bit)    ((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define TRUECOLOR(r,g,b)    (1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)        (1 << 24 & (x))

/* X modifiers */
#define XK_ANY_MOD UINT_MAX
#define XK_NO_MOD 0
#define XK_SWITCH_MOD (1 << 13 | 1 << 14)

#define IS_CONTROL_C0(c) (BETWEEN(c, 0, 0x1f) || (c) == 0x7f)
#define IS_CONTROL_C1(c) (BETWEEN(c, 0x80, 0x9f))
#define IS_CONTROl(c) (IS_CONTROL_C0(c) || IS_CONTROL_C1(c))
#define IS_DELIM(u) (u && wcschr(CONF_WORD_DELIMITERS, (wchar_t)u))

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
    SELECTION_SNAP_NONE,
    SELECTION_SNAP_WORD,
    SELECTION_SNAP_LINE,
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
    uint32 rune;               /* character code */
    enum GlyphAttribute mode;  /* attribute flags */
    int32 fg;                  /* foreground  */
    int32 bg;                  /* background  */
} StGlyph;

#define MULTI_CODE_POINT_FLAG (1U << 31)

typedef struct StringPool {
    uint32 *runes;
    int32 length;
    int32 capacity;
} StringPool;

static StringPool *string_pool = NULL;
static int32 string_pool_length = 0;
static int32 string_pool_capacity = 0;

union Arg {
    int64 i;
    uint64 ui;
    double f;
    void *v;
    char *s;
};

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
    bool transparent;
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
    StGlyph **lines;             // screen
    StGlyph *hist[HISTORY_SIZE]; // history buffer
    int32 i_hist;                // history index
    int32 n_hist;                // nb history available
    int32 scrolled_up;           // scroll back
    int32 wrap_char_width[2];    // used in updating WRAPNEXT when resizing
    bool *dirts;                 // dirtyness of lines
    bool *tabs;
    TCursor cursor;
    int32 old_cursor_x;
    int32 old_cursor_y;
    int32 top_scroll_limit;
    int32 bot_scroll_limit;
    enum TermMode mode;
    enum EscapeState esc;
    char translation_table[4];   // charset table translation
    int32 charset;               // current charset
    int32 icharset;              // selection_is_selected charset for sequence
    uint32 last_char; // last printed char outside of sequence, 0 if control
    ImageList *images;
    ImageList *images_alt;
} term;

typedef struct StFont {
    int32 height;
    int32 width;
    int32 ascent;
    int32 descent;
    bool bad_slant;
    bool bad_weight;
    XftFont *match;
    FcFontSet *set;
    FcPattern *pattern;
    hb_font_t *hbfont;
} StFont;

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
    X(MOUSE, WIN_MODE_MOUSEBTN    \
            |WIN_MODE_MOUSEMOTION \
            |WIN_MODE_MOUSEX10    \
            |WIN_MODE_MOUSEMANY)
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
    XftGlyphFontSpec *font_spec_buf; /* font spec buffer used for rendering */
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
    bool is_fixed;
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
    int32 clipboard_len;
    struct timespec tclick1;
    struct timespec tclick2;
} xsel;

static void redraw(void);
static int32 x_get_color(int32 x, uint *r, uint *g, uint *b);
static void term_delete_images(void);

static void tty_hangup(void);
static void tty_write(char *s, int64 len, int32 fd);

static void tty_write_raw(char *s, int64 len);

static char *term_get_glyphs(char *buffer, StGlyph *glyph, StGlyph *lgp);
static inline void term_set_sixel_attr(StGlyph *line, int32 x1, int32 x2);
static bool term_is_wrapped(StGlyph *line);
static int32 term_line_len(StGlyph *line);
static int32 term_write(char *buf, int32 len, bool show_ctrl);
static void term_clear_glyph(StGlyph *glyph, bool use_current_attr);
static void term_clear_region(int32 x1, int32 y1, int32 x2, int32 y2, bool use_current_attr);
static void term_delete_char(int32 n);
static void term_delete_line(int32 n);
static void term_dump(void);
static void term_dump_line(int32 n);
static void term_dump_sel(void);
static void term_full_dirt(void);
static void term_insert_blank(int32 n);
static void term_insert_blank_line(int32 n);
static void term_load_alt_screen(bool clear, bool savecursor);
static void term_load_def_screen(bool clear, bool loadcursor);
static void term_move_abs_to(int32 x, int32 y);
static void term_move_to(int32 x, int32 y);
static void term_new_line(bool first_col);
static void term_printer(char *s, int64 len);
static void term_put_tab(int32 inst);
static void term_putc(uint32 u);
static void term_reflow(int32 new_ncols, int32 new_nrows);
static void term_reset(void);
static void term_resize_alt(int32 new_ncols, int32 new_nrows);
static void term_resize_def(int32 new_ncols, int32 new_nrows);
static void term_scroll_down(int32 top, int32 n);
static void term_scroll_up(int32 top, int32 bot, int32 n, enum ScrollMode mode);
static void term_set_char(uint32 u, StGlyph *attr, int32 x, int32 y);
static void term_set_dirt(int32 top, int32 bot);
static void term_swap_screen(void);
static void check_consistent_state(void);
static bool term_mode_is_set(enum TermMode flag);
static bool win_mode_is_set(enum WinMode flag);
static StGlyph *term_line(int32 y);
static StGlyph *term_line_abs(int32 y);
static StGlyph *term_line_hist(int32 y);
static void update_wrap_next(int32 alt, int32 col);
static void term_cursor(enum CursorMovement mode);

static void x_clear(int32 x1, int32 y1, int32 x2, int32 y2);
static int32 x_geom_mask_to_gravity(int32 mask);
static int32 x_im_open(Display *display);
static void x_configure_resize(int32 width, int32 height);
static void reflow_scroll_down(int32 n);

static void user_clipboard_copy(union Arg *arg);
static void user_clipboard_paste(union Arg *arg);
static void user_toggle_numlock(union Arg *arg);
static void user_selection_paste(union Arg *arg);
static void user_change_alpha(union Arg *arg);
static void user_zoom(union Arg *arg);
static void user_zoom_reset(union Arg *arg);
static void user_tty_send(union Arg *arg);
static void user_scroll_down(union Arg *arg);
static void user_scroll_up(union Arg *arg);
static void user_external_pipe(union Arg *arg);
static void user_print_screen(union Arg *arg);
static void user_print_sel(union Arg *arg);
static void user_send_break(union Arg *arg);
static void user_toggle_printer(union Arg *arg);
static void user_vim_select(union Arg *arg);
static void user_copy_output(union Arg *arg);
static void user_url_select(union Arg *arg);
static void user_smart_scroll_up(union Arg *arg);
static void user_smart_scroll_down(union Arg *arg);
static void user_toggle_colorscheme(union Arg *arg);

static int64 xwrite(int32 fd, char *s, int64 len);
static double timediff_ms(struct timespec t1, struct timespec t2);

typedef struct MouseShortcut {
    uint32 mod;
    uint32 button;
    void (*func)(union Arg *);
    union Arg arg;
    uint32 release;
} MouseShortcut;

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
    char app_key;    /* application keypad */
    char app_cursor; /* application cursor */
} Key;

#include "sixel.h"
static SixelState sixel_st;

static double used_font_size = 0;
static double default_font_size = 0;
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

static char *used_font = NULL;
static int32 used_font_len;
static char *program_path;

static struct timespec sutv;

static int su = 0;
static int twrite_aborted = 0;

static void
tsync_begin()
{
	clock_gettime(CLOCK_MONOTONIC, &sutv);
	su = 1;
}

static void
tsync_end()
{
	su = 0;
}

static int
tinsync(uint timeout)
{
	struct timespec now;
	if (su && !clock_gettime(CLOCK_MONOTONIC, &now)
	       && timediff_ms(now, sutv) >= timeout)
		su = 0;
	return su;
}

static int
ttyread_pending()
{
	return twrite_aborted;
}

#endif /* ST_H */
