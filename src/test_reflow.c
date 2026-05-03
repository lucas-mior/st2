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
verify_line(int32 row, char *expected_text, bool expected_wrap) {
    char buffer[1024];
    char *ptr;
    StGlyph *fgp;
    StGlyph *lgp;
    
    fgp = &term.lines[row][0];
    lgp = &fgp[term.ncols - 1];
    
    while (lgp > fgp) {
        if (lgp->mode & (ATTR_SET | ATTR_WRAP)) {
            break;
        }
        lgp -= 1;
    }
    
    ptr = buffer;
    while (fgp <= lgp) {
        if (!(fgp->mode & ATTR_WDUMMY)) {
            ptr += utf8_encode(fgp->rune, ptr);
        }
        fgp += 1;
    }
    *ptr = '\0';
    
    if (strcmp(buffer, expected_text) != 0) {
        fprintf(stderr, "Assertion failed on row %d:\n", row);
        fprintf(stderr, "  Expected: '%s'\n", expected_text);
        fprintf(stderr, "  Actual:   '%s'\n", buffer);
        assert(false);
    }
    
    if (expected_wrap) {
        assert((term.lines[row][term.ncols - 1].mode & ATTR_WRAP) != 0);
    } else {
        assert((term.lines[row][term.ncols - 1].mode & ATTR_WRAP) == 0);
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

    /* Scenario A: Width Shrinkage (Forcing Wraps) */
    inject_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    check_consistent_state();
    verify_line(0, "ABCDEFGHIJKLMNOPQRST", true);
    verify_line(1, "UVWXYZ", false);

    /* Scenario B: Width Expansion (Unwrapping) */
    term_resize(30, 10);
    check_consistent_state();
    verify_line(0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", false);

    /* Scenario C: Shrink width to force wrap again before testing history */
    term_resize(15, 10);
    check_consistent_state();
    verify_line(0, "ABCDEFGHIJKLMNO", true);
    verify_line(1, "PQRSTUVWXYZ", false);

    /* Scenario C.2: Height Shrinkage (Push to History) */
    term_resize(15, 5);
    check_consistent_state();
    
    /* Scenario D: Height Expansion (Pull from History) */
    term_resize(15, 10);
    check_consistent_state();
    verify_line(0, "ABCDEFGHIJKLMNO", true);
    verify_line(1, "PQRSTUVWXYZ", false);

    /* Scenario E: Alternate Screen Integrity */
    term_load_alt_screen(true, true);
    inject_text("ALT SCREEN TEXT");
    check_consistent_state();
    verify_line(0, "ALT SCREEN TEXT", false);
    
    /* Resize while in alternate screen (triggers term_resize_alt instead of term_reflow) */
    term_resize(25, 12);
    check_consistent_state();
    verify_line(0, "ALT SCREEN TEXT", false);

    /* Switch back to default screen - it should trigger a reflow to the new 25x12 dimensions */
    term_load_def_screen(false, true);
    check_consistent_state();
    
    /* The main screen was "ABCDEFGHIJKLMNO" (wrap) + "PQRSTUVWXYZ". 
       Expanded to 25 cols, it should now reflow to one line wrapped and one short line, 
       or since 25 < 26, it should be: "ABCDEFGHIJKLMNOPQRSTUVWXY" (wrap) + "Z" */
    verify_line(0, "ABCDEFGHIJKLMNOPQRSTUVWXY", true);
    verify_line(1, "Z", false);

    printf("All resize and reflow tests passed!\n");
    return 0;
}
