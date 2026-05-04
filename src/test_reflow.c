#include <stdlib.h>

#include "st.c"
#include "user.c"
#include "util.c"

static void
verify_viewport_line(int32 screen_y, char *expected_text) {
    StGlyph *line = term_line(screen_y);
    int32 len = term_line_len(line);
    char buffer[1024];
    char *ptr = buffer;

    if (len > 0) {
        ptr = term_get_glyphs(buffer, &line[0], &line[len - 1]);
    }
    *ptr = '\0';

    if (strcmp(buffer, expected_text) != 0) {
        error("Viewport assertion failed at screen row %d:\n", screen_y);
        error("  Expected: '%s'\n", expected_text);
        error("  Actual:   '%s'\n", buffer);
        assert(false);
    }

    return;
}

static void
inject_text(char *text) {
    StGlyph attr;

    attr.mode = ATTR_NONE;
    attr.fg = 7;
    attr.bg = 0;

    for (int32 i = 0; text[i] != '\0'; i += 1) {
        if (text[i] == '\n') {
            term_new_line(true);
            continue;
        }

        if (term.cursor.state & CURSOR_WRAPNEXT) {
            /* FIX: ATTR_WRAP must be set on the LAST column (ncols - 1), not ncols */
            term.lines[term.cursor.y][term.ncols - 1].mode |= ATTR_WRAP;
            term_new_line(true);
        }

        term_set_char((uint32)text[i], &attr, term.cursor.x, term.cursor.y);

        if (term.cursor.x + 1 < term.ncols) {
            term.cursor.x += 1;
        } else {
            term.cursor.state |= CURSOR_WRAPNEXT;
        }
    }

    return;
}

static void
verify_full_state(int32 expected_count, char **expected_texts, bool *expected_wraps, int32 expected_cx, int32 expected_cy) {
    bool is_alt = term_mode_is_set(TERM_MODE_ALTSCREEN);
    int32 active_hist = 0;
    int32 total_lines = 0;

    if (is_alt) {
        active_hist = 0;
    } else {
        active_hist = term.n_hist;
    }

    total_lines = active_hist + term.nrows;

    if (total_lines != expected_count) {
        error("Total lines mismatch. Expected: %d, Actual: %d (Hist: %d, Rows: %d)\n",
                expected_count, total_lines, active_hist, term.nrows);
        assert(false);
    }

    /* Assert Cursor Tracking if specified */
    if (expected_cx >= 0 && expected_cy >= 0) {
        if (term.cursor.x != expected_cx || term.cursor.y != expected_cy) {
            error("Cursor mismatch. Expected: (%d, %d), Actual: (%d, %d)\n",
                    expected_cx, expected_cy, term.cursor.x, term.cursor.y);
            assert(false);
        }
    }

    for (int32 idx = 0; idx < total_lines; idx += 1) {
        StGlyph *line = NULL;
        int32 len = 0;
        char *ptr = NULL;
        bool actual_wrap = false;
        char buffer[1024];

        if (idx < active_hist) {
            int32 hist_idx = (term.i_hist - active_hist + 1 + idx + HISTORY_SIZE) % HISTORY_SIZE;
            line = term.hist[hist_idx];
        } else {
            int32 lines_idx = idx - active_hist;
            line = term.lines[lines_idx];
        }

        len = term_line_len(line);
        ptr = buffer;

        if (len > 0) {
            ptr = term_get_glyphs(buffer, &line[0], &line[len - 1]);
        }
        *ptr = '\0';

        actual_wrap = term_is_wrapped(line);

        if (strcmp(buffer, expected_texts[idx]) != 0) {
            char *is_hist = NULL;

            if (idx < active_hist) {
                is_hist = "yes";
            } else {
                is_hist = "no";
            }
            error("Assertion failed at absolute line %d (Hist: %s):\n", idx, is_hist);
            error("  Expected: '%s'\n", expected_texts[idx]);
            error("  Actual:   '%s'\n", buffer);
            assert(false);
        }

        if (expected_wraps[idx] != actual_wrap) {
            error("Wrap assertion failed at absolute line %d:\n", idx);
            error("  Expected wrap: %d, Actual wrap: %d\n", expected_wraps[idx], actual_wrap);
            assert(false);
        }
    }

    return;
}

int
main(void) {
    int32 init_cols = 20;
    int32 init_rows = 10;

    /* Bootstrap headless terminal memory */
    term.ncols = init_cols;
    term.nrows = init_rows;
    term.dirty = xmalloc((int64)init_rows * SIZEOF(*(term.dirty)));
    term.tabs = xmalloc((int64)init_cols * SIZEOF(*(term.tabs)));
    memset64(term.tabs, 0, init_cols * SIZEOF(*(term.tabs)));

    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        term.hist[i] = xmalloc((int64)init_cols * SIZEOF(StGlyph));
        for (int32 j = 0; j < init_cols; j += 1) {
            term.hist[i][j].mode = ATTR_NONE;
            term.hist[i][j].rune = ' ';
        }
    }

    for (int32 i = 0; i < 2; i += 1) {
        term.lines = xmalloc((int64)init_rows * SIZEOF(*(term.lines)));
        for (int32 j = 0; j < init_rows; j += 1) {
            term.lines[j] = xmalloc((int64)init_cols * SIZEOF(StGlyph));
            for (int32 k = 0; k < init_cols; k += 1) {
                term_clear_glyph(&term.lines[j][k], false);
            }
        }
        term_swap_screen();
    }

    term_reset();

    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }

    /* Scenario A: Width Shrinkage (Forcing Wraps & Cursor X adjustment) */
    inject_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNOPQRST", "UVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, true, false };
        verify_full_state(11, state_texts, state_wraps, 6, 9);
    }

    /* Scenario B: Width Expansion (Unwrapping & Cursor X mapping) */
    term_resize(30, 10);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNOPQRSTUVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, false };
        verify_full_state(10, state_texts, state_wraps, 26, 9);
    }

    /* Scenario C: Shrink width to force wrap again */
    term_resize(15, 10);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNO", "PQRSTUVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, true, false };
        verify_full_state(11, state_texts, state_wraps, 11, 9);
    }

    /* Scenario J: Viewport Scroll State Anchor Integrity */
    printf("Testing Viewport Scroll Anchoring...\n");
    term_resize(init_cols, init_rows);
    term_reset();
    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }

    for (int32 i = 0; i < 5; i += 1) {
        inject_text("HISTORY_LINE\n");
    }

    term.lines_scrolled_up = 3;

    term_resize(20, 12);
    if (term.lines_scrolled_up != 1) {
        error("Viewport scroll assertion failed. Expected: 1, Actual: %d\n", term.lines_scrolled_up);
        assert(false);
    }

    /* Scenario K: Inline Image (Sixel) translation during history pull */
    printf("Testing Inline Image Translation...\n");
    term_resize(init_cols, init_rows);
    term_reset();
    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }

    for (int32 i = 0; i < 3; i += 1) {
        inject_text("PADDING\n");
    }

    {
        ImageList *dummy_img = xmalloc(SIZEOF(ImageList));

        memset64(dummy_img, 0, SIZEOF(ImageList));
        dummy_img->x = 2;
        dummy_img->y = 5;
        dummy_img->ch = 16;
        dummy_img->cw = 8;
        dummy_img->height = 32;
        dummy_img->width = 32;
        dummy_img->next = term.images;
        term.images = dummy_img;

        term_resize(20, 12);

        if (term.images->y != 7) {
            error("Image translation failed. Expected Y: 7, Actual Y: %d\n", term.images->y);
            assert(false);
        }
    }

    /* Scenario I: Random Fuzzing */
    printf("Running Fuzzing Phase...\n");
    srand((uint)time(NULL));
    for (int32 i = 0; i < 1000; i += 1) {
        int32 new_w = (rand() % 150) + 2;
        int32 new_h = (rand() % 100) + 2;
        int32 actions = rand() % 5;

        term_resize(new_w, new_h);
        check_consistent_state();

        for (int32 a = 0; a < actions; a += 1) {
            int32 choice = rand() % 2;

            if (choice == 0) {
                term_new_line(true);
            } else {
                int32 slen = (rand() % 80) + 1;
                char buf[128];

                for (int32 c = 0; c < slen; c += 1) {
                    buf[c] = 'A' + (rand() % 26);
                }
                buf[slen] = '\0';
                inject_text(buf);
            }
        }
        check_consistent_state();
    }

    /* Scenario L: The Viewport Desync Bug */
    printf("Testing Viewport Desync on Width Resize...\n");
    term_resize(20, 5);
    term_reset();

    inject_text("00000000000000000000\n");
    inject_text("11111111111111111111\n");
    inject_text("22222222222222222222\n");
    inject_text("33333333333333333333\n");
    inject_text("44444444444444444444\n");
    inject_text("55555555555555555555\n");
    inject_text("66666666666666666666\n");
    inject_text("77777777777777777777");
    check_consistent_state();

    term.lines_scrolled_up = 2;
    verify_viewport_line(0, "11111111111111111111");

    term_resize(10, 5);
    check_consistent_state();
    verify_viewport_line(0, "1111111111");

    /* Scenario M: Image displacement when lines below wrap (The Bug) */
    printf("Testing Image position when lines below wrap...\n");
    term_resize(20, 10);
    term_reset();

    inject_text("TOP_LINE\n");
    inject_text("IMAGE_ANCHOR_LINE\n");
    inject_text("THIS_LONG_LINE_WILL_WRAP_INTO_MULTIPLE_ROWS_LATER\n");
    check_consistent_state();

    {
        ImageList *img = xmalloc(SIZEOF(ImageList));
        StGlyph *img_line = NULL;
        char buf[32];

        memset64(img, 0, SIZEOF(ImageList));
        img->x = 0;
        img->y = 1;
        img->ch = 16;
        img->cw = 8;
        img->height = 16;
        img->width = 16;
        img->next = NULL;
        term.images = img;

        term_resize(10, 10);
        check_consistent_state();

        img_line = term_line_abs(term.images->y);
        term_get_glyphs(buf, &img_line[0], &img_line[term.ncols - 1]);

        if (strstr(buf, "IMAGE_ANCH") == NULL) {
            error("Scenario M failed! Image not at anchor text. Found: %s\n", buf);
            assert(false);
        }
    }

    /* Scenario N: The "Push-Down" Bug */
    printf("Testing Image Displacement when text BELOW wraps...\n");
    term_resize(20, 5);
    term_reset();

    inject_text("TOP\n");
    inject_text("ANCHOR\n");
    inject_text("THIS_IS_A_VERY_LONG_LINE_THAT_WILL_WRAP_INTO_MANY_ROWS_WHEN_SHRINKING");

    {
        ImageList *img = xmalloc(SIZEOF(ImageList));
        StGlyph *n_img_line = NULL;
        char n_buf[32];

        memset64(img, 0, SIZEOF(ImageList));
        img->x = 0;
        img->y = 0;
        img->ch = 16;
        img->cw = 8;
        img->height = 16;
        img->width = 16;
        img->next = NULL;
        term.images = img;

        term_resize(10, 5);
        check_consistent_state();

        n_img_line = term_line_abs(term.images->y);
        term_get_glyphs(n_buf, &n_img_line[0], &n_img_line[5]);
        n_buf[6] = '\0';

        if (strcmp(n_buf, "ANCHOR") != 0) {
            error("Scenario N Sync Error! Image not at anchor text.\n");
            error("  Expected: 'ANCHOR' at Y=-3\n");
            error("  Found:    '%s' at Y=%d\n", n_buf, term.images->y);
            assert(false);
        }
    }

    /* Scenario O: Cursor Visibility on Shrink (Anchoring to Cursor vs Top) */
    printf("Testing Cursor Visibility after width shrinkage...\n");
    {
        int32 target_y;
        StGlyph *c_line;
        char c_buf[32];

        term_resize(20, 5);
        term_reset();

        /* 
         * Inject text to fill the screen.
         * Row 0: 1_TOP
         * Row 1: 2_MIDDLE
         * Row 2: 3_BOTTOM
         * Rows 3-4: The long line below (35 chars takes 2 rows at width 20)
         */
        inject_text("1_TOP\n");
        inject_text("2_MIDDLE\n");
        inject_text("3_BOTTOM\n");
        inject_text("4_THIS_LONG_LINE_WILL_WRAP_A_LOT_HERE");
        
        /* 
         * Initial State (20x5):
         * 0: 1_TOP
         * 1: 2_MIDDLE
         * 2: 3_BOTTOM
         * 3: 4_THIS_LONG_LINE_WI
         * 4: LL_WRAP_A_LOT_HERE (Cursor is here at Row 4)
         */
        check_consistent_state();

        /* 
         * Shrink width to 10.
         * 1_TOP -> 1 row
         * 2_MIDDLE -> 1 row
         * 3_BOTTOM -> 1 row
         * 4_THIS_LONG... (35 chars) -> 4 rows
         * Total logical lines in the active screen buffer = 7.
         *
         * Since the screen height is only 5:
         * - If anchored to the TOP: The viewport stays at history/top, 
         *   showing "1_TOP" at the top. The cursor at the bottom of the 
         *   buffer will be pushed off-screen (scrolled out of view).
         * - If anchored to the CURSOR (Goal): The terminal should adjust 
         *   scroll state so the cursor line ("OT_HERE") remains on Row 4.
         */
        term_resize(10, 5);
        check_consistent_state();

        /* 
         * If the cursor line is visible at the bottom of the viewport, 
         * lines_scrolled_up must be 0.
         */
        if (term.lines_scrolled_up != 0) {
            fprintf(stderr, "Scenario O failed! Terminal is scrolled up after resize.\n");
            fprintf(stderr, "  lines_scrolled_up: %d (expected 0 to keep cursor visible)\n", 
                    term.lines_scrolled_up);
            assert(false);
        }

        /* 
         * Verify that the visible line where the cursor resides contains 
         * the expected wrapped text.
         */
        target_y = term.cursor.y;
        c_line = term_line(target_y);
        term_get_glyphs(c_buf, &c_line[0], &c_line[term.ncols - 1]);

        if (strstr(c_buf, "OT_HERE") == NULL) {
            fprintf(stderr, "Scenario O failed! Cursor line does not contain expected text.\n");
            fprintf(stderr, "  Found: '%s' at screen Y=%d\n", c_buf, target_y);
            fprintf(stderr, "  (Viewport is likely anchored to the top of the terminal)\n");
            assert(false);
        }
    }

    printf("All resize, reflow, image translation, and viewport tests passed successfully!\n");

    return 0;
}
