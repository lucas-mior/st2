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
#include "user.c"

#include <Imlib2.h>
#include "config.h"

static void usage(void) __attribute__((noreturn));

static void (*handler[LASTEvent])(XEvent *) = {
    [KeyPress]         = handler_key_press,
    [ClientMessage]    = handler_client_message,
    [ConfigureNotify]  = handler_configure_notify,
    [VisibilityNotify] = handler_visibility,
    [UnmapNotify]      = handler_unmap,
    [Expose]           = handler_expose,
    [FocusIn]          = handler_focus,
    [FocusOut]         = handler_focus,
    [MotionNotify]     = handler_button_motion,
    [ButtonPress]      = handler_button_press,
    [ButtonRelease]    = handler_button_release,
    [SelectionClear]   = handler_selection_clear,
    [SelectionNotify]  = handler_selection_notify,
    [PropertyNotify]   = handler_prop_notify,
    [SelectionRequest] = handler_selection_request,
};

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
        LIMIT(CONF_ALPHA, 0.0, 1.0);
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
        term.lines = xmalloc(CONF_NUMBER_ROWS*SIZEOF(*(term.lines)));
        for (int32 j = 0; j < CONF_NUMBER_ROWS; j += 1) {
            term.lines[j]
                = xmalloc(CONF_NUMBER_COLS*SIZEOF(*(term.lines[j])));
        }
        term.ncols = CONF_NUMBER_COLS;
        term.nrows = CONF_NUMBER_ROWS;
        term_swap_screen();
    }
    term.dirts = xmalloc(CONF_NUMBER_ROWS*SIZEOF(*term.dirts));
    term.tabs = xmalloc(CONF_NUMBER_COLS*SIZEOF(*term.tabs));
    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        term.hist[i] = xmalloc(CONF_NUMBER_COLS*SIZEOF(StGlyph));
    }
    term_reset();

    {
        Window parent = 0;
        Window root;
        pid_t pid_this = getpid();
        XWindowAttributes attr;
        XVisualInfo visual;

        if (!(x_window.display = XOpenDisplay(NULL))) {
            error("can't open display\n");
            exit(EXIT_FAILURE);
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
            error("could not init fontconfig.\n");
            exit(EXIT_FAILURE);
        }

        if (opt_font) {
            used_font = opt_font;
        } else {
            used_font = CONF_FONT;
        }
        x_load_fonts(used_font, 0);

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
        x_window.attrs.event_mask = FocusChangeMask
                                    | KeyPressMask | KeyReleaseMask
                                    | ButtonPressMask | ButtonReleaseMask
                                    | ButtonMotionMask
                                    | ExposureMask
                                    | VisibilityChangeMask
                                    | StructureNotifyMask;
        x_window.attrs.colormap = x_window.color_map;

        {
            ulong cw_flags = CWBackPixel
                             | CWBorderPixel
                             | CWBitGravity
                             | CWEventMask
                             | CWColormap;
        x_window.win = XCreateWindow(x_window.display, parent,
                                     x_window.left_offset, x_window.top_offset,
                                     (uint32)term_window.w, (uint32)term_window.h,
                                     0, x_window.depth,
                                     InputOutput, x_window.visual,
                                     cw_flags,
                                     &x_window.attrs);
        }

        if (parent != root) {
            XReparentWindow(x_window.display, x_window.win, parent,
                            x_window.left_offset, x_window.top_offset);
        }

        {
            XGCValues xgc_values;
            memset64(&xgc_values, 0, SIZEOF(xgc_values));
            xgc_values.graphics_exposures = False;
            draw_context.graphics = XCreateGC(x_window.display, x_window.win,
                                              GCGraphicsExposures, &xgc_values);
        }
        x_window.drawable = XCreatePixmap(x_window.display, x_window.win,
                                          (uint32)term_window.w,
                                          (uint32)term_window.h,
                                          (uint32)x_window.depth);
        XSetForeground(x_window.display, draw_context.graphics,
                       draw_context.colors[CONF_COLOR_BG].pixel);
        XFillRectangle(x_window.display, x_window.drawable,
                       draw_context.graphics,
                       0, 0, (uint32)term_window.w, (uint32)term_window.h);

        x_window.font_spec_buf = xmalloc(CONF_NUMBER_COLS*SIZEOF(XftGlyphFontSpec));

        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable,
                                          x_window.visual, x_window.color_map);

        /* input methods */
        if (!x_im_open(x_window.display)) {
            XRegisterIMInstantiateCallback(x_window.display, NULL, NULL, NULL,
                                           x_im_instantiate, NULL);
        }

        {
            /* white cursor, black outline */
            XColor xmouse_fg;
            XColor xmouse_bg;
            Cursor cursor = XCreateFontCursor(x_window.display,
                                              (uint32)CONF_MOUSE_SHAPE);
            XDefineCursor(x_window.display, x_window.win, cursor);

            if (XParseColor(x_window.display, x_window.color_map,
                            CONF_COLORS[CONF_MOUSE_COLOR_FG], &xmouse_fg)
                == 0) {
                xmouse_fg.red = 0xffff;
                xmouse_fg.green = 0xffff;
                xmouse_fg.blue = 0xffff;
            }

            if (XParseColor(x_window.display, x_window.color_map,
                            CONF_COLORS[CONF_MOUSE_COLOR_BG], &xmouse_bg) == 0) {
                xmouse_bg.red = 0x0000;
                xmouse_bg.green = 0x0000;
                xmouse_bg.blue = 0x0000;
            }

            XRecolorCursor(x_window.display, cursor, &xmouse_fg, &xmouse_bg);
        }

        x_window.xembed = XInternAtom(x_window.display, "_XEMBED", False);
        x_window.wm_delete_win = XInternAtom(x_window.display,
                                             "WM_DELETE_WINDOW", False);
        x_window.net_wm_name = XInternAtom(x_window.display,
                                           "_NET_WM_NAME", False);
        x_window.net_wm_iconname = XInternAtom(x_window.display,
                                               "_NET_WM_ICON_NAME", False);
        XSetWMProtocols(x_window.display,
                        x_window.win, &x_window.wm_delete_win, 1);

        x_window.net_wm_pid = XInternAtom(x_window.display,
                                          "_NET_WM_PID", False);

        XChangeProperty(x_window.display, x_window.win, x_window.net_wm_pid,
                        XA_CARDINAL, 32, PropModeReplace,
                        (uchar *)&pid_this, 1);

        term_window.mode = WIN_MODE_NUMLOCK;
        reset_title();
        x_hints();
        XMapWindow(x_window.display, x_window.win);
        XSync(x_window.display, False);

        clock_gettime(CLOCK_MONOTONIC, &xsel.tclick1);
        clock_gettime(CLOCK_MONOTONIC, &xsel.tclick2);

        xsel.primary = NULL;
        xsel.clipboard = NULL;
        if ((xsel.xtarget
                 = XInternAtom(x_window.display, "UTF8_STRING", 0)) == None) {
            xsel.xtarget = XA_STRING;
        }

        boxdraw_xinit(x_window.display,
				      x_window.color_map, x_window.xft_draw, x_window.visual);
    }

    {
        char buffer[SIZEOF(int64)*8 + 1];

        SNPRINTF(buffer, "%lu", x_window.win);
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
        int32 tty_fd;
        int32 xev;
        int32 drawing;
        struct timespec seltv;
        struct timespec *tv;
        struct timespec now;
        struct timespec lastblink;
        struct timespec trigger;
        float timeout;

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

        tty_fd = tty_new(opt_line, CONF_SHELl, opt_iofile, opt_cmd);
        cresize(w, h);

        timeout = -1;
        drawing = 0;
        lastblink = (struct timespec){0};

        while (1) {
            FD_ZERO(&read_fd);
            FD_SET(tty_fd, &read_fd);
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

            if (pselect((int32)MAX(xfd, tty_fd) + 1, &read_fd, NULL, NULL, tv,
                        NULL)
                < 0) {
                if (errno == EINTR) {
                    continue;
                }
                error("select failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            clock_gettime(CLOCK_MONOTONIC, &now);

            if (FD_ISSET(tty_fd, &read_fd)) {
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
            if (FD_ISSET(tty_fd, &read_fd) || xev) {
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

            if (DEBUGGING) {
                check_consistent_state();
            }

            XFlush(x_window.display);
            drawing = 0;
        }
    }
}
