#if !defined(SELECTION_C)
#define SELECTION_C

#include "util.c"
#include "st.h"
#include "config.def.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_selection 1
#elif !defined(TESTING_selection)
#define TESTING_selection 0
#endif

enum SelectionMode {
    SELECTION_IDLE = 0,
    SELECTION_EMPTY = 1,
    SELECTION_READY = 2
};

static struct {
    enum SelectionMode mode;
    enum SelectionType type;
    int32 snap;
    /*
     * Selection variables:
     * nb – normalized coordinates of the beginning of the selection
     * ne – normalized coordinates of the end of the selection
     * ob – original coordinates of the beginning of the selection
     * oe – original coordinates of the end of the selection
     */
    struct {
        int32 x;
        int32 y;
    } nb, ne, ob, oe;

    uint32 alt;
} selection;

static void
selection_remove(void) {
    selection.mode = SELECTION_IDLE;
    selection.ob.x = -1;
    return;
}

static void
SelectionSnap(int32 *x, int32 *y, int32 direction) {
    int32 newx;
    int32 newy;
    int32 xt;
    int32 yt;
    int32 rtop = 0;
    int32 rbot = term.nrows - 1;
    int32 delim;
    StGlyph *gp;

    if (!TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        rtop += term.lines_scrolled_up - term.n_hist;
        rbot += term.lines_scrolled_up;
    }

    switch (selection.snap) {
    case 0:
        return;
    case SELECTION_SNAP_WORD: {
        /*
         * Snap around if the word wraps around at the end or
         * beginning of a line.
         */
        StGlyph *prev_gp = &TERM_LINE(*y)[*x];
        int32 prev_delim = IS_DELIM(prev_gp->rune);

        while (1) {
            newx = *x + direction;
            newy = *y;
            if (!BETWEEN(newx, 0, term.ncols - 1)) {
                newy += direction;
                newx = (newx + term.ncols) % term.ncols;
                if (!BETWEEN(newy, rtop, rbot)) {
                    break;
                }

                if (direction > 0) {
                    yt = *y;
                    xt = *x;
                } else {
                    yt = newy;
                    xt = newx;
                }
                if (!(TERM_LINE(yt)[xt].mode & ATTR_WRAP)) {
                    break;
                }
            }

            if (newx >= term_line_len(TERM_LINE(newy))) {
                break;
            }

            gp = &TERM_LINE(newy)[newx];
            delim = IS_DELIM(gp->rune);
            if (!(gp->mode & ATTR_WDUMMY)
                && (delim != prev_delim
                    || (delim && !(gp->rune == ' ' && prev_gp->rune == ' ')))) {
                break;
            }

            *x = newx;
            *y = newy;
            prev_gp = gp;
            prev_delim = delim;
        }
        break;
                                      }
    case SELECTION_SNAP_LINE:
        /*
         * Snap around if the the previous line or the current one
         * has set ATTR_WRAP at its end. Then the whole next or
         * previous line will be selection_is_selected.
         */
        *x = (direction < 0) ? 0 : term.ncols - 1;
        if (direction < 0) {
            for (; *y > rtop; *y -= 1) {
                if (!term_is_wrapped(TERM_LINE(*y - 1))) {
                    break;
                }
            }
        } else if (direction > 0) {
            for (; *y < rbot; *y += 1) {
                if (!term_is_wrapped(TERM_LINE(*y))) {
                    break;
                }
            }
        }
        break;
    default:
        fprintf(stderr, "SelectionSnap: did not match.\n");
        break;
    }
    return;
}

static void
selection_normalize(void) {
    int32 len;

    if (selection.type == SELECTION_NORMAL
        && selection.ob.y != selection.oe.y) {
        selection.nb.x
            = selection.ob.y < selection.oe.y ? selection.ob.x : selection.oe.x;
        selection.ne.x
            = selection.ob.y < selection.oe.y ? selection.oe.x : selection.ob.x;
    } else {
        selection.nb.x = (int32)MIN(selection.ob.x, selection.oe.x);
        selection.ne.x = (int32)MAX(selection.ob.x, selection.oe.x);
    }
    selection.nb.y = (int32)MIN(selection.ob.y, selection.oe.y);
    selection.ne.y = (int32)MAX(selection.ob.y, selection.oe.y);

    SelectionSnap(&selection.nb.x, &selection.nb.y, -1);
    SelectionSnap(&selection.ne.x, &selection.ne.y, +1);

    /* expand selection over line breaks */
    if (selection.type == SELECTION_RECTANGULAR) {
        return;
    }

    len = term_line_len(TERM_LINE(selection.nb.y));
    if (selection.nb.x > len) {
        selection.nb.x = len;
    }
    if (selection.ne.x >= term_line_len(TERM_LINE(selection.ne.y))) {
        selection.ne.x = term.ncols - 1;
    }
    return;
}

static void
selection_start(int32 col, int32 row, int32 snap) {
    selection_clear();
    selection.mode = SELECTION_EMPTY;
    selection.type = SELECTION_NORMAL;
    selection.alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);
    selection.snap = snap;
    selection.oe.x = selection.ob.x = col;
    selection.oe.y = selection.ob.y = row;
    selection_normalize();

    if (selection.snap != 0) {
        selection.mode = SELECTION_READY;
    }
    term_set_dirt(selection.nb.y, selection.ne.y);
    return;
}

static void
selection_extend(int32 col, int32 row, enum SelectionType type, int32 done) {
    int32 oldey;
    int32 oldex;
    int32 oldsby;
    int32 oldsey;
    enum SelectionType oldtype;

    if (selection.mode == SELECTION_IDLE) {
        return;
    }
    if (done && selection.mode == SELECTION_EMPTY) {
        selection_clear();
        return;
    }

    oldey = selection.oe.y;
    oldex = selection.oe.x;
    oldsby = selection.nb.y;
    oldsey = selection.ne.y;
    oldtype = selection.type;

    selection.oe.x = col;
    selection.oe.y = row;
    selection.type = type;
    selection_normalize();

    if (oldey != selection.oe.y || oldex != selection.oe.x
        || oldtype != selection.type || selection.mode == SELECTION_EMPTY) {
        term_set_dirt((int32)MIN(selection.nb.y, oldsby),
                      (int32)MAX(selection.ne.y, oldsey));
    }

    if (done) {
        selection.mode = SELECTION_IDLE;
    } else {
        selection.mode = SELECTION_READY;
    }
    return;
}

static int32
selection_is_selected4(int32 x1, int32 y1, int32 x2, int32 y2) {
    int32 is_selected;

    if (selection.ob.x == -1) {
        return 0;
    }
    if (selection.mode == SELECTION_EMPTY) {
        return 0;
    }
    if (selection.alt != TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        return 0;
    }

    if (selection.nb.y > y2) {
        return 0;
    }
    if (selection.ne.y < y1) {
        return 0;
    }
    if (selection.type == SELECTION_RECTANGULAR) {
        is_selected = selection.nb.x <= x2 && selection.ne.x >= x1;
    } else {
        is_selected = (selection.nb.y != y2 || selection.nb.x <= x2)
                      && (selection.ne.y != y1 || selection.ne.x >= x1);
    }
    return is_selected;
}

static int32
selection_is_selected(int32 x, int32 y) {
    return selection_is_selected4(x, y, x, y);
}

static char *
selection_get(void) {
    char *string, *ptr;
    int32 lastx;
    int32 line_len;

    if (selection.ob.x == -1
        || selection.alt != TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        return NULL;
    }

    string
        = xmalloc((int64)((term.ncols + 1)
                          * (selection.ne.y - selection.nb.y + 1)*UTF_SIZ));
    ptr = string;

    /* append every set & selection_is_selected glyph to the selection */
    for (int32 y = selection.nb.y; y <= selection.ne.y; y += 1) {
        StGlyph *line = TERM_LINE(y);
        StGlyph *gp;
        StGlyph *lgp;

        if ((line_len = term_line_len(line)) == 0) {
            *ptr++ = '\n';
            continue;
        }

        if (selection.type == SELECTION_RECTANGULAR) {
            gp = &line[selection.nb.x];
            lastx = selection.ne.x;
        } else {
            gp = &line[selection.nb.y == y ? selection.nb.x : 0];
            lastx = (selection.ne.y == y) ? selection.ne.x : term.ncols - 1;
        }
        lgp = &line[MIN(lastx, line_len - 1)];

        ptr = term_get_glyphs(ptr, gp, lgp);

        /*
         * Copy and pasting of line endings
         * is inconsistent in the inconsistent terminal and GUI world.
         * The best solution seems like to produce '\n'
         * when something is copied from st
         * and convert '\n' to '\r'
         * when something to be pasted is received by st.
         */
        if ((y < selection.ne.y || lastx >= line_len)
            && (!(lgp->mode & ATTR_WRAP)
                || selection.type == SELECTION_RECTANGULAR)) {
            *ptr++ = '\n';
        }
    }
    *ptr = '\0';
    return string;
}

static void
selection_clear(void) {
    if (selection.ob.x == -1) {
        return;
    }
    selection_remove();
    term_set_dirt(selection.nb.y, selection.ne.y);
    return;
}

static void
selection_move(int32 n) {
    selection.ob.y += n;
    selection.nb.y += n;
    selection.oe.y += n;
    selection.ne.y += n;
    return;
}

static void
selection_scroll(int32 top, int32 bot, int32 n) {
    /* turn absolute coordinates into relative */
    top += term.lines_scrolled_up;
    bot += term.lines_scrolled_up;

    if (BETWEEN(selection.nb.y, top, bot)
        != BETWEEN(selection.ne.y, top, bot)) {
        selection_clear();
    } else {
        if (BETWEEN(selection.nb.y, top, bot)) {
            selection_move(n);
            if (selection.nb.y < top || selection.ne.y > bot) {
                selection_clear();
            }
        }
    }
    return;
}

static void
selection_set(char *string, Time t) {
    if (!string) {
        return;
    }

    free(xsel.primary);
    xsel.primary = string;

    XSetSelectionOwner(x_window.display, XA_PRIMARY, x_window.win, t);
    if (XGetSelectionOwner(x_window.display, XA_PRIMARY) != x_window.win) {
        selection_clear();
    }
    return;
}

#if TESTING_selection

#include <stdbool.h>
#include <stdlib.h>
#include <X11/Xlib.h>

#include "assert.c"
#include "st.c"

int32
main(void) {
    /* 1. Environment and State Initialization */
    {
        x_window.display = XOpenDisplay(NULL);
        if (!x_window.display) {
            exit(EXIT_FAILURE);
        }
        x_window.screen = XDefaultScreen(x_window.display);
        x_window.win = XCreateSimpleWindow(x_window.display, RootWindow(x_window.display, x_window.screen), 0, 0, 10, 10, 0, 0, 0);

        CONF_NUMBER_COLS = 80;
        CONF_NUMBER_ROWS = 24;
        term.ncols = CONF_NUMBER_COLS;
        term.nrows = CONF_NUMBER_ROWS;

        term.dirty = xmalloc(term.nrows * SIZEOF(*term.dirty));
        term.tabs = xmalloc(term.ncols * SIZEOF(*term.tabs));
        term.lines = xmalloc(term.nrows * SIZEOF(*term.lines));
        for (int32 j = 0; j < term.nrows; j += 1) {
            term.lines[j] = xmalloc(term.ncols * SIZEOF(*term.lines[j]));
        }
        
        term_reset();
    }

    /* 2. selection_remove and basics */
    {
        selection.mode = SELECTION_READY;
        selection.ob.x = 10;
        selection_remove();
        ASSERT(selection.mode == SELECTION_IDLE);
        ASSERT_EQUAL(selection.ob.x, -1);
    }

    /* 3. selection_start and selection_normalize */
    {
        /* Populate lines with enough glyphs so line length != 0 */
        for (int32 i = 0; i < 20; i += 1) {
            term.lines[2][i].rune = 'A';
            term.lines[2][i].mode |= ATTR_SET;
            term.lines[5][i].rune = 'A';
            term.lines[5][i].mode |= ATTR_SET;
            term.lines[10][i].rune = 'A';
            term.lines[10][i].mode |= ATTR_SET;
        }

        /* Start at (5, 5) */
        selection_start(5, 5, 0);
        ASSERT(selection.mode == SELECTION_EMPTY);
        ASSERT_EQUAL(selection.nb.x, 5);
        ASSERT_EQUAL(selection.nb.y, 5);

        /* Extend to (10, 10) */
        selection_extend(10, 10, SELECTION_NORMAL, 0);
        ASSERT(selection.mode == SELECTION_READY);
        ASSERT_EQUAL(selection.nb.x, 5);
        ASSERT_EQUAL(selection.ne.x, 10);
        
        /* Swap: Extend to (2, 2) which is before start */
        selection_extend(2, 2, SELECTION_NORMAL, 0);
        ASSERT_EQUAL(selection.nb.x, 2);
        ASSERT_EQUAL(selection.ne.x, 5);
        ASSERT_EQUAL(selection.nb.y, 2);
        ASSERT_EQUAL(selection.ne.y, 5);
    }

    /* 4. SelectionSnap: Word Mode */
    {
        /* [space][A][B][space] */
        term_clear_region(0, 10, term.ncols - 1, 10, 0);
        term.lines[10][1].rune = 'A';
        term.lines[10][1].mode |= ATTR_SET;
        term.lines[10][2].rune = 'B';
        term.lines[10][2].mode |= ATTR_SET;
        
        selection_start(1, 10, SELECTION_SNAP_WORD);
        
        ASSERT_EQUAL(selection.nb.x, 1);
        ASSERT_EQUAL(selection.ne.x, 2);
    }

    /* 5. selection_is_selected and Rectangular Mode */
    {
        for (int32 y = 10; y <= 15; y += 1) {
            for (int32 x = 0; x < 30; x += 1) {
                term.lines[y][x].rune = 'X';
                term.lines[y][x].mode |= ATTR_SET;
            }
        }

        selection_start(10, 10, 0);
        selection_extend(20, 15, SELECTION_RECTANGULAR, 0);
        
        ASSERT_EQUAL(selection_is_selected(15, 12), 1);
        ASSERT_EQUAL(selection_is_selected(5, 12), 0);
        ASSERT_EQUAL(selection_is_selected(15, 16), 0);
    }

    /* 6. selection_get */
    {
        char *result;

        term_clear_region(0, 10, term.ncols - 1, 10, 0);
        term.lines[10][1].rune = 'A';
        term.lines[10][1].mode |= ATTR_SET;
        term.lines[10][2].rune = 'B';
        term.lines[10][2].mode |= ATTR_SET;

        selection_start(1, 10, 0);
        selection_extend(2, 10, SELECTION_NORMAL, 1);
        
        result = selection_get();
        ASSERT(result != NULL);
        ASSERT_EQUAL(result[0], 'A');
        ASSERT_EQUAL(result[1], 'B');
        free(result);
    }

    /* 7. selection_move and selection_scroll */
    {
        for (int32 y = 0; y < 24; y += 1) {
            term.lines[y][0].rune = 'X';
            term.lines[y][0].mode |= ATTR_SET;
        }

        selection_start(0, 5, 0);
        selection_extend(0, 10, SELECTION_NORMAL, 0);
        
        selection_move(2);
        ASSERT_EQUAL(selection.nb.y, 7);
        ASSERT_EQUAL(selection.ne.y, 12);
        
        term.lines_scrolled_up = 0;
        selection_scroll(0, 20, -1);
        ASSERT_EQUAL(selection.nb.y, 6);
        
        selection_scroll(10, 20, 5);
        ASSERT_EQUAL(selection.ob.x, -1);
    }

    /* 8. selection_set and selection_clear */
    {
        char *clip = xstrdup("test clip");
        selection.ob.x = 0; 
        
        selection_set(clip, CurrentTime);
        ASSERT_EQUAL(xsel.primary, "test clip");
        
        selection_clear();
        ASSERT_EQUAL(selection.ob.x, -1);
    }

    XCloseDisplay(x_window.display);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_selection */

#endif /* SELECTION_C */
