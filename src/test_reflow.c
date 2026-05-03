#include <stdlib.h>
#include "st.c"

static void
verify_viewport_line(int32 screen_y, char *expected_text) {
    StGlyph *line;
    char buffer[1024];
    char *ptr;
    int32 len;

    /* TERM_LINE handles the math to pull from hist or active screen based on scroll */
    line = TERM_LINE(screen_y);
    len = term_line_len(line);
    ptr = buffer;

    if (len > 0) {
        ptr = term_get_glyphs(buffer, &line[0], &line[len - 1]);
    }
    *ptr = '\0';

    if (strcmp(buffer, expected_text) != 0) {
        fprintf(stderr, "Viewport assertion failed at screen row %d:\n", screen_y);
        fprintf(stderr, "  Expected: '%s'\n", expected_text);
        fprintf(stderr, "  Actual:   '%s'\n", buffer);
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
            term.lines[term.cursor.y][term.cursor.x].mode |= ATTR_WRAP;
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
    bool is_alt;
    int32 active_hist;
    int32 total_lines;
    
    is_alt = term_mode_is_set(TERM_MODE_ALTSCREEN);
    
    if (is_alt) {
        active_hist = 0;
    } else {
        active_hist = term.n_hist;
    }
    
    total_lines = active_hist + term.nrows;
    
    if (total_lines != expected_count) {
        fprintf(stderr, "Total lines mismatch. Expected: %d, Actual: %d (Hist: %d, Rows: %d)\n",
                expected_count, total_lines, active_hist, term.nrows);
        assert(false);
    }

    /* Assert Cursor Tracking if specified */
    if (expected_cx >= 0 && expected_cy >= 0) {
        if (term.cursor.x != expected_cx || term.cursor.y != expected_cy) {
            fprintf(stderr, "Cursor mismatch. Expected: (%d, %d), Actual: (%d, %d)\n",
                    expected_cx, expected_cy, term.cursor.x, term.cursor.y);
            assert(false);
        }
    }
    
    for (int32 idx = 0; idx < total_lines; idx += 1) {
        StGlyph *line;
        char buffer[1024];
        char *ptr;
        int32 len;
        bool actual_wrap;
        
        line = NULL;
        ptr = buffer;
        
        if (idx < active_hist) {
            int32 hist_idx;
            
            hist_idx = (term.i_hist - active_hist + 1 + idx + HISTORY_SIZE) % HISTORY_SIZE;
            line = term.hist[hist_idx];
        } else {
            int32 lines_idx;
            
            lines_idx = idx - active_hist;
            line = term.lines[lines_idx];
        }
        
        len = term_line_len(line);
        
        if (len > 0) {
            ptr = term_get_glyphs(buffer, &line[0], &line[len - 1]);
        }
        *ptr = '\0';
        
        actual_wrap = term_is_wrapped(line);
        
        if (strcmp(buffer, expected_texts[idx]) != 0) {
            char *is_hist;
            
            if (idx < active_hist) {
                is_hist = "yes";
            } else {
                is_hist = "no";
            }
            fprintf(stderr, "Assertion failed at absolute line %d (Hist: %s):\n", idx, is_hist);
            fprintf(stderr, "  Expected: '%s'\n", expected_texts[idx]);
            fprintf(stderr, "  Actual:   '%s'\n", buffer);
            assert(false);
        }
        
        if (expected_wraps[idx] != actual_wrap) {
            fprintf(stderr, "Wrap assertion failed at absolute line %d:\n", idx);
            fprintf(stderr, "  Expected wrap: %d, Actual wrap: %d\n", expected_wraps[idx], actual_wrap);
            assert(false);
        }
    }
    
    return;
}

int
main(void) {
    int32 init_cols;
    int32 init_rows;
    
    init_cols = 20;
    init_rows = 10;

    /* Bootstrap headless terminal memory */
    term.ncols = init_cols;
    term.nrows = init_rows;
    term.dirty = xmalloc(init_rows * SIZEOF(*(term.dirty)));
    term.tabs = xmalloc(init_cols * SIZEOF(*(term.tabs)));
    memset64(term.tabs, 0, init_cols * SIZEOF(*(term.tabs)));

    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        term.hist[i] = xmalloc(init_cols * SIZEOF(StGlyph));
        for (int32 j = 0; j < init_cols; j += 1) {
            term.hist[i][j].mode = ATTR_NONE;
            term.hist[i][j].rune = ' ';
        }
    }

    for (int32 i = 0; i < 2; i += 1) {
        term.lines = xmalloc(init_rows * SIZEOF(*(term.lines)));
        for (int32 j = 0; j < init_rows; j += 1) {
            term.lines[j] = xmalloc(init_cols * SIZEOF(StGlyph));
            for (int32 k = 0; k < init_cols; k += 1) {
                term.lines[j][k].mode = ATTR_NONE;
                term.lines[j][k].rune = ' ';
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
        verify_full_state(11, state_texts, state_wraps, 6, 9); /* Cursor X=6 on "UVWXYZ" */
    }

    /* Scenario B: Width Expansion (Unwrapping & Cursor X mapping) */
    term_resize(30, 10);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNOPQRSTUVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, false };
        verify_full_state(10, state_texts, state_wraps, 26, 9); /* Cursor maps perfectly to end of string */
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
    term_resize(init_cols, init_rows); /* <--- ADD THIS FIX */
    term_reset();
    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }
    
    /* Inject text to create 5 lines of history */
    for (int32 i = 0; i < 5; i += 1) {
        inject_text("HISTORY_LINE\n");
    }
    
    /* Simulate user pressing Shift+PageUp to scroll up by 3 lines */
    term.lines_scrolled_up = 3;
    
    /* Expand height by 2. st should pull lines down, meaning the viewport 
       needs to decrement by 2 to keep looking at the exact same text. */
    term_resize(20, 12);
    if (term.lines_scrolled_up != 1) {
        fprintf(stderr, "Viewport scroll assertion failed. Expected: 1, Actual: %d\n", term.lines_scrolled_up);
        assert(false);
    }

    /* Scenario K: Inline Image (Sixel) translation during history pull */
    printf("Testing Inline Image Translation...\n");
    term_resize(init_cols, init_rows); /* <--- ADD THIS FIX */
    term_reset();
    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }
    
    /* Create 3 lines of history */
    for (int32 i = 0; i < 3; i += 1) {
        inject_text("PADDING\n");
    }
    
{
        ImageList *dummy_img;
        
        dummy_img = xmalloc(SIZEOF(ImageList));
        memset64(dummy_img, 0, SIZEOF(ImageList));
        dummy_img->x = 2;
        dummy_img->y = 5; /* Image starts at row 5 on active screen */
        
        /* FIX: Provide valid pixel dimensions to prevent Division by Zero */
        dummy_img->ch = 16;     /* Mock character height */
        dummy_img->cw = 8;      /* Mock character width */
        dummy_img->height = 32; /* Mock image height (spans 2 rows) */
        dummy_img->width = 32;  /* Mock image width */
        
        dummy_img->next = term.images;
        term.images = dummy_img;
        
        /* Expand height by 2. term_resize_def calls reflow_scroll_down(2),
           pulling 2 lines from history and pushing active screen down. */
        term_resize(20, 12);
        
        /* Image must shift down by 2 rows to stay glued to its text */
        if (term.images->y != 7) {
            fprintf(stderr, "Image translation failed. Expected Y: 7, Actual Y: %d\n", term.images->y);
            assert(false);
        }
    }

    /* Scenario I: Random Fuzzing */
    printf("Running Fuzzing Phase...\n");
    srand(0x1337);
    for (int32 i = 0; i < 5000; i += 1) {
        int32 new_w;
        int32 new_h;
        int32 actions;
        
        new_w = (rand() % 150) + 2;
        new_h = (rand() % 100) + 2;
        
        term_resize(new_w, new_h);
        check_consistent_state();
        
        actions = rand() % 5;
        for (int32 a = 0; a < actions; a += 1) {
            int32 choice;
            
            choice = rand() % 2;
            if (choice == 0) {
                term_new_line(true);
            } else {
                int32 slen;
                char buf[128];
                
                slen = (rand() % 80) + 1;
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
    
    /* Inject 8 lines of exactly 20 characters */
    inject_text("00000000000000000000\n");
    inject_text("11111111111111111111\n");
    inject_text("22222222222222222222\n");
    inject_text("33333333333333333333\n");
    inject_text("44444444444444444444\n");
    inject_text("55555555555555555555\n");
    inject_text("66666666666666666666\n");
    inject_text("77777777777777777777");
    check_consistent_state();
    
    /* 
     * Screen is 5 rows high. 
     * Active screen: 333..., 444..., 555..., 666..., 777...
     * History (n_hist=3): 000..., 111..., 222...
     */
    
    /* Scroll up by 2 lines. 
     * Viewport should now show: 111..., 222..., 333..., 444..., 555...
     */
    term.lines_scrolled_up = 2;
    
    /* Top of the viewport (row 0) is "111..." */
    verify_viewport_line(0, "11111111111111111111");
    
    /* Shrink the width to 10. 
     * Every 20-character line will wrap into TWO 10-character lines.
     * The line "111..." should still be at the top of the viewport 
     * if the terminal correctly anchors the scroll position.
     */
    term_resize(10, 5);
    check_consistent_state();
    
    /* The top of the viewport SHOULD be the first half of "111..." */
    verify_viewport_line(0, "1111111111");

    printf("All resize, reflow, image translation, and viewport tests passed successfully!\n");
    return 0;
}
