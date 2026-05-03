#if !defined(MOUSE_C)
#define MOUSE_C

#include "st.h"
#include "config.def.h"
#include "selection.c"

static int32
match_mask_state(uint32 mask, uint32 state) {
    if (mask == XK_ANY_MOD) {
        return 1;
    }
    if (mask == (state & ~CONF_IGNORE_MOD)) {
        return 1;
    }
    return 0;
}

static void
mouse_select(XEvent *xevent, int32 done) {
    enum SelectionType seltype = SELECTION_NORMAL;
    uint32 state = xevent->xbutton.state & ~(Button1Mask | CONF_FORCE_MOUSE_MOD);

    for (enum SelectionType type = 1; type < LENGTH(CONF_SELECTION_MASKS); type += 1) {
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

static void
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
        len = SNPRINTF(buffer, "\033[<%d;%d;%d%c",
                               code, x + 1, y + 1, c);
    } else if (x < 223 && y < 223) {
        len = SNPRINTF(buffer, "\033[M%c%c%c",
                               32 + code, 32 + x + 1, 32 + y + 1);
    } else {
        return;
    }

    tty_write(buffer, (int64)len, 0);
    return;
}

static uint32
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

static int32
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

#endif /* MOUSE_C */
