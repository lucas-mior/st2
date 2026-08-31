#if !defined(HANDLERS_C)
#define HANDLERS_C

#include "cbase.h"
#include "st.h"
#include "config.h"
#include "selection.c"
#include "utf8.c"
#include "mouse.c"
#include "x.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_handlers 1
#elif !defined(TESTING_handlers)
#define TESTING_handlers 0
#endif

static void
handler_sigchld(int32 unused) {
    int32 stat;
    pid_t p;
    (void)unused;

    if ((p = waitpid(st_pid, &stat, WNOHANG)) < 0) {
        error("waiting for pid %hd failed: %s\n", st_pid, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (st_pid != p) {
        pid_t ret = 0;
        
        while ((ret = waitpid(-1, &stat, WNOHANG)) > 0) {
            if (ret == st_pid) {
                if (WIFEXITED(stat) && WEXITSTATUS(stat)) {
                    error("child exited with status %d\n", WEXITSTATUS(stat));
                    exit(EXIT_FAILURE);
                } else if (WIFSIGNALED(stat)) {
                    error("child terminated due to signal %d\n", WTERMSIG(stat));
                    exit(EXIT_FAILURE);
                }
                _exit(0);
            }
        }

        /* reinstall handler_sigchld handler */
        signal(SIGCHLD, handler_sigchld);
        return;
    }

    if (WIFEXITED(stat) && WEXITSTATUS(stat)) {
        error("child exited with status %d\n", WEXITSTATUS(stat));
        exit(EXIT_FAILURE);
    } else if (WIFSIGNALED(stat)) {
        error("child terminated due to signal %d\n", WTERMSIG(stat));
        exit(EXIT_FAILURE);
    }
    _exit(0);
}

static void
handler_button_press(XEvent *xevent) {
    int32 button = (int32)xevent->xbutton.button;
    struct timespec tnow;
    enum SelectionSnap snap;

    if (1 <= button && button <= 11) {
        buttons |= 1 << (button - 1);
    }

    if (win_mode_is_set(WIN_MODE_MOUSE)) {
        if (!(xevent->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
            mouse_report(xevent);
            return;
        }
    }

    if (mouse_action(xevent, 0)) {
        return;
    }

    /* Log information about the clicked cell */
    if (DEBUGGING) {
        int32 col = xevent_col(xevent);
        int32 row = xevent_row(xevent);
        char utf8_buf[5];
        int32 utf8_len;
        char *multi_code_point = "NO";

        if (BETWEEN(row, 0, term.nrows - 1) && BETWEEN(col, 0, term.ncols - 1)) {
            StGlyph glyph = term.lines[row][col];
            StFont *st_font = &draw_context.font;
            XftFont *xfont;
            FcChar8 *fontname;

            /* Determine base font variant based on attributes */
            if (glyph.mode & ATTR_ITALIC) {
                if (glyph.mode & ATTR_BOLD) {
                    st_font = &draw_context.ibfont;
                } else {
                    st_font = &draw_context.ifont;
                }
            } else if (glyph.mode & ATTR_BOLD) {
                st_font = &draw_context.bfont;
            }
            xfont = st_font->match;

            /* Search Font Ring Cache if glyph was rendered via fallback */
            if (glyph.rune != ' ' && !XftCharIndex(x_window.display, xfont, 
                                                   glyph.rune)) {
                for (int32 i = 0; i < frc_len; i += 1) {
                    if (frc[i].unicodep == glyph.rune) {
                        xfont = frc[i].font;
                        break;
                    }
                }
            }

            utf8_len = utf8_encode_raw(glyph.rune, utf8_buf);
            utf8_buf[utf8_len] = '\0';
            if (glyph.rune & MULTI_CODE_POINT_FLAG) {
                multi_code_point = "YES";
            }

            fontname = FcNameUnparse(xfont->pattern);
            fprintf(stderr, "st Cell [%d, %d] {\n", col, row);
            fprintf(stderr, "  rune: U+%08X = %s\n", glyph.rune, utf8_buf);
            fprintf(stderr, "  multi code point? %s\n", multi_code_point);
            fprintf(stderr, "  font: %.50s\n", (char *)fontname);
            fprintf(stderr, "  attr: %s\n", ATTR_str(glyph.mode));
            fprintf(stderr, "  color: fg=%d, bg=%d\n\n", glyph.fg, glyph.bg);
            fprintf(stderr, "}\n");
            free(fontname);
        }
    }

    if (button == Button1) {
        /* Snapping behavior based on double/triple click timeouts. */
        clock_gettime(CLOCK_MONOTONIC, &tnow);
        if (timediff_ms(tnow, xsel.tclick2) <= CONF_TRIPLE_CLICK_TIMEOUT) {
            snap = SELECTION_SNAP_LINE;
        } else if (timediff_ms(tnow, xsel.tclick1) <= CONF_DOUBLE_CLICK_TIMEOUT) {
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

static void
handler_selection_notify(XEvent *xevent) {
    ulong nitems_return;
    ulong offset;
    ulong bytes_after_return;
    int actual_format_return;
    uchar *prop_return;
    uchar *last;
    uchar *repl;
    Atom actual_type_return;
    Atom INCR = XInternAtom(x_window.display, "INCR", 0);
    Atom property = None;

    offset = 0;
    if (xevent->type == SelectionNotify) {
        property = xevent->xselection.property;
    } else if (xevent->type == PropertyNotify) {
        property = xevent->xproperty.atom;
    }

    if (property == None) {
        return;
    }

    do {
        if (XGetWindowProperty(x_window.display, x_window.win,
                               property, (int64)offset, BUFSIZ / 4,
                               False, AnyPropertyType,
                               &actual_type_return, &actual_format_return,
                               &nitems_return, &bytes_after_return, &prop_return)) {
            error("Clipboard allocation failed\n");
            return;
        }

        if ((xevent->type == PropertyNotify)
                && (nitems_return == 0)
                && (bytes_after_return == 0)) {
            MODBIT(x_window.attrs.event_mask, 0, PropertyChangeMask);
            XChangeWindowAttributes(x_window.display, x_window.win,
                                    CWEventMask, &x_window.attrs);
        }

        if (actual_type_return == INCR) {
            MODBIT(x_window.attrs.event_mask, 1, PropertyChangeMask);
            XChangeWindowAttributes(x_window.display, x_window.win, CWEventMask,
                                    &x_window.attrs);
            XDeleteProperty(x_window.display, x_window.win, (ulong)property);
            XFree(prop_return);
            continue; 
        }

        ASSERT(prop_return);
        repl = prop_return;
        last = prop_return + nitems_return*(uint64)actual_format_return / 8;
        while (1) {
            repl = memchr64(repl, '\n', last - repl);
            if (!repl) {
                break;
            }
            *repl = '\r';
            repl += 1;
        }

        if (win_mode_is_set(WIN_MODE_BRCKTPASTE) && offset == 0) {
            tty_write("\033[200~", 6, 0);
        }
        tty_write((char *)prop_return, nitems_return*(uint64)actual_format_return / 8, 1);
        if (win_mode_is_set(WIN_MODE_BRCKTPASTE) && bytes_after_return == 0) {
            tty_write("\033[201~", 6, 0);
        }
        XFree(prop_return);
        offset += nitems_return*(uint64)actual_format_return / 32;
    } while (bytes_after_return > 0);

    XDeleteProperty(x_window.display, x_window.win, (ulong)property);
    return;
}

static void
handler_prop_notify(XEvent *xevent) {
    XPropertyEvent *x_property_event = &xevent->xproperty;
    Atom clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);

    if (x_property_event->state == PropertyNewValue) {
        if (x_property_event->atom == XA_PRIMARY) {
            handler_selection_notify(xevent);
        } else if (x_property_event->atom == clipboard) {
            handler_selection_notify(xevent);
        }
    }
    return;
}

static void
handler_selection_clear(XEvent *xevent) {
    (void)xevent;
    selection_clear();
    return;
}

static void
handler_selection_request(XEvent *xevent) {
    XSelectionRequestEvent *xselection_request_event;
    XSelectionEvent xselection_event;
    Atom xa_targets;
    Atom clipboard;
    char *selection_text = NULL;
    int32 selection_text_len = 0;

    xselection_request_event = (XSelectionRequestEvent *)xevent;
    xselection_event.type = SelectionNotify;
    xselection_event.requestor = xselection_request_event->requestor;
    xselection_event.selection = xselection_request_event->selection;
    xselection_event.target = xselection_request_event->target;
    xselection_event.time = xselection_request_event->time;
    if (xselection_request_event->property == None) {
        xselection_request_event->property = xselection_request_event->target;
    }

    /* reject by default */
    xselection_event.property = None;

    xa_targets = XInternAtom(x_window.display, "TARGETS", 0);
    clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);

    if (xselection_request_event->target == xa_targets) {
        Atom targets[4];
        int32 ntargets = 0;

        targets[ntargets++] = xa_targets;

        if (xselection_request_event->selection == XA_PRIMARY) {
            if (xsel.primary != NULL) {
                targets[ntargets++] = xsel.xtarget;
                targets[ntargets++] = XA_STRING;
            }
        } else if (xselection_request_event->selection == clipboard) {
            if (xsel.clipboard != NULL) {
                targets[ntargets++] = xsel.clipboard_target;
                if (xsel.clipboard_target == xsel.xtarget) {
                    targets[ntargets++] = XA_STRING;
                }
            }
        } else {
            error("Unhandled clipboard selection 0x%lx\n",
                  xselection_request_event->selection);
        }

        XChangeProperty(xselection_request_event->display,
                        xselection_request_event->requestor,
                        xselection_request_event->property, XA_ATOM, 32,
                        PropModeReplace, (uchar *)targets, ntargets);
        xselection_event.property = xselection_request_event->property;
    } else {
        if (xselection_request_event->selection == XA_PRIMARY) {
            if (xselection_request_event->target == xsel.xtarget
                || xselection_request_event->target == XA_STRING) {
                selection_text = xsel.primary;
                if (selection_text != NULL) {
                    selection_text_len = strlen32(selection_text);
                }
            }
        } else if (xselection_request_event->selection == clipboard) {
            if (xsel.clipboard != NULL) {
                if (xselection_request_event->target == xsel.clipboard_target) {
                    selection_text = xsel.clipboard;
                    selection_text_len = xsel.clipboard_len;
                } else if (xsel.clipboard_target == xsel.xtarget
                           && xselection_request_event->target == XA_STRING) {
                    selection_text = xsel.clipboard;
                    selection_text_len = xsel.clipboard_len;
                }
            }
        } else {
            error("Unhandled clipboard selection 0x%lx\n",
                  xselection_request_event->selection);
        }

        if (selection_text != NULL) {
            XChangeProperty(xselection_request_event->display,
                            xselection_request_event->requestor,
                            xselection_request_event->property,
                            xselection_request_event->target, 8,
                            PropModeReplace, (uchar *)selection_text,
                            selection_text_len);
            xselection_event.property = xselection_request_event->property;
        }
    }

    /* all done, send a notification to the listener */
    if (!XSendEvent(xselection_request_event->display,
                    xselection_request_event->requestor, 1, 0,
                    (XEvent *)&xselection_event)) {
        error("Error sending SelectionNotify event\n");
    }
    return;
}

static void
handler_button_release(XEvent *xevent) {
    uint32 button = xevent->xbutton.button;

    if (1 <= button && button <= 11) {
        buttons &= (uint32) ~(1 << (button - 1));
    }

    if (win_mode_is_set(WIN_MODE_MOUSE)) {
        if (!(xevent->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
            mouse_report(xevent);
            return;
        }
    }

    if (mouse_action(xevent, 1)) {
        return;
    }
    if (button == Button1) {
        mouse_select(xevent, 1);
        if (!user_clipboard_copy_selection_image(xevent->xbutton.time)) {
            user_clipboard_copy(NULL);
        }
    }
    return;
}

static void
handler_button_motion(XEvent *xevent) {
    if (win_mode_is_set(WIN_MODE_MOUSE)) {
        if (!(xevent->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
            mouse_report(xevent);
            return;
        }
    }

    mouse_select(xevent, 0);
    return;
}

static void
handler_expose(XEvent *xevent) {
    (void)xevent;
    redraw();
    return;
}

static void
handler_visibility(XEvent *xevent) {
    XVisibilityEvent *e = &xevent->xvisibility;
    int32 visible;
    if (e->state != VisibilityFullyObscured) {
        visible = 1;
    } else {
        visible = 0;
    }
    MODBIT(term_window.mode, visible, WIN_MODE_VISIBLE);
    return;
}

static void
handler_unmap(XEvent *xevent) {
    (void)xevent;
    term_window.mode &= ~WIN_MODE_VISIBLE;
    return;
}

static void
handler_focus(XEvent *xevent) {
    XFocusChangeEvent *xevent_focus = &xevent->xfocus;

    if (xevent_focus->mode == NotifyGrab) {
        return;
    }

    if (xevent->type == FocusIn) {
        if (x_window.ime.xic) {
            XSetICFocus(x_window.ime.xic);
        }
        term_window.mode |= WIN_MODE_FOCUSED;
        x_set_urgency(0);
        if (win_mode_is_set(WIN_MODE_FOCUS)) {
            tty_write("\033[I", 3, 0);
        }
    } else {
        if (x_window.ime.xic) {
            XUnsetICFocus(x_window.ime.xic);
        }
        term_window.mode &= ~WIN_MODE_FOCUSED;
        if (win_mode_is_set(WIN_MODE_FOCUS)) {
            tty_write("\033[O", 3, 0);
        }
    }
    return;
}

static void
handler_key_press(XEvent *xevent) {
    XKeyEvent *key_event = &xevent->xkey;
    KeySym key_sym = NoSymbol;
    char buffer[64];
    char *custom_key = NULL;
    int32 len;
    uint32 c;

    if (win_mode_is_set(WIN_MODE_KBDLOCK)) {
        return;
    }

    if (x_window.ime.xic) {
        Status status;
        len = XmbLookupString(x_window.ime.xic, key_event, buffer,
                              SIZEOF(buffer), &key_sym, &status);
        if (status == XBufferOverflow) {
            return;
        }
    } else {
        len = XLookupString(key_event, buffer, SIZEOF(buffer), &key_sym, NULL);
    }

    for (int32 i = 0; i < LENGTH(CONF_KEYBOARD_SHORTCUTS); i += 1) {
        Shortcut *shortcut = &CONF_KEYBOARD_SHORTCUTS[i];
        if (key_sym == shortcut->keysym) {
            if (match_mask_state(shortcut->mod, key_event->state)) {
                shortcut->func(&(shortcut->arg));
                return;
            }
        }
    }

    {
        int32 i;
        for (i = 0; i < LENGTH(CONF_MAPPED_KEYS); i += 1) {
            if (CONF_MAPPED_KEYS[i] == key_sym) {
                break;
            }
        }
        if (i == LENGTH(CONF_MAPPED_KEYS)) {
            if ((key_sym & 0xFFFF) < 0xFD00) {
                goto tried_custom_keys;
            }
        }

        for (int32 j = 0; j < LENGTH(CONF_KEYS); j += 1) {
            Key *key = &CONF_KEYS[j];

            if (key->k != key_sym) {
                continue;
            }
            if (!match_mask_state(key->mask, key_event->state)) {
                continue;
            }
            if (win_mode_is_set(WIN_MODE_APPKEYPAD)) {
                if (key->app_key < 0) {
                    continue;
                }
            } else {
                if (key->app_key > 0) {
                    continue;
                }
            }
            if (win_mode_is_set(WIN_MODE_NUMLOCK) && key->app_key == 2) {
                continue;
            }
            if (win_mode_is_set(WIN_MODE_APPCURSOR)) {
                if (key->app_cursor < 0) {
                    continue;
                }
            } else {
                if (key->app_cursor > 0) {
                    continue;
                }
            }

            custom_key = key->s;
            goto tried_custom_keys;
        }
        custom_key = NULL;
    }
tried_custom_keys:
    if (custom_key) {
        tty_write(custom_key, strlen32(custom_key), 1);
        return;
    }

    if (len == 0) {
        return;
    }
    if ((len == 1) && (key_event->state & Mod1Mask)) {
        if (win_mode_is_set(WIN_MODE_8BIT)) {
            if (*buffer < 0177) {
                c = (uint32)(*buffer | 0x80);
                len = utf8_encode_raw(c, buffer);
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

static void
handler_client_message(XEvent *xevent) {
    if (xevent->xclient.message_type == x_window.xembed) {
        if (xevent->xclient.format == 32) {
            if (xevent->xclient.data.l[1] == XEMBED_FOCUS_IN) {
                term_window.mode |= WIN_MODE_FOCUSED;
                x_set_urgency(0);
            } else if (xevent->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
                term_window.mode &= ~WIN_MODE_FOCUSED;
            }
        }
    } else if (xevent->xclient.data.l[0] == (int64)x_window.wm_delete_win) {
        tty_hangup();
        exit(0);
    }
    return;
}

static void
handler_configure_notify(XEvent *xevent) {
    if (xevent->xconfigure.width == term_window.w) {
        if (xevent->xconfigure.height == term_window.h) {
            return;
        }
    }
    x_configure_resize(xevent->xconfigure.width, xevent->xconfigure.height);
    return;
}

#if 0 == TESTING_handlers
static inline void
handlers_functions_sink(void) {
    (void)handlers_functions_sink;
    (void)handler_sigchld;
    (void)handler_button_press;
    (void)handler_selection_notify;
    (void)handler_prop_notify;
    (void)handler_selection_clear;
    (void)handler_selection_request;
    (void)handler_button_release;
    (void)handler_button_motion;
    (void)handler_expose;
    (void)handler_visibility;
    (void)handler_unmap;
    (void)handler_focus;
    (void)handler_key_press;
    (void)handler_client_message;
    (void)handler_configure_notify;
    return;
}
#endif
#if TESTING_handlers
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "tty.c"
#include "st.c"
#include "x.c"
#include "user.c"

int
main(void) {
    {
        Window parent;
        Window root;
        XWindowAttributes attr;
        XVisualInfo visual;

        x_window.display = XOpenDisplay(NULL);
        if (!x_window.display) {
            error("can't open display\n");
            exit(EXIT_FAILURE);
        }
        x_window.screen = XDefaultScreen(x_window.display);
        root = XRootWindow(x_window.display, x_window.screen);
        parent = root;

        if (XMatchVisualInfo(x_window.display, x_window.screen,
                             32, TrueColor, &visual) != 0) {
            x_window.visual = visual.visual;
            x_window.depth = visual.depth;
        } else {
            XGetWindowAttributes(x_window.display, parent, &attr);
            x_window.visual = attr.visual;
            x_window.depth = attr.depth;
        }

        term_window.w = 800;
        term_window.h = 600;
        term_window.cw = 10;
        term_window.ch = 20;

        x_window.win = XCreateSimpleWindow(x_window.display, parent,
                                           0, 0,
                                           (uint32)term_window.w,
                                           (uint32)term_window.h,
                                           0, 0, 0);

        xsel.xtarget = XInternAtom(x_window.display, "UTF8_STRING", 0);
        if (xsel.xtarget == None) {
            xsel.xtarget = XA_STRING;
        }

        x_window.xembed = XInternAtom(x_window.display, "_XEMBED", False);
        x_window.wm_delete_win = XInternAtom(x_window.display, "WM_DELETE_WINDOW", False);
    }

    {
        XEvent ev;

        ev.type = VisibilityNotify;
        ev.xvisibility.state = VisibilityUnobscured;
        term_window.mode = 0;
        handler_visibility(&ev);
        ASSERT_MORE((int32)(term_window.mode & WIN_MODE_VISIBLE), 0);

        ev.xvisibility.state = VisibilityFullyObscured;
        handler_visibility(&ev);
        ASSERT_ZERO((int32)(term_window.mode & WIN_MODE_VISIBLE));
    }

    {
        XEvent ev;

        term_window.mode = WIN_MODE_VISIBLE;
        handler_unmap(&ev);
        ASSERT_ZERO((int32)(term_window.mode & WIN_MODE_VISIBLE));
    }
    
    {
        XEvent ev;

        ev.type = FocusIn;
        ev.xfocus.mode = NotifyGrab;
        term_window.mode = 0;
        handler_focus(&ev);
        ASSERT_ZERO((int32)(term_window.mode & WIN_MODE_FOCUSED));
    }

    if (fork() == 0) {
        handler_sigchld(0);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        XEvent ev;

        ev.type = ButtonPress;
        ev.xbutton.button = Button2;
        ev.xbutton.state = 0;
        handler_button_press(&ev);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    {
        XEvent ev;

        ev.type = SelectionNotify;
        ev.xselection.property = None;
        handler_selection_notify(&ev);
        ASSERT_EQUAL(1, 1);
    }

    {
        XEvent ev;

        ev.type = PropertyNotify;
        ev.xproperty.state = PropertyDelete;
        handler_prop_notify(&ev);
        ASSERT_EQUAL(1, 1);
    }

    if (fork() == 0) {
        XEvent ev;

        handler_selection_clear(&ev);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        XEvent ev;

        ev.type = SelectionRequest;
        ev.xselectionrequest.display = x_window.display;
        ev.xselectionrequest.requestor = x_window.win;
        ev.xselectionrequest.property = None;
        ev.xselectionrequest.target = None;
        ev.xselectionrequest.time = CurrentTime;
        handler_selection_request(&ev);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        XEvent ev;

        ev.type = ButtonRelease;
        ev.xbutton.button = Button2;
        ev.xbutton.state = 0;
        handler_button_release(&ev);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        XEvent ev;

        ev.type = MotionNotify;
        ev.xbutton.state = 0;
        handler_button_motion(&ev);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        XEvent ev;

        handler_expose(&ev);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    {
        XEvent ev;

        term_window.mode = WIN_MODE_KBDLOCK;
        handler_key_press(&ev);
        ASSERT_EQUAL(1, 1);
    }

    {
        XEvent ev;

        ev.type = ClientMessage;
        ev.xclient.message_type = None;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = (long)None;
        ev.xclient.data.l[1] = (long)None;
        handler_client_message(&ev);
        ASSERT_EQUAL(1, 1);
    }

    {
        XEvent ev;

        ev.type = ConfigureNotify;
        ev.xconfigure.width = term_window.w;
        ev.xconfigure.height = term_window.h;
        handler_configure_notify(&ev);
        ASSERT_EQUAL(1, 1);
    }

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_handlers */

#endif /* HANDLERS_C */
