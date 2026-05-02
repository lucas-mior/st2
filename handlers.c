#if !defined(HANDLERS_C)
#define HANDLERS_C

#include "st.h"

void
handler_sigchld(int32 unused) {
    int32 stat;
    pid_t p;
    (void)unused;

    if ((p = waitpid(pid, &stat, WNOHANG)) < 0) {
        die("waiting for pid %hd failed: %s\n", pid, strerror(errno));
    }

    if (pid != p) {
        if (p == 0 && wait(&stat) < 0) {
            die("wait: %s\n", strerror(errno));
        }

        /* reinstall handler_sigchld handler */
        signal(SIGCHLD, handler_sigchld);
        return;
    }

    if (WIFEXITED(stat) && WEXITSTATUS(stat)) {
        die("child exited with status %d\n", WEXITSTATUS(stat));
    } else if (WIFSIGNALED(stat)) {
        die("child terminated due to signal %d\n", WTERMSIG(stat));
    }
    _exit(0);
}

void
handler_button_press(XEvent *xevent) {
    int32 button = (int32)xevent->xbutton.button;
    struct timespec tnow;
    int32 snap;

    if (1 <= button && button <= 11) {
        buttons |= 1 << (button - 1);
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSE)) {
        if (!(xevent->xbutton.state & CONF_FORCE_MOUSE_MOD)) {
            mouse_report(xevent);
            return;
        }
    }

    if (mouse_action(xevent, 0)) {
        return;
    }

    if (button == Button1) {
        /* Snapping behavior based on double/triple click timeouts. */
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
    if (x_property_event->state == PropertyNewValue) {
        if (x_property_event->atom == XA_PRIMARY) {
            handler_selection_notify(xevent);
        } else if (x_property_event->atom == clipboard) {
            handler_selection_notify(xevent);
        }
    }
    return;
}

void
handler_selection_notify(XEvent *xevent) {
    uint64 nitems;
    uint64 ofs;
    uint64 rem;
    int32 format;
    uchar *data;
    uchar *last;
    uchar *repl;
    Atom type;
    Atom incratom;
    Atom property = None;

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
            MODBIT(x_window.attrs.event_mask, 0, PropertyChangeMask);
            XChangeWindowAttributes(x_window.display, x_window.win, CWEventMask,
                                    &x_window.attrs);
        }

        if (type == incratom) {
            MODBIT(x_window.attrs.event_mask, 1, PropertyChangeMask);
            XChangeWindowAttributes(x_window.display, x_window.win, CWEventMask,
                                    &x_window.attrs);
            XDeleteProperty(x_window.display, x_window.win, (ulong)property);
            continue;
        }

        repl = data;
        last = data + nitems*(uint64)format / 8;
        while (1) {
            repl = memchr(repl, '\n', (size_t)(last - repl));
            if (!repl) {
                break;
            }
            *repl = '\r';
            repl += 1;
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

#endif
