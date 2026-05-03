#include <stdlib.h>
#include "st.c"

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
verify_full_state(int32 expected_count, char **expected_texts, bool *expected_wraps) {
    bool is_alt;
    int32 active_hist;
    int32 total_lines;
    
    is_alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);
    
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

    /* Scenario A: Width Shrinkage (Forcing Wraps) */
    inject_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNOPQRST", "UVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, true, false };
        verify_full_state(11, state_texts, state_wraps);
    }

    /* Scenario B: Width Expansion (Unwrapping) */
    term_resize(30, 10);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNOPQRSTUVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, false };
        verify_full_state(10, state_texts, state_wraps);
    }

    /* Scenario C: Shrink width to force wrap again before testing history */
    term_resize(15, 10);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNO", "PQRSTUVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, true, false };
        verify_full_state(11, state_texts, state_wraps);
    }

    /* Scenario C.2: Height Shrinkage (Push to History) */
    term_resize(15, 5);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNO", "PQRSTUVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, true, false };
        verify_full_state(11, state_texts, state_wraps);
    }
    
    /* Scenario D: Height Expansion (Pull from History) */
    term_resize(15, 10);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNO", "PQRSTUVWXYZ" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, true, false };
        verify_full_state(11, state_texts, state_wraps);
    }

    /* Scenario E: Alternate Screen Integrity */
    term_load_alt_screen(true, true);
    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }
    inject_text("ALT SCREEN TEXT");
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ALT SCREEN TEXT" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, false };
        verify_full_state(10, state_texts, state_wraps);
    }
    
    term_resize(25, 12);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ALT SCREEN TEXT", "", "" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, false, false, false };
        verify_full_state(12, state_texts, state_wraps);
    }

    term_load_def_screen(false, true);
    check_consistent_state();
    {
        char *state_texts[] = { "", "", "", "", "", "", "", "", "", "ABCDEFGHIJKLMNOPQRSTUVWXY", "Z", "" };
        bool state_wraps[] = { false, false, false, false, false, false, false, false, false, true, false, false };
        verify_full_state(12, state_texts, state_wraps);
    }

    /* --- Advanced Resizing Scenarios --- */
    term_reset();
    for (int32 i = 0; i < init_rows - 1; i += 1) {
        term_new_line(true);
    }
    inject_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    check_consistent_state();
    
    /* Scenario F: Large to Small (Change both Width and Height) */
    term_resize(10, 5);
    check_consistent_state();
    {
        char *state_texts[] = { 
            "", "", "", "", "", "", "", "", "", 
            "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789" 
        };
        bool state_wraps[] = { 
            false, false, false, false, false, false, false, false, false,
            true, true, true, false 
        };
        verify_full_state(13, state_texts, state_wraps);
    }

    /* Scenario G: Small to Even Smaller (Massive wrapping) */
    term_resize(5, 8);
    check_consistent_state();
    {
        char *state_texts[] = {
            "", "", "", "", "", "", "", "", "", 
            "ABCDE", "FGHIJ", "KLMNO", "PQRST", "UVWXY", "Z0123", "45678", "9" 
        };
        bool state_wraps[] = {
            false, false, false, false, false, false, false, false, false,
            true, true, true, true, true, true, true, false
        };
        verify_full_state(17, state_texts, state_wraps);
    }

    /* Scenario H: Small to Large (Expansion of both) */
    term_resize(40, 20);
    check_consistent_state();
    {
        char *state_texts[] = {
            "", "", "", "", "", "", "", "", "", 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 
            "", "", "", "", "", "", "", "", "", "" 
        };
        bool state_wraps[] = {
            false, false, false, false, false, false, false, false, false,
            false,
            false, false, false, false, false, false, false, false, false, false
        };
        verify_full_state(20, state_texts, state_wraps);
    }

    /* Scenario I: Random Fuzzing for rare bugs */
    printf("Running Fuzzing Phase...\n");
    srand((uint)time(NULL));
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

    printf("All resize, reflow, and fuzzing tests passed successfully!\n");
    return 0;
}
