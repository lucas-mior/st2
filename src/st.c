#if !defined(ST_C)
#define ST_C

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <assert.h>

#include "st.h"
#include "boxdraw.c"
#include "handlers.c"
#include "selection.c"
#include "sixel.c"
#include "tty.c"
#include "utf8.c"
#include "escape.c"
#include "arg.h"
#include "mouse.c"
#include "base64.c"
#include "st_util.c"
#include "resize.c"

#if defined(__linux)
#include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <libutil.h>
#endif

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_st 1
#elif !defined(TESTING_st)
#define TESTING_st 0
#endif

static void
check_consistent_state(void) {
    /* 1. Basic Geometry and Core Buffers */
    ASSERT_MORE(term.nrows, 0);
    ASSERT_MORE(term.ncols, 0);
    ASSERT_LESS(term.nrows, MAX_NROWS);
    ASSERT_LESS(term.ncols, MAX_NCOLS);
    ASSERT(term.lines);
    ASSERT(term.tabs);
    ASSERT(term.dirts);

    for (int32 i = 0; i < term.nrows; i += 1) {
        ASSERT(term.lines[i]);
    }

    /* 2. History Integrity and Row Uniqueness */
    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        ASSERT(term.hist[i]);
        /* Ensure history rows aren't cross-linked with active screen rows */
        for (int32 j = 0; j < term.nrows; j += 1) {
            ASSERT(term.hist[i] != term.lines[j]);
        }
    }

    ASSERT_MORE_EQUAL(term.n_hist, 0);
    ASSERT_LESS_EQUAL(term.n_hist, HISTORY_SIZE);

    ASSERT_MORE_EQUAL(term.i_hist, 0);
    ASSERT_LESS(term.i_hist, HISTORY_SIZE);

    /* 3. Scrolling and Viewport Logic */
    ASSERT_MORE_EQUAL(term.scrolled_up, 0);
    ASSERT_LESS_EQUAL(term.scrolled_up, term.n_hist);

    ASSERT_MORE_EQUAL(term.top_scroll_limit, 0);
    ASSERT_LESS_EQUAL(term.top_scroll_limit, term.bot_scroll_limit);

    ASSERT_MORE_EQUAL(term.bot_scroll_limit, 0);
    ASSERT_LESS(term.bot_scroll_limit, term.nrows);

    /* 4. Cursor and Ghost Cursor Sanity */
    ASSERT_MORE_EQUAL(term.cursor.x, 0);
    ASSERT_LESS(term.cursor.x, term.ncols);
    ASSERT_MORE_EQUAL(term.cursor.y, 0);
    ASSERT_LESS(term.cursor.y, term.nrows);

    ASSERT_MORE_EQUAL(term.old_cursor_x, 0);
    ASSERT_LESS(term.old_cursor_x, term.ncols);
    ASSERT_MORE_EQUAL(term.old_cursor_y, 0);
    ASSERT_LESS(term.old_cursor_y, term.nrows);

    /* 5. Selection Invariants */
    if (selection.ob.x != -1) {
        /* 
         * In this coordinate system:
         * - The oldest history line: term.scrolled_up - term.n_hist
         * - The bottom visible screen line: term.scrolled_up + term.nrows - 1
         */
        int32 min_y = term.scrolled_up - term.n_hist;
        int32 max_y = term.scrolled_up + term.nrows;

        /* If we are in AltScreen, history is logically unreachable. */
        if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
            min_y = 0;
            max_y = term.nrows;
        }

        /* Check X bounds */
        ASSERT_MORE_EQUAL(selection.nb.x, 0);
        ASSERT_LESS(selection.nb.x, term.ncols);
        ASSERT_MORE_EQUAL(selection.ne.x, 0);
        ASSERT_LESS(selection.ne.x, term.ncols);

        /* Check Y bounds against history + screen range */
        ASSERT_MORE_EQUAL(selection.nb.y, min_y);
        ASSERT_LESS(selection.nb.y, max_y);
        ASSERT_MORE_EQUAL(selection.ne.y, min_y);
        ASSERT_LESS(selection.ne.y, max_y);
        
        /* Ensure selection endpoints are correctly ordered */
        ASSERT_LESS_EQUAL(selection.nb.y, selection.ne.y);
        if (selection.nb.y == selection.ne.y) {
            ASSERT_LESS_EQUAL(selection.nb.x, selection.ne.x);
        }
    }

    /* 6. Charset and Metadata */
    ASSERT_MORE_EQUAL(term.charset, 0);
    ASSERT_LESS(term.charset, 4); /* CS_USA to CS_GRAPHIC1 */

    /* 7. Image List Consistency (Doubly Linked) */
    {
        ImageList *im = term.images;
        int32 safety_limit = 0;
        while (im) {
            /* Detect infinite loops/cycles */
            ASSERT_LESS(safety_limit, 10000); 
            if (im->next) {
                ASSERT(im->next->prev == im);
            }
            im = im->next;
            safety_limit += 1;
        }
    }

    memory_check();

    return;
}

static void
term_allocate(void) {
    for (int32 i = 0; i < 2; i += 1) {
        term.lines = malloc2(CONF_NROWS*SIZEOF(*(term.lines)));
        for (int32 j = 0; j < CONF_NROWS; j += 1) {
            term.lines[j] = malloc2(CONF_NCOLS*SIZEOF(*(term.lines[j])));
            memset64(term.lines[j], 0, CONF_NCOLS*SIZEOF(*(term.lines[j])));
        }
        term.ncols = CONF_NCOLS;
        term.nrows = CONF_NROWS;
        term_swap_screen();
    }

    term.dirts = malloc2(CONF_NROWS*SIZEOF(*term.dirts));
    term.tabs = malloc2(CONF_NCOLS*SIZEOF(*term.tabs));

    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        term.hist[i] = malloc2(CONF_NCOLS*SIZEOF(StGlyph));
        for (int32 j = 0; j < CONF_NCOLS; j += 1) {
            term_clear_glyph(&term.hist[i][j], false);
        }
    }

    return;
}

static int32
term_line_len(StGlyph *line) {
    int32 i = term.ncols - 1;

    while ((i >= 0) && !(line[i].mode & (ATTR_SET | ATTR_WRAP))) {
        i -= 1;
    }

    return i + 1;
}

static bool
term_mode_is_set(enum TermMode flag) {
    return !(!(term.mode & flag));
}

static bool
win_mode_is_set(enum WinMode flag) {
    return !(!(term_window.mode & flag));
}

static StGlyph *
term_line(int32 y) {
    StGlyph *line_ptr = NULL;
    int32 idx;

    if (y < term.scrolled_up) {
        idx = (term.i_hist + y - term.scrolled_up + 1 + HISTORY_SIZE) % HISTORY_SIZE;
        line_ptr = term.hist[idx];
    } else {
        idx = y - term.scrolled_up;
        line_ptr = term.lines[idx];
    }

    return line_ptr;
}

static StGlyph *
term_line_abs(int32 y) {
    StGlyph *line_ptr = NULL;

    if (y < 0) {
        int32 hist_index = (term.i_hist + y + 1 + HISTORY_SIZE) % HISTORY_SIZE;
        line_ptr = term.hist[hist_index];
    } else {
        line_ptr = term.lines[y];
    }

    return line_ptr;
}

static StGlyph *
term_line_hist(int32 y) {
    StGlyph *line_ptr = NULL;

    if (y <= HISTORY_SIZE - term.nrows + 2) {
        line_ptr = term.hist[y];
    } else {
        int32 lines_index = y - HISTORY_SIZE + term.nrows - 3;
        line_ptr = term.lines[lines_index];
    }

    return line_ptr;
}

static void
update_wrap_next(int32 alt, int32 col) {
    if ((term.cursor.state & CURSOR_WRAPNEXT) 
        && term.cursor.x + term.wrap_char_width[alt] < col) {
        term.cursor.x += term.wrap_char_width[alt];
        term.cursor.state &= ~CURSOR_WRAPNEXT;
    }
    
    return;
}

static bool
term_is_wrapped(StGlyph *line) {
    int32 len = term_line_len(line);
    bool wrapped = false;

    if (len > 0) {
        if (line[len - 1].mode & ATTR_WRAP) {
            wrapped = true;
        }
    }

    return wrapped;
}

static char *
term_get_glyphs(char *buffer, StGlyph *glyph, StGlyph *lgp) {
    while (glyph <= lgp) {
        if (glyph->mode & ATTR_WDUMMY) {
            glyph += 1;
            continue;
        }

        if (glyph->rune & MULTI_CODE_POINT_FLAG) {
            uint32 pool_index = glyph->rune & ~MULTI_CODE_POINT_FLAG;
            for (int32 i = 0; i < string_pool[pool_index].length; i += 1) {
                buffer += utf8_encode(string_pool[pool_index].runes[i], buffer);
            }
        } else {
            buffer += utf8_encode(glyph->rune, buffer);
        }
        glyph += 1;
    }
    return buffer;
}

static void
term_set_sixel_attr(StGlyph *line, int x1, int x2) {
    while (x1 <= x2) {
        line[x1].mode |= ATTR_SIXEL;
        x1 += 1;
    }
    return;
}

static bool
term_attr_set(enum GlyphAttribute attr) {
    for (int32 i = 0; i < term.nrows - 1; i += 1) {
        for (int32 j = 0; j < term.ncols - 1; j += 1) {
            if (term.lines[i][j].mode & attr) {
                return true;
            }
        }
    }

    return false;
}

static void
term_set_dirt(int32 top, int32 bot) {
    LIMIT(top, 0, term.nrows - 1);
    LIMIT(bot, 0, term.nrows - 1);

    for (int32 i = top; i <= bot; i += 1) {
        term.dirts[i] = true;
    }
    return;
}

static void
term_set_dirt_attr(enum GlyphAttribute attr) {
    for (int32 i = 0; i < term.nrows - 1; i += 1) {
        for (int32 j = 0; j < term.ncols - 1; j += 1) {
            if (term.lines[i][j].mode & attr) {
                term.dirts[i] = true;
                break;
            }
        }
    }
    return;
}

static void
term_full_dirt(void) {
	tsync_end();
    for (int32 i = 0; i < term.nrows; i += 1) {
        term.dirts[i] = true;
    }
    return;
}

static void
term_delete_images(void) {
    ImageList *next;

    for (ImageList *image = term.images; image; image = next) {
        next = image->next;
        sixel_image_delete(image);
    }
    
    term.images = NULL;
    
    return;
}

static void
term_reset(void) {
    ImageList *image = term.images;
    while (image) {
        ImageList *next = image->next;
        sixel_image_delete(image);
        image = next;
    }
    term.images = NULL;
    
    term.cursor.attr.mode = ATTR_NONE;
    term.cursor.attr.fg = CONF_COLOR_INDEX_FONT;
    term.cursor.attr.bg = CONF_COLOR_BG;
    term.cursor.x = 0;
    term.cursor.y = 0;
    term.cursor.state = CURSOR_DEFAULT;

    memset64(term.tabs, 0, term.ncols*SIZEOF(*term.tabs));
    for (int32 i = CONF_TAB_NSPACES; i < term.ncols; i += CONF_TAB_NSPACES) {
        term.tabs[i] = true;
    }
    term.top_scroll_limit = 0;
    term.n_hist = 0;
    term.scrolled_up = 0;
    term.bot_scroll_limit = term.nrows - 1;
    term.mode = TERM_MODE_WRAP | TERM_MODE_UTF8;
    memset64(term.translation_table, CS_USA, SIZEOF(term.translation_table));
    term.charset = 0;

    selection_remove();
    for (int32 i = 0; i < 2; i += 1) {
        term_cursor(CURSOR_SAVE); /* reset saved cursor */
        for (int32 y = 0; y < term.nrows; y += 1) {
            for (int32 x = 0; x < term.ncols; x += 1) {
                term_clear_glyph(&term.lines[y][x], false);
            }
        }
        term_delete_images();
        term_swap_screen();
    }
    term_full_dirt();
    return;
}

static void
term_swap_screen(void) {
    static StGlyph **alt_lines;
    static int32 alt_ncols;
    static int32 alt_nrows;

    StGlyph **tmp_line = term.lines;
    int32 tmp_ncols = term.ncols;
    int32 tmp_nrows = term.nrows;
    ImageList *tmpimages = term.images;

    term.lines = alt_lines;
    term.ncols = alt_ncols;
    term.nrows = alt_nrows;
    term.images = term.images_alt;

    alt_lines = tmp_line;
    alt_ncols = tmp_ncols;
    alt_nrows = tmp_nrows;
    term.images_alt = tmpimages;

    term.mode ^= TERM_MODE_ALTSCREEN;
    return;
}

static void
term_load_def_screen(bool clear, bool loadcursor) {
    int32 col = 0;
    int32 row = 0;
    int32 alt = term_mode_is_set(TERM_MODE_ALTSCREEN);

    if (alt) {
        if (clear) {
            term_clear_region(0, 0, term.ncols - 1, term.nrows - 1, true);
        }
        col = term.ncols;
        row = term.nrows;
        term_swap_screen();
    }
    if (loadcursor) {
        term_cursor(CURSOR_LOAD);
    }
    if (alt) {
        term_resize_def(col, row);
    }
    return;
}

static void
term_load_alt_screen(bool clear, bool savecursor) {
    int32 col;
    int32 row;
    int32 def = !term_mode_is_set(TERM_MODE_ALTSCREEN);

    if (savecursor) {
        term_cursor(CURSOR_SAVE);
    }
    if (def) {
        col = term.ncols;
        row = term.nrows;
        term_swap_screen();
        term.scrolled_up = 0;
        term_resize_alt(col, row);
    }
    if (clear) {
        term_clear_region(0, 0, term.ncols - 1, term.nrows - 1, true);
    }
    return;
}

static void
term_scroll_down(int32 top, int32 n) {
    int32 bot = term.bot_scroll_limit;

    if (n <= 0) {
        return;
    }
    n = (int32)MIN(n, bot - top + 1);

    term_set_dirt(top, bot - n);
    term_clear_region(0, bot - n + 1, term.ncols - 1, bot, true);

    for (int32 i = bot; i >= top + n; i -= 1) {
        StGlyph *temp = term.lines[i];
        term.lines[i] = term.lines[i - n];
        term.lines[i - n] = temp;
    }

    if ((selection.ob.x != -1)
        && (selection.alt == term_mode_is_set(TERM_MODE_ALTSCREEN))) {
        selection_scroll(top, bot, n);
    }

    {
        ImageList *image = term.images;
        while (image) {
            if (image->y >= top && image->y <= bot) {
                image->y += n;
            }
            image = image->next;
        }
    }
    return;
}

static void
term_scroll_up(int32 top, int32 bot, int32 n, enum ScrollMode mode) {
    int32 s = 0;
    uint32 alt = term_mode_is_set(TERM_MODE_ALTSCREEN);
    bool savehist = !alt && top == 0 && mode != SCROLL_NOSAVEHIST;

    if (n <= 0) {
        return;
    }
    n = (int32)MIN(n, bot - top + 1);

    if (savehist) {
        for (int32 i = 0; i < n; i += 1) {
            StGlyph *temp;
            term.i_hist = (term.i_hist + 1) % HISTORY_SIZE;
            temp = term.hist[term.i_hist];
            for (int32 j = 0; j < term.ncols; j += 1) {
                term_clear_glyph(&temp[j], true);
            }
            term.hist[term.i_hist] = term.lines[i];
            term.lines[i] = temp;
        }
        term.n_hist = (int32)MIN(term.n_hist + n, HISTORY_SIZE);
        s = n;
        if (term.scrolled_up) {
            int32 j = term.scrolled_up;
            term.scrolled_up = (int32)MIN(j + n, HISTORY_SIZE);
            s = j + n - term.scrolled_up;
        }
        if (mode != SCROLL_RESIZE) {
            term_full_dirt();
        }
    } else {
        term_clear_region(0, top, term.ncols - 1, top + n - 1, true);
        term_set_dirt(top + n, bot);
    }

    for (int32 i = top; i <= bot - n; i += 1) {
        StGlyph *temp = term.lines[i];
        term.lines[i] = term.lines[i + n];
        term.lines[i + n] = temp;
    }

    if (selection.ob.x != -1 && selection.alt == alt) {
        if (!savehist) {
            selection_scroll(top, bot, -n);
        } else {
            if (s > 0) {
                selection_move_y(-s);
                if (-term.scrolled_up + selection.nb.y < -term.n_hist) {
                    selection_remove();
                }
            }
        }
    }

    {
        ImageList **image_ptr = &term.images;
        while (*image_ptr) {
            ImageList *image = *image_ptr;
            int32 height_in_rows = (image->height + image->ch - 1) / image->ch;

            if (image->y <= bot && (image->y >= top || (savehist && image->y < 0))) {
                image->y -= n;
            }

            if (image->y + height_in_rows < -term.n_hist) {
                /* Explicitly unlink before freeing to satisfy the analyzer */
                *image_ptr = image->next;
                if (image->next) {
                    image->next->prev = image->prev;
                }
                sixel_image_free(image);
                /* Do not advance image_ptr;
                 * it now points to the new head of the remaining list */
            } else {
                image_ptr = &image->next;
            }
        }
    }
    return;
}

static void
term_new_line(bool first_col) {
    int32 y = term.cursor.y;
    int32 x_pos;

    if (y == term.bot_scroll_limit) {
        term_scroll_up(term.top_scroll_limit, term.bot_scroll_limit,
                       1, SCROLL_SAVEHIST);
    } else {
        y += 1;
    }
    
    if (first_col) {
        x_pos = 0;
    } else {
        x_pos = term.cursor.x;
    }
    
    term_move_to(x_pos, y);
    return;
}

/* for absolute user moves, when decom is set */
static void
term_move_abs_to(int32 x, int32 y) {
    int32 y_offset;
    if (term.cursor.state & CURSOR_ORIGIN) {
        y_offset = term.top_scroll_limit;
    } else {
        y_offset = 0;
    }

    term_move_to(x, y + y_offset);
    return;
}

static void
term_move_to(int32 x, int32 y) {
    int32 miny;
    int32 maxy;

    if (term.cursor.state & CURSOR_ORIGIN) {
        miny = term.top_scroll_limit;
        maxy = term.bot_scroll_limit;
    } else {
        miny = 0;
        maxy = term.nrows - 1;
    }
    term.cursor.state &= ~CURSOR_WRAPNEXT;
    term.cursor.x = LIMIT(x, 0, term.ncols - 1);
    term.cursor.y = LIMIT(y, miny, maxy);
    return;
}

static void
term_set_char(uint32 u, StGlyph *attr, int32 x, int32 y) {
    static char *vt100_0[62] = {
        /* 0x41 - 0x7e */
        "↑", "↓", "→", "←", "█", "▚", "☃",      /* A - G */
        0,   0,   0,   0,   0,   0,   0,   0,   /* H - O */
        0,   0,   0,   0,   0,   0,   0,   0,   /* P - W */
        0,   0,   0,   0,   0,   0,   0,   " ", /* X - _ */
        "◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
        "␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
        "⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
        "│", "≤", "≥", "π", "≠", "£", "·",      /* x - ~ */
    };

    /*
     * The table is proudly stolen from rxvt.
     */
    if ((term.translation_table[term.charset] == CS_GRAPHIC0)
        && BETWEEN(u, 0x41, 0x7e) && vt100_0[u - 0x41]) {
        utf8_decode(vt100_0[u - 0x41], &u, UTF_SIZ);
    }

    if (term.lines[y][x].mode & ATTR_WIDE) {
        if (x + 1 < term.ncols) {
            term.lines[y][x + 1].rune = ' ';
            term.lines[y][x + 1].mode &= ~ATTR_WDUMMY;
        }
    } else {
        if (term.lines[y][x].mode & ATTR_WDUMMY) {
            term.lines[y][x - 1].rune = ' ';
            term.lines[y][x - 1].mode &= ~ATTR_WIDE;
        }
    }

    if (term.lines[y][x].rune & MULTI_CODE_POINT_FLAG) {
        uint32 pool_index = term.lines[y][x].rune & ~MULTI_CODE_POINT_FLAG;
        string_pool[pool_index].length = 0;
    }

    term.dirts[y] = true;
    term.lines[y][x] = *attr;
    term.lines[y][x].rune = u;
    term.lines[y][x].mode |= ATTR_SET;

    if (isboxdraw(u)) {
        term.lines[y][x].mode |= ATTR_BOXDRAW;
    }
    return;
}

static void
term_clear_glyph(StGlyph *glyph, bool use_current_attr) {
    if (use_current_attr) {
        glyph->fg = term.cursor.attr.fg;
        glyph->bg = term.cursor.attr.bg;
    } else {
        glyph->fg = CONF_COLOR_INDEX_FONT;
        glyph->bg = CONF_COLOR_BG;
    }
    glyph->mode = ATTR_NONE;
    glyph->rune = ' ';
    return;
}

static void
term_clear_region(int32 x1, int32 y1, int32 x2, int32 y2,
                  bool use_current_attr) {
    /* selection_is_selected4() takes relative coordinates */
    if (selection_is_selected4(x1 + term.scrolled_up, y1 + term.scrolled_up,
                               x2 + term.scrolled_up, y2 + term.scrolled_up)) {
        selection_remove();
    }

    for (int32 y = y1; y <= y2; y += 1) {
        term.dirts[y] = true;
        for (int32 x = x1; x <= x2; x += 1) {
            term_clear_glyph(&term.lines[y][x], use_current_attr);
        }
    }
    return;
}

static void
term_delete_char(int32 n) {
    int32 src;
    int32 dst;
    int32 size;
    StGlyph *line;

    if (n <= 0) {
        return;
    }

    dst = term.cursor.x;
    src = (int32)MIN(term.cursor.x + n, term.ncols);
    size = term.ncols - src;
    if (size > 0) {
        /*
         * otherwise src would point beyond the array
         * https://stackoverflow.com/questions/29844298
         */
        line = term.lines[term.cursor.y];
        memmove64(&line[dst], &line[src], size*SIZEOF(StGlyph));
    }
    term_clear_region(dst + size, term.cursor.y,
                      term.ncols - 1, term.cursor.y, true);
    return;
}

static void
term_insert_blank(int32 n) {
    int32 src;
    int32 dst;
    int32 size;
    StGlyph *line;

    if (n <= 0) {
        return;
    }
    dst = (int32)MIN(term.cursor.x + n, term.ncols);
    src = term.cursor.x;
    size = term.ncols - dst;
    if (size > 0) { /* otherwise dst would point beyond the array */
        line = term.lines[term.cursor.y];
        memmove64(&line[dst], &line[src], size*SIZEOF(StGlyph));
    }
    term_clear_region(src, term.cursor.y, dst - 1, term.cursor.y, true);
    return;
}

static void
term_insert_blank_line(int32 n) {
    if (BETWEEN(term.cursor.y, term.top_scroll_limit, term.bot_scroll_limit)) {
        term_scroll_down(term.cursor.y, n);
    }
    return;
}

static void
term_delete_line(int32 n) {
    if (BETWEEN(term.cursor.y, term.top_scroll_limit, term.bot_scroll_limit)) {
        term_scroll_up(term.cursor.y, term.bot_scroll_limit,
                       n, SCROLL_NOSAVEHIST);
    }
    return;
}

static void
term_printer(char *s, int64 len) {
    if (io_fd != -1) {
        if (xwrite(io_fd, s, len) < 0) {
            perror("Error writing to output file");
            XCLOSE(&io_fd);
        }
    }
    return;
}

static void
term_dump_sel(void) {
    char *ptr;
    int64 len;

    if ((ptr = selection_get())) {
        len = (int64)strlen32(ptr);
        term_printer(ptr, len);
        free2(ptr, len + 1);
    }
    return;
}

static void
term_dump_line(int32 n) {
    int32 size = (term.ncols + 1)*UTF_SIZ*SIZEOF(char);
    char *string;
    char *buffer;
    StGlyph *fgp = &term.lines[n][0];
    StGlyph *lgp = &fgp[term.ncols - 1];
    char *ptr;
    int32 required_bytes;

    while (lgp > fgp && !(lgp->mode & (ATTR_SET | ATTR_WRAP))) {
        lgp -= 1;
    }

    /* Verify if the pre-calculated size can accommodate the string pool items */
    required_bytes = 0;
    for (StGlyph *g = fgp; g <= lgp; g += 1) {
        if (!(g->mode & ATTR_WDUMMY)) {
            if (g->rune & MULTI_CODE_POINT_FLAG) {
                uint32 pool_index = g->rune & ~MULTI_CODE_POINT_FLAG;
                required_bytes += string_pool[pool_index].length * UTF_SIZ;
            } else {
                required_bytes += UTF_SIZ;
            }
        }
    }

    if (required_bytes + 2 * UTF_SIZ > size) {
        size = required_bytes + 2 * UTF_SIZ;
    }

    string = malloc2(size);
    buffer = string;

    ptr = term_get_glyphs(buffer, fgp, lgp);

    if (!(lgp->mode & ATTR_WRAP)) {
        *ptr = '\n';
        ptr += 1;
    }

    term_printer(string, ptr - buffer);
    free2(string, size);
    return;
}

static void
term_dump(void) {
    for (int32 i = 0; i < term.nrows; i += 1) {
        term_dump_line(i);
    }
    return;
}

static void
reflow_scroll_down(int32 n) {
    int32 actual_n = (int32)MIN(n, term.n_hist);

    if (actual_n <= 0) {
        return;
    }

    /* 
     * Shift OLD screen lines down. 
     * We MUST use term.nrows (the old height) to ensure we move the 
     * entire contiguous block of pointers and avoid scrambling them.
     */
    for (int32 i = term.nrows - 1; i >= 0; i -= 1) {
        StGlyph *temp = term.lines[i + actual_n];
        term.lines[i + actual_n] = term.lines[i];
        term.lines[i] = temp;
    }

    /* Pull from history into the top */
    for (int32 i = actual_n - 1; i >= 0; i -= 1) {
        StGlyph *temp = term.lines[i];
        term.lines[i] = term.hist[term.i_hist];
        term.hist[term.i_hist] = temp;
        term.i_hist = (term.i_hist - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    }

    term.cursor.y += actual_n;
    term.n_hist -= actual_n;
    term.scrolled_up = (int32)MAX(0, term.scrolled_up - actual_n);

    {
        ImageList *im = term.images;
        while (im) {
            im->y += actual_n;
            im = im->next;
        }
    }
    return;
}

static void
draw(void) {
    int32 cx = term.cursor.x;
    int32 old_cursor_x = term.old_cursor_x;
    int32 old_cursor_y = term.old_cursor_y;

    if (!x_start_draw()) {
        return;
    }

    LIMIT(term.old_cursor_x, 0, term.ncols - 1);
    LIMIT(term.old_cursor_y, 0, term.nrows - 1);
    if (term.lines[term.old_cursor_y][term.old_cursor_x].mode & ATTR_WDUMMY) {
        if (term.old_cursor_x > 0) {
            term.old_cursor_x -= 1;
        }
    }
    if (term.lines[term.cursor.y][cx].mode & ATTR_WDUMMY) {
        if (cx > 0) {
            cx -= 1;
        }
    }

    for (int32 y = 0; y < term.nrows; y += 1) {
        if (!term.dirts[y]) {
            continue;
        }

        term.dirts[y] = false;
        x_draw_line(term_line(y), 0, y, term.ncols);
    }

    x_draw_cursor(cx, term.cursor.y, term.lines[term.cursor.y][cx],
                  term.old_cursor_x, term.old_cursor_y,
                  term.lines[term.old_cursor_y][term.old_cursor_x]);
    term.old_cursor_x = cx;
    term.old_cursor_y = term.cursor.y;

    /* x_finish_draw() */ {
        int32 bw = term_window.hborderpx;
        int32 bh = term_window.vborderpx;
        int32 term_h = term.nrows*term_window.ch;
        GC gc = NULL;
        ImageList *next;

        XSetClipMask(x_window.display, draw_context.graphics, None);

        for (ImageList *image = term.images; image; image = next) {
            int32 rel_y = image->y + term.scrolled_up;
            int32 height_in_rows = (image->height + image->ch - 1) / image->ch;
            int32 scaled_w;
            int32 scaled_h;
            next = image->next;

            /* Check if ANY part of the image is on the visible screen */
            if ((image->x >= term.ncols)
                    || (rel_y >= term.nrows)
                    || (rel_y + height_in_rows <= 0)) {
                continue;
            }

            /* 
             * Calculate the new dimensions based on current font cell size 
             * vs the font cell size when the image was generated.
             */
            scaled_w = (image->width*term_window.cw) / image->cw;
            scaled_h = (image->height*term_window.ch) / image->ch;
            scaled_w = (int32)MAX(scaled_w, 1);
            scaled_h = (int32)MAX(scaled_h, 1);

            if (image->pixmap == NULL) {
                uint32 *scaled_pixels;
                uint32 *src_pixels;
                XImage ximage;

                scaled_pixels = malloc2(scaled_w*scaled_h*SIZEOF(*scaled_pixels));
                src_pixels = (uint32 *)image->pixels;

                /* Fast Nearest-Neighbor scaling */
                for (int32 sy = 0; sy < scaled_h; sy += 1) {
                    int32 orig_y = (sy*image->height) / scaled_h;
                    for (int32 sx = 0; sx < scaled_w; sx += 1) {
                        int32 orig_x = (sx*image->width) / scaled_w;
                        scaled_pixels[sy*scaled_w + sx] = src_pixels[orig_y*image->width + orig_x];
                    }
                }

                image->pixmap = (void *)XCreatePixmap(x_window.display, x_window.win,
                                                      (uint32)scaled_w, (uint32)scaled_h,
                                                      (uint32)x_window.depth);

                if (image->transparent) {
                    image->clipmask = (void *)sixel_create_clipmask((char *)scaled_pixels,
                                                                     scaled_w, scaled_h);
                }

                ximage.format = ZPixmap;
                ximage.data = (char *)scaled_pixels;
                ximage.width = scaled_w;
                ximage.height = scaled_h;
                ximage.depth = x_window.depth;
                ximage.bits_per_pixel = 32;
                ximage.bytes_per_line = scaled_w*4;
                ximage.byte_order = ImageByteOrder(x_window.display);
                ximage.bitmap_unit = 32;
                ximage.bitmap_pad = 32;

                XPutImage(x_window.display, (Drawable)image->pixmap,
                          draw_context.graphics, &ximage, 0, 0, 0, 0, (uint32)scaled_w,
                          (uint32)scaled_h);

                free2(scaled_pixels, scaled_w*scaled_h*SIZEOF(*scaled_pixels));
            }

            if (gc == NULL) {
                XGCValues gcvalues;
                memset64(&gcvalues, 0, SIZEOF(gcvalues));
                gcvalues.graphics_exposures = False;
                gc = XCreateGC(x_window.display, x_window.win, GCGraphicsExposures, &gcvalues);
            }

            {
                int32 draw_w = scaled_w;
                int32 draw_h = scaled_h;
                int32 src_y = 0;
                int32 dest_y = bh + rel_y*term_window.ch;

                /* Clip the top if the image starts in history */
                if (dest_y < bh) {
                    src_y = bh - dest_y;
                    draw_h -= src_y;
                    dest_y = bh;
                }

                /* Clip the bottom if the image extends past the screen */
                if (dest_y + draw_h > bh + term_h) {
                    draw_h = (bh + term_h) - dest_y;
                }

                if (draw_h > 0 && draw_w > 0) {
                    bool is_deleted = true;
                    int32 x_limit;
                    int32 x_start;
                    StGlyph *line_ptr;

                    if (image->transparent && image->clipmask) {
                        /* Mask origin must track the logical position */
                        XSetClipOrigin(x_window.display, gc, bw + image->x*term_window.cw, bh + rel_y*term_window.ch);
                        XSetClipMask(x_window.display, gc, (Pixmap)image->clipmask);
                    } else {
                        XSetClipMask(x_window.display, gc, None);
                    }

                    x_limit = image->x + image->cols;
                    if (term.ncols < x_limit) {
                        x_limit = term.ncols;
                    }
                    
                    x_start = image->x;
                    line_ptr = term_line_abs(image->y) + image->x;

                    while (x_start < x_limit) {
                        int32 mode = line_ptr->mode & ATTR_SIXEL;
                        int32 x_end = x_start + 1;

                        while (x_end < x_limit) {
                            int32 next_mode;

                            line_ptr += 1;
                            next_mode = line_ptr->mode & ATTR_SIXEL;
                            if (next_mode != mode) {
                                break;
                            }
                            x_end += 1;
                        }

                        if (mode != 0) {
                            int32 src_x = (x_start - image->x) * term_window.cw;
                            int32 copy_w = (x_end - x_start) * term_window.cw;
                            int32 remaining_w = scaled_w - src_x;
                            int32 dest_x;

                            if (remaining_w < copy_w) {
                                copy_w = remaining_w;
                            }

                            dest_x = bw + (x_start * term_window.cw);

                            XCopyArea(x_window.display, (Drawable)image->pixmap, x_window.drawable, gc,
                                      src_x, src_y, (uint32)copy_w, (uint32)draw_h,
                                      dest_x, dest_y);
                            is_deleted = 0;
                        }

                        x_start = x_end;
                    }

                    if (image->clipmask) {
                        XSetClipMask(x_window.display, gc, None);
                    }

                    if (is_deleted != 0) {
                        if ((image->x + image->cols) <= term.ncols) {
                            sixel_image_delete(image);
                        }
                    }
                }
            }
        }

        if (gc) {
            XFreeGC(x_window.display, gc);
        }

        XCopyArea(x_window.display, x_window.drawable, x_window.win,
                  draw_context.graphics,
                  0, 0,
                  (uint32)term_window.w, (uint32)term_window.h,
                  0, 0);
    }

    if (old_cursor_x != term.old_cursor_x
        || old_cursor_y != term.old_cursor_y) {
        x_xim_spot(term.old_cursor_x, term.old_cursor_y);
    }

    return;
}

static void
redraw(void) {
    term_full_dirt();
    draw();
    return;
}

static void
term_cursor(enum CursorMovement mode) {
    static TCursor c[2];
    int32 alt = term_mode_is_set(TERM_MODE_ALTSCREEN);

    if (mode == CURSOR_SAVE) {
        c[alt] = term.cursor;
    } else {
        if (mode == CURSOR_LOAD) {
            term.cursor = c[alt];
            term_move_to(c[alt].x, c[alt].y);
        }
    }
    return;
}

#if TESTING_st

#include <stdbool.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <fcntl.h>
#include <unistd.h>

#include "assert.c"
#include "user.c"
#include "x.c"

int32
main(void) {
    x_window.display = XOpenDisplay(NULL);
    if (x_window.display) {
        XGCValues xgc_values;
        
        x_window.screen = XDefaultScreen(x_window.display);
        x_window.visual = DefaultVisual(x_window.display, x_window.screen);
        x_window.depth = DefaultDepth(x_window.display, x_window.screen);
        x_window.color_map = DefaultColormap(x_window.display, x_window.screen);
        x_window.win = XCreateSimpleWindow(x_window.display, RootWindow(x_window.display, x_window.screen), 0, 0, 100, 100, 0, 0, 0);
        
        memset64(&xgc_values, 0, SIZEOF(xgc_values));
        xgc_values.graphics_exposures = False;
        
        draw_context.graphics = XCreateGC(x_window.display, x_window.win, GCGraphicsExposures, &xgc_values);
        x_window.drawable = XCreatePixmap(x_window.display, x_window.win, 100, 100, (uint32)x_window.depth);
        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable, x_window.visual, x_window.color_map);
        
        /* Initialize Fontconfig, load standard fonts, and load colors */
        FcInit();
        used_font = CONF_FONT;
        x_load_fonts(used_font, 0);
        x_load_colors();
    }

    CONF_NCOLS = 10;
    CONF_NROWS = 5;
    term_allocate();
    term_reset();

    /* Test: term_line_len */
    {
        StGlyph line[10];
        int32 len;
        
        term.ncols = 10;
        for (int32 i = 0; i < 10; i += 1) { 
            line[i].mode = ATTR_NONE; 
        }
        len = term_line_len(line);
        ASSERT_EQUAL(len, 0);
        
        line[4].mode = ATTR_SET;
        len = term_line_len(line);
        ASSERT_EQUAL(len, 5);
    }

    /* Test: term_is_wrapped */
    {
        StGlyph line[10];
        bool wrapped;
        
        term.ncols = 10;
        for (int32 i = 0; i < 10; i += 1) { 
            line[i].mode = ATTR_NONE; 
        }
        
        line[4].mode = ATTR_SET;
        wrapped = term_is_wrapped(line);
        ASSERT_EQUAL((int32)wrapped, 0);
        
        line[4].mode = ATTR_SET | ATTR_WRAP;
        wrapped = term_is_wrapped(line);
        ASSERT_EQUAL((int32)wrapped, 1);
    }

    /* Test: term_get_glyphs */
    {
        StGlyph line[2];
        char buffer[10];
        char *ptr;
        
        line[0].rune = 'A';
        line[0].mode = ATTR_SET;
        line[1].rune = 'B';
        line[1].mode = ATTR_SET;
        
        ptr = term_get_glyphs(buffer, &line[0], &line[1]);
        *ptr = '\0';
        ASSERT(strcmp(buffer, "AB") == 0);
    }

    /* Test: term_set_sixel_attr */
    {
        StGlyph line[10];
        
        for (int32 i = 0; i < 10; i += 1) { 
            line[i].mode = ATTR_NONE; 
        }
        
        term_set_sixel_attr(line, 2, 5);
        ASSERT_MORE((int32)(line[2].mode & ATTR_SIXEL), 0);
        ASSERT_MORE((int32)(line[5].mode & ATTR_SIXEL), 0);
    }

    /* Test: term_attr_set */
    {
        bool res;
        
        term_reset();
        res = term_attr_set(ATTR_BOLD);
        ASSERT_EQUAL((int32)res, 0);
        
        term.lines[0][0].mode |= ATTR_BOLD;
        res = term_attr_set(ATTR_BOLD);
        ASSERT_EQUAL((int32)res, 1);
    }

    /* Test: term_set_dirt, term_set_dirt_attr, term_full_dirt */
    {
        term_full_dirt();
        ASSERT_EQUAL((int32)term.dirts[0], 1);
        
        term.dirts[0] = false;
        term_set_dirt(0, 0);
        ASSERT_EQUAL((int32)term.dirts[0], 1);
        
        term.dirts[0] = false;
        term.lines[0][0].mode |= ATTR_ITALIC;
        term_set_dirt_attr(ATTR_ITALIC);
        ASSERT_EQUAL((int32)term.dirts[0], 1);
    }

    /* Test: term_swap_screen */
    {
        enum TermMode prev_mode;
        
        prev_mode = term.mode & TERM_MODE_ALTSCREEN;
        term_swap_screen();
        ASSERT((term.mode & TERM_MODE_ALTSCREEN) != prev_mode);
        term_swap_screen();
    }

    /* Test: term_move_abs_to, term_move_to */
    {
        term_move_to(5, 2);
        ASSERT_EQUAL(term.cursor.x, 5);
        ASSERT_EQUAL(term.cursor.y, 2);
        
        term_move_abs_to(1, 1);
        ASSERT_EQUAL(term.cursor.x, 1);
        ASSERT_EQUAL(term.cursor.y, 1);
    }

    /* Test: term_set_char */
    {
        StGlyph attr_val;
        
        attr_val.mode = ATTR_BOLD;
        attr_val.fg = 1;
        attr_val.bg = 2;
        
        term_set_char('X', &attr_val, 0, 0);
        ASSERT_EQUAL(term.lines[0][0].rune, 'X');
        ASSERT_MORE((int32)(term.lines[0][0].mode & ATTR_BOLD), 0);
    }

    /* Test: term_delete_char, term_insert_blank */
    {
        term_reset();
        term.lines[0][0].rune = 'A';
        term.lines[0][0].mode |= ATTR_SET;
        term.lines[0][1].rune = 'B';
        term.lines[0][1].mode |= ATTR_SET;
        
        term_delete_char(1);
        ASSERT_EQUAL(term.lines[0][0].rune, 'B');
        
        term_insert_blank(1);
        ASSERT_EQUAL(term.lines[0][0].rune, ' ');
    }

    /* Test: term_mode_is_set & win_mode_is_set */
    {
        bool res;
        
        term.mode |= TERM_MODE_INSERT;
        res = term_mode_is_set(TERM_MODE_INSERT);
        ASSERT_EQUAL((int32)res, 1);
        
        term.mode &= ~TERM_MODE_WRAP;
        res = term_mode_is_set(TERM_MODE_WRAP);
        ASSERT_EQUAL((int32)res, 0);

        term_window.mode |= WIN_MODE_VISIBLE;
        res = win_mode_is_set(WIN_MODE_VISIBLE);
        ASSERT_EQUAL((int32)res, 1);
    }

    /* Test: term_line, term_line_abs, term_line_hist */
    {
        StGlyph *ptr;
        
        term_reset();
        term.lines[0][0].rune = 'A';
        term.hist[0][0].rune = 'H';
        term.scrolled_up = 1;
        term.i_hist = 0;
        
        ptr = term_line(1);
        ASSERT_EQUAL(ptr[0].rune, 'A');
        
        ptr = term_line_abs(-1);
        ASSERT_EQUAL(ptr[0].rune, 'H');
        
        ptr = term_line_hist(0);
        ASSERT(ptr != NULL);
    }

    /* Test: update_wrap_next */
    {
        term_reset();
        term.cursor.state |= CURSOR_WRAPNEXT;
        term.cursor.x = 0;
        term.wrap_char_width[1] = 1;
        
        update_wrap_next(1, 5);
        ASSERT_EQUAL(term.cursor.x, 1);
        ASSERT_EQUAL((int32)(term.cursor.state & CURSOR_WRAPNEXT), 0);
    }

    /* Test: term_delete_images */
    {
        ImageList *img;
        
        img = malloc2(SIZEOF(ImageList));
        memset64(img, 0, SIZEOF(ImageList));
        term.images = img;
        
        term_delete_images();
        ASSERT(term.images == NULL);
    }

    /* Test: term_clear_region */
    {
        term_reset();
        term.lines[1][1].rune = 'X';
        term.lines[1][1].mode = ATTR_SET;
        
        term_clear_region(0, 0, 2, 2, false);
        ASSERT_EQUAL(term.lines[1][1].rune, ' ');
    }

    /* Test: term_scroll_up and term_scroll_down */
    {
        term_reset();
        term.lines[1][0].rune = 'Y';
        
        term_scroll_up(0, 2, 1, SCROLL_NOSAVEHIST);
        ASSERT_EQUAL(term.lines[0][0].rune, 'Y');
        
        term_scroll_down(0, 1);
        ASSERT_EQUAL(term.lines[1][0].rune, 'Y');
        ASSERT_EQUAL(term.lines[0][0].rune, ' ');
    }

    /* Test: term_insert_blank_line and term_delete_line */
    {
        term_reset();
        term.cursor.y = 0;
        term.lines[0][0].rune = 'Z';
        
        term_insert_blank_line(1);
        ASSERT_EQUAL(term.lines[1][0].rune, 'Z');
        ASSERT_EQUAL(term.lines[0][0].rune, ' ');
        
        term.cursor.y = 0;
        term_delete_line(1);
        ASSERT_EQUAL(term.lines[0][0].rune, 'Z');
    }

    /* Test: reflow_scroll_down (via term_resize) */
    {
        term_reset();
        term.n_hist = 1;
        term.i_hist = 0;
        term.hist[0][0].rune = 'H';
        
        term_resize(10, 6); 
        ASSERT_EQUAL(term.lines[0][0].rune, 'H');
    }

    /* Test: resize, reflow, x_configure_resize */
    {
        term_reset();
        term_resize(20, 10);
        ASSERT_EQUAL(term.ncols, 20);
        ASSERT_EQUAL(term.nrows, 10);

        term_load_alt_screen(true, true);
        term_resize(15, 8);
        ASSERT_EQUAL(term.ncols, 15);
        ASSERT_EQUAL(term.nrows, 8);
        term_load_def_screen(true, true);
        
        /* Initialize mock character dimensions if font load failed */
        if (term_window.cw == 0 || term_window.ch == 0) {
            term_window.cw = 10;
            term_window.ch = 20;
        }
        
        x_configure_resize(400, 300);
        ASSERT_EQUAL(term_window.w, 400);
        ASSERT_EQUAL(term_window.h, 300);
    }

    /* Test: printer and dump functions */
    {
        int32 temp_fd;
        
        temp_fd = open("/dev/null", O_WRONLY);
        if (temp_fd >= 0) {
            io_fd = temp_fd;
            term_reset();
            term.lines[0][0].rune = 'D';
            term.lines[0][0].mode = ATTR_SET;
            
            term_dump_line(0);
            term_dump();
            
            selection.ob.x = 0;
            selection.ob.y = 0;
            selection.oe.x = 0;
            selection.oe.y = 0;
            selection.alt = false;
            
            term_dump_sel();
            
            close(temp_fd);
            io_fd = -1;
        }
    }

    /* Test: draw and redraw */
    {
        term_reset();
        term.dirts[0] = true;
        
        draw();
        ASSERT_EQUAL((int32)term.dirts[0], 0);
        
        redraw();
        ASSERT_EQUAL((int32)term.dirts[0], 0);
    }

    /* Test: check_consistent_state */
    {
        check_consistent_state();
    }

    /* Test: term_load_alt_screen and term_load_def_screen */
    {
        term_load_alt_screen(true, true);
        ASSERT_MORE((int32)(term.mode & TERM_MODE_ALTSCREEN), 0);
        
        term_load_def_screen(true, true);
        ASSERT_EQUAL((int32)(term.mode & TERM_MODE_ALTSCREEN), 0);
    }

    /* Test: term_new_line */
    {
        term_reset();
        term_new_line(true);
        ASSERT_EQUAL(term.cursor.y, 1);
        ASSERT_EQUAL(term.cursor.x, 0);
    }

    /* Test: term_clear_glyph */
    {
        StGlyph glyph_val;
        
        term_clear_glyph(&glyph_val, false);
        ASSERT_EQUAL((int32)glyph_val.mode, (int32)ATTR_NONE);
        ASSERT_EQUAL(glyph_val.rune, ' ');
        ASSERT_EQUAL(glyph_val.fg, CONF_COLOR_INDEX_FONT);
        ASSERT_EQUAL(glyph_val.bg, CONF_COLOR_BG);
    }

    if (x_window.display) {
        XCloseDisplay(x_window.display);
    }
    
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_st */

#endif /* ST_C */
