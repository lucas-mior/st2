/* See LICENSE for license details. */
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>

#include "arg.h"
#include "st.h"
#include "st.c"
#include "boxdraw.c"

/* types used in config.def.h */

/* config.def.h for applying patches and the configuration. */
#include "config.def.h"

/* XEMBED messages */
#define XEMBED_FOCUS_IN 4
#define XEMBED_FOCUS_OUT 5

/* macros */
#define TERM_WINDOW_IS_SET(flag) ((term_window.mode & (flag)) != 0)
#define TRUE_RED(x) (uint16)(((x) & 0xff0000) >> 8)
#define TRUE_GREEN(x) (uint16)(((x) & 0xff00))
#define TRUE_BLUE(x) (uint16)(((x) & 0xff) << 8)

static inline uint16 sixd_to_16bit(int32);
static int32 x_make_glyph_font_specs(XftGlyphFontSpec *, const Glyph *, int32,
                                     int32, int32);
static void x_draw_glyph_font_specs(const XftGlyphFontSpec *, Glyph, int32,
                                    int32, int32);
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
static int32 x_load_color(int32, const char *, Color *);
static int32 x_load_font(Font *, FcPattern *);
static void x_load_fonts(const char *, float);
static int32 xloadsparefont(FcPattern *, int32);
static void x_load_spare_fonts(void);
static void x_unload_font(Font *);
static void x_unload_fonts(void);
static void x_set_urgency(int32);
static int32 xevent_col(XEvent *);
static int32 xevent_row(XEvent *);

static void handler_expose(XEvent *);
static void handler_visibility(XEvent *);
static void handler_unmap(XEvent *);
static void handler_key_press(XEvent *);
static void handler_client_message(XEvent *);
static void handler_configure_notify(XEvent *);
static void handler_focus(XEvent *);
static uint32 button_mask(uint32);
static int32 mouse_action(XEvent *, uint32);
static void handler_button_release(XEvent *);
static void handler_button_press(XEvent *);
static void handler_button_motion(XEvent *);
static void handler_prop_notify(XEvent *);
static void handler_selection_notify(XEvent *);
static void handler_selection_clear(XEvent *);
static void handler_selection_request(XEvent *);
static void mouse_select(XEvent *, int32);
static void mouse_report(XEvent *);
static int32 match_mask_state(uint32, uint32);

static void usage(void) __attribute__((noreturn));

static void (*handler[LASTEvent])(XEvent *) = {
    [KeyPress] = handler_key_press,
    [ClientMessage] = handler_client_message,
    [ConfigureNotify] = handler_configure_notify,
    [VisibilityNotify] = handler_visibility,
    [UnmapNotify] = handler_unmap,
    [Expose] = handler_expose,
    [FocusIn] = handler_focus,
    [FocusOut] = handler_focus,
    [MotionNotify] = handler_button_motion,
    [ButtonPress] = handler_button_press,
    [ButtonRelease] = handler_button_release,
    [SelectionClear] = handler_selection_clear,
    [SelectionNotify] = handler_selection_notify,
    /*
     * PropertyNotify is only turned on when there is INCR transfer happening
     * for the selection retrieval.
     */
    [PropertyNotify] = handler_prop_notify,
    [SelectionRequest] = handler_selection_request,
};

/* Globals */
static XWindow x_window;
static XSelection xsel;
static TermWindow term_window;

/* Font Ring Cache */
enum {
    FRC_NORMAL,
    FRC_ITALIC,
    FRC_BOLD,
    FRC_ITALICBOLD
};

typedef struct {
    XftFont *font;
    int32 flags;
    Rune unicodep;
} Fontcache;

/* Fontcache is an array now. A new font will be appended to the array. */
static Fontcache *frc = NULL;
static int32 frclen = 0;
static int32 frccap = 0;
static char *usedfont = NULL;
static float usedfontsize = 0;
static float defaultfontsize = 0;

static char *opt_class = NULL;
static char **opt_cmd = NULL;
static char *opt_embed = NULL;
static char *opt_font = NULL;
static char *opt_iofile = NULL;
static char *opt_line = NULL;
static char *opt_name = NULL;
static char *opt_title = NULL;

static uint32 buttons; /* bit field of pressed buttons */

int32
main(int32 argc, char *argv[]) {
    x_window.left_offset = x_window.top_offset = 0;
    x_window.is_fixed = False;
    x_set_cursor((int32)CONF_CURSOR_SHAPE);

    ARGBEGIN {
    case 'a':
        CONF_ALLOW_ALT_SCREEN = 0;
        break;
    case 'A':
        CONF_ALPHA = strtof(EARGF(usage()), NULL);
        LIMIT(CONF_ALPHA, 0.0f, 1.0f);
        break;
    case 'c':
        opt_class = EARGF(usage());
        break;
    case 'e':
        if (argc > 0) {
            --argc;
            ++argv;
        }
        goto run;
    case 'f':
        opt_font = EARGF(usage());
        break;
    case 'g':
        x_window.geo_mask = XParseGeometry(
            EARGF(usage()), &x_window.left_offset, &x_window.top_offset,
            (uint32 *)&CONF_NUMBER_COLS, (uint32 *)&CONF_NUMBER_ROWS);
        break;
    case 'i':
        x_window.is_fixed = 1;
        break;
    case 'o':
        opt_iofile = EARGF(usage());
        break;
    case 'l':
        opt_line = EARGF(usage());
        break;
    case 'n':
        opt_name = EARGF(usage());
        break;
    case 't':
    case 'T':
        opt_title = EARGF(usage());
        break;
    case 'w':
        opt_embed = EARGF(usage());
        break;
    case 'v':
        die("%s " VERSION "\n", argv0);
    default:
        usage();
    }
    ARGEND;

run:
    if (argc > 0) { /* eat all remaining arguments */
        opt_cmd = argv;
    }

    if (!opt_title) {
        if (opt_line || !opt_cmd) {
            opt_title = "st";
        } else {
            opt_title = opt_cmd[0];
        }
    }

    setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    CONF_NUMBER_COLS = MAX(CONF_NUMBER_COLS, 1);
    CONF_NUMBER_ROWS = MAX(CONF_NUMBER_ROWS, 1);

    for (int32 i = 0; i < 2; i++) {
        term.line = xmalloc((int64)CONF_NUMBER_ROWS*SIZEOF(*(term.line)));
        for (int32 j = 0; j < CONF_NUMBER_ROWS; j++) {
            term.line[j]
                = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(*(term.line[j])));
        }
        term.ncols = CONF_NUMBER_COLS;
        term.nrows = CONF_NUMBER_ROWS;
        term_swap_screen();
    }
    term.dirty = xmalloc((int64)CONF_NUMBER_ROWS*SIZEOF(*term.dirty));
    term.tabs = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(*term.tabs));
    for (int32 i = 0; i < HISTORY_SIZE; i++) {
        term.hist[i] = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(Glyph));
    }
    term_reset();

    {
        XGCValues xgc_values;
        Cursor cursor;
        Window parent = 0;
        Window root;
        pid_t pid_this = getpid();
        XColor xmouse_fg, xmouse_bg;
        XWindowAttributes attr;
        XVisualInfo visual;

        if (!(x_window.display = XOpenDisplay(NULL))) {
            die("can't open display\n");
        }
        x_window.screen = XDefaultScreen(x_window.display);

        root = XRootWindow(x_window.display, x_window.screen);
        if (!(opt_embed && (parent = (Window)strtol(opt_embed, NULL, 0)))) {
            parent = root;
        }

        if (XMatchVisualInfo(x_window.display, x_window.screen, 32, TrueColor,
                             &visual)
            != 0) {
            x_window.visual = visual.visual;
            x_window.depth = visual.depth;
        } else {
            XGetWindowAttributes(x_window.display, parent, &attr);
            x_window.visual = attr.visual;
            x_window.depth = attr.depth;
        }

        if (!FcInit()) {
            die("could not init fontconfig.\n");
        }

        if (opt_font) {
            usedfont = opt_font;
        } else {
            usedfont = CONF_FONT;
        }
        x_load_fonts(usedfont, 0);

        x_load_spare_fonts();

        x_window.color_map
            = XCreateColormap(x_window.display, parent, x_window.visual, None);
        x_load_cols();

        /* adjust fixed window geometry */
        term_window.w = 2*term_window.hborderpx + 2*CONF_BORDER_PIXELS
                        + CONF_NUMBER_COLS*term_window.cw;
        term_window.h = 2*term_window.vborderpx + 2*CONF_BORDER_PIXELS
                        + CONF_NUMBER_ROWS*term_window.ch;
        if (x_window.geo_mask & XNegative) {
            x_window.left_offset
                += DisplayWidth(x_window.display, x_window.screen)
                   - term_window.w - 2;
        }
        if (x_window.geo_mask & YNegative) {
            x_window.top_offset
                += DisplayHeight(x_window.display, x_window.screen)
                   - term_window.h - 2;
        }

        /* Events */
        x_window.attrs.background_pixel
            = draw_context.color[CONF_COLOR_BG].pixel;
        x_window.attrs.border_pixel = draw_context.color[CONF_COLOR_BG].pixel;
        x_window.attrs.bit_gravity = NorthWestGravity;
        x_window.attrs.event_mask
            = FocusChangeMask | KeyPressMask | KeyReleaseMask | ExposureMask
              | VisibilityChangeMask | StructureNotifyMask | ButtonMotionMask
              | ButtonPressMask | ButtonReleaseMask;
        x_window.attrs.colormap = x_window.color_map;

        x_window.win = XCreateWindow(
            x_window.display, parent, x_window.left_offset, x_window.top_offset,
            (uint32)term_window.w, (uint32)term_window.h, 0, x_window.depth,
            InputOutput, x_window.visual,
            CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask
                | CWColormap,
            &x_window.attrs);
        if (parent != root) {
            XReparentWindow(x_window.display, x_window.win, parent,
                            x_window.left_offset, x_window.top_offset);
        }

        memset(&xgc_values, 0, SIZEOF(xgc_values));
        xgc_values.graphics_exposures = False;
        draw_context.graphics = XCreateGC(x_window.display, x_window.win,
                                          GCGraphicsExposures, &xgc_values);
        x_window.drawable = XCreatePixmap(
            x_window.display, x_window.win, (uint32)term_window.w,
            (uint32)term_window.h, (uint32)x_window.depth);
        XSetForeground(x_window.display, draw_context.graphics,
                       draw_context.color[CONF_COLOR_BG].pixel);
        XFillRectangle(x_window.display, x_window.drawable,
                       draw_context.graphics, 0, 0, (uint32)term_window.w,
                       (uint32)term_window.h);

        /* font spec buffer */
        x_window.specbuf
            = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(GlyphFontSpec));

        /* Xft rendering context */
        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable,
                                          x_window.visual, x_window.color_map);

        /* input methods */
        if (!x_im_open(x_window.display)) {
            XRegisterIMInstantiateCallback(x_window.display, NULL, NULL, NULL,
                                           x_im_instantiate, NULL);
        }

        /* white cursor, black outline */
        cursor = XCreateFontCursor(x_window.display, (uint32)CONF_MOUSE_SHAPE);
        XDefineCursor(x_window.display, x_window.win, cursor);

        if (XParseColor(x_window.display, x_window.color_map,
                        CONF_COLORS[CONF_MOUSE_COLOR_FG], &xmouse_fg)
            == 0) {
            xmouse_fg.red = 0xffff;
            xmouse_fg.green = 0xffff;
            xmouse_fg.blue = 0xffff;
        }

        if (XParseColor(x_window.display, x_window.color_map,
                        CONF_COLORS[CONF_MOUSE_COLOR_BG], &xmouse_bg)
            == 0) {
            xmouse_bg.red = 0x0000;
            xmouse_bg.green = 0x0000;
            xmouse_bg.blue = 0x0000;
        }

        XRecolorCursor(x_window.display, cursor, &xmouse_fg, &xmouse_bg);

        x_window.xembed = XInternAtom(x_window.display, "_XEMBED", False);
        x_window.wm_delete_win
            = XInternAtom(x_window.display, "WM_DELETE_WINDOW", False);
        x_window.net_wm_name
            = XInternAtom(x_window.display, "_NET_WM_NAME", False);
        x_window.net_wm_iconname
            = XInternAtom(x_window.display, "_NET_WM_ICON_NAME", False);
        XSetWMProtocols(x_window.display, x_window.win, &x_window.wm_delete_win,
                        1);

        x_window.net_wm_pid
            = XInternAtom(x_window.display, "_NET_WM_PID", False);
        XChangeProperty(x_window.display, x_window.win, x_window.net_wm_pid,
                        XA_CARDINAL, 32, PropModeReplace, (uchar *)&pid_this,
                        1);

        term_window.mode = WIN_MODE_NUMLOCK;
        reset_title();
        x_hints();
        XMapWindow(x_window.display, x_window.win);
        XSync(x_window.display, False);

        clock_gettime(CLOCK_MONOTONIC, &xsel.tclick1);
        clock_gettime(CLOCK_MONOTONIC, &xsel.tclick2);
        xsel.primary = NULL;
        xsel.clipboard = NULL;
        xsel.xtarget = XInternAtom(x_window.display, "UTF8_STRING", 0);
        if (xsel.xtarget == None) {
            xsel.xtarget = XA_STRING;
        }

        boxdraw_xinit(x_window.display, x_window.color_map, x_window.xft_draw,
                      x_window.visual);
    }

    {
        char buffer[SIZEOF(int64)*8 + 1];

        snprintf(buffer, SIZEOF(buffer), "%lu", x_window.win);
        setenv("WINDOWID", buffer, 1);
    }
    selection.mode = SELECTION_IDLE;
    selection.snap = 0;
    selection.ob.x = -1;

    {
        XEvent xevent;
        int32 w = term_window.w;
        int32 h = term_window.h;
        fd_set read_fd;
        int32 xfd = XConnectionNumber(x_window.display), ttyfd, xev, drawing;
        struct timespec seltv, *tv, now, lastblink, trigger;
        float timeout;

        /* Waiting for window mapping */
        do {
            XNextEvent(x_window.display, &xevent);
            /*
             * This XFilterEvent call is required because of XOpenIM. It
             * does filter out the CONF_KEYS event and some client message for
             * the input method too.
             */
            if (XFilterEvent(&xevent, None)) {
                continue;
            }
            if (xevent.type == ConfigureNotify) {
                w = xevent.xconfigure.width;
                h = xevent.xconfigure.height;
            }
        } while (xevent.type != MapNotify);

        ttyfd = tty_new(opt_line, CONF_SHELl, opt_iofile, opt_cmd);
        cresize(w, h);

        timeout = -1;
        drawing = 0;
        lastblink = (struct timespec){0};

        while (1) {
            FD_ZERO(&read_fd);
            FD_SET(ttyfd, &read_fd);
            FD_SET(xfd, &read_fd);

            if (XPending(x_window.display)) {
                timeout = 0; /* existing events might not set xfd */
            }

            seltv.tv_sec = timeout / 1E3f;
            seltv.tv_nsec = 1E6f*(timeout - 1E3f*(float)seltv.tv_sec);
            if (timeout >= 0) {
                tv = &seltv;
            } else {
                tv = NULL;
            }

            if (pselect(MAX(xfd, ttyfd) + 1, &read_fd, NULL, NULL, tv, NULL)
                < 0) {
                if (errno == EINTR) {
                    continue;
                }
                die("select failed: %s\n", strerror(errno));
            }
            clock_gettime(CLOCK_MONOTONIC, &now);

            if (FD_ISSET(ttyfd, &read_fd)) {
                tty_read();
            }

            xev = 0;
            while (XPending(x_window.display)) {
                xev = 1;
                XNextEvent(x_window.display, &xevent);
                if (XFilterEvent(&xevent, None)) {
                    continue;
                }
                if (handler[xevent.type]) {
                    (handler[xevent.type])(&xevent);
                }
            }

            /*
             * To reduce flicker and tearing, when new content or event
             * triggers drawing, we first wait a bit to ensure we got
             * everything, and if nothing new arrives - we draw.
             * We start with trying to wait CONF_LATENCY_MIN ms. If more content
             * arrives sooner, we retry with shorter and shorter periods,
             * and eventually draw even without idle after CONF_LATENCY_MAX ms.
             * Typically this results in low latency while interacting,
             * maximum latency intervals during `cat huge.txt`, and perfect
             * sync with periodic updates from animations/CONF_KEYS-repeats/etc.
             */
            if (FD_ISSET(ttyfd, &read_fd) || xev) {
                if (!drawing) {
                    trigger = now;
                    drawing = 1;
                }
                timeout = (CONF_LATENCY_MAX - (float)TIMEDIFF(now, trigger))
                          / CONF_LATENCY_MAX*CONF_LATENCY_MIN;
                if (timeout > 0) {
                    continue; /* we have time, try to find idle */
                }
            }

            /* idle detected or CONF_LATENCY_MAX exhausted -> draw */
            timeout = -1;
            if (CONF_BLINK_TIMEOUT && term_attr_set(ATTR_BLINK)) {
                timeout = (float)CONF_BLINK_TIMEOUT
                          - (float)TIMEDIFF(now, lastblink);
                if (timeout <= 0) {
                    if (-timeout
                        > (float)CONF_BLINK_TIMEOUT) { /* start visible */
                        term_window.mode |= WIN_MODE_BLINK;
                    }
                    term_window.mode ^= WIN_MODE_BLINK;
                    term_set_dirt_attr(ATTR_BLINK);
                    lastblink = now;
                    timeout = (float)CONF_BLINK_TIMEOUT;
                }
            }

            draw();
            /* error("Terminal:\n"); */
            /* error("nrows = %d\n", term.nrows); */
            /* error("ncols = %d\n", term.ncols); */
            /* error("line = %p\n", term.line); */
            /* error("hist = %p\n", term.hist); */
            /* error("i_hist = %d\n", term.i_hist); */
            /* error("n_hist = %d\n", term.n_hist); */
            /* error("lines_scrolled_up = %d\n", term.lines_scrolled_up); */
            /* error("wrap_char_width[2] = [%d, %d]\n", term.wrap_char_width[0],
             * term.wrap_char_width[1]); */
            /* error("dirty[0] = %d\n", term.dirty[0]); */
            /* error("TCursor.xy = (%d, term.%d)\n", term.cursor.x,
             * term.cursor.y); */
            /* error("old_cursor_x = %d\n", term.old_cursor_x); */
            /* error("old_cursor_y = %d\n", term.old_cursor_y); */
            /* error("top = %d\n", term.top); */
            /* error("bot = %d\n", term.bot); */
            /* error("mode = %d\n", term.mode); */
            /* error("esc = %d\n", term.esc); */
            /* error("translation_table[4] = %c %c %c %c\n",
             * term.translation_table[0], term.translation_table[1],
             * term.translation_table[2], */
            /*       term.translation_table[3]); */
            /* error("charset = %d\n", term.charset); */
            /* error("icharset = %d\n", term.icharset); */
            /* error("tabs[0] = %d\n", term.tabs[0]); */
            /* error("Rune last_char = %u\n\n", term.last_char); */

            XFlush(x_window.display);
            drawing = 0;
        }
    }
}

void
user_clipboard_copy(const Arg *arg) {
    Atom clipboard;
    (void)arg;

    xfree(xsel.clipboard);
    xsel.clipboard = NULL;

    if (xsel.primary != NULL) {
        xsel.clipboard = xstrdup(xsel.primary);
        clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
        XSetSelectionOwner(x_window.display, clipboard, x_window.win,
                           CurrentTime);
    }
    return;
}

void
user_clipboard_paste(const Arg *arg) {
    Atom clipboard;
    (void)arg;

    clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
    XConvertSelection(x_window.display, clipboard, xsel.xtarget, clipboard,
                      x_window.win, CurrentTime);
    return;
}

void
user_selection_paste(const Arg *arg) {
    (void)arg;
    XConvertSelection(x_window.display, XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
                      x_window.win, CurrentTime);
    return;
}

void
user_change_alpha(const Arg *arg) {
    if ((CONF_ALPHA > 0 && arg->f < 0) || (CONF_ALPHA < 1 && arg->f > 0)) {
        CONF_ALPHA += arg->f;
    }
    if (CONF_ALPHA < 0) {
        CONF_ALPHA = 0;
    }
    if (CONF_ALPHA > 1) {
        CONF_ALPHA = 1;
    }

    x_load_cols();
    redraw();
    return;
}

void
user_toggle_numlock(const Arg *arg) {
    (void)arg;
    term_window.mode ^= WIN_MODE_NUMLOCK;
    return;
}

void
user_zoom(const Arg *arg) {
    Arg larg;

    larg.f = usedfontsize + arg->f;
    zoom_abs(&larg);
    return;
}

void
zoom_abs(const Arg *arg) {
    x_unload_fonts();
    x_load_fonts(usedfont, arg->f);
    x_load_spare_fonts();
    cresize(0, 0);
    redraw();
    x_hints();
    return;
}

void
user_zoom_reset(const Arg *arg) {
    Arg larg;
    (void)arg;

    if (defaultfontsize > 0) {
        larg.f = defaultfontsize;
        zoom_abs(&larg);
    }
    return;
}

void
user_tty_send(const Arg *arg) {
    tty_write(arg->s, (int64)strlen(arg->s), 1);
    return;
}

int32
xevent_col(XEvent *xevent) {
    int32 x = xevent->xbutton.x - term_window.hborderpx;
    LIMIT(x, 0, term_window.tty_width - 1);
    return x / term_window.cw;
}

int32
xevent_row(XEvent *xevent) {
    int32 y = xevent->xbutton.y - term_window.vborderpx;
    LIMIT(y, 0, term_window.tty_height - 1);
    return y / term_window.ch;
}

void
mouse_select(XEvent *xevent, int32 done) {
    int32 type, seltype = SELECTION_REGULAR;
    uint32 state
        = xevent->xbutton.state & ~(Button1Mask | CONF_FORCE_MOUSE_MOD);

    for (type = 1; type < LENGTH(CONF_SELECTION_MASKS); ++type) {
        if (match_mask_state(CONF_SELECTION_MASKS[type], state)) {
            seltype = type;
            break;
        }
    }
    selection_extend(xevent_col(xevent), xevent_row(xevent), seltype, done);
    if (done) {
        selection_set(selection_get(), xevent->xbutton.time);
    }
    return;
}

void
mouse_report(XEvent *xevent) {
    int32 len, button, code;
    int32 x = xevent_col(xevent), y = xevent_row(xevent);
    int32 state = (int32)xevent->xbutton.state;
    char buffer[40];
    static int32 ox, oy;

    if (xevent->type == MotionNotify) {
        if (x == ox && y == oy) {
            return;
        }
        if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMOTION)
            && !TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMANY)) {
            return;
        }
        /* WIN_MODE_MOUSEMOTION: no reporting if no button is pressed */
        if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMOTION) && buttons == 0) {
            return;
        }
        /* Set button to lowest-numbered pressed button, or 12 if no
         * buttons are pressed. */
        for (button = 1; button <= 11 && !(buttons & (1 << (button - 1)));
             button++)
            ;
        code = 32;
    } else {
        button = (int32)xevent->xbutton.button;
        /* Only buttons 1 through 11 can be encoded */
        if (button < 1 || button > 11) {
            return;
        }
        if (xevent->type == ButtonRelease) {
            /* WIN_MODE_MOUSEX10: no button release reporting */
            if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSEX10)) {
                return;
            }
            /* Don't send release events for the scroll wheel */
            if (button == 4 || button == 5) {
                return;
            }
        }
        code = 0;
    }

    ox = x;
    oy = y;

    /* Encode button into code. If no button is pressed for a motion event in
     * WIN_MODE_MOUSEMANY, then encode it as a release. */
    if ((!TERM_WINDOW_IS_SET(WIN_MODE_MOUSESGR)
         && xevent->type == ButtonRelease)
        || button == 12) {
        code += 3;
    } else if (button >= 8) {
        code += 128 + button - 8;
    } else if (button >= 4) {
        code += 64 + button - 4;
    } else {
        code += button - 1;
    }

    if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSEX10)) {
        code += ((state & ShiftMask) ? 4 : 0)
                + ((state & Mod1Mask) ? 8 : 0) /* meta CONF_KEYS: alt */
                + ((state & ControlMask) ? 16 : 0);
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSESGR)) {
        char c;
        if (xevent->type == ButtonRelease) {
            c = 'm';
        } else {
            c = 'M';
        }
        len = snprintf(buffer, SIZEOF(buffer), "\033[<%d;%d;%d%c", code, x + 1,
                       y + 1, c);
    } else if (x < 223 && y < 223) {
        len = snprintf(buffer, SIZEOF(buffer), "\033[M%c%c%c", 32 + code,
                       32 + x + 1, 32 + y + 1);
    } else {
        return;
    }

    tty_write(buffer, (int64)len, 0);
    return;
}

uint32
button_mask(uint32 button) {
    return button == Button1   ? Button1Mask
           : button == Button2 ? Button2Mask
           : button == Button3 ? Button3Mask
           : button == Button4 ? Button4Mask
           : button == Button5 ? Button5Mask
                               : 0;
}

int32
mouse_action(XEvent *xevent, uint32 release) {
    MouseShortcut *mouse_shortcut;

    /* ignore Button<N>mask for Button<N> - it's set on release */
    uint32 state = xevent->xbutton.state & ~button_mask(xevent->xbutton.button);

    for (mouse_shortcut = CONF_MOUSE_SHORTCUTS;
         mouse_shortcut < CONF_MOUSE_SHORTCUTS + LENGTH(CONF_MOUSE_SHORTCUTS);
         mouse_shortcut++) {
        if (mouse_shortcut->release == release
            && mouse_shortcut->button == xevent->xbutton.button
            && (match_mask_state(mouse_shortcut->mod, state)
                || /* exact or forced */
                match_mask_state(mouse_shortcut->mod,
                                 state & ~CONF_FORCE_MOUSE_MOD))) {
            mouse_shortcut->func(&(mouse_shortcut->arg));
            return 1;
        }
    }

    return 0;
}

void
handler_button_press(XEvent *xevent) {
    int32 button = (int32)xevent->xbutton.button;
    struct timespec tnow;
    int32 snap;

    if (1 <= button && button <= 11) {
        buttons |= 1 << (button - 1);
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSE)
        && !(xevent->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
        mouse_report(xevent);
        return;
    }

    if (mouse_action(xevent, 0)) {
        return;
    }

    if (button == Button1) {
        /*
         * If the user clicks below predefined timeouts specific
         * snapping behaviour is exposed.
         */
        clock_gettime(CLOCK_MONOTONIC, &tnow);
        if (TIMEDIFF(tnow, xsel.tclick2) <= (float)CONF_TRIPLE_CLICK_TIMEOUT) {
            snap = SELECTION_SNAP_LINE;
        } else if (TIMEDIFF(tnow, xsel.tclick1)
                   <= (float)CONF_DOUBLE_CLICK_TIMEOUT) {
            snap = SELECTION_SNAP_WORD;
        } else {
            snap = 0;
        }
        xsel.tclick2 = xsel.tclick1;
        xsel.tclick1 = tnow;

        selection_start(xevent_col(xevent), xevent_row(xevent), snap);
    }
    return;
}

void
handler_prop_notify(XEvent *xevent) {
    XPropertyEvent *x_property_event;
    Atom clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);

    x_property_event = &xevent->xproperty;
    if ((x_property_event->state == PropertyNewValue)
        && ((x_property_event->atom == XA_PRIMARY)
            || (x_property_event->atom == clipboard))) {
        handler_selection_notify(xevent);
    }
    return;
}

void
handler_selection_notify(XEvent *xevent) {
    uint64 nitems, ofs, rem;
    int32 format;
    uchar *data;
    uchar *last;
    uchar *repl;
    Atom type, incratom, property = None;

    incratom = XInternAtom(x_window.display, "INCR", 0);

    ofs = 0;
    if (xevent->type == SelectionNotify) {
        property = xevent->xselection.property;
    } else if (xevent->type == PropertyNotify) {
        property = xevent->xproperty.atom;
    }

    if (property == None) {
        return;
    }

    do {
        if (XGetWindowProperty(x_window.display, x_window.win, property,
                               (int64)ofs, BUFSIZ / 4, False, AnyPropertyType,
                               &type, &format, &nitems, &rem, &data)) {
            fprintf(stderr, "Clipboard allocation failed\n");
            return;
        }

        if (xevent->type == PropertyNotify && nitems == 0 && rem == 0) {
            /*
             * If there is some PropertyNotify with no data, then
             * this is the signal of the selection owner that all
             * data has been transferred. We won't need to receive
             * PropertyNotify events anymore.
             */
            MODBIT(x_window.attrs.event_mask, 0, PropertyChangeMask);
            XChangeWindowAttributes(x_window.display, x_window.win, CWEventMask,
                                    &x_window.attrs);
        }

        if (type == incratom) {
            /*
             * Activate the PropertyNotify events so we receive
             * when the selection owner does send us the next
             * chunk of data.
             */
            MODBIT(x_window.attrs.event_mask, 1, PropertyChangeMask);
            XChangeWindowAttributes(x_window.display, x_window.win, CWEventMask,
                                    &x_window.attrs);

            /*
             * Deleting the property is the transfer start signal.
             */
            XDeleteProperty(x_window.display, x_window.win, (ulong)property);
            continue;
        }

        /*
         * As seen in selection_get:
         * Glyph*endings are inconsistent in the terminal and GUI world
         * copy and pasting. When receiving some selection data,
         * replace all '\n' with '\r'.
         * FIXME: Fix the computer world.
         */
        repl = data;
        last = data + nitems*(uint64)format / 8;
        while ((repl = memchr(repl, '\n', (size_t)(last - repl)))) {
            *repl++ = '\r';
        }

        if (TERM_WINDOW_IS_SET(WIN_MODE_BRCKTPASTE) && ofs == 0) {
            tty_write("\033[200~", 6, 0);
        }
        tty_write((char *)data, nitems*(uint64)format / 8, 1);
        if (TERM_WINDOW_IS_SET(WIN_MODE_BRCKTPASTE) && rem == 0) {
            tty_write("\033[201~", 6, 0);
        }
        XFree(data);
        /* number of 32-bit chunks returned */
        ofs += nitems*(uint64)format / 32;
    } while (rem > 0);

    /*
     * Deleting the property again tells the selection owner to send the
     * next data chunk in the property.
     */
    XDeleteProperty(x_window.display, x_window.win, (ulong)property);
    return;
}

void
handler_selection_clear(XEvent *xevent) {
    (void)xevent;
    selection_clear();
    return;
}

void
handler_selection_request(XEvent *xevent) {
    XSelectionRequestEvent *xselection_request_event;
    XSelectionEvent xselection_event;
    Atom xa_targets;
    Atom string;
    Atom clipboard;
    char *selection_text;

    xselection_request_event = (XSelectionRequestEvent *)xevent;
    xselection_event.type = SelectionNotify;
    xselection_event.requestor = xselection_request_event->requestor;
    xselection_event.selection = xselection_request_event->selection;
    xselection_event.target = xselection_request_event->target;
    xselection_event.time = xselection_request_event->time;
    if (xselection_request_event->property == None) {
        xselection_request_event->property = xselection_request_event->target;
    }

    /* reject */
    xselection_event.property = None;

    xa_targets = XInternAtom(x_window.display, "TARGETS", 0);
    if (xselection_request_event->target == xa_targets) {
        /* respond with the supported type */
        string = xsel.xtarget;
        XChangeProperty(xselection_request_event->display,
                        xselection_request_event->requestor,
                        xselection_request_event->property, XA_ATOM, 32,
                        PropModeReplace, (uchar *)&string, 1);
        xselection_event.property = xselection_request_event->property;
    } else if (xselection_request_event->target == xsel.xtarget
               || xselection_request_event->target == XA_STRING) {
        /*
         * xith XA_STRING non ascii characters may be incorrect in the
         * requestor. It is not our problem, use utf8.
         */
        clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
        if (xselection_request_event->selection == XA_PRIMARY) {
            selection_text = xsel.primary;
        } else if (xselection_request_event->selection == clipboard) {
            selection_text = xsel.clipboard;
        } else {
            fprintf(stderr, "Unhandled clipboard selection 0x%lx\n",
                    xselection_request_event->selection);
            return;
        }
        if (selection_text != NULL) {
            XChangeProperty(xselection_request_event->display,
                            xselection_request_event->requestor,
                            xselection_request_event->property,
                            xselection_request_event->target, 8,
                            PropModeReplace, (uchar *)selection_text,
                            (int32)(int64)strlen(selection_text));
            xselection_event.property = xselection_request_event->property;
        }
    }

    /* all done, send a notification to the listener */
    if (!XSendEvent(xselection_request_event->display,
                    xselection_request_event->requestor, 1, 0,
                    (XEvent *)&xselection_event)) {
        fprintf(stderr, "Error sending SelectionNotify event\n");
    }
    return;
}

void
selection_set(char *string, Time t) {
    if (!string) {
        return;
    }

    xfree(xsel.primary);
    xsel.primary = string;

    XSetSelectionOwner(x_window.display, XA_PRIMARY, x_window.win, t);
    if (XGetSelectionOwner(x_window.display, XA_PRIMARY) != x_window.win) {
        selection_clear();
    }
    return;
}

void
handler_button_release(XEvent *xevent) {
    int32 button = (int32)xevent->xbutton.button;

    if (1 <= button && button <= 11) {
        buttons &= ~(1 << (button - 1));
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSE)
        && !(xevent->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
        mouse_report(xevent);
        return;
    }

    if (mouse_action(xevent, 1)) {
        return;
    }
    if (button == Button1) {
        mouse_select(xevent, 1);
    }
    return;
}

void
handler_button_motion(XEvent *xevent) {
    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSE)
        && !(xevent->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
        mouse_report(xevent);
        return;
    }

    mouse_select(xevent, 0);
    return;
}

void
cresize(int32 width, int32 height) {
    int32 col, row;

    if (width != 0) {
        term_window.w = width;
    }
    if (height != 0) {
        term_window.h = height;
    }

    col = (term_window.w - 2*CONF_BORDER_PIXELS) / term_window.cw;
    row = (term_window.h - 2*CONF_BORDER_PIXELS) / term_window.ch;
    col = MAX(1, col);
    row = MAX(1, row);

    term_window.hborderpx = (term_window.w - col*term_window.cw) / 2;
    term_window.vborderpx = (term_window.h - row*term_window.ch) / 2;

    term_resize(col, row);
    x_resize(col, row);
    tty_resize(term_window.tty_width, term_window.tty_height);
    return;
}

void
x_resize(int32 col, int32 row) {
    term_window.tty_width = col*term_window.cw;
    term_window.tty_height = row*term_window.ch;

    XFreePixmap(x_window.display, x_window.drawable);
    x_window.drawable
        = XCreatePixmap(x_window.display, x_window.win, (uint32)term_window.w,
                        (uint32)term_window.h, (uint32)x_window.depth);
    XftDrawChange(x_window.xft_draw, x_window.drawable);
    x_clear(0, 0, term_window.w, term_window.h);

    /* handler_configure_notify to new width */
    x_window.specbuf
        = xrealloc(x_window.specbuf, (int64)col*SIZEOF(GlyphFontSpec));
    return;
}

uint16
sixd_to_16bit(int32 x) {
    int32 y;
    if (x == 0) {
        y = 0;
    } else {
        y = 0x3737 + 0x2828*x;
    }
    return (uint16)y;
}

int32
x_load_color(int32 i, const char *name, Color *ncolor) {
    XRenderColor color = {.alpha = 0xffff};

    if (!name) {
        if (BETWEEN(i, 16 + CONF_NTRANSPARENT_COLORS, 255)) { /* 256 color */
            if (i < 6*6 * 6 + 16) { /* same colors as xterm */
                color.red = sixd_to_16bit(((i - 16) / 36) % 6);
                color.green = sixd_to_16bit(((i - 16) / 6) % 6);
                color.blue = sixd_to_16bit(((i - 16) / 1) % 6);
            } else { /* greyscale */
                color.red = (uint16)(0x0808 + 0x0a0a*(i - (6*6 * 6 + 16)));
                color.green = color.blue = color.red;
            }
            return XftColorAllocValue(x_window.display, x_window.visual,
                                      x_window.color_map, &color, ncolor);
        } else {
            name = CONF_COLORS[i];
        }
    }

    return XftColorAllocName(x_window.display, x_window.visual,
                             x_window.color_map, name, ncolor);
}

void
x_load_cols(void) {
    static int32 loaded = 0;
    Color *cp;

    if (loaded) {
        for (cp = draw_context.color;
             cp < &draw_context.color[draw_context.collen]; ++cp) {
            XftColorFree(x_window.display, x_window.visual, x_window.color_map,
                         cp);
        }
    } else {
        draw_context.collen = MAX(LENGTH(CONF_COLORS), 256);
        draw_context.color
            = xmalloc((uint16)draw_context.collen*SIZEOF(Color));
    }

    for (int32 i = 0; i < draw_context.collen; i += 1) {
        if (!x_load_color(i, NULL, &draw_context.color[i])) {
            if (CONF_COLORS[i]) {
                die("could not allocate color '%s'\n", CONF_COLORS[i]);
            } else {
                die("could not allocate color %d\n", i);
            }
        }
    }

    draw_context.color[CONF_COLOR_BG].color.alpha
        = (uint16)(0xffff*CONF_ALPHA);
    draw_context.color[CONF_COLOR_BG].pixel &= 0x00FFFFFF;
    draw_context.color[CONF_COLOR_BG].pixel
        |= ((uint32)(0xFF*CONF_ALPHA) & 0xFF) << 24;

    for (int32 i = 16; i < 16 + CONF_NTRANSPARENT_COLORS; i += 1) {
        draw_context.color[i].color.alpha = (uint16)(0xffff*CONF_ALPHA);
        draw_context.color[i].pixel &= 0x00FFFFFF;
        draw_context.color[i].pixel |= ((uint32)(0xff*CONF_ALPHA) & 0xff)
                                       << 24;
    }
    loaded = 1;
    return;
}

int32
x_set_color_name(int32 x, const char *name) {
    Color ncolor;

    if (!BETWEEN(x, 0, draw_context.collen - 1)) {
        return 1;
    }

    if (!x_load_color(x, name, &ncolor)) {
        return 1;
    }

    XftColorFree(x_window.display, x_window.visual, x_window.color_map,
                 &draw_context.color[x]);
    draw_context.color[x] = ncolor;

    if (x == CONF_COLOR_BG) {
        draw_context.color[CONF_COLOR_BG].color.alpha
            = (uint16)(0xffff*CONF_ALPHA);
        draw_context.color[CONF_COLOR_BG].pixel &= 0x00FFFFFF;
        draw_context.color[CONF_COLOR_BG].pixel
            |= ((uint32)(0xff*CONF_ALPHA) & 0xff) << 24;
    }

    return 0;
}

/*
 * Absolute coordinates.
 */
void
x_clear(int32 x1, int32 y1, int32 x2, int32 y2) {
    int32 color_index;
    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        color_index = CONF_COLOR_INDEX_FONT;
    } else {
        color_index = CONF_COLOR_BG;
    }

    XftDrawRect(x_window.xft_draw, &draw_context.color[color_index], x1, y1,
                (uint32)(x2 - x1), (uint32)(y2 - y1));
    return;
}

void
x_hints(void) {
    XClassHint class = {opt_name ? opt_name : CONF_TERM_NAME,
                        opt_class ? opt_class : CONF_TERM_NAME};
    XWMHints wm = {.flags = InputHint, .input = 1};
    XSizeHints *sizeh;

    sizeh = XAllocSizeHints();

    sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
    sizeh->height = term_window.h;
    sizeh->width = term_window.w;
    sizeh->height_inc = 1;
    sizeh->width_inc = 1;
    sizeh->base_height = 2*CONF_BORDER_PIXELS;
    sizeh->base_width = 2*CONF_BORDER_PIXELS;
    sizeh->min_height = term_window.ch + 2*CONF_BORDER_PIXELS;
    sizeh->min_width = term_window.cw + 2*CONF_BORDER_PIXELS;
    if (x_window.is_fixed) {
        sizeh->flags |= PMaxSize;
        sizeh->min_width = sizeh->max_width = term_window.w;
        sizeh->min_height = sizeh->max_height = term_window.h;
    }
    if (x_window.geo_mask & (XValue | YValue)) {
        sizeh->flags |= USPosition | PWinGravity;
        sizeh->x = x_window.left_offset;
        sizeh->y = x_window.top_offset;
        sizeh->win_gravity = x_geom_mask_to_gravity(x_window.geo_mask);
    }

    XSetWMProperties(x_window.display, x_window.win, NULL, NULL, NULL, 0, sizeh,
                     &wm, &class);
    XFree(sizeh);
    return;
}

int32
x_geom_mask_to_gravity(int32 mask) {
    switch (mask & (XNegative | YNegative)) {
    case 0:
        return NorthWestGravity;
    case XNegative:
        return NorthEastGravity;
    case YNegative:
        return SouthWestGravity;
    default:
        fprintf(stderr, "x_geom_mask_to_gravity: Unhandled switch case.\n");
        break;
    }

    return SouthEastGravity;
}

int32
x_load_font(Font *f, FcPattern *pattern) {
    FcPattern *configured;
    FcPattern *match;
    FcResult result;
    XGlyphInfo extents;
    int32 wantattr, haveattr;

    /*
     * Manually configure instead of calling XftMatchFont
     * so that we can use the configured pattern for
     * "missing glyph" lookups.
     */
    configured = FcPatternDuplicate(pattern);
    if (!configured) {
        return 1;
    }

    FcConfigSubstitute(NULL, configured, FcMatchPattern);
    XftDefaultSubstitute(x_window.display, x_window.screen, configured);

    match = FcFontMatch(NULL, configured, &result);
    if (!match) {
        FcPatternDestroy(configured);
        return 1;
    }

    if (!(f->match = XftFontOpenPattern(x_window.display, match))) {
        FcPatternDestroy(configured);
        FcPatternDestroy(match);
        return 1;
    }

    if ((XftPatternGetInteger(pattern, "slant", 0, &wantattr)
         == XftResultMatch)) {
        /*
         * Check if xft was unable to find a font with the appropriate
         * slant but gave us one anyway. Try to mitigate.
         */
        if ((XftPatternGetInteger(f->match->pattern, "slant", 0, &haveattr)
             != XftResultMatch)
            || haveattr < wantattr) {
            f->badslant = 1;
            fputs("font slant does not match\n", stderr);
        }
    }

    if ((XftPatternGetInteger(pattern, "weight", 0, &wantattr)
         == XftResultMatch)) {
        if ((XftPatternGetInteger(f->match->pattern, "weight", 0, &haveattr)
             != XftResultMatch)
            || haveattr != wantattr) {
            f->badweight = 1;
            fputs("font weight does not match\n", stderr);
        }
    }

    XftTextExtentsUtf8(x_window.display, f->match,
                       (const FcChar8 *)CONF_ASCII_PRINTABLE,
                       (int32)(int64)strlen(CONF_ASCII_PRINTABLE), &extents);

    f->set = NULL;
    f->pattern = configured;

    f->ascent = f->match->ascent;
    f->descent = f->match->descent;
    f->lbearing = 0;
    f->rbearing = (int16)f->match->max_advance_width;

    f->height = f->ascent + f->descent;
    f->width
        = DIVCEIL(extents.xOff, (int32)(int64)strlen(CONF_ASCII_PRINTABLE));

    return 0;
}

void
x_load_fonts(const char *fontstr, float fontsize) {
    FcPattern *pattern;
    double fontval;

    if (fontstr[0] == '-') {
        pattern = XftXlfdParse(fontstr, False, False);
    } else {
        pattern = FcNameParse((const FcChar8 *)fontstr);
    }

    if (!pattern) {
        die("can't open font %s\n", fontstr);
    }

    if (fontsize > 1) {
        FcPatternDel(pattern, FC_PIXEL_SIZE);
        FcPatternDel(pattern, FC_SIZE);
        FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)fontsize);
        usedfontsize = fontsize;
    } else {
        if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval)
            == FcResultMatch) {
            usedfontsize = (float)fontval;
        } else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval)
                   == FcResultMatch) {
            usedfontsize = -1;
        } else {
            /*
             * Default font size is 12, if none given. This is to
             * have a known usedfontsize value.
             */
            FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
            usedfontsize = 12;
        }
        defaultfontsize = usedfontsize;
    }

    if (x_load_font(&draw_context.font, pattern)) {
        die("can't open font %s\n", fontstr);
    }

    if (usedfontsize < 0) {
        FcPatternGetDouble(draw_context.font.match->pattern, FC_PIXEL_SIZE, 0,
                           &fontval);
        usedfontsize = (float)fontval;
        if (fabsf(fontsize) <= 0) {
            defaultfontsize = (float)fontval;
        }
    }

    /* Setting character width and height. */
    term_window.cw
        = ceilf((float)(draw_context.font.width)*CONF_CHAR_WIDTH_SCALE);
    term_window.ch
        = ceilf((float)(draw_context.font.height)*CONF_CHAR_HEIGHT_SCALE);

    FcPatternDel(pattern, FC_SLANT);
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
    if (x_load_font(&draw_context.ifont, pattern)) {
        die("can't open font %s\n", fontstr);
    }

    FcPatternDel(pattern, FC_WEIGHT);
    FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
    if (x_load_font(&draw_context.ibfont, pattern)) {
        die("can't open font %s\n", fontstr);
    }

    FcPatternDel(pattern, FC_SLANT);
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
    if (x_load_font(&draw_context.bfont, pattern)) {
        die("can't open font %s\n", fontstr);
    }

    FcPatternDestroy(pattern);
    return;
}

int32
xloadsparefont(FcPattern *pattern, int32 flags) {
    FcPattern *match;
    FcResult result;

    match = FcFontMatch(NULL, pattern, &result);
    if (!match) {
        return 1;
    }

    if (!(frc[frclen].font = XftFontOpenPattern(x_window.display, match))) {
        FcPatternDestroy(match);
        return 1;
    }

    frc[frclen].flags = flags;
    /* Believe U+0000 glyph will present in each default font */
    frc[frclen].unicodep = 0;
    frclen++;

    return 0;
}

void
x_load_spare_fonts(void) {
    FcPattern *pattern;
    double sizeshift, fontval;
    int32 fc;
    char **fp;

    if (frclen != 0) {
        die("can't embed spare fonts. cache isn't empty");
    }

    /* Calculate count of spare fonts */
    fc = SIZEOF(CONF_FONT2) / SIZEOF(*CONF_FONT2);
    if (fc == 0) {
        return;
    }

    /* Allocate memory for cache entries. */
    if (frccap < 4*fc) {
        frccap += 4*fc - frccap;
        frc = xrealloc(frc, (int64)frccap*SIZEOF(Fontcache));
    }

    for (fp = CONF_FONT2; fp - CONF_FONT2 < fc; ++fp) {

        if (**fp == '-') {
            pattern = XftXlfdParse(*fp, False, False);
        } else {
            pattern = FcNameParse((FcChar8 *)*fp);
        }

        if (!pattern) {
            die("can't open spare font %s\n", *fp);
        }

        if (defaultfontsize > 0) {
            sizeshift = (double)(usedfontsize - defaultfontsize);
            if ((fabs(sizeshift) < 0.001) != 0
                && FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval)
                       == FcResultMatch) {
                fontval += sizeshift;
                FcPatternDel(pattern, FC_PIXEL_SIZE);
                FcPatternDel(pattern, FC_SIZE);
                FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontval);
            }
        }

        FcPatternAddBool(pattern, FC_SCALABLE, 1);

        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        XftDefaultSubstitute(x_window.display, x_window.screen, pattern);

        if (xloadsparefont(pattern, FRC_NORMAL)) {
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
        if (xloadsparefont(pattern, FRC_ITALIC)) {
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDel(pattern, FC_WEIGHT);
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
        if (xloadsparefont(pattern, FRC_ITALICBOLD)) {
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
        if (xloadsparefont(pattern, FRC_BOLD)) {
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDestroy(pattern);
    }
    return;
}

void
x_unload_font(Font *f) {
    XftFontClose(x_window.display, f->match);
    FcPatternDestroy(f->pattern);
    if (f->set) {
        FcFontSetDestroy(f->set);
    }
    return;
}

void
x_unload_fonts(void) {
    /* Free the loaded fonts in the font cache.  */
    while (frclen > 0) {
        XftFontClose(x_window.display, frc[--frclen].font);
    }

    x_unload_font(&draw_context.font);
    x_unload_font(&draw_context.bfont);
    x_unload_font(&draw_context.ifont);
    x_unload_font(&draw_context.ibfont);
    return;
}

int32
x_im_open(Display *display) {
    XIMCallback imdestroy = {.client_data = NULL, .callback = x_im_destroy};
    XICCallback icdestroy = {.client_data = NULL, .callback = x_ic_destroy};
    (void)display;

    x_window.ime.xim = XOpenIM(x_window.display, NULL, NULL, NULL);
    if (x_window.ime.xim == NULL) {
        return 0;
    }

    if (XSetIMValues(x_window.ime.xim, XNDestroyCallback, &imdestroy, NULL)) {
        fprintf(stderr, "XSetIMValues: "
                        "Could not set XNDestroyCallback.\n");
    }

    x_window.ime.spotlist
        = XVaCreateNestedList(0, XNSpotLocation, &x_window.ime.point, NULL);

    if (x_window.ime.xic == NULL) {
        x_window.ime.xic
            = XCreateIC(x_window.ime.xim, XNInputStyle,
                        XIMPreeditNothing | XIMStatusNothing, XNClientWindow,
                        x_window.win, XNDestroyCallback, &icdestroy, NULL);
    }
    if (x_window.ime.xic == NULL) {
        fprintf(stderr, "XCreateIC: Could not create input context.\n");
    }

    return 1;
}

void
x_im_instantiate(Display *display, XPointer client, XPointer call) {
    (void)client;
    (void)call;
    if (x_im_open(display)) {
        XUnregisterIMInstantiateCallback(x_window.display, NULL, NULL, NULL,
                                         x_im_instantiate, NULL);
    }
    return;
}

void
x_im_destroy(XIM xim, XPointer client, XPointer call) {
    (void)xim;
    (void)client;
    (void)call;
    x_window.ime.xim = NULL;
    XRegisterIMInstantiateCallback(x_window.display, NULL, NULL, NULL,
                                   x_im_instantiate, NULL);
    XFree(x_window.ime.spotlist);
    return;
}

int32
x_ic_destroy(XIC xim, XPointer client, XPointer call) {
    (void)xim;
    (void)client;
    (void)call;
    x_window.ime.xic = NULL;
    return 1;
}

int32
x_make_glyph_font_specs(XftGlyphFontSpec *specs, const Glyph *glyphs, int32 len,
                        int32 x, int32 y) {
    int32 winx = term_window.hborderpx + x*term_window.cw;
    int32 winy = term_window.vborderpx + y*term_window.ch;
    uint16 mode, prevmode = USHRT_MAX;
    Font *font_local = &draw_context.font;
    int32 frcflags = FRC_NORMAL;
    int32 runewidth = term_window.cw;
    Rune rune;
    FT_UInt glyphidx;
    FcResult fcres;
    FcPattern *fcpattern, *fontpattern;
    FcFontSet *fcsets[] = {NULL};
    FcCharSet *fccharset;
    int32 f, numspecs = 0;
    int32 xp = winx;
    int32 yp = winy + font_local->ascent;

    for (int32 i = 0; i < len; ++i) {
        /* Fetch rune and mode for current glyph. */
        rune = glyphs[i].rune;
        mode = glyphs[i].mode;

        /* Skip dummy wide-character spacing. */
        if (mode == ATTR_WDUMMY) {
            continue;
        }

        /* Determine font for glyph if different from previous glyph. */
        if (prevmode != mode) {
            prevmode = mode;
            font_local = &draw_context.font;
            frcflags = FRC_NORMAL;
            runewidth = term_window.cw*((mode & ATTR_WIDE) ? 2 : 1);
            if ((mode & ATTR_ITALIC) && (mode & ATTR_BOLD)) {
                font_local = &draw_context.ibfont;
                frcflags = FRC_ITALICBOLD;
            } else if (mode & ATTR_ITALIC) {
                font_local = &draw_context.ifont;
                frcflags = FRC_ITALIC;
            } else if (mode & ATTR_BOLD) {
                font_local = &draw_context.bfont;
                frcflags = FRC_BOLD;
            }
            yp = winy + font_local->ascent;
        }

        if (mode & ATTR_BOXDRAW) {
            /* minor shoehorning: CONF_BOXDRAW uses only this uint16 */
            glyphidx = boxdrawindex(&glyphs[i]);
        } else {
            /* Lookup character index with default font. */
            glyphidx = XftCharIndex(x_window.display, font_local->match, rune);
        }
        if (glyphidx) {
            specs[numspecs].font = font_local->match;
            specs[numspecs].glyph = glyphidx;
            specs[numspecs].x = (int16)xp;
            specs[numspecs].y = (int16)yp;
            xp += runewidth;
            numspecs++;
            continue;
        }

        /* Fallback on font cache, search the font cache for match. */
        for (f = 0; f < frclen; f++) {
            glyphidx = XftCharIndex(x_window.display, frc[f].font, rune);
            /* Everything correct. */
            if (glyphidx && frc[f].flags == frcflags) {
                break;
            }
            /* We got a default font for a not found glyph. */
            if (!glyphidx && frc[f].flags == frcflags
                && frc[f].unicodep == rune) {
                break;
            }
        }

        /* Nothing was found. Use fontconfig to find matching font. */
        if (f >= frclen) {
            if (!font_local->set) {
                font_local->set
                    = FcFontSort(0, font_local->pattern, 1, 0, &fcres);
            }
            fcsets[0] = font_local->set;

            /*
             * Nothing was found in the cache. Now use
             * some dozen of Fontconfig calls to get the
             * font for one single character.
             *
             * Xft and fontconfig are design failures.
             */
            fcpattern = FcPatternDuplicate(font_local->pattern);
            fccharset = FcCharSetCreate();

            FcCharSetAddChar(fccharset, rune);
            FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
            FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

            FcConfigSubstitute(0, fcpattern, FcMatchPattern);
            FcDefaultSubstitute(fcpattern);

            fontpattern = FcFontSetMatch(0, fcsets, 1, fcpattern, &fcres);

            /* Allocate memory for the new cache entry. */
            if (frclen >= frccap) {
                frccap += 16;
                frc = xrealloc(frc, (int64)frccap*SIZEOF(Fontcache));
            }

            frc[frclen].font
                = XftFontOpenPattern(x_window.display, fontpattern);
            if (!frc[frclen].font) {
                die("XftFontOpenPattern failed seeking fallback font: %s\n",
                    strerror(errno));
            }
            frc[frclen].flags = frcflags;
            frc[frclen].unicodep = rune;

            glyphidx = XftCharIndex(x_window.display, frc[frclen].font, rune);

            f = frclen;
            frclen++;

            FcPatternDestroy(fcpattern);
            FcCharSetDestroy(fccharset);
        }

        specs[numspecs].font = frc[f].font;
        specs[numspecs].glyph = glyphidx;
        specs[numspecs].x = (int16)xp;
        specs[numspecs].y = (int16)yp;
        xp += runewidth;
        numspecs++;
    }

    return numspecs;
}

void
x_draw_glyph_font_specs(const XftGlyphFontSpec *specs, Glyph base, int32 len,
                        int32 x, int32 y) {
    int32 charlen = len*((base.mode & ATTR_WIDE) ? 2 : 1);
    int32 winx = term_window.hborderpx + x*term_window.cw;
    int32 winy = term_window.vborderpx + y*term_window.ch;
    int32 width = charlen*term_window.cw;
    Color *fg, *bg, *temp, revfg, revbg, truefg, truebg;
    XRenderColor colfg, colbg;
    XRectangle r;

    /* Fallback on color display for attributes not supported by the font */
    if (base.mode & ATTR_ITALIC && base.mode & ATTR_BOLD) {
        if (draw_context.ibfont.badslant || draw_context.ibfont.badweight) {
            base.fg = (int32)CONF_DEFAULT_ATTR;
        }
    } else if ((base.mode & ATTR_ITALIC && draw_context.ifont.badslant)
               || (base.mode & ATTR_BOLD && draw_context.bfont.badweight)) {
        base.fg = (int32)CONF_DEFAULT_ATTR;
    }

    if (IS_TRUECOL(base.fg)) {
        colfg.alpha = 0xffff;
        colfg.red = TRUE_RED(base.fg);
        colfg.green = TRUE_GREEN(base.fg);
        colfg.blue = TRUE_BLUE(base.fg);
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &colfg, &truefg);
        fg = &truefg;
    } else {
        fg = &draw_context.color[base.fg];
    }

    if (IS_TRUECOL(base.bg)) {
        colbg.alpha = 0xffff;
        colbg.green = TRUE_GREEN(base.bg);
        colbg.red = TRUE_RED(base.bg);
        colbg.blue = TRUE_BLUE(base.bg);
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &colbg, &truebg);
        bg = &truebg;
    } else {
        bg = &draw_context.color[base.bg];
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        if (fg == &draw_context.color[CONF_COLOR_INDEX_FONT]) {
            fg = &draw_context.color[CONF_COLOR_BG];
        } else {
            colfg.red = ~fg->color.red;
            colfg.green = ~fg->color.green;
            colfg.blue = ~fg->color.blue;
            colfg.alpha = fg->color.alpha;
            XftColorAllocValue(x_window.display, x_window.visual,
                               x_window.color_map, &colfg, &revfg);
            fg = &revfg;
        }

        if (bg == &draw_context.color[CONF_COLOR_BG]) {
            bg = &draw_context.color[CONF_COLOR_INDEX_FONT];
        } else {
            colbg.red = ~bg->color.red;
            colbg.green = ~bg->color.green;
            colbg.blue = ~bg->color.blue;
            colbg.alpha = bg->color.alpha;
            XftColorAllocValue(x_window.display, x_window.visual,
                               x_window.color_map, &colbg, &revbg);
            bg = &revbg;
        }
    }

    if ((base.mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
        colfg.red = fg->color.red / 2;
        colfg.green = fg->color.green / 2;
        colfg.blue = fg->color.blue / 2;
        colfg.alpha = fg->color.alpha;
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &colfg, &revfg);
        fg = &revfg;
    }

    if (base.mode & ATTR_REVERSE) {
        temp = fg;
        fg = bg;
        bg = temp;
    }

    if (base.mode & ATTR_SELECTED) {
        bg = &draw_context.color[CONF_COLOR_INDEX_SELECTION_BACK];
        if (!CONF_COLOR_IGNORE_SELECTION_FONT_COLOR) {
            fg = &draw_context.color[CONF_COLOR_INDEX_SELECTION_FONT];
        }
    }

    if (base.mode & ATTR_BLINK && term_window.mode & WIN_MODE_BLINK) {
        fg = bg;
    }

    if (base.mode & ATTR_INVISIBLE) {
        fg = bg;
    }

    /* Intelligent cleaning up of the borders. */
    if (x == 0) {
        x_clear(0, (y == 0) ? 0 : winy, term_window.hborderpx,
                winy + term_window.ch
                    + ((winy + term_window.ch
                        >= term_window.vborderpx + term_window.tty_height)
                           ? term_window.h
                           : 0));
    }
    if (winx + width >= term_window.hborderpx + term_window.tty_width) {
        x_clear(winx + width, (y == 0) ? 0 : winy, term_window.w,
                ((winy + term_window.ch
                  >= term_window.vborderpx + term_window.tty_height)
                     ? term_window.h
                     : (winy + term_window.ch)));
    }
    if (y == 0) {
        x_clear(winx, 0, winx + width, term_window.vborderpx);
    }
    if (winy + term_window.ch
        >= term_window.vborderpx + term_window.tty_height) {
        x_clear(winx, winy + term_window.ch, winx + width, term_window.h);
    }

    /* Clean up the region we want to draw to. */
    XftDrawRect(x_window.xft_draw, bg, winx, winy, (uint32)width,
                (uint32)term_window.ch);

    /* Set the clip region because Xft is sometimes dirty. */
    r.x = 0;
    r.y = 0;
    r.height = (uint16)term_window.ch;
    r.width = (uint16)width;
    XftDrawSetClipRectangles(x_window.xft_draw, winx, winy, &r, 1);

    if (base.mode & ATTR_BOXDRAW) {
        drawboxes(winx, winy, width / len, term_window.ch, fg, bg, specs, len);
    } else {
        /* Render the glyphs. */
        XftDrawGlyphFontSpec(x_window.xft_draw, fg, specs, len);
    }

    /* Render underline and strikethrough. */
    if (base.mode & ATTR_UNDERLINE) {
        XftDrawRect(x_window.xft_draw, fg, winx,
                    winy
                        + (int32)((float)draw_context.font.ascent
                                  * CONF_CHAR_HEIGHT_SCALE)
                        + 1,
                    (uint32)width, 1);
    }

    if (base.mode & ATTR_STRUCK) {
        XftDrawRect(x_window.xft_draw, fg, winx,
                    winy
                        + 2
                              * (int32)((float)draw_context.font.ascent
                                        * CONF_CHAR_HEIGHT_SCALE / 3),
                    (uint32)width, 1);
    }

    /* Reset clip to none. */
    XftDrawSetClip(x_window.xft_draw, 0);
    return;
}

void
x_draw_glyph(Glyph g, int32 x, int32 y) {
    int32 numspecs;
    XftGlyphFontSpec spec;

    numspecs = x_make_glyph_font_specs(&spec, &g, 1, x, y);
    x_draw_glyph_font_specs(&spec, g, numspecs, x, y);
    return;
}

void
x_draw_cursor(int32 cx, int32 cy, Glyph g, int32 ox, int32 oy, Glyph og) {
    Color drawcol;

    /* remove the old cursor */
    if (selection_is_selected(ox, oy)) {
        og.mode |= ATTR_SELECTED;
    }
    x_draw_glyph(og, ox, oy);

    if (TERM_WINDOW_IS_SET(WIN_MODE_HIDE)) {
        return;
    }

    /*
     * Select the right color for the right mode.
     */
    g.mode &= ATTR_BOLD | ATTR_ITALIC | ATTR_UNDERLINE | ATTR_STRUCK | ATTR_WIDE
              | ATTR_BOXDRAW;

    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        g.mode |= ATTR_REVERSE;
        g.fg = CONF_COLOR_INDEX_CURSOR;
        g.bg = CONF_COLOR_INDEX_FONT;
        drawcol = draw_context.color[CONF_COLOR_INDEX_REVCURSOR];
    } else {
        g.fg = CONF_COLOR_BG;
        g.bg = CONF_COLOR_INDEX_CURSOR;
        drawcol = draw_context.color[CONF_COLOR_INDEX_CURSOR];
    }

    /* draw the new one */
    if (TERM_WINDOW_IS_SET(WIN_MODE_FOCUSED)) {
        switch (term_window.cursor) {
        case 7:              /* st extension */
            g.rune = 0x2603; /* snowman (U+2603) */
                             /* FALLTHROUGH */
        case 0:              /* Blinking Block */
        case 1:              /* Blinking Block (Default) */
        case 2:              /* Steady Block */
            x_draw_glyph(g, cx, cy);
            break;
        case 3: /* Blinking Underline */
        case 4: /* Steady Underline */
            XftDrawRect(x_window.xft_draw, &drawcol,
                        term_window.hborderpx + cx*term_window.cw,
                        term_window.vborderpx + (cy + 1)*term_window.ch
                            - (int32)CONF_CURSOR_THICKNESS,
                        (uint32)term_window.cw, (uint32)CONF_CURSOR_THICKNESS);
            break;
        case 5: /* Blinking bar */
        case 6: /* Steady bar */
            XftDrawRect(x_window.xft_draw, &drawcol,
                        term_window.hborderpx + cx*term_window.cw,
                        term_window.vborderpx + cy*term_window.ch,
                        CONF_CURSOR_THICKNESS, (uint32)term_window.ch);
            break;
        default:
            fprintf(stderr, "x_draw_cursor: Unhandled switch case.\n");
            break;
        }
    } else {
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + cy*term_window.ch,
                    (uint32)(term_window.cw - 1), 1);
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + cy*term_window.ch, 1,
                    (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + (cx + 1)*term_window.cw - 1,
                    term_window.vborderpx + cy*term_window.ch, 1,
                    (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + (cy + 1)*term_window.ch - 1,
                    (uint32)term_window.cw, 1);
    }
    return;
}

void
x_set_icon_title(char *p) {
    XTextProperty prop;
    DEFAULT(p, opt_title);

    if (p[0] == '\0') {
        p = opt_title;
    }

    if (Xutf8TextListToTextProperty(x_window.display, &p, 1, XUTF8StringStyle,
                                    &prop)
        != Success) {
        return;
    }
    XSetWMIconName(x_window.display, x_window.win, &prop);
    XSetTextProperty(x_window.display, x_window.win, &prop,
                     x_window.net_wm_iconname);
    XFree(prop.value);
    return;
}

void
x_set_title(char *p) {
    XTextProperty prop;
    DEFAULT(p, opt_title);

    if (p[0] == '\0') {
        p = opt_title;
    }

    if (Xutf8TextListToTextProperty(x_window.display, &p, 1, XUTF8StringStyle,
                                    &prop)
        != Success) {
        return;
    }
    XSetWMName(x_window.display, x_window.win, &prop);
    XSetTextProperty(x_window.display, x_window.win, &prop,
                     x_window.net_wm_name);
    XFree(prop.value);
    return;
}

int32
x_start_draw(void) {
    return TERM_WINDOW_IS_SET(WIN_MODE_VISIBLE);
}

void
x_draw_line(Glyph *line, int32 x1, int32 y1, int32 x2) {
    int32 i, x, ox, numspecs;
    Glyph base = {0}, new = {0};
    XftGlyphFontSpec *specs = x_window.specbuf;

    numspecs = x_make_glyph_font_specs(specs, &line[x1], x2 - x1, x1, y1);
    i = ox = 0;
    for (x = x1; x < x2 && i < numspecs; x++) {
        new = line[x];
        if (new.mode == ATTR_WDUMMY) {
            continue;
        }
        if (selection_is_selected(x, y1)) {
            new.mode |= ATTR_SELECTED;
        }
        if (i > 0 && ATTRCMP(base, new)) {
            x_draw_glyph_font_specs(specs, base, i, ox, y1);
            specs += i;
            numspecs -= i;
            i = 0;
        }
        if (i == 0) {
            ox = x;
            base = new;
        }
        i++;
    }
    if (i > 0) {
        x_draw_glyph_font_specs(specs, base, i, ox, y1);
    }
    return;
}

void
x_finish_draw(void) {
    XCopyArea(x_window.display, x_window.drawable, x_window.win,
              draw_context.graphics, 0, 0, (uint32)term_window.w,
              (uint32)term_window.h, 0, 0);
    XSetForeground(
        x_window.display, draw_context.graphics,
        draw_context
            .color[TERM_WINDOW_IS_SET(WIN_MODE_REVERSE) ? CONF_COLOR_INDEX_FONT
                                                        : CONF_COLOR_BG]
            .pixel);
    return;
}

void
x_xim_spot(int32 x, int32 y) {
    if (x_window.ime.xic == NULL) {
        return;
    }

    x_window.ime.point.x = (int16)(CONF_BORDER_PIXELS + x*term_window.cw);
    x_window.ime.point.y
        = (int16)(CONF_BORDER_PIXELS + (y + 1)*term_window.ch);

    XSetICValues(x_window.ime.xic, XNPreeditAttributes, x_window.ime.spotlist,
                 NULL);
    return;
}

void
handler_expose(XEvent *xevent) {
    (void)xevent;
    redraw();
    return;
}

void
handler_visibility(XEvent *xevent) {
    XVisibilityEvent *e = &xevent->xvisibility;

    MODBIT(term_window.mode, e->state != VisibilityFullyObscured,
           WIN_MODE_VISIBLE);
    return;
}

void
handler_unmap(XEvent *xevent) {
    (void)xevent;
    term_window.mode &= ~WIN_MODE_VISIBLE;
    return;
}

void
x_set_pointer_motion(int32 set) {
    MODBIT(x_window.attrs.event_mask, set, PointerMotionMask);
    XChangeWindowAttributes(x_window.display, x_window.win, CWEventMask,
                            &x_window.attrs);
    return;
}

void
x_set_mode(int32 set, uint32 flags) {
    int32 mode = term_window.mode;
    MODBIT(term_window.mode, set, flags);
    if ((term_window.mode & WIN_MODE_REVERSE) != (mode & WIN_MODE_REVERSE)) {
        redraw();
    }
    return;
}

int32
x_set_cursor(int32 cursor) {
    if (!BETWEEN(cursor, 0, 7)) { /* 7: st extension */
        return 1;
    }
    term_window.cursor = cursor;
    return 0;
}

void
x_set_urgency(int32 add) {
    XWMHints *h = XGetWMHints(x_window.display, x_window.win);

    MODBIT(h->flags, add, XUrgencyHint);
    XSetWMHints(x_window.display, x_window.win, h);
    XFree(h);
    return;
}

void
x_bell(void) {
    if (!(TERM_WINDOW_IS_SET(WIN_MODE_FOCUSED))) {
        x_set_urgency(1);
    }
    if (CONF_BELL_VOLUME) {
        XkbBell(x_window.display, x_window.win, CONF_BELL_VOLUME, (Atom)NULL);
    }
    return;
}

void
handler_focus(XEvent *xevent) {
    XFocusChangeEvent *e = &xevent->xfocus;

    if (e->mode == NotifyGrab) {
        return;
    }

    if (xevent->type == FocusIn) {
        if (x_window.ime.xic) {
            XSetICFocus(x_window.ime.xic);
        }
        term_window.mode |= WIN_MODE_FOCUSED;
        x_set_urgency(0);
        if (TERM_WINDOW_IS_SET(WIN_MODE_FOCUS)) {
            tty_write("\033[I", 3, 0);
        }
    } else {
        if (x_window.ime.xic) {
            XUnsetICFocus(x_window.ime.xic);
        }
        term_window.mode &= ~WIN_MODE_FOCUSED;
        if (TERM_WINDOW_IS_SET(WIN_MODE_FOCUS)) {
            tty_write("\033[O", 3, 0);
        }
    }
    return;
}

int32
match_mask_state(uint32 mask, uint32 state) {
    return mask == XK_ANY_MOD || mask == (state & ~CONF_IGNORE_MOD);
}

void
handler_key_press(XEvent *xevent) {
    XKeyEvent *e = &xevent->xkey;
    KeySym key_sym = NoSymbol;
    char buffer[64];
    char *custom_key = NULL;
    int32 len;
    Rune c;
    Status status;
    Shortcut *bp;

    if (TERM_WINDOW_IS_SET(WIN_MODE_KBDLOCK)) {
        return;
    }

    if (x_window.ime.xic) {
        len = XmbLookupString(x_window.ime.xic, e, buffer, SIZEOF(buffer),
                              &key_sym, &status);
        if (status == XBufferOverflow) {
            return;
        }
    } else {
        len = XLookupString(e, buffer, SIZEOF(buffer), &key_sym, NULL);
    }
    /* 1. CONF_KEYBOARD_SHORTCUTS */
    for (bp = CONF_KEYBOARD_SHORTCUTS;
         bp < CONF_KEYBOARD_SHORTCUTS + LENGTH(CONF_KEYBOARD_SHORTCUTS); bp++) {
        if (key_sym == bp->keysym && match_mask_state(bp->mod, e->state)) {
            bp->func(&(bp->arg));
            return;
        }
    }

    /* 2. custom keys from config.def.h */
    {
        int32 i;

        /* Check for mapped keys out of X11 function keys. */
        for (i = 0; i < LENGTH(CONF_MAPPED_KEYS); i++) {
            if (CONF_MAPPED_KEYS[i] == key_sym) {
                break;
            }
        }
        if (i == LENGTH(CONF_MAPPED_KEYS)) {
            if ((key_sym & 0xFFFF) < 0xFD00) {
                goto tried_custom_keys;
            }
        }

        for (Key *kp = CONF_KEYS; kp < CONF_KEYS + LENGTH(CONF_KEYS); kp += 1) {
            if (kp->k != key_sym) {
                continue;
            }

            if (!match_mask_state(kp->mask, e->state)) {
                continue;
            }

            if (TERM_WINDOW_IS_SET(WIN_MODE_APPKEYPAD)) {
                if (kp->appkey < 0) {
                    continue;
                }
            } else {
                if (kp->appkey > 0) {
                    continue;
                }
            }

            if (TERM_WINDOW_IS_SET(WIN_MODE_NUMLOCK) && kp->appkey == 2) {
                continue;
            }

            if (TERM_WINDOW_IS_SET(WIN_MODE_APPCURSOR)) {
                if (kp->appcursor < 0) {
                    continue;
                }
            } else {
                if (kp->appcursor > 0) {
                    continue;
                }
            }
            custom_key = kp->s;
            goto tried_custom_keys;
        }
        custom_key = NULL;
    }
tried_custom_keys:
    if (custom_key) {
        tty_write(custom_key, (int64)strlen(custom_key), 1);
        return;
    }

    /* 3. composed string from input method */
    if (len == 0) {
        return;
    }
    if (len == 1 && e->state & Mod1Mask) {
        if (TERM_WINDOW_IS_SET(WIN_MODE_8BIT)) {
            if (*buffer < 0177) {
                c = (Rune)(*buffer | 0x80);
                len = (int32)utf8_encode(c, buffer);
            }
        } else {
            buffer[1] = buffer[0];
            buffer[0] = '\033';
            len = 2;
        }
    }
    tty_write(buffer, (int64)len, 1);
    return;
}

void
handler_client_message(XEvent *xevent) {
    /*
     * See xembed specs
     *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
     */
    if (xevent->xclient.message_type == x_window.xembed
        && xevent->xclient.format == 32) {
        if (xevent->xclient.data.l[1] == XEMBED_FOCUS_IN) {
            term_window.mode |= WIN_MODE_FOCUSED;
            x_set_urgency(0);
        } else if (xevent->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
            term_window.mode &= ~WIN_MODE_FOCUSED;
        }
    } else if (xevent->xclient.data.l[0] == (int64)x_window.wm_delete_win) {
        tty_hangup();
        exit(0);
    }
    return;
}

void
handler_configure_notify(XEvent *xevent) {
    if ((xevent->xconfigure.width == term_window.w)
        && (xevent->xconfigure.height == term_window.h)) {
        return;
    }

    cresize(xevent->xconfigure.width, xevent->xconfigure.height);
}

void
usage(void) {
    die("usage: %s [-aiv] [-c class] [-f font] [-g geometry]"
        " [-n name] [-o file]\n"
        "          [-T title] [-t title] [-w windowid]"
        " [[-e] command [args ...]]\n"
        "       %s [-aiv] [-c class] [-f font] [-g geometry]"
        " [-n name] [-o file]\n"
        "          [-T title] [-t title] [-w windowid] -l line"
        " [CONF_STTY_ARGS ...]\n",
        argv0, argv0);
}
