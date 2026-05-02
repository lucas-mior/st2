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

#include "util.c"
#include "arg.h"
#include "st.h"
#include "st.c"
#include "boxdraw.c"
#include "sixel.c"
#include "x.c"

#include <Imlib2.h>
/* types used in config.def.h */

/* config.def.h for applying patches and the configuration. */
#include "config.def.h"

static inline uint16 sixd_to_16bit(int32);

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

static char *opt_class = NULL;
static char **opt_cmd = NULL;
static char *opt_embed = NULL;
static char *opt_font = NULL;
static char *opt_iofile = NULL;
static char *opt_line = NULL;
static char *opt_name = NULL;
static char *opt_title = NULL;

int32
main(int32 argc, char *argv[]) {
    x_window.left_offset = 0;
    x_window.top_offset = 0;
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
            argc -= 1;
            argv += 1;
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
    CONF_NUMBER_COLS = (int32)MAX(CONF_NUMBER_COLS, 1);
    CONF_NUMBER_ROWS = (int32)MAX(CONF_NUMBER_ROWS, 1);

    for (int32 i = 0; i < 2; i += 1) {
        term.line = xmalloc((int64)CONF_NUMBER_ROWS*SIZEOF(*(term.line)));
        for (int32 j = 0; j < CONF_NUMBER_ROWS; j += 1) {
            term.line[j]
                = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(*(term.line[j])));
        }
        term.ncols = CONF_NUMBER_COLS;
        term.nrows = CONF_NUMBER_ROWS;
        term_swap_screen();
    }
    term.dirty = xmalloc((int64)CONF_NUMBER_ROWS*SIZEOF(*term.dirty));
    term.tabs = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(*term.tabs));
    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        term.hist[i] = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(Glyph));
    }
    term_reset();

    {
        XGCValues xgc_values;
        Cursor cursor;
        Window parent = 0;
        Window root;
        pid_t pid_this = getpid();
        XColor xmouse_fg;
        XColor xmouse_bg;
        XWindowAttributes attr;
        XVisualInfo visual;

        if (!(x_window.display = XOpenDisplay(NULL))) {
            die("can't open display\n");
        }
        /* XSynchronize(x_window.display, 1); */
        x_window.screen = XDefaultScreen(x_window.display);

        root = XRootWindow(x_window.display, x_window.screen);
        if (opt_embed) {
            parent = (Window)strtol(opt_embed, NULL, 0);
        }
        if (!parent) {
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
            = draw_context.colors[CONF_COLOR_BG].pixel;
        x_window.attrs.border_pixel = draw_context.colors[CONF_COLOR_BG].pixel;
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
                       draw_context.colors[CONF_COLOR_BG].pixel);
        XFillRectangle(x_window.display, x_window.drawable,
                       draw_context.graphics, 0, 0, (uint32)term_window.w,
                       (uint32)term_window.h);

        /* font spec buffer */
        x_window.specbuf
            = xmalloc((int64)CONF_NUMBER_COLS*SIZEOF(XftGlyphFontSpec));

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
        int32 xfd = XConnectionNumber(x_window.display);
        int32 ttyfd;
        int32 xev;
        int32 drawing;
        struct timespec seltv;
        struct timespec *tv;
        struct timespec now;
        struct timespec lastblink;
        struct timespec trigger;
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

            seltv.tv_sec = (long)((float)timeout / 1E3f);
            seltv.tv_nsec
                = (long)(1E6f*((float)timeout - 1E3f*(float)seltv.tv_sec));
            if (timeout >= 0) {
                tv = &seltv;
            } else {
                tv = NULL;
            }

            if (pselect((int32)MAX(xfd, ttyfd) + 1, &read_fd, NULL, NULL, tv,
                        NULL)
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

            XFlush(x_window.display);
            drawing = 0;
        }
    }
}

void
zoom_abs(Arg *arg) {
    int32 i;
    ImageList *im;
    x_unload_fonts();
    x_load_fonts(usedfont, arg->f);
    x_load_spare_fonts();

    for (im = term.images, i = 0; i < 2; i += 1, im = term.images_alt) {
        for (; im; im = im->next) {
            if (im->pixmap) {
                XFreePixmap(x_window.display, (Drawable)im->pixmap);
            }
            if (im->clipmask) {
                XFreePixmap(x_window.display, (Drawable)im->clipmask);
            }
            im->pixmap = NULL;
            im->clipmask = NULL;
        }
    }

    cresize(0, 0);
    redraw();
    x_hints();
    return;
}

#include "user.c"

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
    int32 type;
    int32 seltype = SELECTION_REGULAR;
    uint32 state = xevent->xbutton.state & ~(Button1Mask | CONF_FORCE_MOUSE_MOD);

    for (type = 1; type < LENGTH(CONF_SELECTION_MASKS); type += 1) {
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
    int32 len;
    int32 button;
    int32 code;
    int32 x = xevent_col(xevent);
    int32 y = xevent_row(xevent);
    int32 state = (int32)xevent->xbutton.state;
    char buffer[40];
    static int32 ox;
    static int32 oy;

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
        for (button = 1;
		     button <= 11 && !(buttons & (1 << (button - 1)));
             button += 1) {
        }
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
    if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSESGR) && xevent->type == ButtonRelease) {
        code += 3;
    } else if (button == 12) {
        code += 3;
    } else if (button >= 8) {
        code += 128 + button - 8;
    } else if (button >= 4) {
        code += 64 + button - 4;
    } else {
        code += button - 1;
    }

    if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSEX10)) {
        if (state & ShiftMask) {
            code += 4;
        }
        if (state & Mod1Mask) {
            code += 8;
        }
        if (state & ControlMask) {
            code += 16;
        }
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
    if (button == Button1) {
        return Button1Mask;
    }
    if (button == Button2) {
        return Button2Mask;
    }
    if (button == Button3) {
        return Button3Mask;
    }
    if (button == Button4) {
        return Button4Mask;
    }
    if (button == Button5) {
        return Button5Mask;
    }
    return 0;
}

int32
mouse_action(XEvent *xevent, uint32 release) {
    MouseShortcut *mouse_shortcut;
    /* ignore Button<N>mask for Button<N> - it's set on release */
    uint32 state = xevent->xbutton.state & ~button_mask(xevent->xbutton.button);

    for (mouse_shortcut = CONF_MOUSE_SHORTCUTS;
         mouse_shortcut < CONF_MOUSE_SHORTCUTS + LENGTH(CONF_MOUSE_SHORTCUTS);
         mouse_shortcut += 1) {
        if (mouse_shortcut->release == release
            && mouse_shortcut->button == xevent->xbutton.button) {
            if (match_mask_state(mouse_shortcut->mod, state)) {
                mouse_shortcut->func(&(mouse_shortcut->arg));
                return 1;
            }
            if (match_mask_state(mouse_shortcut->mod, state & ~CONF_FORCE_MOUSE_MOD)) {
                mouse_shortcut->func(&(mouse_shortcut->arg));
                return 1;
            }
        }
    }

    return 0;
}

void
cresize(int32 width, int32 height) {
    int32 col;
    int32 row;

    if (width != 0) {
        term_window.w = width;
    }
    if (height != 0) {
        term_window.h = height;
    }

    col = (term_window.w - 2*CONF_BORDER_PIXELS) / term_window.cw;
    row = (term_window.h - 2*CONF_BORDER_PIXELS) / term_window.ch;
    col = (int32)MAX(1, col);
    row = (int32)MAX(1, row);

    term_window.hborderpx = (term_window.w - col*term_window.cw) / 2;
    term_window.vborderpx = (term_window.h - row*term_window.ch) / 2;

    term_resize(col, row);
    x_resize(col, row);
    tty_resize(term_window.tty_width, term_window.tty_height);
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
match_mask_state(uint32 mask, uint32 state) {
    if (mask == XK_ANY_MOD) {
        return 1;
    }
    if (mask == (state & ~CONF_IGNORE_MOD)) {
        return 1;
    }
    return 0;
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
