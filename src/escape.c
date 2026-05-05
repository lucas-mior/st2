#if !defined(ESCAPE_C)
#define ESCAPE_C

#include <wchar.h>
#include <ctype.h>

#include "st.h"
#include "config.h"
#include "base64.c"
#include "x.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_escape 1
#elif !defined(TESTING_escape)
#define TESTING_escape 0
#endif

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
typedef struct CSIEscape {
    char buffer[ESC_BUF_SIZ]; /* raw string */
    int64 len;                /* raw string length */
    char priv;
    int32 arg[ESC_ARG_SIZ];
    int32 narg; /* nb of args */
    char mode[2];
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct STREscape {
    char type;    /* ESC type ... */
    char *buffer; /* allocated raw string */
    int32 siz;    /* allocation size */
    int32 len;    /* raw string length */
    char *args[STR_ARG_SIZ];
    int32 nargs;
} STREscape;

static CSIEscape csi_escape_seq;
static STREscape str_escape_seq;

static void
control_seq_intro_dump(void) {
    error("ESC[");

    for (int64 i = 0; i < csi_escape_seq.len; i += 1) {
        uint32 c = csi_escape_seq.buffer[i] & 0xff;

        if (isprint(c)) {
            putc((int32)c, stderr);
            continue;
        }

        switch (c) {
        case '\n':
            error("(\\n)");
            break;
        case '\r':
            error("(\\r)");
            break;
        case 0x1b:
            error("(\\e)");
            break;
        default:
            error("(%02x)", c);
            break;
        }
    }

    putc('\n', stderr);
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

static void
control_seq_intro_parse(void) {
    char *p = csi_escape_seq.buffer;
    int32 sep = ';';

    csi_escape_seq.narg = 0;
    if (*p == '?') {
        csi_escape_seq.priv = 1;
        p += 1;
    }

    csi_escape_seq.buffer[csi_escape_seq.len] = '\0';
    while (p < csi_escape_seq.buffer + csi_escape_seq.len) {
        char *np = NULL;
        int64 v;

        v = strtol(p, &np, 10);
        if (np == p) {
            v = 0;
        }
        if (v == LONG_MAX || v == LONG_MIN) {
            v = -1;
        }
        csi_escape_seq.arg[csi_escape_seq.narg] = (int32)v;
        csi_escape_seq.narg += 1;
        p = np;
        if (sep == ';' && *p == ':') {
            sep = ':';
        }
        if (*p != sep || csi_escape_seq.narg == ESC_ARG_SIZ) {
            break;
        }
        p += 1;
    }
    csi_escape_seq.mode[0] = *p;
    p += 1;
    if (p < csi_escape_seq.buffer + csi_escape_seq.len) {
        csi_escape_seq.mode[1] = *p;
    } else {
        csi_escape_seq.mode[1] = '\0';
    }
    return;
}

static int32_t
term_def_color(int32 *attr, int32 *npar, int32 l) {
    int32_t idx = -1;
    uint32 r;
    uint32 g;
    uint32 b;

    switch (attr[*npar + 1]) {
    case 2:
        if (*npar + 4 >= l) {
            error("erresc(38): Incorrect number of parameters (%d)\n", *npar);
            break;
        }
        r = (uint32)attr[*npar + 2];
        g = (uint32)attr[*npar + 3];
        b = (uint32)attr[*npar + 4];
        *npar += 4;
        if (!BETWEEN(r, 0, 255) || !BETWEEN(g, 0, 255) || !BETWEEN(b, 0, 255)) {
            error("erresc: bad rgb color (%u,%u,%u)\n", r, g, b);
        } else {
            idx = (int32)TRUECOLOR(r, g, b);
        }
        break;
    case 5:
        if (*npar + 2 >= l) {
            error("erresc(38): Incorrect number of parameters (%d)\n", *npar);
            break;
        }
        *npar += 2;
        if (!BETWEEN(attr[*npar], 0, 255)) {
            error("erresc: bad fgcolor %d\n", attr[*npar]);
        } else {
            idx = attr[*npar];
        }
        break;
    case 0:
    case 1:
    case 3:
    case 4:
    default:
        error("erresc(38): gfx attr %d unknown\n", attr[*npar]);
        break;
    }

    return idx;
}

static void
term_set_attr(int32 *attr, int32 l) {
    for (int32 i = 0; i < l; i += 1) {
        int32_t idx;

        switch (attr[i]) {
        case 0:
            term.cursor.attr.mode &= ~(
                ATTR_BOLD | ATTR_FAINT | ATTR_ITALIC | ATTR_UNDERLINE
                | ATTR_BLINK | ATTR_REVERSE | ATTR_INVISIBLE | ATTR_STRUCK);
            term.cursor.attr.fg = CONF_COLOR_INDEX_FONT;
            term.cursor.attr.bg = CONF_COLOR_BG;
            break;
        case 1:
            term.cursor.attr.mode |= ATTR_BOLD;
            break;
        case 2:
            term.cursor.attr.mode |= ATTR_FAINT;
            break;
        case 3:
            term.cursor.attr.mode |= ATTR_ITALIC;
            break;
        case 4:
            term.cursor.attr.mode |= ATTR_UNDERLINE;
            break;
        case 5:
        case 6:
            term.cursor.attr.mode |= ATTR_BLINK;
            break;
        case 7:
            term.cursor.attr.mode |= ATTR_REVERSE;
            break;
        case 8:
            term.cursor.attr.mode |= ATTR_INVISIBLE;
            break;
        case 9:
            term.cursor.attr.mode |= ATTR_STRUCK;
            break;
        case 22:
            term.cursor.attr.mode &= ~(ATTR_BOLD | ATTR_FAINT);
            break;
        case 23:
            term.cursor.attr.mode &= ~ATTR_ITALIC;
            break;
        case 24:
            term.cursor.attr.mode &= ~ATTR_UNDERLINE;
            break;
        case 25:
            term.cursor.attr.mode &= ~ATTR_BLINK;
            break;
        case 27:
            term.cursor.attr.mode &= ~ATTR_REVERSE;
            break;
        case 28:
            term.cursor.attr.mode &= ~ATTR_INVISIBLE;
            break;
        case 29:
            term.cursor.attr.mode &= ~ATTR_STRUCK;
            break;
        case 38:
            idx = term_def_color(attr, &i, l);
            if (idx >= 0) {
                term.cursor.attr.fg = idx;
            }
            break;
        case 39:
            term.cursor.attr.fg = CONF_COLOR_INDEX_FONT;
            break;
        case 48:
            idx = term_def_color(attr, &i, l);
            if (idx >= 0) {
                term.cursor.attr.bg = idx;
            }
            break;
        case 49:
            term.cursor.attr.bg = CONF_COLOR_BG;
            break;
        case 58:
            term_def_color(attr, &i, l);
            break;
        default:
            if (BETWEEN(attr[i], 30, 37)) {
                term.cursor.attr.fg = attr[i] - 30;
            } else if (BETWEEN(attr[i], 40, 47)) {
                term.cursor.attr.bg = attr[i] - 40;
            } else if (BETWEEN(attr[i], 90, 97)) {
                term.cursor.attr.fg = attr[i] - 90 + 8;
            } else if (BETWEEN(attr[i], 100, 107)) {
                term.cursor.attr.bg = attr[i] - 100 + 8;
            } else {
                error("erresc(default): gfx attr %d unknown\n", attr[i]);
                control_seq_intro_dump();
            }
            break;
        }
    }
    return;
}

static void
term_set_mode(int32 priv, int32 set, int32 *args, int32 narg) {
    for (int32 i = 0; i < narg; i += 1) {
        if (priv) {
            switch (args[i]) {
            case 1:
                x_set_mode(set, WIN_MODE_APPCURSOR);
                break;
            case 5:
                x_set_mode(set, WIN_MODE_REVERSE);
                break;
            case 6:
                MODBIT(term.cursor.state, set, CURSOR_ORIGIN);
                term_move_abs_to(0, 0);
                break;
            case 7:
                MODBIT(term.mode, set, TERM_MODE_WRAP);
                break;
            case 0:
            case 2:
            case 3:
            case 4:
            case 8:
            case 18:
            case 19:
            case 42:
            case 12:
                break;
            case 25:
                x_set_mode(!set, WIN_MODE_HIDE);
                break;
            case 9:
                x_set_pointer_motion(0);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEX10);
                break;
            case 1000:
                x_set_pointer_motion(0);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEBTN);
                break;
            case 1002:
                x_set_pointer_motion(0);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEMOTION);
                break;
            case 1003:
                x_set_pointer_motion(set);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEMANY);
                break;
            case 1004:
                x_set_mode(set, WIN_MODE_FOCUS);
                break;
            case 1006:
                x_set_mode(set, WIN_MODE_MOUSESGR);
                break;
            case 1034:
                x_set_mode(set, WIN_MODE_8BIT);
                break;
            case 1049:
                if (!CONF_ALLOW_ALT_SCREEN) {
                    break;
                }
                if (set) {
                    term_cursor(CURSOR_SAVE);
                } else {
                    term_cursor(CURSOR_LOAD);
                }
                _X_FALLTHROUGH;
            case 47:
            case 1047:
                if (!CONF_ALLOW_ALT_SCREEN) {
                    break;
                }
                if (set) {
                    int32 should_clear;
                    int32 should_save;

                    if (args[i] == 1049) {
                        should_clear = 1;
                    } else {
                        should_clear = 0;
                    }

                    if (args[i] == 1049) {
                        should_save = 1;
                    } else {
                        should_save = 0;
                    }
                    term_load_alt_screen(should_clear, should_save);
                } else {
                    int32 should_clear_def;
                    int32 should_load_def;

                    if (args[i] == 1047) {
                        should_clear_def = 1;
                    } else {
                        should_clear_def = 0;
                    }

                    if (args[i] == 1049) {
                        should_load_def = 1;
                    } else {
                        should_load_def = 0;
                    }
                    term_load_def_screen(should_clear_def, should_load_def);
                }
                break;
            case 1048:
                if (!CONF_ALLOW_ALT_SCREEN) {
                    break;
                }
                if (set) {
                    term_cursor(CURSOR_SAVE);
                } else {
                    term_cursor(CURSOR_LOAD);
                }
                break;
            case 2004:
                x_set_mode(set, WIN_MODE_BRCKTPASTE);
                break;
            case 1001:
            case 1005:
            case 1015:
                break;
            case 80:
                MODBIT(term.mode, set, TERM_MODE_SIXEL_SDM);
                break;
            case 8452:
                MODBIT(term.mode, set, TERM_MODE_SIXEL_CUR_RT);
                break;
            default:
                error("erresc: unknown private set/reset mode %d\n", args[i]);
                break;
            }
        } else {
            switch (args[i]) {
            case 0:
                break;
            case 2:
                x_set_mode(set, WIN_MODE_KBDLOCK);
                break;
            case 4:
                MODBIT(term.mode, set, TERM_MODE_INSERT);
                break;
            case 12:
                MODBIT(term.mode, !set, TERM_MODE_ECHOO);
                break;
            case 20:
                MODBIT(term.mode, set, TERM_MODE_CRLF);
                break;
            default:
                error("erresc: unknown set/reset mode %d\n", args[i]);
                break;
            }
        }
    }

    return;
}

static void
control_seq_intro_handle(void) {
    char buffer[256];
    int32 n;
    int32 x;

    switch (csi_escape_seq.mode[0]) {
    default:
    unknown:
        error("erresc: unknown csi ");
        control_seq_intro_dump();
        break;
    case '@':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_insert_blank(csi_escape_seq.arg[0]);
        break;
    case 'A':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x, term.cursor.y - csi_escape_seq.arg[0]);
        break;
    case 'B':
    case 'e':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x, term.cursor.y + csi_escape_seq.arg[0]);
        break;
    case 'i':
        switch (csi_escape_seq.arg[0]) {
        case 0:
            term_dump();
            break;
        case 1:
            term_dump_line(term.cursor.y);
            break;
        case 2:
            term_dump_sel();
            break;
        case 4:
            term.mode &= ~TERM_MODE_PRINT;
            break;
        case 5:
            term.mode |= TERM_MODE_PRINT;
            break;
        default:
            error("control_seq_intro_handle: Unhandled switch case.\n");
            break;
        }
        break;
    case 'c':
        if (csi_escape_seq.arg[0] == 0) {
            tty_write(CONF_VTIDEN, strlen32(CONF_VTIDEN), 0);
        }
        break;
    case 'b':
        LIMIT(csi_escape_seq.arg[0], 1, 65535);
        if (term.last_char) {
            while (csi_escape_seq.arg[0] > 0) {
                term_putc(term.last_char);
                csi_escape_seq.arg[0] -= 1;
            }
        }
        break;
    case 'C':
    case 'a':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x + csi_escape_seq.arg[0], term.cursor.y);
        break;
    case 'D':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x - csi_escape_seq.arg[0], term.cursor.y);
        break;
    case 'E':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(0, term.cursor.y + csi_escape_seq.arg[0]);
        break;
    case 'F':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(0, term.cursor.y - csi_escape_seq.arg[0]);
        break;
    case 'g':
        switch (csi_escape_seq.arg[0]) {
        case 0:
            term.tabs[term.cursor.x] = false;
            break;
        case 3:
            memset64(term.tabs, 0, term.ncols*SIZEOF(*term.tabs));
            break;
        default:
            goto unknown;
        }
        break;
    case 'G':
    case '`':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(csi_escape_seq.arg[0] - 1, term.cursor.y);
        break;
    case 'H':
    case 'f':
        DEFAULT(csi_escape_seq.arg[0], 1);
        DEFAULT(csi_escape_seq.arg[1], 1);
        term_move_abs_to(csi_escape_seq.arg[1] - 1, csi_escape_seq.arg[0] - 1);
        break;
    case 'I':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_put_tab(csi_escape_seq.arg[0]);
        break;
    case 'J':
        switch (csi_escape_seq.arg[0]) {
        case 0:
            term_clear_region(term.cursor.x, term.cursor.y,
                              term.ncols - 1, term.cursor.y, true);
            if (term.cursor.y < term.nrows - 1) {
                term_clear_region(0, term.cursor.y + 1,
                                  term.ncols - 1, term.nrows - 1, true);
            }
            break;
        case 1:
            if (term.cursor.y >= 1) {
                term_clear_region(0, 0,
                                  term.ncols - 1, term.cursor.y - 1, true);
            }
            term_clear_region(0, term.cursor.y,
                              term.cursor.x, term.cursor.y, true);
            break;
        case 2:
            if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
                term_clear_region(0, 0, term.ncols - 1, term.nrows - 1, true);
                term_delete_images();
                break;
            }
            n = term.nrows - 1;
            while (n >= 0 && term_line_len(term.lines[n]) == 0) {
                n -= 1;
            }
            for (ImageList *im = term.images; im; im = im->next) {
                n = (int32)MAX(im->y - term.scrolled_up, n);
            }
            if (n >= 0) {
                term_scroll_up(0, term.nrows - 1, n + 1, SCROLL_SAVEHIST);
            }
            term_scroll_up(0, term.nrows - 1,
                           term.nrows - n - 1, SCROLL_NOSAVEHIST);
            term_delete_images();
            break;
        case 6:
            term_delete_images();
            term_full_dirt();
            break;
        default:
            goto unknown;
        }
        break;
    case 'K':
        switch (csi_escape_seq.arg[0]) {
        case 0:
            term_clear_region(term.cursor.x, term.cursor.y,
                              term.ncols - 1, term.cursor.y, true);
            break;
        case 1:
            term_clear_region(0, term.cursor.y,
                              term.cursor.x, term.cursor.y, true);
            break;
        case 2:
            term_clear_region(0, term.cursor.y,
                              term.ncols - 1, term.cursor.y, true);
            break;
        default:
            error("control_seq_intro_handle: Unhandled switch case.\n");
            break;
        }
        break;
    case 'S':
        if (csi_escape_seq.priv) {
            if (csi_escape_seq.narg > 1) {
                int32 pi = csi_escape_seq.arg[0];
                int32 pa = csi_escape_seq.arg[1];
                if (pi == 1 && (pa == 1 || pa == 2 || pa == 4)) {
                    n = SNPRINTF(buffer, "\033[?1;0;%dS", DECSIXEL_PALETTE_MAX);
                    tty_write(buffer, n, 1);
                    break;
                } else {
                    if (pi == 2 && (pa == 1 || pa == 2 || pa == 4)) {
                        llong mw = (llong)MIN(term.ncols*term_window.cw,
                                              DECSIXEL_WIDTH_MAX);
                        llong mh = (llong)MIN(term.nrows*term_window.ch,
                                              DECSIXEL_HEIGHT_MAX);
                        n = SNPRINTF(buffer, "\033[?2;0;%lld;%lldS", mw, mh);
                        tty_write(buffer, n, 1);
                        break;
                    }
                }
                n = SNPRINTF(buffer, "\033[?%d;3;0S", pi);
                tty_write(buffer, n, 1);
            }
        }
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_scroll_up(term.top_scroll_limit, term.bot_scroll_limit,
                       csi_escape_seq.arg[0], SCROLL_SAVEHIST);
        break;
    case 'T':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_scroll_down(term.top_scroll_limit, csi_escape_seq.arg[0]);
        break;
    case 'L':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_insert_blank_line(csi_escape_seq.arg[0]);
        break;
    case 'l':
        term_set_mode(csi_escape_seq.priv, 0, csi_escape_seq.arg,
                      csi_escape_seq.narg);
        break;
    case 'M':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_delete_line(csi_escape_seq.arg[0]);
        break;
    case 'X':
        if (csi_escape_seq.arg[0] < 0) {
            return;
        }
        DEFAULT(csi_escape_seq.arg[0], 1);
        x = (int32)MIN(term.cursor.x + csi_escape_seq.arg[0], term.ncols) - 1;
        term_clear_region(term.cursor.x, term.cursor.y, x, term.cursor.y, true);
        break;
    case 'P':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_delete_char(csi_escape_seq.arg[0]);
        break;
    case 'Z':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_put_tab(-csi_escape_seq.arg[0]);
        break;
    case 'd':
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_abs_to(term.cursor.x, csi_escape_seq.arg[0] - 1);
        break;
    case 'h':
        term_set_mode(csi_escape_seq.priv, 1, csi_escape_seq.arg,
                      csi_escape_seq.narg);
        break;
    case 'm':
        term_set_attr(csi_escape_seq.arg, csi_escape_seq.narg);
        break;
    case 'n':
        switch (csi_escape_seq.arg[0]) {
        case 5:
            tty_write("\033[0n", SIZEOF("\033[0n") - 1, 0);
            break;
        case 6:
            n = SNPRINTF(buffer,
                         "\033[%i;%iR", term.cursor.y + 1, term.cursor.x + 1);
            tty_write(buffer, (int64)n, 0);
            break;
        default:
            goto unknown;
        }
        break;
    case '$':
        if (csi_escape_seq.mode[1] == 'p' && csi_escape_seq.priv) {
            switch (csi_escape_seq.arg[0]) {
            case 5:
                tty_write("\033[?5;2$y", 8,
                          0);
                break;
            case 80:
                if (term_mode_is_set(TERM_MODE_SIXEL_SDM)) {
                    tty_write("\033[?80;1$y", 9, 0);
                } else {
                    tty_write("\033[?80;2$y", 9, 0);
                }
                break;
            case 8452:
                if (term_mode_is_set(TERM_MODE_SIXEL_CUR_RT)) {
                    tty_write("\033[?8452;1$y", 11, 0);
                } else {
                    tty_write("\033[?8452;2$y", 11, 0);
                }
                break;
            default:
                goto unknown;
            }
            break;
        }
        goto unknown;
    case 'r':
        if (csi_escape_seq.priv) {
            goto unknown;
        } else {
            DEFAULT(csi_escape_seq.arg[0], 1);
            DEFAULT(csi_escape_seq.arg[1], term.nrows);
            {
                int32 t = csi_escape_seq.arg[0] - 1;
                int32 b = csi_escape_seq.arg[1] - 1;

                LIMIT(t, 0, term.nrows - 1);
                LIMIT(b, 0, term.nrows - 1);
                if (t > b) {
                    int32 temp_val = t;
                    t = b;
                    b = temp_val;
                }
                term.top_scroll_limit = t;
                term.bot_scroll_limit = b;
            }
            term_move_abs_to(0, 0);
        }
        break;
    case 's':
        term_cursor(CURSOR_SAVE);
        break;
    case 't':
        switch (csi_escape_seq.arg[0]) {
        case 14:
            if (csi_escape_seq.narg > 1) {
                goto unknown;
            }
            n = SNPRINTF(buffer,
                         "\033[4;%d;%dt",
                         term.nrows*term_window.ch, term.ncols*term_window.cw);
            tty_write(buffer, n, 1);
            break;
        case 16:
            n = SNPRINTF(buffer,
                         "\033[6;%d;%dt",
                         term_window.ch, term_window.cw);
            tty_write(buffer, n, 1);
            break;
        case 18:
            n = SNPRINTF(buffer, "\033[8;%d;%dt", term.nrows, term.ncols);
            tty_write(buffer, n, 1);
            break;
        default:
            goto unknown;
        }
        break;
    case 'u':
        if (csi_escape_seq.priv) {
            goto unknown;
        } else {
            term_cursor(CURSOR_LOAD);
        }
        break;
    case ' ':
        switch (csi_escape_seq.mode[1]) {
        case 'q':
            if (x_set_cursor(csi_escape_seq.arg[0])) {
                goto unknown;
            }
            break;
        default:
            goto unknown;
        }
        break;
    }
    return;
}

static void
control_seq_intro_reset(void) {
    memset64(&csi_escape_seq, 0, SIZEOF(csi_escape_seq));
    return;
}

static void
osc_color_response(int32 num, int32 index, int32 is_osc4) {
    int32 n;
    char buffer[128];
    uchar r;
    uchar g;
    uchar b;
    int32 x;
    char *prefix;

    if (is_osc4) {
        x = num;
        prefix = "4;";
    } else {
        x = index;
        prefix = "";
    }

    if (!BETWEEN(x, 0, draw_context.colors_len - 1)) {
        char *type_str;
        if (is_osc4) {
            type_str = "osc4";
        } else {
            type_str = "osc";
        }

        if (is_osc4) {
            error("erresc: failed to fetch %s color %d\n", type_str, num);
        } else {
            error("erresc: failed to fetch %s color %d\n", type_str, index);
        }
    }

    r = draw_context.colors[x].color.red >> 8;
    g = draw_context.colors[x].color.green >> 8;
    b = draw_context.colors[x].color.blue >> 8;

    n = SNPRINTF(buffer,
                 "\033]%s%d;rgb:%02x%02x/%02x%02x/%02x%02x\007",
                 prefix, num, r, r, g, g, b, b);
    tty_write(buffer, (int64)n, 1);
    return;
}

static void
string_handle(void) {
    char *p = NULL;
    int32 j;
    int32 narg;
    int32 par;
    int32 scr_offset;

    struct {
        int32 idx;
        char *string;
    } osc_table[] = {{CONF_COLOR_INDEX_FONT, "foreground"},
                     {CONF_COLOR_BG, "background"},
                     {CONF_COLOR_INDEX_CURSOR, "cursor"}};

    if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        scr_offset = 0;
    } else {
        scr_offset = term.scrolled_up;
    }

    term.esc &= ~(ESC_STR_END | ESC_STR);
    {
        int32 c_char;
        char *p2 = str_escape_seq.buffer;

        str_escape_seq.nargs = 0;
        str_escape_seq.buffer[str_escape_seq.len] = '\0';

        if (*p2 != '\0') {
            while (str_escape_seq.nargs < STR_ARG_SIZ) {
                str_escape_seq.args[str_escape_seq.nargs] = p2;
                str_escape_seq.nargs += 1;
                while (1) {
                    c_char = *p2;
                    if (c_char == ';' || c_char == '\0') {
                        break;
                    }
                    p2 += 1;
                }
                if (c_char == '\0') {
                    break;
                }
                *p2 = '\0';
                p2 += 1;
            }
        }
    }

    narg = str_escape_seq.nargs;
    if (narg) {
        par = atoi(str_escape_seq.args[0]);
    } else {
        par = 0;
    }

    switch (str_escape_seq.type) {
    case ']':
        switch (par) {
        case 0:
            if (narg > 1) {
                x_set_title(str_escape_seq.args[1]);
                x_set_icon_title(str_escape_seq.args[1]);
            }
            return;
        case 1:
            if (narg > 1) {
                x_set_icon_title(str_escape_seq.args[1]);
            }
            return;
        case 2:
            if (narg > 1) {
                x_set_title(str_escape_seq.args[1]);
            }
            return;
        case 52:
            if (narg > 2 && CONF_ALLOW_WINDOW_OPS) {
                char *dec = base64_decode(str_escape_seq.args[2]);
                if (dec) {
                    selection_set(dec, CurrentTime);
                    user_clipboard_copy(NULL);
                    free(dec);
                } else {
                    error("erresc: invalid base64\n");
                }
            }
            return;
        case 10:
        case 11:
        case 12:
            if (narg < 2) {
                break;
            }
            p = str_escape_seq.args[1];
            j = par - 10;
            if (j < 0 || j >= LENGTH(osc_table)) {
                break;
            }

            if (!strcmp(p, "?")) {
                osc_color_response(par, osc_table[j].idx, 0);
            } else {
                if (x_set_color_name(osc_table[j].idx, p)) {
                    error("erresc: invalid %s color: %s\n",
                          osc_table[j].string, p);
                } else {
                    term_full_dirt();
                }
            }
            return;
        case 4:
            if (narg < 3) {
                break;
            }
            p = str_escape_seq.args[2];
            _X_FALLTHROUGH;
        case 104:
            if (narg > 1) {
                j = atoi(str_escape_seq.args[1]);
            } else {
                j = -1;
            }

            if (p && !strcmp(p, "?")) {
                osc_color_response(j, 0, 1);
            } else {
                if (x_set_color_name(j, p)) {
                    if (par == 104 && narg <= 1) {
                        x_load_cols();
                        return;
                    }
                    if (p) {
                        error("erresc: invalid color j=%d, p=%s\n", j, p);
                    } else {
                        error("erresc: invalid color j=%d, p=%s\n", j, "(null)");
                    }
                } else {
                    term_full_dirt();
                }
            }
            return;
        case 110:
        case 111:
        case 112:
            if (narg != 1) {
                break;
            }
            j = par - 110;
            if (j < 0 || j >= LENGTH(osc_table)) {
                break;
            }
            if (x_set_color_name(osc_table[j].idx, NULL)) {
                error("erresc: %s color not found\n", osc_table[j].string);
            } else {
                term_full_dirt();
            }
            return;
        default:
            error("string_handle: Unhandled switch case.\n");
            break;
        }
        break;
    case 'k':
        x_set_title(str_escape_seq.args[0]);
        return;
    case 'P':
        if (term_mode_is_set(TERM_MODE_SIXEL)) {
            ImageList *newimages = NULL;
            ImageList *next_im;
            ImageList *tail = NULL;
            int32 x1_im;
            int32 y1_im;
            int32 x2_im;
            int32 y2_im;
            int32 y_line;
            int32 numimages;
            int32 cx_pos;
            int32 cy_pos;

            term.mode &= ~TERM_MODE_SIXEL;
            if (sixel_st.image.data == NULL) {
                sixel_parser_deinit(&sixel_st);
                return;
            }
            if (term_mode_is_set(TERM_MODE_SIXEL_SDM)) {
                cx_pos = 0;
            } else {
                cx_pos = term.cursor.x;
            }
            if (term_mode_is_set(TERM_MODE_SIXEL_SDM)) {
                cy_pos = 0;
            } else {
                cy_pos = term.cursor.y;
            }
            numimages
                = sixel_parser_finalize(&sixel_st, &newimages, cx_pos, cy_pos + scr_offset,
                                        term_window.cw, term_window.ch);

            if (numimages <= 0 || newimages == NULL || newimages->cols <= 0) {
                if (newimages) {
                    delete_image(newimages);
                }
                sixel_parser_deinit(&sixel_st);
                return;
            }

            sixel_parser_deinit(&sixel_st);
            x1_im = newimages->x;
            y1_im = newimages->y;
            x2_im = x1_im + newimages->cols;
            y2_im = y1_im + numimages;

            if (term.images) {
                char *transparent_rows = xmalloc(numimages);
                ImageList *im_ptr;
                int32 i_idx;
                for (i_idx = 0, im_ptr = newimages; im_ptr; im_ptr = im_ptr->next, i_idx += 1) {
                    transparent_rows[i_idx] = (char)im_ptr->transparent;
                }
                for (im_ptr = term.images; im_ptr; im_ptr = next_im) {
                    next_im = im_ptr->next;
                    if (im_ptr->y >= y1_im && im_ptr->y < y2_im) {
                        y_line = im_ptr->y - scr_offset;
                        if (y_line >= 0 && y_line < term.nrows && term.dirts[y_line]) {
                            StGlyph *line_ptr = term.lines[y_line];
                            j = (int32)MIN(im_ptr->x + im_ptr->cols, term.ncols);
                            for (i_idx = im_ptr->x; i_idx < j; i_idx += 1) {
                                if (line_ptr[i_idx].mode & ATTR_SIXEL) {
                                    break;
                                }
                            }
                            // TODO: Use-After-Free / Corrupted Linked List.
                            // `delete_image(im_ptr)` frees the image, but
                            // `im_ptr` is never unlinked from the `term.images`
                            // list. The preceding node's `next` pointer will
                            // still point to this freed memory, leading to a
                            // Use-After-Free crash on the next traversal.
                            if (i_idx == j) {
                                delete_image(im_ptr);
                                continue;
                            }
                        }
                        // TODO: Use-After-Free / Corrupted Linked List.
                        // Similar to above, `im_ptr` is freed but not unlinked
                        // from the linked list.
                        if (im_ptr->x >= x1_im && im_ptr->x + im_ptr->cols <= x2_im
                            && !transparent_rows[im_ptr->y - y1_im]) {
                            delete_image(im_ptr);
                            continue;
                        }
                    }
                    tail = im_ptr;
                }
                free(transparent_rows);
            }

            if (tail) {
                tail->next = newimages;
                newimages->prev = tail;
            } else {
                term.images = newimages;
            }

            x2_im = (int32)MIN(x2_im, term.ncols) - 1;

            if (term_mode_is_set(TERM_MODE_SIXEL_SDM)) {
                ImageList *im_sdm;
                int32 i_sdm;
                for (i_sdm = 0, im_sdm = newimages; im_sdm; im_sdm = next_im, i_sdm += 1) {
                    next_im = im_sdm->next;
                    if (i_sdm >= term.nrows) {
                        delete_image(im_sdm);
                        continue;
                    }
                    im_sdm->y = i_sdm + scr_offset;
                    term_set_sixel_attr(term.lines[i_sdm], x1_im, x2_im);
                    term.dirts[MIN(im_sdm->y, term.nrows - 1)] = 1;
                }
            } else {
                ImageList *im_cur;
                int32 i_cur;
                for (i_cur = 0, im_cur = newimages; im_cur; im_cur = next_im, i_cur += 1) {
                    next_im = im_cur->next;
                    if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
                        scr_offset = 0;
                    } else {
                        scr_offset = term.scrolled_up;
                    }
                    im_cur->y = term.cursor.y + scr_offset;
                    term_set_sixel_attr(term.lines[term.cursor.y], x1_im, x2_im);
                    term.dirts[MIN(im_cur->y, term.nrows - 1)] = 1;

                    if (i_cur < numimages - 1) {
                        im_cur->next = NULL;
                        term_new_line(0);
                        im_cur->next = next_im;
                    }
                }

                if (term_mode_is_set(TERM_MODE_SIXEL_CUR_RT)) {
                    term.cursor.x = (int32)MIN(term.cursor.x + newimages->cols,
                                               term.ncols - 1);
                } else {
                    /* Reset X to 0 on the current row. Trailing \n in the stream 
                       will move to the next physical row without a gap. */
                    term.cursor.x = 0;
                }
            }
        }
        return;
    case '_':
    case '^':
        return;
    default:
        error("string_handle: Unhandled switch case.\n");
        break;
    }

    error("erresc: unknown string ");
    {
        error("ESC%c", str_escape_seq.type);
        for (int32 i = 0; i < str_escape_seq.len; i += 1) {
            uint32 c_code = str_escape_seq.buffer[i] & 0xff;

            if (c_code == '\0') {
                putc('\n', stderr);
                return;
            }

            if (isprint(c_code)) {
                putc((int32)c_code, stderr);
                continue;
            }

            switch (c_code) {
            case '\n':
                error("(\\n)");
                break;
            case '\r':
                error("(\\r)");
                break;
            case 0x1b:
                error("(\\e)");
                break;
            default:
                error("(%02x)", c_code);
                break;
            }
        }
        error("ESC\\\n");
    }

    return;
}

static void
term_put_tab(int32 n) {
    int32 x = term.cursor.x;

    if (n > 0) {
        while (x < term.ncols && n > 0) {
            x += 1;
            while (x < term.ncols && !term.tabs[x]) {
                x += 1;
            }
            n -= 1;
        }
    } else {
        if (n < 0) {
            while (x > 0 && n < 0) {
                x -= 1;
                while (x > 0 && !term.tabs[x]) {
                    x -= 1;
                }
                n += 1;
            }
        }
    }
    term.cursor.x = LIMIT(x, 0, term.ncols - 1);
    return;
}

static void
term_def_utf8(char ascii) {
    if (ascii == 'G') {
        term.mode |= TERM_MODE_UTF8;
    } else {
        if (ascii == '@') {
            term.mode &= ~TERM_MODE_UTF8;
        }
    }
    return;
}

static void
term_def_tran(char ascii) {
    static char cs[] = "0B";
    static int32 vcs[] = {CS_GRAPHIC0, CS_USA};
    char *p;

    p = strchr(cs, ascii);
    if (p == NULL) {
        error("esc unhandled charset: ESC ( %c\n", ascii);
    } else {
        term.translation_table[term.icharset] = (char)vcs[p - cs];
    }
    return;
}

static void
term_dec_test(char c) {
    if (c == '8') {
        for (int32 x = 0; x < term.ncols; x += 1) {
            for (int32 y = 0; y < term.nrows; y += 1) {
                term_set_char('E', &term.cursor.attr, x, y);
            }
        }
    }
    return;
}

static void
term_str_sequence(uchar c) {
    str_escape_seq.buffer = xrealloc(str_escape_seq.buffer, STR_BUF_SIZ);
    str_escape_seq.siz = STR_BUF_SIZ;
    str_escape_seq.len = 0;
    str_escape_seq.nargs = 0;

    switch (c) {
    case 0x90:
        c = 'P';
        term.esc |= ESC_DCS;
        break;
    case 0x9f:
        c = '_';
        break;
    case 0x9e:
        c = '^';
        break;
    case 0x9d:
        c = ']';
        break;
    default:
        error("term_str_sequence: unhandled switch case.\n");
        break;
    }
    str_escape_seq.type = (char)c;
    term.esc |= ESC_STR;
    return;
}

static void
dcs_handle(void) {
    uint bgcolor;
    int32 transparent;
    uint r = 0xCD;
    uint g = 0xCD;
    uint b = 0xCD;
    uint a = 255;

    switch (csi_escape_seq.mode[0]) {
    default:
        error("erresc: unknown csi ");
        control_seq_intro_dump();
        break;
    case 'q':
        if (csi_escape_seq.narg >= 2 && csi_escape_seq.arg[1] == 1) {
            transparent = 1;
        } else {
            transparent = 0;
        }
        
        if (IS_TRUECOL(term.cursor.attr.bg)) {
            r = (term.cursor.attr.bg >> 16) & 255;
            g = (term.cursor.attr.bg >> 8) & 255;
            b = (term.cursor.attr.bg >> 0) & 255;
        } else {
            x_get_color(term.cursor.attr.bg, &r, &g, &b);
            if (term.cursor.attr.bg == CONF_COLOR_BG) {
                a = (draw_context.colors[CONF_COLOR_BG].pixel >> 24) & 255;
            }
        }
        bgcolor = (a << 24) | (r << 16) | (g << 8) | b;
        if (sixel_parser_init(&sixel_st, transparent,
                              (255u << 24), bgcolor, 1,
                              term_window.cw, term_window.ch)) {
            error("Error in sixel_parser_init.\n");
            fatal(EXIT_FAILURE);
        }
        term.mode |= TERM_MODE_SIXEL;
        break;
    }
    return;
}

static void
term_control_code(uchar ascii) {
    switch (ascii) {
    case '\t':
        term_put_tab(1);
        return;
    case '\b':
        term_move_to(term.cursor.x - 1, term.cursor.y);
        return;
    case '\r':
        term_move_to(0, term.cursor.y);
        return;
    case '\f':
    case '\v':
    case '\n':
        term_new_line(term_mode_is_set(TERM_MODE_CRLF));
        return;
    case '\a':
        if (term.esc & ESC_STR_END) {
            string_handle();
        } else {
            x_bell();
        }
        break;
    case '\033':
        control_seq_intro_reset();
        term.esc &= ~(ESC_CSI | ESC_ALTCHARSET | ESC_TEST);
        term.esc |= ESC_START;
        return;
    case '\016':
    case '\017':
        term.charset = 1 - (ascii - '\016');
        return;
    case '\032':
        term_set_char('?', &term.cursor.attr, term.cursor.x, term.cursor.y);
        _X_FALLTHROUGH;
    case '\030':
        control_seq_intro_reset();
        break;
    case '\005':
    case '\000':
    case '\021':
    case '\023':
    case 0177:
        return;
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
        break;
    case 0x85:
        term_new_line(1);
        break;
    case 0x86:
    case 0x87:
        break;
    case 0x88:
        term.tabs[term.cursor.x] = true;
        break;
    case 0x89:
    case 0x8a:
    case 0x8b:
    case 0x8c:
    case 0x8d:
    case 0x8e:
    case 0x8f:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97:
    case 0x98:
    case 0x99:
        break;
    case 0x9a:
        tty_write(CONF_VTIDEN, strlen32(CONF_VTIDEN), 0);
        break;
    case 0x9b:
    case 0x9c:
        break;
    case 0x90:
    case 0x9d:
    case 0x9e:
    case 0x9f:
        term_str_sequence(ascii);
        return;
    default:
        error("term_control_code: unhandled switch case.\n");
        break;
    }
    term.esc &= ~(ESC_STR_END | ESC_STR);
    return;
}

static int32
eschandle(uchar ascii) {
    switch (ascii) {
    case '[':
        term.esc |= ESC_CSI;
        return 0;
    case '#':
        term.esc |= ESC_TEST;
        return 0;
    case '%':
        term.esc |= ESC_UTF8;
        return 0;
    case 'P':
        term.esc |= ESC_DCS;
        _X_FALLTHROUGH;
    case '_':
    case '^':
    case ']':
    case 'k':
        term_str_sequence(ascii);
        return 0;
    case 'n':
    case 'o':
        term.charset = 2 + (ascii - 'n');
        break;
    case '(':
    case ')':
    case '*':
    case '+':
        term.icharset = ascii - '(';
        term.esc |= ESC_ALTCHARSET;
        return 0;
    case 'D':
        if (term.cursor.y == term.bot_scroll_limit) {
            term_scroll_up(term.top_scroll_limit, term.bot_scroll_limit,
                           1, SCROLL_SAVEHIST);
        } else {
            term_move_to(term.cursor.x, term.cursor.y + 1);
        }
        break;
    case 'E':
        term_new_line(1);
        break;
    case 'H':
        term.tabs[term.cursor.x] = true;
        break;
    case 'M':
        if (term.cursor.y == term.top_scroll_limit) {
            term_scroll_down(term.top_scroll_limit, 1);
        } else {
            term_move_to(term.cursor.x, term.cursor.y - 1);
        }
        break;
    case 'Z':
        tty_write(CONF_VTIDEN, strlen32(CONF_VTIDEN), 0);
        break;
    case 'c':
        term_reset();
        x_set_title(NULL);
        x_load_cols();
        x_set_mode(0, WIN_MODE_HIDE);
        break;
    case '=':
        x_set_mode(1, WIN_MODE_APPKEYPAD);
        break;
    case '>':
        x_set_mode(0, WIN_MODE_APPKEYPAD);
        break;
    case '7':
        term_cursor(CURSOR_SAVE);
        break;
    case '8':
        term_cursor(CURSOR_LOAD);
        break;
    case '\\':
        if (term.esc & ESC_STR_END) {
            string_handle();
        }
        break;
    default:
        error("erresc: unknown sequence ESC 0x%02X '%c'\n",
              (uchar)ascii, isprint(ascii) ? ascii : '.');
        break;
    }
    return 1;
}

static void
term_putc(uint32 u) {
    char c[UTF_SIZ];
    int32 control;
    int32 width = 0;
    int32 len;
    StGlyph *glyph;

    control = IS_CONTROl(u);
    if (u < 127 || !term_mode_is_set(TERM_MODE_UTF8)) {
        c[0] = (char)u;
        width = 1;
        len = 1;
    } else {
        len = (int32)utf8_encode(u, c);
        if (!control) {
            width = wcwidth((wchar_t)u);
            if (width == -1) {
                width = 1;
            }
        }
    }

    if (term_mode_is_set(TERM_MODE_PRINT)) {
        term_printer(c, len);
    }

    if (term.esc & ESC_STR) {
        if (u == '\a' || u == 030 || u == 032 || u == 033 || IS_CONTROL_C1(u)) {
            term.esc &= ~(ESC_START | ESC_STR | ESC_DCS);
            term.esc |= ESC_STR_END;
            goto check_control_code;
        }

        if (term.esc & ESC_DCS) {
            goto check_control_code;
        }

        // TODO: Memory Error / DoS (Unbounded Allocation).
        // str_escape_seq.siz is doubled without any upper limit. An attacker
        // can send an unterminated STR/OSC sequence, causing the terminal to
        // allocate memory until it crashes from OOM. Add a maximum size limit.
        if (str_escape_seq.len + len >= str_escape_seq.siz) {
            str_escape_seq.siz *= 2;
            str_escape_seq.buffer = xrealloc(str_escape_seq.buffer,
                                             str_escape_seq.siz);
        }

        memmove64(&str_escape_seq.buffer[str_escape_seq.len], c, len);
        str_escape_seq.len += (uint64)len;

        if (str_escape_seq.type == 'P' && u == 'q') {
            int32 is_sixel = 1;
            for (int32 i = 0; i < str_escape_seq.len - 1; i += 1) {
                if (str_escape_seq.buffer[i] != ';'
                    && !isdigit((uchar)str_escape_seq.buffer[i])) {
                    is_sixel = 0;
                    break;
                }
            }
            // TODO: Integration Bug / State Inconsistency.
            // This inline detection sets `term.esc |= ESC_SIXEL`, but the main
            // read loop in `term_write` checks
            // `term_mode_is_set(TERM_MODE_SIXEL)`. The payload will not be
            // routed to `sixel_parser_parse` and will incorrectly buffer in
            // `str_escape_seq` until OOM.
            if (is_sixel) {
                term.esc |= ESC_SIXEL;
                sixel_parser_init(&sixel_st, 1, 0, 0, 1, term_window.cw,
                                  term_window.ch);
                str_escape_seq.len = 0;
            }
        }
        return;
    }

check_control_code:
    if (control) {
        if (term_mode_is_set(TERM_MODE_UTF8) && IS_CONTROL_C1(u)) {
            return;
        }
        term_control_code((uchar)u);
        if (!term.esc) {
            term.last_char = 0;
        }
        return;
    } else {
        if (term.esc & ESC_START) {
            if (term.esc & ESC_CSI) {
                csi_escape_seq.buffer[csi_escape_seq.len] = (char)u;
                csi_escape_seq.len += 1;
                if (BETWEEN(u, 0x40, 0x7E)
                    || csi_escape_seq.len >= SIZEOF(csi_escape_seq.buffer) - 1) {
                    term.esc = 0;
                    control_seq_intro_parse();
                    control_seq_intro_handle();
                }
                return;
            } else {
                if (term.esc & ESC_DCS) {
                    if (csi_escape_seq.len < SIZEOF(csi_escape_seq.buffer) - 1) {
                        csi_escape_seq.buffer[csi_escape_seq.len] = (char)u;
                        csi_escape_seq.len += 1;
                        if (BETWEEN(u, 0x40, 0x7E)
                            || csi_escape_seq.len >= SIZEOF(csi_escape_seq.buffer) - 1) {
                            control_seq_intro_parse();
                            dcs_handle();
                        }
                    }
                    return;
                } else {
                    if (term.esc & ESC_UTF8) {
                        term_def_utf8((char)u);
                    } else {
                        if (term.esc & ESC_ALTCHARSET) {
                            term_def_tran((char)u);
                        } else {
                            if (term.esc & ESC_TEST) {
                                term_dec_test((char)u);
                            } else {
                                if (!eschandle((uchar)u)) {
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            term.esc = 0;
            return;
        }
    }

    if (selection_is_selected(term.cursor.x + term.scrolled_up,
                              term.cursor.y + term.scrolled_up)) {
        selection_clear();
    }

    glyph = &term.lines[term.cursor.y][term.cursor.x];
    if (term_mode_is_set(TERM_MODE_WRAP)) {
        if (term.cursor.state & CURSOR_WRAPNEXT) {
            glyph->mode |= ATTR_WRAP;
            term_new_line(1);
            glyph = &term.lines[term.cursor.y][term.cursor.x];
        }
    }

    if (term_mode_is_set(TERM_MODE_INSERT)) {
        if (term.cursor.x + width < term.ncols) {
            memmove64(glyph + width, glyph,
                      (term.ncols - term.cursor.x - width)*SIZEOF(StGlyph));
            glyph->mode &= ~ATTR_WIDE;
        }
    }

    if (term.cursor.x + width > term.ncols) {
        if (term_mode_is_set(TERM_MODE_WRAP)) {
            term_new_line(1);
        } else {
            term_move_to(term.ncols - width, term.cursor.y);
        }
        glyph = &term.lines[term.cursor.y][term.cursor.x];
    }

    term_set_char(u, &term.cursor.attr, term.cursor.x, term.cursor.y);
    term.last_char = u;

    if (width == 2) {
        glyph->mode |= ATTR_WIDE;
        if (term.cursor.x + 1 < term.ncols) {
            if (glyph[1].mode == ATTR_WIDE && term.cursor.x + 2 < term.ncols) {
                glyph[2].rune = ' ';
                glyph[2].mode &= ~ATTR_WDUMMY;
            }
            glyph[1].rune = '\0';
            glyph[1].mode = ATTR_WDUMMY;
        }
    }
    if (term.cursor.x + width < term.ncols) {
        term_move_to(term.cursor.x + width, term.cursor.y);
    } else {
        if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
            term.wrap_char_width[1] = width;
        } else {
            term.wrap_char_width[0] = width;
        }
        term.cursor.state |= CURSOR_WRAPNEXT;
    }
    return;
}

static int32
term_write(char *buffer, int32 buflen, bool show_ctrl) {
    int32 char_size;
    int32 n;

    for (n = 0; n < buflen; n += char_size) {
        uint32 u;

        if (term_mode_is_set(TERM_MODE_SIXEL)
                && (sixel_st.state != PARSE_STATE_ESC)) {
            // TODO: Unhandled Edge Case / Infinite Loop.
            // If `sixel_parser_parse` returns 0 (e.g., waiting for more data or
            // on error), `char_size` becomes 0. The loop counter `n` will not
            // increment (`n += char_size`), causing an infinite loop that
            // freezes the terminal.
            char_size = sixel_parser_parse(&sixel_st,
                                          (uchar *)buffer + n, buflen - n);
            continue;
        } else if (term_mode_is_set(TERM_MODE_UTF8)) {
            char_size = (int32)utf8_decode(buffer + n, &u, (int64)(buflen - n));
            if (char_size == 0) {
                break;
            }
        } else {
            u = buffer[n] & 0xFF;
            char_size = 1;
        }

        if (show_ctrl && IS_CONTROl(u)) {
            if (u & 0x80) {
                u &= 0x7f;
                term_putc('^');
                term_putc('[');
            } else {
                if (u != '\n' && u != '\r' && u != '\t') {
                    u ^= 0x40;
                    term_putc('^');
                    term_putc('[');
                }
            }
        }

        term_putc(u);
    }
    return n;
}

#if TESTING_escape

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "assert.c"
#include "st.c"
#include "x.c"
#include "user.c"
#include "boxdraw.c"

int32
main(void) {
    /* 1. Reset Global State and Context */
    {
        x_window.display = XOpenDisplay(NULL);
        if (!x_window.display) {
            exit(EXIT_FAILURE);
        }
        x_window.screen = XDefaultScreen(x_window.display);
        x_window.visual = DefaultVisual(x_window.display, x_window.screen);
        x_window.depth = DefaultDepth(x_window.display, x_window.screen);
        x_window.color_map = DefaultColormap(x_window.display, x_window.screen);
        x_window.win = XCreateSimpleWindow(x_window.display, RootWindow(x_window.display, x_window.screen), 0, 0, 10, 10, 0, 0, 0);

        CONF_NCOLS = 80;
        CONF_NROWS = 24;

        term_allocate();

        term.charset = 0;
        term.icharset = 0;

        term_window.cw = 10;
        term_window.ch = 20;
        x_window.drawable = XCreatePixmap(x_window.display, x_window.win, 100, 100, (uint32)x_window.depth);
        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable, x_window.visual, x_window.color_map);
        
        draw_context.colors_len = 256;
        draw_context.colors = xmalloc(256 * SIZEOF(XftColor));

        term_reset();
    }

    /* 2. Cursor Tests */
    {
        term.cursor.x = 42;
        term_cursor(CURSOR_SAVE);
        term.cursor.x = 0;
        term_cursor(CURSOR_LOAD);
        ASSERT_EQUAL(term.cursor.x, 42);
    }

    /* 3. Parser Tests */
    {
        char *buf = "?1;23;45m";
        control_seq_intro_reset();
        memcpy64(csi_escape_seq.buffer, buf, 9);
        csi_escape_seq.len = 9;
        control_seq_intro_parse();
        ASSERT_EQUAL(csi_escape_seq.priv, 1);
        ASSERT_EQUAL(csi_escape_seq.arg[1], 23);
    }

    /* 4. Attribute Tests */
    {
        int32 attr[1] = {1}; /* Bold */
        term_set_attr(attr, 1);
        ASSERT_MORE((int32)(term.cursor.attr.mode & ATTR_BOLD), 0);
    }

    /* 5. Mode Tests */
    {
        int32 args[1] = {4}; /* Insert Mode */
        term_set_mode(0, 1, args, 1);
        ASSERT_MORE((int32)(term.mode & TERM_MODE_INSERT), 0);
    }

    /* 6. Tabs and DECALN */
    {
        /* Clear attributes and set charset to identity before filling screen */
        memset64(&term.cursor.attr, 0, SIZEOF(term.cursor.attr));
        term.cursor.attr.fg = CONF_COLOR_INDEX_FONT;
        term.cursor.attr.bg = CONF_COLOR_BG;
        term.charset = 0;

        term_dec_test('8'); 
        ASSERT_EQUAL((int32)term.lines[0][0].rune, (int32)'E');
    }

    /* 7. Strings and Sequences */
    {
        term_str_sequence(0x9d); /* OSC */
        ASSERT_MORE((int32)(term.esc & ESC_STR), 0);
        ASSERT_EQUAL(str_escape_seq.type, ']');
    }

    /* 8. Control Codes and I/O */
    {
        /* RESET STATE: Clear any pending escape sequences from previous tests */
        term.esc = 0; 
        
        term.cursor.x = 10;
        term_control_code('\r');
        ASSERT_EQUAL(term.cursor.x, 0);

        term_putc('Z');
        
        ASSERT_EQUAL((int32)term.lines[term.cursor.y][0].rune, (int32)'Z');
    }

    XCloseDisplay(x_window.display);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_escape */

#endif /* ESCAPE_C */
