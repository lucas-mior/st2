#if !defined(MOUSE_C)
#define MOUSE_C

#include "st.h"
#include "config.h"
#include "selection.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_mouse 1
#elif !defined(TESTING_mouse)
#define TESTING_mouse 0
#endif

static int32
xevent_col(XEvent *xevent) {
    int32 x = xevent->xbutton.x - term_window.hborderpx;
    LIMIT(x, 0, term_window.tty_width - 1);
    return x / term_window.cw;
}

static int32
xevent_row(XEvent *xevent) {
    int32 y = xevent->xbutton.y - term_window.vborderpx;
    LIMIT(y, 0, term_window.tty_height - 1);
    return y / term_window.ch;
}

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
    int32 col;
    int32 row;

    /* 
     * Feature: Auto-scroll when dragging above or below the window.
     * We check the pixel coordinates directly against the borders.
     */
    if (!done && xevent->type == MotionNotify) {
        if (xevent->xbutton.y < term_window.vborderpx) {
            /* Above the window: Scroll up into history */
            user_scroll_up(&(union Arg){.i = 1});
        } else if (xevent->xbutton.y > term_window.vborderpx + term_window.tty_height) {
            /* Below the window: Scroll down toward active screen */
            user_scroll_down(&(union Arg){.i = 1});
        }
    }

    /* 
     * We calculate col and row AFTER potential scrolling to ensure 
     * the selection extension happens relative to the updated viewport.
     */
    col = xevent_col(xevent);
    row = xevent_row(xevent);

    for (enum SelectionType type = 1; type < LENGTH(CONF_SELECTION_MASKS); type += 1) {
        if (match_mask_state(CONF_SELECTION_MASKS[type], state)) {
            seltype = type;
            break;
        }
    }

    selection_extend(col, row, seltype, done);

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
        if (!term_window_is_set(WIN_MODE_MOUSEMOTION)
            && !term_window_is_set(WIN_MODE_MOUSEMANY)) {
            return;
        }
        /* WIN_MODE_MOUSEMOTION: no reporting if no button is pressed */
        if (term_window_is_set(WIN_MODE_MOUSEMOTION) && buttons == 0) {
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
            if (term_window_is_set(WIN_MODE_MOUSEX10)) {
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
    if (!term_window_is_set(WIN_MODE_MOUSESGR) && xevent->type == ButtonRelease) {
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

    if (!term_window_is_set(WIN_MODE_MOUSEX10)) {
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

    if (term_window_is_set(WIN_MODE_MOUSESGR)) {
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

#if TESTING_mouse

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "assert.c"
#include "st.c"
#include "user.c"
#include "tty.c"

int
main(void) {
    int32 pipefd[2];
    char captured_tty_buf[256];
    int64 bytes_read;
    int32 flags;
    XEvent ev;

    /*
     * We create a pipe and point the global 'command_fd' to the write end.
     * When mouse_report() calls the real tty_write(), it will write 
     * directly into our pipe via pselect/write64.
     */
    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        exit(EXIT_FAILURE);
    }
    command_fd = pipefd[1];

    /* Ensure the read end is non-blocking so we don't hang the test suite */
    flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    // Setup initial terminal state
    term_window.mode = 0; // Standard X10 mouse mode
    buttons = 0;
    
    // Setup terminal geometry so the real xevent_col/row functions work
    term_window.cw = 10;
    term_window.ch = 20;
    term_window.hborderpx = 5;
    term_window.vborderpx = 5;
    term_window.tty_width = 800;
    term_window.tty_height = 480;

    memset64(&ev, 0, SIZEOF(ev));
    ev.type = ButtonRelease;
    ev.xbutton.button = Button1;
    ev.xbutton.state = 0;

    // Test 1: Normal positive coordinates (Click at logical col 0, row 0)
    ev.xbutton.x = 5 + 0; // hborderpx + col * cw
    ev.xbutton.y = 5 + 0; // vborderpx + row * ch
    mouse_report(&ev);
    
    memset64(captured_tty_buf, 0, SIZEOF(captured_tty_buf));
    bytes_read = read(pipefd[0], captured_tty_buf, SIZEOF(captured_tty_buf) - 1);
    if (bytes_read < 0) {
        bytes_read = 0;
    }
    captured_tty_buf[bytes_read] = '\0';

    // Expected output: "\033[M#!!" 
    // 32 + code (3 for release) = 35 '#'
    // 32 + x + 1 = 33 '!'
    // 32 + y + 1 = 33 '!'
    ASSERT_EQUAL(captured_tty_buf, "\033[M#!!");

    // Test 2: Border Click Mitigation
    // A click at absolute 0,0 is inside the 5px border.
    // xevent->xbutton.x - hborderpx = -5. 
    // st.c's LIMIT macro clamps this to 0.
    ev.xbutton.x = 0; 
    ev.xbutton.y = 0;
    mouse_report(&ev);
    
    memset64(captured_tty_buf, 0, SIZEOF(captured_tty_buf));
    bytes_read = read(pipefd[0], captured_tty_buf, SIZEOF(captured_tty_buf) - 1);
    if (bytes_read < 0) {
        bytes_read = 0;
    }
    captured_tty_buf[bytes_read] = '\0';
    
    // Output should perfectly match col 0, row 0, proving no underflow injection
    ASSERT_EQUAL(captured_tty_buf, "\033[M#!!");

    XCLOSE(&pipefd[0]);
    XCLOSE(&pipefd[1]);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_mouse */

#endif /* MOUSE_C */
