#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "st.c"
#include "user.c"
#include "util.c"

static char *current_test_name = "None";

static void
test_verify_viewport_line(int32 screen_y, char *expected_text) {
    StGlyph *line = term_line(screen_y);
    int32 len = term_line_len(line);
    char *ptr = NULL;
    char buffer[1024];

    ptr = buffer;
    if (len > 0) {
        ptr = term_get_glyphs(buffer, &line[0], &line[len - 1]);
    }
    *ptr = '\0';

    if (strcmp(buffer, expected_text) != 0) {
        error("[%s] Viewport assertion failed at screen row %d:\n",
              current_test_name, screen_y);
        error("  Expected: '%s'\n", expected_text);
        error("  Actual:   '%s'\n", buffer);
        assert(false);
    }

    return;
}

static void
test_verify_dump(char *expected_text) {
    FILE *tmp = tmpfile();
    int32 fd;
    char buffer[8192];
    ssize_t read_bytes;

    if (!tmp) {
        error("[%s] Failed to create temporary file for dump test\n",
              current_test_name);
        assert(false);
    }

    fd = fileno(tmp);
    dump_terminal_to_fd(fd);

    if (lseek(fd, 0, SEEK_SET) < 0) {
        error("[%s] Failed to rewind dump fd\n", current_test_name);
        assert(false);
    }

    read_bytes = read(fd, buffer, SIZEOF(buffer) - 1);
    if (read_bytes < 0) {
        read_bytes = 0;
    }
    buffer[read_bytes] = '\0';

    fclose(tmp);

    if (strcmp(buffer, expected_text) != 0) {
        error("[%s] Dump to FD mismatch:\n", current_test_name);
        error("  Expected: '%s'\n", expected_text);
        error("  Actual:   '%s'\n", buffer);
        assert(false);
    }

    return;
}

static void
test_verify_editor_dump(char *expected_text,
                        int32 expected_row, int32 expected_col) {
    FILE *tmp = tmpfile();
    int32 fd;
    char buffer[8192];
    ssize_t read_bytes;
    int32 out_row = 0;
    int32 out_col = 0;
    int32 expected_len = strlen32(expected_text);

    if (!tmp) {
        error("[%s] Failed to create temporary file for editor test\n",
              current_test_name);
        assert(false);
    }

    fd = fileno(tmp);
    dump_for_editor(fd, &out_row, &out_col);

    if (lseek(fd, 0, SEEK_SET) < 0) {
        error("[%s] Failed to rewind dump fd\n", current_test_name);
        assert(false);
    }

    read_bytes = read(fd, buffer, SIZEOF(buffer) - 1);
    if (read_bytes < 0) {
        read_bytes = 0;
    }
    buffer[read_bytes] = '\0';

    fclose(tmp);

    if (strcmp(buffer, expected_text) != 0) {
        error("[%s] Editor Dump mismatch:\n", current_test_name);
        error("  Expected: [%d]'%s'\n", expected_len, expected_text);
        error("  Actual:   [%zd]'%s'\n", read_bytes, buffer);
        assert(false);
    }

    if (out_row != expected_row || out_col != expected_col) {
        error("[%s] Editor Cursor mismatch:\n", current_test_name);
        error("  Expected: (row %d, col %d)\n", expected_row, expected_col);
        error("  Actual:   (row %d, col %d)\n", out_row, out_col);
        assert(false);
    }

    return;
}

static void
test_inject_text(char *text) {
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
test_verify_state(int32 expected_count,
                  char **expected_texts, bool *expected_wraps,
                  int32 expected_cx, int32 expected_cy) {
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
        error("[%s] Total lines mismatch.\n", current_test_name);
        error("Expected: %d, Actual: %d (Hist: %d, Rows: %d)\n",
              expected_count, total_lines, active_hist, term.nrows);
        assert(false);
    }

    if (expected_cx >= 0 && expected_cy >= 0) {
        if (term.cursor.x != expected_cx || term.cursor.y != expected_cy) {
            error("[%s] Cursor mismatch.\n", current_test_name);
            error("Expected: (%d, %d), Actual: (%d, %d)\n",
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
            error("[%s] Content mismatch at absolute line %d (In History: %s):\n",
                  current_test_name, idx, is_hist);
            error("  Expected: '%s'\n", expected_texts[idx]);
            error("  Actual:   '%s'\n", buffer);
            assert(false);
        }

        if (expected_wraps[idx] != actual_wrap) {
            error("[%s] Wrap flag mismatch at absolute line %d:\n",
                  current_test_name, idx);
            error("  Expected: %d, Actual: %d\n",
                  expected_wraps[idx], actual_wrap);
            assert(false);
        }
    }

    return;
}

int
main(void) {
    int32 init_cols = 20;
    int32 init_rows = 10;

    /* Use standardized allocation helper */
    CONF_NCOLS = init_cols;
    CONF_NROWS = init_rows;
    term_allocate();
    term_reset();

    /* Move cursor to the bottom row to prepare for tests */
    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }

    {
        current_test_name = "Scenario A: Width Shrinkage";
        printf("Running: %s...\n", current_test_name);
        test_inject_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        check_consistent_state();
        {
            char *state_texts[] = { 
                "", "", "", "", "", "", "", "", "",
                "ABCDEFGHIJKLMNOPQRST", "UVWXYZ" 
            };
            bool state_wraps[] = { 
                false, false, false, false, false, false, false, false, false,
                true, false 
            };
            test_verify_state(11, state_texts, state_wraps, 6, 9);
        }
        test_verify_dump("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
    }

    {
        current_test_name = "Scenario B: Width Expansion";
        printf("Running: %s...\n", current_test_name);
        term_resize(30, 10);
        check_consistent_state();
        {
            char *state_texts[] = { 
                "", "", "", "", "", "", "", "", "",
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ" 
            };
            bool state_wraps[] = { 
                false, false, false, false, false, false, false, false, false,
                false 
            };
            test_verify_state(10, state_texts, state_wraps, 26, 9);
        }
        test_verify_dump("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
    }

    {
        current_test_name = "Scenario C: Forced Wrap Re-shrink";
        printf("Running: %s...\n", current_test_name);
        term_resize(15, 10);
        check_consistent_state();
        {
            char *state_texts[] = { 
                "", "", "", "", "", "", "", "", "",
                "ABCDEFGHIJKLMNO", "PQRSTUVWXYZ" 
            };
            bool state_wraps[] = { 
                false, false, false, false, false, false, false, false, false,
                true, false 
            };
            test_verify_state(11, state_texts, state_wraps, 11, 9);
        }
        test_verify_dump("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
    }

    {
        current_test_name = "Scenario J: Viewport Scroll Anchoring";
        printf("Running: %s...\n", current_test_name);
        term_resize(init_cols, init_rows);
        term_reset();
        for (int32 i = 0; i < init_rows - 1; i += 1) {
            term_new_line(true);
        }
        for (int32 i = 0; i < 5; i += 1) {
            test_inject_text("HISTORY_LINE\n");
        }
        term.scrolled_up = 3;
        term_resize(20, 12);
        if (term.scrolled_up != 1) {
            error("[%s] Viewport scroll mismatch. Expected: 1, Actual: %d\n",
                  current_test_name, term.scrolled_up);
            assert(false);
        }
    }

    {
        current_test_name = "Scenario K: Inline Image Translation";
        printf("Running: %s...\n", current_test_name);
        term_resize(init_cols, init_rows);
        term_reset();
        for (int32 i = 0; i < init_rows - 1; i += 1) {
            term_new_line(true);
        }
        for (int32 i = 0; i < 3; i += 1) {
            test_inject_text("PADDING\n");
        }
        {
            ImageList *dummy_img = malloc2(SIZEOF(ImageList));
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
                error("[%s] Image coordinate mismatch.\n", current_test_name);
                error("Expected: 7, Actual: %d\n", term.images->y);
                assert(false);
            }
        }
    }

    {
        current_test_name = "Scenario I: Fuzzing Phase";
        printf("Running: %s...\n", current_test_name);
        srand((uint)time(NULL));
        for (int32 i = 0; i < 100; i += 1) {
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
                        buf[c] = (char)('A' + (rand() % 26));
                    }
                    buf[slen] = '\0';
                    test_inject_text(buf);
                }
            }
            check_consistent_state();
        }
    }

    {
        current_test_name = "Scenario L: Viewport Desync Width Resize";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 5);
        term_reset();
        test_inject_text("00000000000000000000\n");
        test_inject_text("11111111111111111111\n");
        test_inject_text("22222222222222222222\n");
        test_inject_text("33333333333333333333\n");
        test_inject_text("44444444444444444444\n");
        test_inject_text("55555555555555555555\n");
        test_inject_text("66666666666666666666\n");
        test_inject_text("77777777777777777777");
        check_consistent_state();
        term.scrolled_up = 2;
        test_verify_viewport_line(0, "11111111111111111111");
        term_resize(10, 5);
        check_consistent_state();
        test_verify_viewport_line(0, "1111111111");
        test_verify_dump("00000000000000000000\n"
                         "11111111111111111111\n"
                         "22222222222222222222\n"
                         "33333333333333333333\n"
                         "44444444444444444444\n"
                         "55555555555555555555\n"
                         "66666666666666666666\n"
                         "77777777777777777777\n");
    }

    {
        current_test_name = "Scenario M: Image Displacement Below Wrap";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 10);
        term_reset();
        test_inject_text("TOP_LINE\n");
        test_inject_text("IMAGE_ANCHOR_LINE\n");
        test_inject_text("THIS_LONG_LINE_WILL_WRAP_INTO_MULTIPLE_ROWS_LATER\n");
        check_consistent_state();
        {
            ImageList *img = malloc2(SIZEOF(ImageList));
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
                error("[%s] Image sync failed. Found: %s\n",
                      current_test_name, buf);
                assert(false);
            }
        }
    }

    {
        current_test_name = "Scenario N: Image Push-Down Bug";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 5);
        term_reset();
        test_inject_text("TOP\n");
        test_inject_text("ANCHOR\n");
        test_inject_text("THIS_IS_A_VERY_LONG_LINE_THAT_WILL_WRAP_INTO_MANY_ROWS_WHEN_SHRINKING");
        {
            ImageList *img = malloc2(SIZEOF(ImageList));
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
                error("[%s] Image sync failed. Expected 'ANCHOR', Found '%s'\n",
                      current_test_name, n_buf);
                assert(false);
            }
        }
    }

    {
        current_test_name = "Scenario O: Cursor Visibility on Shrink";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 5);
        term_reset();
        test_inject_text("1_TOP\n");
        test_inject_text("2_MIDDLE\n");
        test_inject_text("3_BOTTOM\n");
        test_inject_text("4_THIS_LONG_LINE_WILL_WRAP_A_LOT_HERE");
        check_consistent_state();
        term_resize(10, 5);
        check_consistent_state();
        if (term.scrolled_up != 0) {
            error("[%s] Viewport scrolled incorrectly.\n", current_test_name);
            error("Expected 0, Actual: %d\n", term.scrolled_up);
            assert(false);
        }
        {
            int32 target_y = term.cursor.y;
            StGlyph *c_line = term_line(target_y);
            char c_buf[32];
            term_get_glyphs(c_buf, &c_line[0], &c_line[term.ncols - 1]);
            if (strstr(c_buf, "OT_HERE") == NULL) {
                error("[%s] Cursor line content mismatch. Found: '%s'\n",
                      current_test_name, c_buf);
                assert(false);
            }
        }
    }

    {
        current_test_name = "Scenario P: Shrink Width then Expand Height Bug";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 5);
        term_reset();
        test_inject_text("AABBCCDDEEFFGG $");
        check_consistent_state();

        term_resize(2, 5);
        check_consistent_state();

        term_resize(2, 10);
        check_consistent_state();

        {
            char *state_texts[] = {
                "AA", "BB", "CC", "DD", "EE", "FF", "GG", " $", "", ""
            };
            bool state_wraps[] = {
                true, true, true, true, true, true, true, false, false, false
            };
            test_verify_state(10, state_texts, state_wraps, 1, 7);
        }
        test_verify_dump("AABBCCDDEEFFGG $\n");
    }

    {
        current_test_name = "Scenario Q: Vim Select Trailing Cursor Padding";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 5);
        term_reset();
        
        test_inject_text("SHORT\n");
        term.cursor.x = 8;
        term.cursor.y = 1;
        
        test_verify_editor_dump("SHORT\n         \n\n\n\n", 2, 9);
    }

    {
        current_test_name = "Scenario R: Vim Select Alternate Screen Target";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 5);
        term_reset();

        for (int32 i = 0; i < 10; i += 1) {
            term_new_line(true);
        }
        
        test_inject_text("ALTSCREEN\n");
        term.cursor.x = 5;
        term.cursor.y = 1;

        term.mode |= TERM_MODE_ALTSCREEN;

        test_verify_editor_dump("\n\n\n\n\n\n\n\n      \n\nALTSCREEN\n\n", term.n_hist + 1, 1);
        
        term.mode &= ~TERM_MODE_ALTSCREEN;
    }

    {
        current_test_name = "Scenario S: Image Scaling Attributes Preservation";
        printf("Running: %s...\n", current_test_name);
        term_resize(20, 10);
        term_reset();

        term_window.cw = 10;
        term_window.ch = 20;

        {
            ImageList *img = malloc2(SIZEOF(ImageList));
            memset64(img, 0, SIZEOF(ImageList));
            img->x = 0;
            img->y = 0;
            img->cw = term_window.cw;
            img->ch = term_window.ch;
            img->width = 100;
            img->height = 200;
            img->next = NULL;
            term.images = img;

            /* Simulate a zoom by changing term_window cw/ch and resizing grid */
            term_window.cw = 15;
            term_window.ch = 30;
            term_resize(15, 8);
            check_consistent_state();

            if (term.images->cw != 10 || term.images->ch != 20 ||
                term.images->width != 100 || term.images->height != 200) {
                error("[%s] Image attributes mutated unexpectedly.\n", current_test_name);
                error("Expected: cw=10, ch=20, w=100, h=200\n");
                error("Actual:   cw=%d, ch=%d, w=%d, h=%d\n",
                      term.images->cw, term.images->ch,
                      term.images->width, term.images->height);
                assert(false);
            }
        }
    }

    printf("\nAll tests passed successfully!\n");
    return 0;
}
