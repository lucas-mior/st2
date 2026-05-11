#if !defined(RESIZE_C)
#define RESIZE_C

#include "st.h"
#include "config.h"
#include "selection.c"
#include "x.c"
#include "tty.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_resize 1
#elif !defined(TESTING_resize)
#define TESTING_resize 0
#endif

static void
term_resize(int32 new_ncols, int32 new_nrows) {
    bool *bp;

    ASSERT_MORE(new_ncols, 0);
    ASSERT_MORE(new_nrows, 0);

    term.dirts = realloc2(term.dirts, term.nrows, new_nrows, SIZEOF(*(term.dirts)));
    term.tabs = realloc2(term.tabs, term.ncols, new_ncols, SIZEOF(*(term.tabs)));
    
    if (new_ncols > term.ncols) {
        bp = term.tabs + term.ncols;
        memset64(bp, 0, SIZEOF(*term.tabs)*(new_ncols - term.ncols));
        bp -= 1;
        while (bp > term.tabs && !*bp) {
            bp -= 1;
        }
        for (bp += CONF_TAB_NSPACES; bp < term.tabs + new_ncols;
             bp += CONF_TAB_NSPACES) {
            *bp = true;
        }
    }

    if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        term_resize_alt(new_ncols, new_nrows);
    } else {
        term_resize_def(new_ncols, new_nrows);
    }
    return;
}

static void
term_resize_def(int32 new_ncols, int32 new_nrows) {
    int64 line_size = (int64)term.ncols*SIZEOF(StGlyph);

    if (term.ncols == new_ncols && term.nrows == new_nrows) {
        term_full_dirt();
        return;
    }
    if (new_ncols != term.ncols) {
        if (!selection.alt) {
            selection_remove();
        }
        term_reflow(new_ncols, new_nrows);
    } else {
        if (term.cursor.y >= new_nrows) {
            term_scroll_up(0, term.nrows - 1, term.cursor.y - new_nrows + 1,
                           SCROLL_RESIZE);
            term.cursor.y = new_nrows - 1;
        }
        for (int32 i = new_nrows; i < term.nrows; i += 1) {
            free2(term.lines[i], line_size);
        }

        term.lines = realloc2(term.lines, term.nrows, new_nrows, SIZEOF(*(term.lines)));

        for (int32 i = term.nrows; i < new_nrows; i += 1) {
            term.lines[i] = malloc2(new_ncols*SIZEOF(StGlyph));
            for (int32 j = 0; j < new_ncols; j += 1) {
                term_clear_glyph(&term.lines[i][j], false);
            }
        }
        reflow_scroll_down(new_nrows - term.nrows);
    }
    term.ncols = new_ncols;
    term.nrows = new_nrows;
    term.top_scroll_limit = 0;
    term.bot_scroll_limit = new_nrows - 1;
    term_full_dirt();
    return;
}

static void
term_resize_alt(int32 new_ncols, int32 new_nrows) {
    int32 shift = 0;
    int64 old_line_size = (int64)term.ncols*SIZEOF(StGlyph);

    if ((term.ncols == new_ncols) && (term.nrows == new_nrows)) {
        term_full_dirt();
        return;
    }
    if (selection.alt) {
        selection_remove();
    }

    /* Only shift if shrinking and the cursor would be pushed off the top */
    if (new_nrows < term.nrows && term.cursor.y >= new_nrows) {
        while (shift <= term.cursor.y - new_nrows) {
            free2(term.lines[shift], old_line_size);
            shift += 1;
        }
    }

    if (shift > 0) {
        /* Move the remaining pointers that fit in the new height */
        int32 to_move = (int32)MIN(new_nrows, term.nrows - shift);
        memmove64(term.lines, term.lines + shift, to_move*SIZEOF(*(term.lines)));
        term.cursor.y = new_nrows - 1;
    }

    for (int32 i = (int32)MAX(new_nrows, shift + new_nrows); i < term.nrows; i += 1) {
        free2(term.lines[i], old_line_size);
    }
    
    term.lines = realloc2(term.lines, term.nrows, new_nrows, SIZEOF(*(term.lines)));

    for (int32 j = 0; j < (int32)MIN(new_nrows, term.nrows); j += 1) {
        term.lines[j] = realloc2(term.lines[j], term.ncols, new_ncols, SIZEOF(*(term.lines[j])));
        for (int32 k = term.ncols; k < new_ncols; k += 1) {
            term_clear_glyph(&term.lines[j][k], false);
        }
    }

    for (int32 j = (int32)MIN(new_nrows, term.nrows); j < new_nrows; j += 1) {
        term.lines[j] = malloc2(new_ncols*SIZEOF(StGlyph));
        for (int32 k = 0; k < new_ncols; k += 1) {
            term_clear_glyph(&term.lines[j][k], false);
        }
    }

    if (term.cursor.x >= new_ncols) {
        term.cursor.state &= ~CURSOR_WRAPNEXT;
        term.cursor.x = new_ncols - 1;
    } else {
        update_wrap_next(1, new_ncols);
    }

    term.ncols = new_ncols;
    term.nrows = new_nrows;
    term.top_scroll_limit = 0;
    term.bot_scroll_limit = new_nrows - 1;

    ImageList *im_alt = term.images;
    while (im_alt != NULL) {
        ImageList *next_im = im_alt->next;

        if (im_alt->x >= term.ncols) {
            sixel_image_delete(im_alt);
            im_alt = next_im;
            continue;
        }

        if (im_alt->y >= term.nrows) {
            sixel_image_delete(im_alt);
            im_alt = next_im;
            continue;
        }

        if (im_alt->y < 0) {
            sixel_image_delete(im_alt);
            im_alt = next_im;
            continue;
        }

        {
            int32 new_cols = im_alt->x + im_alt->cols;
            if (term.ncols < new_cols) {
                new_cols = term.ncols;
            }
            im_alt->cols = new_cols - im_alt->x;
        }

        if (im_alt->cols <= 0) {
            sixel_image_delete(im_alt);
        }

        im_alt = next_im;
    }

    term_full_dirt();
    return;
}

static void
term_reflow(int32 new_ncols, int32 new_nrows) {
    int32 old_nrows = term.nrows;
    int32 old_ncols = term.ncols;
    int32 last_used_line = term.cursor.y;
    int32 capacity = 0;
    bool was_at_bottom = false;
    StGlyph **ref_lines = NULL;

    int32 old_y_idx = -term.n_hist;
    int32 new_y_idx = -1;
    int32 old_x_off = 0;
    int32 new_x_off = 0;
    int32 new_cursor_y_proxy = -1;
    int32 new_view_proxy = -1;
    int32 new_active_proxy = -1;

    int32 space_in_new;
    int32 chars_in_old;
    int32 step;

    int32 total_reflowed_count;
    int32 screen_top_idx;
    int32 desired_screen_top;

    int64 old_line_size = (int64)old_ncols*SIZEOF(StGlyph);
    int64 new_line_size = (int64)new_ncols*SIZEOF(StGlyph);

    ASSERT_MORE(new_ncols, 0);
    ASSERT_MORE(new_nrows, 0);

    if (term.scrolled_up == 0) {
        was_at_bottom = true;
    }

    #define OFFSET_OLD 1000000
    #define OFFSET_REF 2000000

    {
        ImageList *im_init = term.images;
        while (im_init != NULL) {
            im_init->y += OFFSET_OLD;
            im_init = im_init->next;
        }
    }

    for (int32 i = old_nrows - 1; i > term.cursor.y; i -= 1) {
        if (term_line_len(term.lines[i]) > 0) {
            last_used_line = i;
            break;
        }
    }
    
    {
        ImageList *im_bound = term.images;
        while (im_bound != NULL) {
            int32 actual_im_y = im_bound->y - OFFSET_OLD;
            if (actual_im_y > last_used_line) {
                if (actual_im_y < old_nrows) {
                    last_used_line = actual_im_y;
                }
            }
            im_bound = im_bound->next;
        }
    }

    {
        for (int32 i = -term.n_hist; i <= last_used_line; i += 1) {
            StGlyph *line = term_line_abs(i);
            int32 len = term_line_len(line);
            if (len <= 0) {
                capacity += 1;
            } else {
                capacity += (len + new_ncols - 1) / new_ncols;
            }
        }
        capacity += 2;
        ref_lines = malloc2(capacity*SIZEOF(StGlyph *));
        memset64(ref_lines, 0, capacity*SIZEOF(StGlyph *));
    }

    while (old_y_idx <= last_used_line) {
        StGlyph *line = term_line_abs(old_y_idx);
        int32 len = term_line_len(line);

        if (new_x_off == 0) {
            new_y_idx += 1;
            ref_lines[new_y_idx] = malloc2(new_ncols*SIZEOF(StGlyph));
            for (int32 j = 0; j < new_ncols; j += 1) {
                term_clear_glyph(&ref_lines[new_y_idx][j], false);
            }
        }

        if (old_x_off == 0) {
            ImageList *im_map = term.images;
            while (im_map != NULL) {
                if (im_map->y == old_y_idx + OFFSET_OLD) {
                    im_map->y = new_y_idx + OFFSET_REF;
                }
                im_map = im_map->next;
            }
            if (old_y_idx == -term.scrolled_up) {
                if (new_view_proxy < 0) {
                    new_view_proxy = new_y_idx;
                }
            }
            if (old_y_idx == 0) {
                if (new_active_proxy < 0) {
                    new_active_proxy = new_y_idx;
                }
            }
        }

        space_in_new = new_ncols - new_x_off;
        chars_in_old = len - old_x_off;
        step = (int32)MIN(chars_in_old, space_in_new);

        if (old_y_idx == term.cursor.y) {
            if (new_cursor_y_proxy < 0) {
                int32 rel_x = term.cursor.x - old_x_off;
                int32 space_left = new_ncols - new_x_off;
                if (rel_x < space_left) {
                    term.cursor.x = new_x_off + rel_x;
                    new_cursor_y_proxy = new_y_idx;
                } else if (rel_x == space_left) {
                    term.cursor.x = new_ncols - 1;
                    term.cursor.state |= CURSOR_WRAPNEXT;
                    new_cursor_y_proxy = new_y_idx;
                } else if (old_x_off + step >= len) {
                    term.cursor.x = new_x_off + step;
                    new_cursor_y_proxy = new_y_idx;
                }
            }
        }

        if (step > 0) {
            memcpy64(&ref_lines[new_y_idx][new_x_off], &line[old_x_off], step*SIZEOF(StGlyph));
            new_x_off += step;
            old_x_off += step;
        }

        if (old_x_off >= len) {
            if (len == 0) {
                new_x_off = 0;
            } else if (!(line[len - 1].mode & ATTR_WRAP)) {
                new_x_off = 0;
            } else if (new_x_off > 0) {
                ref_lines[new_y_idx][new_x_off - 1].mode &= ~ATTR_WRAP;
            }
            old_x_off = 0;
            old_y_idx += 1;
        } else {
            ref_lines[new_y_idx][new_ncols - 1].mode |= ATTR_WRAP;
            new_x_off = 0;
        }
    }

    if (new_cursor_y_proxy < 0) {
        new_cursor_y_proxy = new_y_idx;
        term.cursor.x = 0;
    }

    total_reflowed_count = new_y_idx + 1;

    /* Goal: Preserve the top of the active buffer */
    if (new_active_proxy >= 0) {
        desired_screen_top = new_active_proxy;
    } else {
        desired_screen_top = 0;
    }

    /* Constraint 1: Prevent array underflow */
    if (desired_screen_top < 0) {
        desired_screen_top = 0;
    }

    /* Constraint 2: Prevent empty space at the bottom if history is available */
    {
        int32 last_data_y = total_reflowed_count - 1;
        int32 old_empty = (old_nrows - 1) - last_used_line;
        if (old_empty < 0) {
            old_empty = 0;
        }

        if (desired_screen_top + new_nrows > last_data_y + 1) {
            int32 new_empty = (desired_screen_top + new_nrows) - (last_data_y + 1);
            if (new_empty > old_empty) {
                desired_screen_top -= (new_empty - old_empty);
                if (desired_screen_top < 0) {
                    desired_screen_top = 0;
                }
            }
        }
    }

    /* Constraint 3: Prevent the cursor from being pushed off the bottom */
    if (new_cursor_y_proxy >= desired_screen_top + new_nrows) {
        desired_screen_top = new_cursor_y_proxy - (new_nrows - 1);
    }

    screen_top_idx = desired_screen_top;
    term.cursor.y = new_cursor_y_proxy - screen_top_idx;

    for (int32 i = 0; i < old_nrows; i += 1) {
        free2(term.lines[i], old_line_size);
    }
    term.lines = realloc2(term.lines, old_nrows, new_nrows, SIZEOF(StGlyph *));

    for (int32 i = 0; i < new_nrows; i += 1) {
        int32 buffer_idx = screen_top_idx + i;
        if (buffer_idx < total_reflowed_count) {
            term.lines[i] = ref_lines[buffer_idx];
            ref_lines[buffer_idx] = NULL;
        } else {
            term.lines[i] = malloc2(new_ncols*SIZEOF(StGlyph));
            for (int32 j = 0; j < new_ncols; j += 1) {
                term_clear_glyph(&term.lines[i][j], false);
            }
        }
    }

    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        free2(term.hist[i], old_line_size);
    }

    {
        int32 history_to_keep = (int32)MIN(screen_top_idx, HISTORY_SIZE);
        int32 history_start_idx = screen_top_idx - history_to_keep;
        term.n_hist = history_to_keep;
        if (history_to_keep > 0) {
            term.i_hist = history_to_keep - 1;
        } else {
            term.i_hist = HISTORY_SIZE - 1;
        }

        for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
            if (i < history_to_keep) {
                term.hist[i] = ref_lines[history_start_idx + i];
                ref_lines[history_start_idx + i] = NULL;
            } else {
                term.hist[i] = malloc2(new_ncols*SIZEOF(StGlyph));
                for (int32 j = 0; j < new_ncols; j += 1) {
                    term_clear_glyph(&term.hist[i][j], false);
                }
            }
        }
    }

    {
        ImageList *im_final = term.images;
        while (im_final != NULL) {
            ImageList *next_im = im_final->next;

            if (im_final->y >= OFFSET_REF) {
                im_final->y = im_final->y - OFFSET_REF - screen_top_idx;
            } else {
                if (im_final->y >= OFFSET_OLD) {
                    im_final->y -= OFFSET_OLD;
                }
            }

            if (im_final->y < -term.n_hist) {
                sixel_image_delete(im_final);
                im_final = next_im;
                continue;
            }

            if (im_final->y >= new_nrows) {
                sixel_image_delete(im_final);
                im_final = next_im;
                continue;
            }

            int32 limit_x = im_final->x + im_final->cols;
            if (limit_x > new_ncols) {
                limit_x = new_ncols;
            }

            StGlyph *line = term_line_abs(im_final->y);
            for (int32 i = im_final->x; i < limit_x; i += 1) {
                if ((line[i].mode & ATTR_SET) == 0) {
                    line[i].mode |= ATTR_SIXEL;
                }
            }

            im_final = next_im;
        }
    }

    if (was_at_bottom == true) {
        term.scrolled_up = 0;
    } else if (new_view_proxy >= 0) {
        int32 diff = screen_top_idx - new_view_proxy;
        term.scrolled_up = (int32)MAX(0, MIN(diff, term.n_hist));
    } else {
        term.scrolled_up = 0;
    }

    for (int32 i = 0; i < total_reflowed_count; i += 1) {
        if (ref_lines[i]) {
            free2(ref_lines[i], new_line_size);
        }
    }
    free2(ref_lines, (int64)capacity*SIZEOF(StGlyph *));

    term.nrows = new_nrows;
    term.ncols = new_ncols;
    return;
}

static void
x_configure_resize(int32 new_width, int32 new_height) {
    int32 new_ncols;
    int32 new_nrows;
    int32 old_ncols;

    if (new_width != 0) {
        term_window.w = new_width;
    }
    if (new_height != 0) {
        term_window.h = new_height;
    }

    new_ncols = (term_window.w - 2*CONF_BORDER_PIXELS) / term_window.cw;
    new_nrows = (term_window.h - 2*CONF_BORDER_PIXELS) / term_window.ch;
    new_ncols = (int32)MAX(1, new_ncols);
    new_nrows = (int32)MAX(1, new_nrows);

    term_window.hborderpx = (term_window.w - new_ncols*term_window.cw) / 2;
    term_window.vborderpx = (term_window.h - new_nrows*term_window.ch) / 2;

    old_ncols = term.ncols;
    term_resize(new_ncols, new_nrows);
    x_resize(new_ncols, new_nrows, old_ncols);
    tty_resize(term_window.tty_width, term_window.tty_height);
    return;
}

#if TESTING_resize

#include <stdbool.h>
#include <stdlib.h>

#include "assert.c"
#include "user.c"
#include "st.c"

int
main(void) {
	ASSERT(true);
	exit(EXIT_SUCCESS);
}

#endif /* TESTING_resize */

#endif /* RESIZE_C */
