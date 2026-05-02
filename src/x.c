#if !defined(X_C)
#define X_C

#include "st.h"

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

    /* x_window.specbuf resize */
    x_window.specbuf
        = xrealloc(x_window.specbuf, (int64)col*SIZEOF(XftGlyphFontSpec));
    return;
}

#endif /* X_C */
