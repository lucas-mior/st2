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
#include "win.h"
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
#define TRUERED(x) (uint16)(((x) & 0xff0000) >> 8)
#define TRUEGREEN(x) (uint16)(((x) & 0xff00))
#define TRUEBLUE(x) (uint16)(((x) & 0xff) << 8)

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
    Display *dpy;
    Colormap cmap;
    Window win;
    Drawable buf;
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

typedef struct {
    Atom xtarget;
    char *primary, *clipboard;
    struct timespec tclick1;
    struct timespec tclick2;
} XSelection;

/* Font structure */
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

/* Drawing Context */
typedef struct {
    Color *col;
    int32 collen;
    Font font, bfont, ifont, ibfont;
    GC graphics;
} DrawingContext;

static inline uint16 sixd_to_16bit(int32);
static int32 x_make_glyph_font_specs(XftGlyphFontSpec *, const Glyph *, int32, int32, int32);
static void x_draw_glyph_font_specs(const XftGlyphFontSpec *, Glyph, int32, int32, int32);
static void x_draw_glyph(Glyph, int32, int32);
static void x_clear(int32, int32, int32, int32);
static int32 x_geom_mask_to_gravity(int32);
static int32 x_im_open(Display *);
static void x_im_instantiate(Display *, XPointer, XPointer);
static void x_im_destroy(XIM, XPointer, XPointer);
static int32 x_ic_destroy(XIC, XPointer, XPointer);
static void x_init(int32, int32);
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
static void x_setenv(void);
static void x_set_urgency(int32);
static int32 evcol(XEvent *);
static int32 evrow(XEvent *);

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
static void setsel(char *, Time);
static void mousesel(XEvent *, int32);
static void mousereport(XEvent *);
static char *kmap(KeySym, uint32);
static int32 match(uint32, uint32);

static void run(void) __attribute__((noreturn));
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
    /*
     * Uncomment if you want the selection to disappear when you select something
     * different in another window.
     */
    [SelectionClear] = handler_selection_clear,
    [SelectionNotify] = handler_selection_notify,
    /*
     * PropertyNotify is only turned on when there is some INCR transfer happening
     * for the selection retrieval.
     */
    [PropertyNotify] = handler_prop_notify,
    [SelectionRequest] = handler_selection_request,
};

/* Globals */
static DrawingContext draw_context;
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
static char *opt_io = NULL;
static char *opt_line = NULL;
static char *opt_name = NULL;
static char *opt_title = NULL;

static uint32 buttons; /* bit field of pressed buttons */

void
user_clipboard_copy(const Arg *arg) {
    Atom clipboard;
    (void)arg;

    free(xsel.clipboard);
    xsel.clipboard = NULL;

    if (xsel.primary != NULL) {
        xsel.clipboard = xstrdup(xsel.primary);
        clipboard = XInternAtom(x_window.dpy, "CLIPBOARD", 0);
        XSetSelectionOwner(x_window.dpy, clipboard, x_window.win, CurrentTime);
    }
}

void
user_clipboard_paste(const Arg *arg) {
    Atom clipboard;
    (void)arg;

    clipboard = XInternAtom(x_window.dpy, "CLIPBOARD", 0);
    XConvertSelection(x_window.dpy, clipboard, xsel.xtarget, clipboard, x_window.win, CurrentTime);
}

void
user_selection_paste(const Arg *arg) {
    (void)arg;
    XConvertSelection(x_window.dpy, XA_PRIMARY, xsel.xtarget, XA_PRIMARY, x_window.win,
                      CurrentTime);
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
}

void
user_toggle_numlock(const Arg *arg) {
    (void)arg;
    term_window.mode ^= WIN_MODE_NUMLOCK;
}

void
user_zoom(const Arg *arg) {
    Arg larg;

    larg.f = usedfontsize + arg->f;
    zoom_abs(&larg);
}

void
zoom_abs(const Arg *arg) {
    x_unload_fonts();
    x_load_fonts(usedfont, arg->f);
    x_load_spare_fonts();
    cresize(0, 0);
    redraw();
    x_hints();
}

void
user_zoom_reset(const Arg *arg) {
    Arg larg;
    (void)arg;

    if (defaultfontsize > 0) {
        larg.f = defaultfontsize;
        zoom_abs(&larg);
    }
}

void
tty_send(const Arg *arg) {
    tty_write(arg->s, (int64)strlen(arg->s), 1);
}

int32
evcol(XEvent *e) {
    int32 x = e->xbutton.x - term_window.hborderpx;
    LIMIT(x, 0, term_window.tty_width - 1);
    return x / term_window.cw;
}

int32
evrow(XEvent *e) {
    int32 y = e->xbutton.y - term_window.vborderpx;
    LIMIT(y, 0, term_window.tty_height - 1);
    return y / term_window.ch;
}

void
mousesel(XEvent *e, int32 done) {
    int32 type, seltype = SELECTION_REGULAR;
    uint32 state = e->xbutton.state & ~(Button1Mask | CONF_FORCE_MOUSE_MOD);

    for (type = 1; type < LENGTH(CONF_SELECTION_MASKS); ++type) {
        if (match(CONF_SELECTION_MASKS[type], state)) {
            seltype = type;
            break;
        }
    }
    selection_extend(evcol(e), evrow(e), seltype, done);
    if (done) {
        setsel(get_sel(), e->xbutton.time);
    }
}

void
mousereport(XEvent *e) {
    int32 len, btn, code;
    int32 x = evcol(e), y = evrow(e);
    int32 state = (int32)e->xbutton.state;
    char buf[40];
    static int32 ox, oy;

    if (e->type == MotionNotify) {
        if (x == ox && y == oy) {
            return;
        }
        if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMOTION) && !TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMANY)) {
            return;
        }
        /* WIN_MODE_MOUSEMOTION: no reporting if no button is pressed */
        if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMOTION) && buttons == 0) {
            return;
        }
        /* Set btn to lowest-numbered pressed button, or 12 if no
         * buttons are pressed. */
        for (btn = 1; btn <= 11 && !(buttons & (1 << (btn - 1))); btn++)
            ;
        code = 32;
    } else {
        btn = (int32)e->xbutton.button;
        /* Only buttons 1 through 11 can be encoded */
        if (btn < 1 || btn > 11) {
            return;
        }
        if (e->type == ButtonRelease) {
            /* WIN_MODE_MOUSEX10: no button release reporting */
            if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSEX10)) {
                return;
            }
            /* Don't send release events for the scroll wheel */
            if (btn == 4 || btn == 5) {
                return;
            }
        }
        code = 0;
    }

    ox = x;
    oy = y;

    /* Encode btn into code. If no button is pressed for a motion event in
     * WIN_MODE_MOUSEMANY, then encode it as a release. */
    if ((!TERM_WINDOW_IS_SET(WIN_MODE_MOUSESGR) && e->type == ButtonRelease) || btn == 12) {
        code += 3;
    } else if (btn >= 8) {
        code += 128 + btn - 8;
    } else if (btn >= 4) {
        code += 64 + btn - 4;
    } else {
        code += btn - 1;
    }

    if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSEX10)) {
        code += ((state & ShiftMask) ? 4 : 0) + ((state & Mod1Mask) ? 8 : 0) /* meta key: alt */
                + ((state & ControlMask) ? 16 : 0);
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSESGR)) {
        len = snprintf(buf, SIZEOF(buf), "\033[<%d;%d;%d%c", code, x + 1, y + 1,
                       e->type == ButtonRelease ? 'm' : 'M');
    } else if (x < 223 && y < 223) {
        len = snprintf(buf, SIZEOF(buf), "\033[M%c%c%c", 32 + code, 32 + x + 1, 32 + y + 1);
    } else {
        return;
    }

    tty_write(buf, (int64)len, 0);
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
mouse_action(XEvent *e, uint32 release) {
    MouseShortcut *mouse_shortcut;

    /* ignore Button<N>mask for Button<N> - it's set on release */
    uint32 state = e->xbutton.state & ~button_mask(e->xbutton.button);

    for (mouse_shortcut = mshortcuts; mouse_shortcut < mshortcuts + LENGTH(mshortcuts);
         mouse_shortcut++) {
        if (mouse_shortcut->release == release && mouse_shortcut->button == e->xbutton.button &&
            (match(mouse_shortcut->mod, state) || /* exact or forced */
             match(mouse_shortcut->mod, state & ~CONF_FORCE_MOUSE_MOD))) {
            mouse_shortcut->func(&(mouse_shortcut->arg));
            return 1;
        }
    }

    return 0;
}

void
handler_button_press(XEvent *e) {
    int32 btn = (int32)e->xbutton.button;
    struct timespec now;
    int32 snap;

    if (1 <= btn && btn <= 11) {
        buttons |= 1 << (btn - 1);
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSE) && !(e->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
        mousereport(e);
        return;
    }

    if (mouse_action(e, 0)) {
        return;
    }

    if (btn == Button1) {
        /*
         * If the user clicks below predefined timeouts specific
         * snapping behaviour is exposed.
         */
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (TIMEDIFF(now, xsel.tclick2) <= (float)TRIPLE_CLICK_TIMEOUT) {
            snap = SELECTION_SNAP_LINE;
        } else if (TIMEDIFF(now, xsel.tclick1) <= (float)DOUBLE_CLICK_TIMEOUT) {
            snap = SELECTION_SNAP_WORD;
        } else {
            snap = 0;
        }
        xsel.tclick2 = xsel.tclick1;
        xsel.tclick1 = now;

        selection_start(evcol(e), evrow(e), snap);
    }
}

void
handler_prop_notify(XEvent *e) {
    XPropertyEvent *x_property_event;
    Atom clipboard = XInternAtom(x_window.dpy, "CLIPBOARD", 0);

    x_property_event = &e->xproperty;
    if (x_property_event->state == PropertyNewValue &&
        (x_property_event->atom == XA_PRIMARY || x_property_event->atom == clipboard)) {
        handler_selection_notify(e);
    }
}

void
handler_selection_notify(XEvent *e) {
    uint64 nitems, ofs, rem;
    int32 format;
    uchar *data, *last, *repl;
    Atom type, incratom, property = None;

    incratom = XInternAtom(x_window.dpy, "INCR", 0);

    ofs = 0;
    if (e->type == SelectionNotify) {
        property = e->xselection.property;
    } else if (e->type == PropertyNotify) {
        property = e->xproperty.atom;
    }

    if (property == None) {
        return;
    }

    do {
        if (XGetWindowProperty(x_window.dpy, x_window.win, property, (int64)ofs, BUFSIZ / 4, False,
                               AnyPropertyType, &type, &format, &nitems, &rem, &data)) {
            fprintf(stderr, "Clipboard allocation failed\n");
            return;
        }

        if (e->type == PropertyNotify && nitems == 0 && rem == 0) {
            /*
             * If there is some PropertyNotify with no data, then
             * this is the signal of the selection owner that all
             * data has been transferred. We won't need to receive
             * PropertyNotify events anymore.
             */
            MODBIT(x_window.attrs.event_mask, 0, PropertyChangeMask);
            XChangeWindowAttributes(x_window.dpy, x_window.win, CWEventMask, &x_window.attrs);
        }

        if (type == incratom) {
            /*
             * Activate the PropertyNotify events so we receive
             * when the selection owner does send us the next
             * chunk of data.
             */
            MODBIT(x_window.attrs.event_mask, 1, PropertyChangeMask);
            XChangeWindowAttributes(x_window.dpy, x_window.win, CWEventMask, &x_window.attrs);

            /*
             * Deleting the property is the transfer start signal.
             */
            XDeleteProperty(x_window.dpy, x_window.win, (ulong)property);
            continue;
        }

        /*
         * As seen in get_sel:
         * Line endings are inconsistent in the terminal and GUI world
         * copy and pasting. When receiving some selection data,
         * replace all '\n' with '\r'.
         * FIXME: Fix the computer world.
         */
        repl = data;
        last = data + nitems * (uint64)format / 8;
        while ((repl = memchr(repl, '\n', (size_t)(last - repl)))) {
            *repl++ = '\r';
        }

        if (TERM_WINDOW_IS_SET(WIN_MODE_BRCKTPASTE) && ofs == 0) {
            tty_write("\033[200~", 6, 0);
        }
        tty_write((char *)data, nitems * (uint64)format / 8, 1);
        if (TERM_WINDOW_IS_SET(WIN_MODE_BRCKTPASTE) && rem == 0) {
            tty_write("\033[201~", 6, 0);
        }
        XFree(data);
        /* number of 32-bit chunks returned */
        ofs += nitems * (uint64)format / 32;
    } while (rem > 0);

    /*
     * Deleting the property again tells the selection owner to send the
     * next data chunk in the property.
     */
    XDeleteProperty(x_window.dpy, x_window.win, (ulong)property);
}

void
x_clipboard_copy(void) {
    user_clipboard_copy(NULL);
}

void
handler_selection_clear(XEvent *e) {
    (void)e;
    selection_clear();
}

void
handler_selection_request(XEvent *e) {
    XSelectionRequestEvent *xsre;
    XSelectionEvent xev;
    Atom xa_targets, string, clipboard;
    char *seltext;

    xsre = (XSelectionRequestEvent *)e;
    xev.type = SelectionNotify;
    xev.requestor = xsre->requestor;
    xev.selection = xsre->selection;
    xev.target = xsre->target;
    xev.time = xsre->time;
    if (xsre->property == None) {
        xsre->property = xsre->target;
    }

    /* reject */
    xev.property = None;

    xa_targets = XInternAtom(x_window.dpy, "TARGETS", 0);
    if (xsre->target == xa_targets) {
        /* respond with the supported type */
        string = xsel.xtarget;
        XChangeProperty(xsre->display, xsre->requestor, xsre->property, XA_ATOM, 32,
                        PropModeReplace, (uchar *)&string, 1);
        xev.property = xsre->property;
    } else if (xsre->target == xsel.xtarget || xsre->target == XA_STRING) {
        /*
         * xith XA_STRING non ascii characters may be incorrect in the
         * requestor. It is not our problem, use utf8.
         */
        clipboard = XInternAtom(x_window.dpy, "CLIPBOARD", 0);
        if (xsre->selection == XA_PRIMARY) {
            seltext = xsel.primary;
        } else if (xsre->selection == clipboard) {
            seltext = xsel.clipboard;
        } else {
            fprintf(stderr, "Unhandled clipboard selection 0x%lx\n", xsre->selection);
            return;
        }
        if (seltext != NULL) {
            XChangeProperty(xsre->display, xsre->requestor, xsre->property, xsre->target, 8,
                            PropModeReplace, (uchar *)seltext, (int32)(int64)strlen(seltext));
            xev.property = xsre->property;
        }
    }

    /* all done, send a notification to the listener */
    if (!XSendEvent(xsre->display, xsre->requestor, 1, 0, (XEvent *)&xev)) {
        fprintf(stderr, "Error sending SelectionNotify event\n");
    }
}

void
setsel(char *str, Time t) {
    if (!str) {
        return;
    }

    free(xsel.primary);
    xsel.primary = str;

    XSetSelectionOwner(x_window.dpy, XA_PRIMARY, x_window.win, t);
    if (XGetSelectionOwner(x_window.dpy, XA_PRIMARY) != x_window.win) {
        selection_clear();
    }
}

void
x_set_sel(char *str) {
    setsel(str, CurrentTime);
}

void
handler_button_release(XEvent *e) {
    int32 btn = (int32)e->xbutton.button;

    if (1 <= btn && btn <= 11) {
        buttons &= ~(1 << (btn - 1));
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSE) && !(e->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
        mousereport(e);
        return;
    }

    if (mouse_action(e, 1)) {
        return;
    }
    if (btn == Button1) {
        mousesel(e, 1);
    }
}

void
handler_button_motion(XEvent *e) {
    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSE) && !(e->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
        mousereport(e);
        return;
    }

    mousesel(e, 0);
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

    col = (term_window.w - 2 * CONF_BORDER_PIXELS) / term_window.cw;
    row = (term_window.h - 2 * CONF_BORDER_PIXELS) / term_window.ch;
    col = MAX(1, col);
    row = MAX(1, row);

    term_window.hborderpx = (term_window.w - col * term_window.cw) / 2;
    term_window.vborderpx = (term_window.h - row * term_window.ch) / 2;

    term_resize(col, row);
    x_resize(col, row);
    tty_resize(term_window.tty_width, term_window.tty_height);
}

void
x_resize(int32 col, int32 row) {
    term_window.tty_width = col * term_window.cw;
    term_window.tty_height = row * term_window.ch;

    XFreePixmap(x_window.dpy, x_window.buf);
    x_window.buf = XCreatePixmap(x_window.dpy, x_window.win, (uint32)term_window.w,
                                 (uint32)term_window.h, (uint32)x_window.depth);
    XftDrawChange(x_window.draw, x_window.buf);
    x_clear(0, 0, term_window.w, term_window.h);

    /* handler_configure_notify to new width */
    x_window.specbuf = xrealloc(x_window.specbuf, (int64)col * SIZEOF(GlyphFontSpec));
}

uint16
sixd_to_16bit(int32 x) {
    return (uint16)(x == 0 ? 0 : 0x3737 + 0x2828 * x);
}

int32
x_load_color(int32 i, const char *name, Color *ncolor) {
    XRenderColor color = {.alpha = 0xffff};

    if (!name) {
        if (BETWEEN(i, 16 + trans_colors, 255)) { /* 256 color */
            if (i < 6 * 6 * 6 + 16) {             /* same colors as xterm */
                color.red = sixd_to_16bit(((i - 16) / 36) % 6);
                color.green = sixd_to_16bit(((i - 16) / 6) % 6);
                color.blue = sixd_to_16bit(((i - 16) / 1) % 6);
            } else { /* greyscale */
                color.red = (uint16)(0x0808 + 0x0a0a * (i - (6 * 6 * 6 + 16)));
                color.green = color.blue = color.red;
            }
            return XftColorAllocValue(x_window.dpy, x_window.vis, x_window.cmap, &color, ncolor);
        } else {
            name = colorname[i];
        }
    }

    return XftColorAllocName(x_window.dpy, x_window.vis, x_window.cmap, name, ncolor);
}

void
x_load_cols(void) {
    static int32 loaded;
    Color *cp;

    if (loaded) {
        for (cp = draw_context.col; cp < &draw_context.col[draw_context.collen]; ++cp) {
            XftColorFree(x_window.dpy, x_window.vis, x_window.cmap, cp);
        }
    } else {
        draw_context.collen = MAX(LENGTH(colorname), 256);
        draw_context.col = xmalloc((uint16)draw_context.collen * SIZEOF(Color));
    }

    for (int32 i = 0; i < draw_context.collen; i++) {
        if (!x_load_color(i, NULL, &draw_context.col[i])) {
            if (colorname[i]) {
                die("could not allocate color '%s'\n", colorname[i]);
            } else {
                die("could not allocate color %d\n", i);
            }
        }
    }

    draw_context.col[CONF_COLOR_INDEX_BACK].color.alpha = (uint16)(0xffff * CONF_ALPHA);
    draw_context.col[CONF_COLOR_INDEX_BACK].pixel &= 0x00FFFFFF;
    draw_context.col[CONF_COLOR_INDEX_BACK].pixel |= ((uint32)(0xFF * CONF_ALPHA) & 0xFF) << 24;

    for (int32 i = 16; i < 16 + trans_colors; i++) {
        draw_context.col[i].color.alpha = (uint16)(0xffff * CONF_ALPHA);
        draw_context.col[i].pixel &= 0x00FFFFFF;
        draw_context.col[i].pixel |= ((uint32)(0xff * CONF_ALPHA) & 0xff) << 24;
    }
    loaded = 1;
}

int32
x_get_color(int32 x, uchar *r, uchar *g, uchar *b) {
    if (!BETWEEN(x, 0, draw_context.collen - 1)) {
        return 1;
    }

    *r = draw_context.col[x].color.red >> 8;
    *g = draw_context.col[x].color.green >> 8;
    *b = draw_context.col[x].color.blue >> 8;

    return 0;
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

    XftColorFree(x_window.dpy, x_window.vis, x_window.cmap, &draw_context.col[x]);
    draw_context.col[x] = ncolor;

    if (x == CONF_COLOR_INDEX_BACK) {
        draw_context.col[CONF_COLOR_INDEX_BACK].color.alpha = (uint16)(0xffff * CONF_ALPHA);
        draw_context.col[CONF_COLOR_INDEX_BACK].pixel &= 0x00FFFFFF;
        draw_context.col[CONF_COLOR_INDEX_BACK].pixel |= ((uint32)(0xff * CONF_ALPHA) & 0xff) << 24;
    }

    return 0;
}

/*
 * Absolute coordinates.
 */
void
x_clear(int32 x1, int32 y1, int32 x2, int32 y2) {
    XftDrawRect(x_window.draw,
                &draw_context.col[TERM_WINDOW_IS_SET(WIN_MODE_REVERSE) ? CONF_COLOR_INDEX_FONT
                                                                       : CONF_COLOR_INDEX_BACK],
                x1, y1, (uint32)(x2 - x1), (uint32)(y2 - y1));
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
    sizeh->base_height = 2 * CONF_BORDER_PIXELS;
    sizeh->base_width = 2 * CONF_BORDER_PIXELS;
    sizeh->min_height = term_window.ch + 2 * CONF_BORDER_PIXELS;
    sizeh->min_width = term_window.cw + 2 * CONF_BORDER_PIXELS;
    if (x_window.isfixed) {
        sizeh->flags |= PMaxSize;
        sizeh->min_width = sizeh->max_width = term_window.w;
        sizeh->min_height = sizeh->max_height = term_window.h;
    }
    if (x_window.gm & (XValue | YValue)) {
        sizeh->flags |= USPosition | PWinGravity;
        sizeh->x = x_window.l;
        sizeh->y = x_window.t;
        sizeh->win_gravity = x_geom_mask_to_gravity(x_window.gm);
    }

    XSetWMProperties(x_window.dpy, x_window.win, NULL, NULL, NULL, 0, sizeh, &wm, &class);
    XFree(sizeh);
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
    XftDefaultSubstitute(x_window.dpy, x_window.scr, configured);

    match = FcFontMatch(NULL, configured, &result);
    if (!match) {
        FcPatternDestroy(configured);
        return 1;
    }

    if (!(f->match = XftFontOpenPattern(x_window.dpy, match))) {
        FcPatternDestroy(configured);
        FcPatternDestroy(match);
        return 1;
    }

    if ((XftPatternGetInteger(pattern, "slant", 0, &wantattr) == XftResultMatch)) {
        /*
         * Check if xft was unable to find a font with the appropriate
         * slant but gave us one anyway. Try to mitigate.
         */
        if ((XftPatternGetInteger(f->match->pattern, "slant", 0, &haveattr) != XftResultMatch) ||
            haveattr < wantattr) {
            f->badslant = 1;
            fputs("font slant does not match\n", stderr);
        }
    }

    if ((XftPatternGetInteger(pattern, "weight", 0, &wantattr) == XftResultMatch)) {
        if ((XftPatternGetInteger(f->match->pattern, "weight", 0, &haveattr) != XftResultMatch) ||
            haveattr != wantattr) {
            f->badweight = 1;
            fputs("font weight does not match\n", stderr);
        }
    }

    XftTextExtentsUtf8(x_window.dpy, f->match, (const FcChar8 *)CONF_ASCII_PRINTABLE,
                       (int32)(int64)strlen(CONF_ASCII_PRINTABLE), &extents);

    f->set = NULL;
    f->pattern = configured;

    f->ascent = f->match->ascent;
    f->descent = f->match->descent;
    f->lbearing = 0;
    f->rbearing = (int16)f->match->max_advance_width;

    f->height = f->ascent + f->descent;
    f->width = DIVCEIL(extents.xOff, (int32)(int64)strlen(CONF_ASCII_PRINTABLE));

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
        if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) == FcResultMatch) {
            usedfontsize = (float)fontval;
        } else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) == FcResultMatch) {
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
        FcPatternGetDouble(draw_context.font.match->pattern, FC_PIXEL_SIZE, 0, &fontval);
        usedfontsize = (float)fontval;
        if (fabsf(fontsize) <= 0) {
            defaultfontsize = (float)fontval;
        }
    }

    /* Setting character width and height. */
    term_window.cw = ceilf((float)(draw_context.font.width) * CONF_CHAR_WIDTH_SCALE);
    term_window.ch = ceilf((float)(draw_context.font.height) * CONF_CHAR_HEIGHT_SCALE);

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
}

int32
xloadsparefont(FcPattern *pattern, int32 flags) {
    FcPattern *match;
    FcResult result;

    match = FcFontMatch(NULL, pattern, &result);
    if (!match) {
        return 1;
    }

    if (!(frc[frclen].font = XftFontOpenPattern(x_window.dpy, match))) {
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
    if (frccap < 4 * fc) {
        frccap += 4 * fc - frccap;
        frc = xrealloc(frc, (int64)frccap * SIZEOF(Fontcache));
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
            if ((fabs(sizeshift) < 0.001) != 0 &&
                FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) == FcResultMatch) {
                fontval += sizeshift;
                FcPatternDel(pattern, FC_PIXEL_SIZE);
                FcPatternDel(pattern, FC_SIZE);
                FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontval);
            }
        }

        FcPatternAddBool(pattern, FC_SCALABLE, 1);

        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        XftDefaultSubstitute(x_window.dpy, x_window.scr, pattern);

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
}

void
x_unload_font(Font *f) {
    XftFontClose(x_window.dpy, f->match);
    FcPatternDestroy(f->pattern);
    if (f->set) {
        FcFontSetDestroy(f->set);
    }
}

void
x_unload_fonts(void) {
    /* Free the loaded fonts in the font cache.  */
    while (frclen > 0) {
        XftFontClose(x_window.dpy, frc[--frclen].font);
    }

    x_unload_font(&draw_context.font);
    x_unload_font(&draw_context.bfont);
    x_unload_font(&draw_context.ifont);
    x_unload_font(&draw_context.ibfont);
}

int32
x_im_open(Display *dpy) {
    XIMCallback imdestroy = {.client_data = NULL, .callback = x_im_destroy};
    XICCallback icdestroy = {.client_data = NULL, .callback = x_ic_destroy};
    (void)dpy;

    x_window.ime.xim = XOpenIM(x_window.dpy, NULL, NULL, NULL);
    if (x_window.ime.xim == NULL) {
        return 0;
    }

    if (XSetIMValues(x_window.ime.xim, XNDestroyCallback, &imdestroy, NULL)) {
        fprintf(stderr, "XSetIMValues: "
                        "Could not set XNDestroyCallback.\n");
    }

    x_window.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &x_window.ime.spot, NULL);

    if (x_window.ime.xic == NULL) {
        x_window.ime.xic =
            XCreateIC(x_window.ime.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                      XNClientWindow, x_window.win, XNDestroyCallback, &icdestroy, NULL);
    }
    if (x_window.ime.xic == NULL) {
        fprintf(stderr, "XCreateIC: Could not create input context.\n");
    }

    return 1;
}

void
x_im_instantiate(Display *dpy, XPointer client, XPointer call) {
    (void)client;
    (void)call;
    if (x_im_open(dpy)) {
        XUnregisterIMInstantiateCallback(x_window.dpy, NULL, NULL, NULL, x_im_instantiate, NULL);
    }
}

void
x_im_destroy(XIM xim, XPointer client, XPointer call) {
    (void)xim;
    (void)client;
    (void)call;
    x_window.ime.xim = NULL;
    XRegisterIMInstantiateCallback(x_window.dpy, NULL, NULL, NULL, x_im_instantiate, NULL);
    XFree(x_window.ime.spotlist);
}

int32
x_ic_destroy(XIC xim, XPointer client, XPointer call) {
    (void)xim;
    (void)client;
    (void)call;
    x_window.ime.xic = NULL;
    return 1;
}

void
x_init(int32 ncols, int32 nrows) {
    XGCValues gcvalues;
    Cursor cursor;
    Window parent = 0;
    Window root;
    pid_t thispid = getpid();
    XColor xmousefg, xmousebg;
    XWindowAttributes attr;
    XVisualInfo vis;

    if (!(x_window.dpy = XOpenDisplay(NULL))) {
        die("can't open display\n");
    }
    x_window.scr = XDefaultScreen(x_window.dpy);

    root = XRootWindow(x_window.dpy, x_window.scr);
    if (!(opt_embed && (parent = (Window)strtol(opt_embed, NULL, 0)))) {
        parent = root;
    }

    if (XMatchVisualInfo(x_window.dpy, x_window.scr, 32, TrueColor, &vis) != 0) {
        x_window.vis = vis.visual;
        x_window.depth = vis.depth;
    } else {
        XGetWindowAttributes(x_window.dpy, parent, &attr);
        x_window.vis = attr.visual;
        x_window.depth = attr.depth;
    }

    if (!FcInit()) {
        die("could not init fontconfig.\n");
    }

    usedfont = (opt_font == NULL) ? CONF_FONT : opt_font;
    x_load_fonts(usedfont, 0);

    x_load_spare_fonts();

    x_window.cmap = XCreateColormap(x_window.dpy, parent, x_window.vis, None);
    x_load_cols();

    /* adjust fixed window geometry */
    term_window.w = 2 * term_window.hborderpx + 2 * CONF_BORDER_PIXELS + ncols * term_window.cw;
    term_window.h = 2 * term_window.vborderpx + 2 * CONF_BORDER_PIXELS + nrows * term_window.ch;
    if (x_window.gm & XNegative) {
        x_window.l += DisplayWidth(x_window.dpy, x_window.scr) - term_window.w - 2;
    }
    if (x_window.gm & YNegative) {
        x_window.t += DisplayHeight(x_window.dpy, x_window.scr) - term_window.h - 2;
    }

    /* Events */
    x_window.attrs.background_pixel = draw_context.col[CONF_COLOR_INDEX_BACK].pixel;
    x_window.attrs.border_pixel = draw_context.col[CONF_COLOR_INDEX_BACK].pixel;
    x_window.attrs.bit_gravity = NorthWestGravity;
    x_window.attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask | ExposureMask |
                                VisibilityChangeMask | StructureNotifyMask | ButtonMotionMask |
                                ButtonPressMask | ButtonReleaseMask;
    x_window.attrs.colormap = x_window.cmap;

    x_window.win = XCreateWindow(
        x_window.dpy, parent, x_window.l, x_window.t, (uint32)term_window.w, (uint32)term_window.h,
        0, x_window.depth, InputOutput, x_window.vis,
        CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap, &x_window.attrs);
    if (parent != root) {
        XReparentWindow(x_window.dpy, x_window.win, parent, x_window.l, x_window.t);
    }

    memset(&gcvalues, 0, SIZEOF(gcvalues));
    gcvalues.graphics_exposures = False;
    draw_context.graphics = XCreateGC(x_window.dpy, x_window.win, GCGraphicsExposures, &gcvalues);
    x_window.buf = XCreatePixmap(x_window.dpy, x_window.win, (uint32)term_window.w,
                                 (uint32)term_window.h, (uint32)x_window.depth);
    XSetForeground(x_window.dpy, draw_context.graphics,
                   draw_context.col[CONF_COLOR_INDEX_BACK].pixel);
    XFillRectangle(x_window.dpy, x_window.buf, draw_context.graphics, 0, 0, (uint32)term_window.w,
                   (uint32)term_window.h);

    /* font spec buffer */
    x_window.specbuf = xmalloc((int64)ncols * SIZEOF(GlyphFontSpec));

    /* Xft rendering context */
    x_window.draw = XftDrawCreate(x_window.dpy, x_window.buf, x_window.vis, x_window.cmap);

    /* input methods */
    if (!x_im_open(x_window.dpy)) {
        XRegisterIMInstantiateCallback(x_window.dpy, NULL, NULL, NULL, x_im_instantiate, NULL);
    }

    /* white cursor, black outline */
    cursor = XCreateFontCursor(x_window.dpy, (uint32)CONF_MOUSE_SHAPE);
    XDefineCursor(x_window.dpy, x_window.win, cursor);

    if (XParseColor(x_window.dpy, x_window.cmap, colorname[CONF_MOUSE_COLOR_FG], &xmousefg) == 0) {
        xmousefg.red = 0xffff;
        xmousefg.green = 0xffff;
        xmousefg.blue = 0xffff;
    }

    if (XParseColor(x_window.dpy, x_window.cmap, colorname[CONF_MOUSE_COLOR_BG], &xmousebg) == 0) {
        xmousebg.red = 0x0000;
        xmousebg.green = 0x0000;
        xmousebg.blue = 0x0000;
    }

    XRecolorCursor(x_window.dpy, cursor, &xmousefg, &xmousebg);

    x_window.xembed = XInternAtom(x_window.dpy, "_XEMBED", False);
    x_window.wmdeletewin = XInternAtom(x_window.dpy, "WM_DELETE_WINDOW", False);
    x_window.netwmname = XInternAtom(x_window.dpy, "_NET_WM_NAME", False);
    x_window.netwmiconname = XInternAtom(x_window.dpy, "_NET_WM_ICON_NAME", False);
    XSetWMProtocols(x_window.dpy, x_window.win, &x_window.wmdeletewin, 1);

    x_window.netwmpid = XInternAtom(x_window.dpy, "_NET_WM_PID", False);
    XChangeProperty(x_window.dpy, x_window.win, x_window.netwmpid, XA_CARDINAL, 32, PropModeReplace,
                    (uchar *)&thispid, 1);

    term_window.mode = WIN_MODE_NUMLOCK;
    reset_title();
    x_hints();
    XMapWindow(x_window.dpy, x_window.win);
    XSync(x_window.dpy, False);

    clock_gettime(CLOCK_MONOTONIC, &xsel.tclick1);
    clock_gettime(CLOCK_MONOTONIC, &xsel.tclick2);
    xsel.primary = NULL;
    xsel.clipboard = NULL;
    xsel.xtarget = XInternAtom(x_window.dpy, "UTF8_STRING", 0);
    if (xsel.xtarget == None) {
        xsel.xtarget = XA_STRING;
    }

    boxdraw_xinit(x_window.dpy, x_window.cmap, x_window.draw, x_window.vis);
}

int32
x_make_glyph_font_specs(XftGlyphFontSpec *specs, const Glyph *glyphs, int32 len, int32 x, int32 y) {
    int32 winx = term_window.hborderpx + x * term_window.cw;
    int32 winy = term_window.vborderpx + y * term_window.ch;
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
            runewidth = term_window.cw * ((mode & ATTR_WIDE) ? 2 : 1);
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
            glyphidx = XftCharIndex(x_window.dpy, font_local->match, rune);
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
            glyphidx = XftCharIndex(x_window.dpy, frc[f].font, rune);
            /* Everything correct. */
            if (glyphidx && frc[f].flags == frcflags) {
                break;
            }
            /* We got a default font for a not found glyph. */
            if (!glyphidx && frc[f].flags == frcflags && frc[f].unicodep == rune) {
                break;
            }
        }

        /* Nothing was found. Use fontconfig to find matching font. */
        if (f >= frclen) {
            if (!font_local->set) {
                font_local->set = FcFontSort(0, font_local->pattern, 1, 0, &fcres);
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
                frc = xrealloc(frc, (int64)frccap * SIZEOF(Fontcache));
            }

            frc[frclen].font = XftFontOpenPattern(x_window.dpy, fontpattern);
            if (!frc[frclen].font) {
                die("XftFontOpenPattern failed seeking fallback font: %s\n", strerror(errno));
            }
            frc[frclen].flags = frcflags;
            frc[frclen].unicodep = rune;

            glyphidx = XftCharIndex(x_window.dpy, frc[frclen].font, rune);

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
x_draw_glyph_font_specs(const XftGlyphFontSpec *specs, Glyph base, int32 len, int32 x, int32 y) {
    int32 charlen = len * ((base.mode & ATTR_WIDE) ? 2 : 1);
    int32 winx = term_window.hborderpx + x * term_window.cw;
    int32 winy = term_window.vborderpx + y * term_window.ch;
    int32 width = charlen * term_window.cw;
    Color *fg, *bg, *temp, revfg, revbg, truefg, truebg;
    XRenderColor colfg, colbg;
    XRectangle r;

    /* Fallback on color display for attributes not supported by the font */
    if (base.mode & ATTR_ITALIC && base.mode & ATTR_BOLD) {
        if (draw_context.ibfont.badslant || draw_context.ibfont.badweight) {
            base.fg = (int32)CONF_DEFAULT_ATTR;
        }
    } else if ((base.mode & ATTR_ITALIC && draw_context.ifont.badslant) ||
               (base.mode & ATTR_BOLD && draw_context.bfont.badweight)) {
        base.fg = (int32)CONF_DEFAULT_ATTR;
    }

    if (IS_TRUECOL(base.fg)) {
        colfg.alpha = 0xffff;
        colfg.red = TRUERED(base.fg);
        colfg.green = TRUEGREEN(base.fg);
        colfg.blue = TRUEBLUE(base.fg);
        XftColorAllocValue(x_window.dpy, x_window.vis, x_window.cmap, &colfg, &truefg);
        fg = &truefg;
    } else {
        fg = &draw_context.col[base.fg];
    }

    if (IS_TRUECOL(base.bg)) {
        colbg.alpha = 0xffff;
        colbg.green = TRUEGREEN(base.bg);
        colbg.red = TRUERED(base.bg);
        colbg.blue = TRUEBLUE(base.bg);
        XftColorAllocValue(x_window.dpy, x_window.vis, x_window.cmap, &colbg, &truebg);
        bg = &truebg;
    } else {
        bg = &draw_context.col[base.bg];
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        if (fg == &draw_context.col[CONF_COLOR_INDEX_FONT]) {
            fg = &draw_context.col[CONF_COLOR_INDEX_BACK];
        } else {
            colfg.red = ~fg->color.red;
            colfg.green = ~fg->color.green;
            colfg.blue = ~fg->color.blue;
            colfg.alpha = fg->color.alpha;
            XftColorAllocValue(x_window.dpy, x_window.vis, x_window.cmap, &colfg, &revfg);
            fg = &revfg;
        }

        if (bg == &draw_context.col[CONF_COLOR_INDEX_BACK]) {
            bg = &draw_context.col[CONF_COLOR_INDEX_FONT];
        } else {
            colbg.red = ~bg->color.red;
            colbg.green = ~bg->color.green;
            colbg.blue = ~bg->color.blue;
            colbg.alpha = bg->color.alpha;
            XftColorAllocValue(x_window.dpy, x_window.vis, x_window.cmap, &colbg, &revbg);
            bg = &revbg;
        }
    }

    if ((base.mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
        colfg.red = fg->color.red / 2;
        colfg.green = fg->color.green / 2;
        colfg.blue = fg->color.blue / 2;
        colfg.alpha = fg->color.alpha;
        XftColorAllocValue(x_window.dpy, x_window.vis, x_window.cmap, &colfg, &revfg);
        fg = &revfg;
    }

    if (base.mode & ATTR_REVERSE) {
        temp = fg;
        fg = bg;
        bg = temp;
    }

    if (base.mode & ATTR_SELECTED) {
        bg = &draw_context.col[CONF_COLOR_INDEX_SELECTION_BACK];
        if (!CONF_COLOR_IGNORE_SELECTION_FONT_COLOR) {
            fg = &draw_context.col[CONF_COLOR_INDEX_SELECTION_FONT];
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
                winy + term_window.ch +
                    ((winy + term_window.ch >= term_window.vborderpx + term_window.tty_height)
                         ? term_window.h
                         : 0));
    }
    if (winx + width >= term_window.hborderpx + term_window.tty_width) {
        x_clear(winx + width, (y == 0) ? 0 : winy, term_window.w,
                ((winy + term_window.ch >= term_window.vborderpx + term_window.tty_height)
                     ? term_window.h
                     : (winy + term_window.ch)));
    }
    if (y == 0) {
        x_clear(winx, 0, winx + width, term_window.vborderpx);
    }
    if (winy + term_window.ch >= term_window.vborderpx + term_window.tty_height) {
        x_clear(winx, winy + term_window.ch, winx + width, term_window.h);
    }

    /* Clean up the region we want to draw to. */
    XftDrawRect(x_window.draw, bg, winx, winy, (uint32)width, (uint32)term_window.ch);

    /* Set the clip region because Xft is sometimes dirty. */
    r.x = 0;
    r.y = 0;
    r.height = (uint16)term_window.ch;
    r.width = (uint16)width;
    XftDrawSetClipRectangles(x_window.draw, winx, winy, &r, 1);

    if (base.mode & ATTR_BOXDRAW) {
        drawboxes(winx, winy, width / len, term_window.ch, fg, bg, specs, len);
    } else {
        /* Render the glyphs. */
        XftDrawGlyphFontSpec(x_window.draw, fg, specs, len);
    }

    /* Render underline and strikethrough. */
    if (base.mode & ATTR_UNDERLINE) {
        XftDrawRect(x_window.draw, fg, winx,
                    winy + (int32)((float)draw_context.font.ascent * CONF_CHAR_HEIGHT_SCALE) + 1,
                    (uint32)width, 1);
    }

    if (base.mode & ATTR_STRUCK) {
        XftDrawRect(x_window.draw, fg, winx,
                    winy +
                        2 * (int32)((float)draw_context.font.ascent * CONF_CHAR_HEIGHT_SCALE / 3),
                    (uint32)width, 1);
    }

    /* Reset clip to none. */
    XftDrawSetClip(x_window.draw, 0);
}

void
x_draw_glyph(Glyph g, int32 x, int32 y) {
    int32 numspecs;
    XftGlyphFontSpec spec;

    numspecs = x_make_glyph_font_specs(&spec, &g, 1, x, y);
    x_draw_glyph_font_specs(&spec, g, numspecs, x, y);
}

void
x_draw_cursor(int32 cx, int32 cy, Glyph g, int32 ox, int32 oy, Glyph og) {
    Color drawcol;

    /* remove the old cursor */
    if (selected(ox, oy)) {
        og.mode |= ATTR_SELECTED;
    }
    x_draw_glyph(og, ox, oy);

    if (TERM_WINDOW_IS_SET(WIN_MODE_HIDE)) {
        return;
    }

    /*
     * Select the right color for the right mode.
     */
    g.mode &= ATTR_BOLD | ATTR_ITALIC | ATTR_UNDERLINE | ATTR_STRUCK | ATTR_WIDE | ATTR_BOXDRAW;

    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        g.mode |= ATTR_REVERSE;
        g.fg = CONF_COLOR_INDEX_CURSOR;
        g.bg = CONF_COLOR_INDEX_FONT;
        drawcol = draw_context.col[CONF_COLOR_INDEX_REVCURSOR];
    } else {
        g.fg = CONF_COLOR_INDEX_BACK;
        g.bg = CONF_COLOR_INDEX_CURSOR;
        drawcol = draw_context.col[CONF_COLOR_INDEX_CURSOR];
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
            XftDrawRect(x_window.draw, &drawcol, term_window.hborderpx + cx * term_window.cw,
                        term_window.vborderpx + (cy + 1) * term_window.ch -
                            (int32)CONF_CURSOR_THICKNESS,
                        (uint32)term_window.cw, (uint32)CONF_CURSOR_THICKNESS);
            break;
        case 5: /* Blinking bar */
        case 6: /* Steady bar */
            XftDrawRect(x_window.draw, &drawcol, term_window.hborderpx + cx * term_window.cw,
                        term_window.vborderpx + cy * term_window.ch, CONF_CURSOR_THICKNESS,
                        (uint32)term_window.ch);
            break;
        default:
            fprintf(stderr, "x_draw_cursor: Unhandled switch case.\n");
            break;
        }
    } else {
        XftDrawRect(x_window.draw, &drawcol, term_window.hborderpx + cx * term_window.cw,
                    term_window.vborderpx + cy * term_window.ch, (uint32)(term_window.cw - 1), 1);
        XftDrawRect(x_window.draw, &drawcol, term_window.hborderpx + cx * term_window.cw,
                    term_window.vborderpx + cy * term_window.ch, 1, (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.draw, &drawcol, term_window.hborderpx + (cx + 1) * term_window.cw - 1,
                    term_window.vborderpx + cy * term_window.ch, 1, (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.draw, &drawcol, term_window.hborderpx + cx * term_window.cw,
                    term_window.vborderpx + (cy + 1) * term_window.ch - 1, (uint32)term_window.cw,
                    1);
    }
}

void
x_setenv(void) {
    char buf[SIZEOF(int64) * 8 + 1];

    snprintf(buf, SIZEOF(buf), "%lu", x_window.win);
    setenv("WINDOWID", buf, 1);
}

void
x_set_icon_title(char *p) {
    XTextProperty prop;
    DEFAULT(p, opt_title);

    if (p[0] == '\0') {
        p = opt_title;
    }

    if (Xutf8TextListToTextProperty(x_window.dpy, &p, 1, XUTF8StringStyle, &prop) != Success) {
        return;
    }
    XSetWMIconName(x_window.dpy, x_window.win, &prop);
    XSetTextProperty(x_window.dpy, x_window.win, &prop, x_window.netwmiconname);
    XFree(prop.value);
}

void
x_set_title(char *p) {
    XTextProperty prop;
    DEFAULT(p, opt_title);

    if (p[0] == '\0') {
        p = opt_title;
    }

    if (Xutf8TextListToTextProperty(x_window.dpy, &p, 1, XUTF8StringStyle, &prop) != Success) {
        return;
    }
    XSetWMName(x_window.dpy, x_window.win, &prop);
    XSetTextProperty(x_window.dpy, x_window.win, &prop, x_window.netwmname);
    XFree(prop.value);
}

int32
x_start_draw(void) {
    return TERM_WINDOW_IS_SET(WIN_MODE_VISIBLE);
}

void
x_draw_line(Line line, int32 x1, int32 y1, int32 x2) {
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
        if (selected(x, y1)) {
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
}

void
x_finish_draw(void) {
    XCopyArea(x_window.dpy, x_window.buf, x_window.win, draw_context.graphics, 0, 0,
              (uint32)term_window.w, (uint32)term_window.h, 0, 0);
    XSetForeground(x_window.dpy, draw_context.graphics,
                   draw_context
                       .col[TERM_WINDOW_IS_SET(WIN_MODE_REVERSE) ? CONF_COLOR_INDEX_FONT
                                                                 : CONF_COLOR_INDEX_BACK]
                       .pixel);
}

void
x_xim_spot(int32 x, int32 y) {
    if (x_window.ime.xic == NULL) {
        return;
    }

    x_window.ime.spot.x = (int16)(CONF_BORDER_PIXELS + x * term_window.cw);
    x_window.ime.spot.y = (int16)(CONF_BORDER_PIXELS + (y + 1) * term_window.ch);

    XSetICValues(x_window.ime.xic, XNPreeditAttributes, x_window.ime.spotlist, NULL);
}

void
handler_expose(XEvent *ev) {
    (void)ev;
    redraw();
}

void
handler_visibility(XEvent *ev) {
    XVisibilityEvent *e = &ev->xvisibility;

    MODBIT(term_window.mode, e->state != VisibilityFullyObscured, WIN_MODE_VISIBLE);
}

void
handler_unmap(XEvent *ev) {
    (void)ev;
    term_window.mode &= ~WIN_MODE_VISIBLE;
}

void
x_set_pointer_motion(int32 set) {
    MODBIT(x_window.attrs.event_mask, set, PointerMotionMask);
    XChangeWindowAttributes(x_window.dpy, x_window.win, CWEventMask, &x_window.attrs);
}

void
x_set_mode(int32 set, uint32 flags) {
    int32 mode = term_window.mode;
    MODBIT(term_window.mode, set, flags);
    if ((term_window.mode & WIN_MODE_REVERSE) != (mode & WIN_MODE_REVERSE)) {
        redraw();
    }
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
    XWMHints *h = XGetWMHints(x_window.dpy, x_window.win);

    MODBIT(h->flags, add, XUrgencyHint);
    XSetWMHints(x_window.dpy, x_window.win, h);
    XFree(h);
}

void
x_bell(void) {
    if (!(TERM_WINDOW_IS_SET(WIN_MODE_FOCUSED))) {
        x_set_urgency(1);
    }
    if (CONF_BELL_VOLUME) {
        XkbBell(x_window.dpy, x_window.win, CONF_BELL_VOLUME, (Atom)NULL);
    }
}

void
handler_focus(XEvent *ev) {
    XFocusChangeEvent *e = &ev->xfocus;

    if (e->mode == NotifyGrab) {
        return;
    }

    if (ev->type == FocusIn) {
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
}

int32
match(uint32 mask, uint32 state) {
    return mask == XK_ANY_MOD || mask == (state & ~CONF_IGNORE_MOD);
}

char *
kmap(KeySym k, uint32 state) {
    Key *kp;
    int32 i;

    /* Check for mapped keys out of X11 function keys. */
    for (i = 0; i < LENGTH(CONF_MAPPED_KEYS); i++) {
        if (CONF_MAPPED_KEYS[i] == k) {
            break;
        }
    }
    if (i == LENGTH(CONF_MAPPED_KEYS)) {
        if ((k & 0xFFFF) < 0xFD00) {
            return NULL;
        }
    }

    for (kp = key; kp < key + LENGTH(key); kp++) {
        if (kp->k != k) {
            continue;
        }

        if (!match(kp->mask, state)) {
            continue;
        }

        if (TERM_WINDOW_IS_SET(WIN_MODE_APPKEYPAD) ? kp->appkey < 0 : kp->appkey > 0) {
            continue;
        }
        if (TERM_WINDOW_IS_SET(WIN_MODE_NUMLOCK) && kp->appkey == 2) {
            continue;
        }

        if (TERM_WINDOW_IS_SET(WIN_MODE_APPCURSOR) ? kp->appcursor < 0 : kp->appcursor > 0) {
            continue;
        }

        return kp->s;
    }

    return NULL;
}

void
handler_key_press(XEvent *ev) {
    XKeyEvent *e = &ev->xkey;
    KeySym ksym = NoSymbol;
    char buf[64], *customkey;
    int32 len;
    Rune c;
    Status status;
    Shortcut *bp;

    if (TERM_WINDOW_IS_SET(WIN_MODE_KBDLOCK)) {
        return;
    }

    if (x_window.ime.xic) {
        len = XmbLookupString(x_window.ime.xic, e, buf, SIZEOF(buf), &ksym, &status);
        if (status == XBufferOverflow) {
            return;
        }
    } else {
        len = XLookupString(e, buf, SIZEOF(buf), &ksym, NULL);
    }
    /* 1. shortcuts */
    for (bp = shortcuts; bp < shortcuts + LENGTH(shortcuts); bp++) {
        if (ksym == bp->keysym && match(bp->mod, e->state)) {
            bp->func(&(bp->arg));
            return;
        }
    }

    /* 2. custom keys from config.def.h */
    if ((customkey = kmap(ksym, e->state))) {
        tty_write(customkey, (int64)strlen(customkey), 1);
        return;
    }

    /* 3. composed string from input method */
    if (len == 0) {
        return;
    }
    if (len == 1 && e->state & Mod1Mask) {
        if (TERM_WINDOW_IS_SET(WIN_MODE_8BIT)) {
            if (*buf < 0177) {
                c = (Rune)(*buf | 0x80);
                len = (int32)utf8_encode(c, buf);
            }
        } else {
            buf[1] = buf[0];
            buf[0] = '\033';
            len = 2;
        }
    }
    tty_write(buf, (int64)len, 1);
}

void
handler_client_message(XEvent *e) {
    /*
     * See xembed specs
     *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
     */
    if (e->xclient.message_type == x_window.xembed && e->xclient.format == 32) {
        if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
            term_window.mode |= WIN_MODE_FOCUSED;
            x_set_urgency(0);
        } else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
            term_window.mode &= ~WIN_MODE_FOCUSED;
        }
    } else if (e->xclient.data.l[0] == (int64)x_window.wmdeletewin) {
        tty_hangup();
        exit(0);
    }
}

void
handler_configure_notify(XEvent *e) {
    if (e->xconfigure.width == term_window.w && e->xconfigure.height == term_window.h) {
        return;
    }

    cresize(e->xconfigure.width, e->xconfigure.height);
}

void
run(void) {
    XEvent ev;
    int32 w = term_window.w, h = term_window.h;
    fd_set rfd;
    int32 xfd = XConnectionNumber(x_window.dpy), ttyfd, xev, drawing;
    struct timespec seltv, *tv, now, lastblink, trigger;
    float timeout;

    /* Waiting for window mapping */
    do {
        XNextEvent(x_window.dpy, &ev);
        /*
         * This XFilterEvent call is required because of XOpenIM. It
         * does filter out the key event and some client message for
         * the input method too.
         */
        if (XFilterEvent(&ev, None)) {
            continue;
        }
        if (ev.type == ConfigureNotify) {
            w = ev.xconfigure.width;
            h = ev.xconfigure.height;
        }
    } while (ev.type != MapNotify);

    ttyfd = tty_new(opt_line, CONF_SHELl, opt_io, opt_cmd);
    cresize(w, h);

    for (timeout = -1, drawing = 0, lastblink = (struct timespec){0};;) {
        FD_ZERO(&rfd);
        FD_SET(ttyfd, &rfd);
        FD_SET(xfd, &rfd);

        if (XPending(x_window.dpy)) {
            timeout = 0; /* existing events might not set xfd */
        }

        seltv.tv_sec = timeout / 1E3f;
        seltv.tv_nsec = 1E6f * (timeout - 1E3f * (float)seltv.tv_sec);
        tv = timeout >= 0 ? &seltv : NULL;

        if (pselect(MAX(xfd, ttyfd) + 1, &rfd, NULL, NULL, tv, NULL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            die("select failed: %s\n", strerror(errno));
        }
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (FD_ISSET(ttyfd, &rfd)) {
            tty_read();
        }

        xev = 0;
        while (XPending(x_window.dpy)) {
            xev = 1;
            XNextEvent(x_window.dpy, &ev);
            if (XFilterEvent(&ev, None)) {
                continue;
            }
            if (handler[ev.type]) {
                (handler[ev.type])(&ev);
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
         * sync with periodic updates from animations/key-repeats/etc.
         */
        if (FD_ISSET(ttyfd, &rfd) || xev) {
            if (!drawing) {
                trigger = now;
                drawing = 1;
            }
            timeout = (CONF_LATENCY_MAX - (float)TIMEDIFF(now, trigger)) / CONF_LATENCY_MAX *
                      CONF_LATENCY_MIN;
            if (timeout > 0) {
                continue; /* we have time, try to find idle */
            }
        }

        /* idle detected or CONF_LATENCY_MAX exhausted -> draw */
        timeout = -1;
        if (BLINK_TIMEOUT && term_attr_set(ATTR_BLINK)) {
            timeout = (float)BLINK_TIMEOUT - (float)TIMEDIFF(now, lastblink);
            if (timeout <= 0) {
                if (-timeout > (float)BLINK_TIMEOUT) { /* start visible */
                    term_window.mode |= WIN_MODE_BLINK;
                }
                term_window.mode ^= WIN_MODE_BLINK;
                term_set_dirt_attr(ATTR_BLINK);
                lastblink = now;
                timeout = (float)BLINK_TIMEOUT;
            }
        }

        draw();
        XFlush(x_window.dpy);
        drawing = 0;
    }
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

int32
main(int32 argc, char *argv[]) {
    x_window.l = x_window.t = 0;
    x_window.isfixed = False;
    x_set_cursor((int32)CONF_CURSOR_SHAPE);

    ARGBEGIN {
    case 'a':
        ALLOW_ALT_SCREEN = 0;
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
        x_window.gm = XParseGeometry(EARGF(usage()), &x_window.l, &x_window.t,
                                     (uint32 *)&CONF_NUMBER_COLS, (uint32 *)&CONF_NUMBER_ROWS);
        break;
    case 'i':
        x_window.isfixed = 1;
        break;
    case 'o':
        opt_io = EARGF(usage());
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
        opt_title = (opt_line || !opt_cmd) ? "st" : opt_cmd[0];
    }

    setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    CONF_NUMBER_COLS = MAX(CONF_NUMBER_COLS, 1);
    CONF_NUMBER_ROWS = MAX(CONF_NUMBER_ROWS, 1);
    term_new(CONF_NUMBER_COLS, CONF_NUMBER_ROWS);
    x_init(CONF_NUMBER_COLS, CONF_NUMBER_ROWS);
    x_setenv();
    selection_init();
    run();
}
