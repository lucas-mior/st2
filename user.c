#include "st.h"
#include "cbase/util.c"

void
user_clipboard_copy(Arg *arg) {
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
user_clipboard_paste(Arg *arg) {
    Atom clipboard;
    (void)arg;

    clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
    XConvertSelection(x_window.display, clipboard, xsel.xtarget, clipboard,
                      x_window.win, CurrentTime);
    return;
}

void
user_selection_paste(Arg *arg) {
    (void)arg;
    XConvertSelection(x_window.display, XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
                      x_window.win, CurrentTime);
    return;
}

void
user_change_alpha(Arg *arg) {
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
user_toggle_numlock(Arg *arg) {
    (void)arg;
    term_window.mode ^= WIN_MODE_NUMLOCK;
    return;
}

void
user_zoom(Arg *arg) {
    Arg larg;

    larg.f = usedfontsize + arg->f;
    if (larg.f >= 1.0f) {
        zoom_abs(&larg);
    }
    return;
}

void
user_zoom_reset(Arg *arg) {
    Arg larg;
    (void)arg;

    if (defaultfontsize > 0) {
        larg.f = defaultfontsize;
        zoom_abs(&larg);
    }
    return;
}

void
user_tty_send(Arg *arg) {
    tty_write(arg->s, (int64)strlen(arg->s), 1);
    return;
}
