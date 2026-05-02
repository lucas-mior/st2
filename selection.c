void
selection_start(int32 col, int32 row, int32 snap) {
    selection_clear();
    selection.mode = SELECTION_EMPTY;
    selection.type = SELECTION_REGULAR;
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

void
selection_extend(int32 col, int32 row, int32 type, int32 done) {
    int32 oldey;
    int32 oldex;
    int32 oldsby;
    int32 oldsey;
    int32 oldtype;

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
        term_set_dirt(MIN(selection.nb.y, oldsby), MAX(selection.ne.y, oldsey));
    }

    if (done) {
        selection.mode = SELECTION_IDLE;
    } else {
        selection.mode = SELECTION_READY;
    }
    return;
}

void
selection_normalize(void) {
    int32 len;

    if (selection.type == SELECTION_REGULAR
        && selection.ob.y != selection.oe.y) {
        selection.nb.x
            = selection.ob.y < selection.oe.y ? selection.ob.x : selection.oe.x;
        selection.ne.x
            = selection.ob.y < selection.oe.y ? selection.oe.x : selection.ob.x;
    } else {
        selection.nb.x = (int32)MIN(selection.ob.x, selection.oe.x);
        selection.ne.x = (int32)MAX(selection.ob.x, selection.oe.x);
    }
    selection.nb.y = MIN(selection.ob.y, selection.oe.y);
    selection.ne.y = MAX(selection.ob.y, selection.oe.y);

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

int32
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

int32
selection_is_selected(int32 x, int32 y) {
    return selection_is_selected4(x, y, x, y);
}

void
SelectionSnap(int32 *x, int32 *y, int32 direction) {
    int32 newx;
    int32 newy;
    int32 xt;
    int32 yt;
    int32 rtop = 0;
    int32 rbot = term.nrows - 1;
    int32 delim;
    int32 prevdelim;
    const Glyph *gp, *prevgp;

    if (!TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        rtop += term.lines_scrolled_up - term.n_hist;
        rbot += term.lines_scrolled_up;
    }

    switch (selection.snap) {
    case SELECTION_SNAP_WORD:
        /*
         * Snap around if the word wraps around at the end or
         * beginning of a line.
         */
        prevgp = &TERM_LINE(*y)[*x];
        prevdelim = IS_DELIM(prevgp->rune);
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
                && (delim != prevdelim
                    || (delim && !(gp->rune == ' ' && prevgp->rune == ' ')))) {
                break;
            }

            *x = newx;
            *y = newy;
            prevgp = gp;
            prevdelim = delim;
        }
        break;
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

char *
selection_get(void) {
    char *string, *ptr;
    int32 lastx;
    int32 line_len;
    const Glyph *gp, *lgp;

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
        Glyph *line = TERM_LINE(y);

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
         * Copy and pasting of line endings is inconsistent
         * in the inconsistent terminal and GUI world.
         * The best solution seems like to produce '\n' when
         * something is copied from st and convert '\n' to
         * '\r', when something to be pasted is received by
         * st.
         * FIXME: Fix the computer world.
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

void
selection_clear(void) {
    if (selection.ob.x == -1) {
        return;
    }
    selection_remove();
    term_set_dirt(selection.nb.y, selection.ne.y);
    return;
}

void
selection_remove(void) {
    selection.mode = SELECTION_IDLE;
    selection.ob.x = -1;
    return;
}
