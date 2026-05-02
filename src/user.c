#if !defined(USER_C)
#define USER_C

#include <termios.h>
#include "st.h"
#include "util.c"
#include "config.def.h"
#include "selection.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_user 1
#elif !defined(TESTING_user)
#define TESTING_user 0
#endif

static void
user_clipboard_copy(union Arg *arg) {
    Atom clipboard;
    (void)arg;

    free(xsel.clipboard);
    xsel.clipboard = NULL;

    if (xsel.primary != NULL) {
        xsel.clipboard = xstrdup(xsel.primary);
        clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
        XSetSelectionOwner(x_window.display,
                           clipboard, x_window.win,
                           CurrentTime);
    }
    return;
}

static void
user_clipboard_paste(union Arg *arg) {
    Atom clipboard;
    (void)arg;

    clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
    XConvertSelection(x_window.display, clipboard, xsel.xtarget, clipboard,
                      x_window.win, CurrentTime);
    return;
}

static void
user_selection_paste(union Arg *arg) {
    (void)arg;
    XConvertSelection(x_window.display,
                      XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
                      x_window.win, CurrentTime);
    return;
}

static void
user_change_alpha(union Arg *arg) {
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

static void
user_toggle_numlock(union Arg *arg) {
    (void)arg;
    term_window.mode ^= WIN_MODE_NUMLOCK;
    return;
}

static void
user_zoom(union Arg *arg) {
    union Arg larg;

    larg.f = usedfontsize + arg->f;
    if (larg.f >= 1.0f) {
        zoom_abs(&larg);
    }
    return;
}

static void
user_zoom_reset(union Arg *arg) {
    union Arg larg;
    (void)arg;

    if (defaultfontsize > 0) {
        larg.f = defaultfontsize;
        zoom_abs(&larg);
    }
    return;
}

static void
user_tty_send(union Arg *arg) {
    tty_write(arg->s, strlen32(arg->s), 1);
    return;
}

static void
user_scroll_down(union Arg *a) {
    int32 n = a->i;

    if (!term.lines_scrolled_up || TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        return;
    }

    if (n < 0) {
        n = (int32)MAX(term.nrows / -n, 1);
    }

    if (n <= term.lines_scrolled_up) {
        term.lines_scrolled_up -= n;
    } else {
        n = term.lines_scrolled_up;
        term.lines_scrolled_up = 0;
    }
    if (selection.ob.x != -1 && !selection.alt) {
        selection_move(-n); /* negate change in term.lines_scrolled_up */
    }
    term_full_dirt();
    return;
}

static void
user_scroll_up(union Arg *a) {
    int32 n = a->i;

    if (!term.n_hist || TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        return;
    }

    if (n < 0) {
        n = (int32)MAX(term.nrows / -n, 1);
    }

    if (term.lines_scrolled_up + n <= term.n_hist) {
        term.lines_scrolled_up += n;
    } else {
        n = term.n_hist - term.lines_scrolled_up;
        term.lines_scrolled_up = term.n_hist;
    }

    if (selection.ob.x != -1 && !selection.alt) {
        selection_move(n); /* negate change in term.lines_scrolled_up */
    }
    term_full_dirt();
    return;
}

static void
user_send_break(union Arg *arg) {
    if (tcsendbreak(command_fd, 0)) {
        perror("Error sending break");
    }
    (void)arg;
    return;
}

static void
user_toggle_printer(union Arg *arg) {
    term.mode ^= TERM_MODE_PRINT;
    (void)arg;
    return;
}

static void
user_print_screen(union Arg *arg) {
    term_dump();
    (void)arg;
    return;
}

static void
user_print_sel(union Arg *arg) {
    term_dump_sel();
    (void)arg;
    return;
}

#if TESTING_user

#include <stdbool.h>
#include <stdlib.h>

#include "assert.c"
#include "st.c"

int
main(void) {
    {
        term_window.mode = 0;
        user_toggle_numlock(NULL);
        ASSERT_EQUAL((int32)term_window.mode, WIN_MODE_NUMLOCK);
        
        user_toggle_numlock(NULL);
        ASSERT_EQUAL((int32)term_window.mode, 0);
    }

    {
        term.mode = 0;
        user_toggle_printer(NULL);
        ASSERT_EQUAL((int32)term.mode, TERM_MODE_PRINT);
        
        user_toggle_printer(NULL);
        ASSERT_EQUAL((int32)term.mode, 0);
    }

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_user */

#endif /* USER_C */
