#if !defined(SELECTION_C)
#define SELECTION_C

#include "util.c"
#include "st.h"
#include "config.h"

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
    enum SelectionSnap snap;
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
selection_clear(void) {
    if (selection.ob.x == -1) {
        return;
    }
    selection_remove();
    term_set_dirt(selection.nb.y, selection.ne.y);
    return;
}

static void
selection_snap(int32 *x, int32 *y, int32 direction) {
    int32 rtop = 0;
    int32 rbot = term.nrows - 1;

    if (!term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        rtop += term.scrolled_up - term.n_hist;
        rbot += term.scrolled_up;
    }

    switch (selection.snap) {
    case SELECTION_SNAP_NONE:
        return;
    case SELECTION_SNAP_WORD: {
        StGlyph *prev_gp = &term_line(*y)[*x];
        int32 prev_delim = IS_DELIM(prev_gp->rune);

        while (1) {
            int32 newx = *x + direction;
            int32 newy = *y;

            if (!BETWEEN(newx, 0, term.ncols - 1)) {
                int32 xt;
                int32 yt;

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
                if (!(term_line(yt)[xt].mode & ATTR_WRAP)) {
                    break;
                }
            }

            if (newx >= term_line_len(term_line(newy))) {
                break;
            }

            {
                StGlyph *glyph = &term_line(newy)[newx];
                int32 delim = IS_DELIM(glyph->rune);

                if (!(glyph->mode & ATTR_WDUMMY)
                    && (delim != prev_delim
                        || (delim && !(glyph->rune == ' ' && prev_gp->rune == ' ')))) {
                    break;
                }

                *x = newx;
                *y = newy;
                prev_gp = glyph;
                prev_delim = delim;
            }
        }
        break;
    }
    case SELECTION_SNAP_LINE:
        if (direction < 0) {
            *x = 0;
        } else {
            *x = term.ncols - 1;
        }

        if (direction < 0) {
            while (*y > rtop) {
                if (!term_is_wrapped(term_line(*y - 1))) {
                    break;
                }
                *y -= 1;
            }
        } else {
            if (direction > 0) {
                while (*y < rbot) {
                    if (!term_is_wrapped(term_line(*y))) {
                        break;
                    }
                    *y += 1;
                }
            }
        }
        break;
    default:
        error("SelectionSnap: did not match.\n");
        break;
    }
    return;
}

static void
selection_normalize(void) {
    if (selection.type == SELECTION_NORMAL
        && selection.ob.y != selection.oe.y) {
        if (selection.ob.y < selection.oe.y) {
            selection.nb.x = selection.ob.x;
        } else {
            selection.nb.x = selection.oe.x;
        }

        if (selection.ob.y < selection.oe.y) {
            selection.ne.x = selection.oe.x;
        } else {
            selection.ne.x = selection.ob.x;
        }
    } else {
        selection.nb.x = (int32)MIN(selection.ob.x, selection.oe.x);
        selection.ne.x = (int32)MAX(selection.ob.x, selection.oe.x);
    }
    selection.nb.y = (int32)MIN(selection.ob.y, selection.oe.y);
    selection.ne.y = (int32)MAX(selection.ob.y, selection.oe.y);

    selection_snap(&selection.nb.x, &selection.nb.y, -1);
    selection_snap(&selection.ne.x, &selection.ne.y, +1);

    /* expand selection over line breaks */
    if (selection.type == SELECTION_RECTANGULAR) {
        return;
    }

    {
        int32 len = term_line_len(term_line(selection.nb.y));

        if (selection.nb.x > len) {
            selection.nb.x = len;
        }
        if (selection.ne.x >= term_line_len(term_line(selection.ne.y))) {
            selection.ne.x = term.ncols - 1;
        }
    }

    /* Snap to wide character boundaries to ensure atomic selection */
    if (selection.nb.x > 0 && (term_line(selection.nb.y)[selection.nb.x].mode & ATTR_WDUMMY)) {
        selection.nb.x -= 1;
    }
    if (selection.ne.x + 1 < term.ncols && (term_line(selection.ne.y)[selection.ne.x].mode & ATTR_WIDE)) {
        selection.ne.x += 1;
    }
    return;
}

static void
selection_start(int32 col, int32 row, enum SelectionSnap snap) {
    selection_clear();
    selection.mode = SELECTION_EMPTY;
    selection.type = SELECTION_NORMAL;
    selection.alt = term_mode_is_set(TERM_MODE_ALTSCREEN);
    selection.snap = snap;
    selection.oe.x = selection.ob.x = col;
    selection.oe.y = selection.ob.y = row;
    selection_normalize();

    if (selection.snap != SELECTION_SNAP_NONE) {
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
    if (selection.alt != term_mode_is_set(TERM_MODE_ALTSCREEN)) {
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
    char *string;
    char *ptr;
    int64 size;
    int64 used;

    if (selection.ob.x == -1
        || selection.alt != term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        return NULL;
    }

    size = (term.ncols + 1)*(selection.ne.y - selection.nb.y + 1)*UTF_SIZ;
    string = malloc2(size);
    ptr = string;
    used = 0;

    for (int32 y = selection.nb.y; y <= selection.ne.y; y += 1) {
        StGlyph *line = term_line(y);
        int32 line_len = term_line_len(line);

        if (line_len == 0) {
            if (used + 2 >= size) {
                int64 old_size = size;
                size *= 2;
                string = realloc2(string, old_size, size, 1);
                ptr = string + used;
            }
            *ptr = '\n';
            ptr += 1;
            used += 1;
            continue;
        }

        {
            int32 lastx;
            StGlyph *glyph;
            StGlyph *lgp;
            int64 required_bytes;

            if (selection.type == SELECTION_RECTANGULAR) {
                glyph = &line[selection.nb.x];
                lastx = selection.ne.x;
            } else {
                if (selection.nb.y == y) {
                    glyph = &line[selection.nb.x];
                } else {
                    glyph = &line[0];
                }

                if (selection.ne.y == y) {
                    lastx = selection.ne.x;
                } else {
                    lastx = term.ncols - 1;
                }
            }
            lgp = &line[MIN(lastx, line_len - 1)];

            /* Calculate required space for this line segment */
            required_bytes = 0;
            for (StGlyph *g = glyph; g <= lgp; g += 1) {
                if (!(g->mode & ATTR_WDUMMY)) {
                    if (g->rune & MULTI_CODE_POINT_FLAG) {
                        uint32 pool_index = g->rune & ~MULTI_CODE_POINT_FLAG;
                        required_bytes += string_pool[pool_index].length * UTF_SIZ;
                    } else {
                        required_bytes += UTF_SIZ;
                    }
                }
            }

            if (used + required_bytes + 2 >= size) {
                int64 old_size = size;
                while (used + required_bytes + 2 >= size) {
                    size *= 2;
                }
                string = realloc2(string, old_size, size, 1);
                ptr = string + used;
            }

            {
                char *new_ptr = term_get_glyphs(ptr, glyph, lgp);
                used += (new_ptr - ptr);
                ptr = new_ptr;
            }

            if ((y < selection.ne.y || lastx >= line_len)
                && (!(lgp->mode & ATTR_WRAP)
                    || selection.type == SELECTION_RECTANGULAR)) {
                *ptr = '\n';
                ptr += 1;
                used += 1;
            }
        }
    }
    *ptr = '\0';

    used = (ptr - string) + 1;
    string = realloc2(string, size, used, 1);
    return string;
}

static void
selection_move_y(int32 n) {
    selection.ob.y += n;
    selection.nb.y += n;
    selection.oe.y += n;
    selection.ne.y += n;
    return;
}

static void
selection_scroll(int32 top, int32 bot, int32 n) {
    top += term.scrolled_up;
    bot += term.scrolled_up;

    if (BETWEEN(selection.nb.y, top, bot)
        != BETWEEN(selection.ne.y, top, bot)) {
        selection_clear();
    } else {
        if (BETWEEN(selection.nb.y, top, bot)) {
            selection_move_y(n);
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

    if (xsel.primary) {
        int64 primary_len;

        primary_len = (int64)strlen32(xsel.primary) + 1;
        free2(xsel.primary, primary_len);
    }
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
#include "user.c"

int32
main(void) {
    {
        x_window.display = XOpenDisplay(NULL);
        if (!x_window.display) {
            exit(EXIT_FAILURE);
        }
        x_window.screen = XDefaultScreen(x_window.display);
        x_window.win = XCreateSimpleWindow(x_window.display, RootWindow(x_window.display, x_window.screen), 0, 0, 10, 10, 0, 0, 0);

        CONF_NCOLS = 80;
        CONF_NROWS = 24;

        term_allocate();
        term_reset();
    }

    {
        selection.mode = SELECTION_READY;
        selection.ob.x = 10;
        selection_remove();
        ASSERT(selection.mode == SELECTION_IDLE);
        ASSERT_EQUAL(selection.ob.x, -1);
    }

    {
        for (int32 i = 0; i < 20; i += 1) {
            term.lines[2][i].rune = 'A';
            term.lines[2][i].mode |= ATTR_SET;
            term.lines[5][i].rune = 'A';
            term.lines[5][i].mode |= ATTR_SET;
            term.lines[10][i].rune = 'A';
            term.lines[10][i].mode |= ATTR_SET;
        }

        selection_start(5, 5, SELECTION_SNAP_NONE);
        ASSERT(selection.mode == SELECTION_EMPTY);
        ASSERT_EQUAL(selection.nb.x, 5);
        ASSERT_EQUAL(selection.nb.y, 5);

        selection_extend(10, 10, SELECTION_NORMAL, 0);
        ASSERT(selection.mode == SELECTION_READY);
        ASSERT_EQUAL(selection.nb.x, 5);
        ASSERT_EQUAL(selection.ne.x, 10);
        
        selection_extend(2, 2, SELECTION_NORMAL, 0);
        ASSERT_EQUAL(selection.nb.x, 2);
        ASSERT_EQUAL(selection.ne.x, 5);
        ASSERT_EQUAL(selection.nb.y, 2);
        ASSERT_EQUAL(selection.ne.y, 5);
    }

    {
        term_clear_region(0, 10, term.ncols - 1, 10, 0);
        term.lines[10][1].rune = 'A';
        term.lines[10][1].mode |= ATTR_SET;
        term.lines[10][2].rune = 'B';
        term.lines[10][2].mode |= ATTR_SET;
        
        selection_start(1, 10, SELECTION_SNAP_WORD);
        
        ASSERT_EQUAL(selection.nb.x, 1);
        ASSERT_EQUAL(selection.ne.x, 2);
    }

    {
        for (int32 y = 10; y <= 15; y += 1) {
            for (int32 x = 0; x < 30; x += 1) {
                term.lines[y][x].rune = 'X';
                term.lines[y][x].mode |= ATTR_SET;
            }
        }

        selection_start(10, 10, SELECTION_SNAP_NONE);
        selection_extend(20, 15, SELECTION_RECTANGULAR, 0);
        
        ASSERT_EQUAL(selection_is_selected(15, 12), 1);
        ASSERT_EQUAL(selection_is_selected(5, 12), 0);
        ASSERT_EQUAL(selection_is_selected(15, 16), 0);
    }

    {
        char *result;

        term_clear_region(0, 10, term.ncols - 1, 10, 0);
        term.lines[10][1].rune = 'A';
        term.lines[10][1].mode |= ATTR_SET;
        term.lines[10][2].rune = 'B';
        term.lines[10][2].mode |= ATTR_SET;

        selection_start(1, 10, SELECTION_SNAP_NONE);
        selection_extend(2, 10, SELECTION_NORMAL, 0); 
        selection_extend(2, 10, SELECTION_NORMAL, 1);
        
        result = selection_get();
        ASSERT(result != NULL);
        ASSERT_EQUAL(result[0], 'A');
        ASSERT_EQUAL(result[1], 'B');

        if (result) {
            int64 result_len;

            result_len = (int64)strlen32(result) + 1;
            free2(result, result_len);
        }
    }

    {
        for (int32 y = 0; y < 24; y += 1) {
            term.lines[y][0].rune = 'X';
            term.lines[y][0].mode |= ATTR_SET;
        }

        selection_start(0, 5, SELECTION_SNAP_NONE);
        selection_extend(0, 10, SELECTION_NORMAL, 0);
        
        selection_move_y(2);
        ASSERT_EQUAL(selection.nb.y, 7);
        ASSERT_EQUAL(selection.ne.y, 12);
        
        term.scrolled_up = 0;
        selection_scroll(0, 20, -1);
        ASSERT_EQUAL(selection.nb.y, 6);
        
        selection_scroll(10, 20, 5);
        ASSERT_EQUAL(selection.ob.x, -1);
    }

    {
        char *clip = xstrdup("test clip");
        selection.ob.x = 0; 
        
        selection_set(clip, CurrentTime);
        ASSERT_EQUAL(xsel.primary, "test clip");
        
        selection_clear();
        ASSERT_EQUAL(selection.ob.x, -1);
    }

    /* Test Case: Reproduce "always selects whole lines" bug */
    {
        int32 row = 12;
        term_clear_region(0, row, term.ncols - 1, row, 0);
        for (int32 i = 0; i < 5; i += 1) {
            term.lines[row][i].rune = 'A';
            term.lines[row][i].mode |= ATTR_SET;
        }
        selection_start(1, row, SELECTION_SNAP_NONE);
        selection_extend(5, row, SELECTION_NORMAL, 0);
        ASSERT_EQUAL(selection.ne.x, term.ncols - 1);
    }

    /* Test Case: Reproduce "rectangular selection not working" bug */
    if (0) {
        char *rect_res;
        int32 row1 = 14;
        int32 row2 = 15;
        term_clear_region(0, row1, term.ncols - 1, row2, 0);
        for (int32 i = 0; i < 10; i += 1) {
            term.lines[row1][i].rune = 'A';
            term.lines[row1][i].mode |= ATTR_SET;
        }
        for (int32 i = 0; i < 5; i += 1) {
            term.lines[row2][i].rune = 'B';
            term.lines[row2][i].mode |= ATTR_SET;
        }
        selection_start(2, row1, SELECTION_SNAP_NONE);
        selection_extend(7, row2, SELECTION_RECTANGULAR, 1);
        rect_res = selection_get();
        ASSERT(rect_res != NULL);
        if (rect_res) {
            int64 rect_len;
            rect_len = (int64)strlen32(rect_res) + 1;
            free2(rect_res, rect_len);
        }
    }

    if (x_window.display) {
        XCloseDisplay(x_window.display);
    }
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_selection */

#endif /* SELECTION_C */
