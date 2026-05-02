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

#if defined(__linux)
#include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <libutil.h>
#endif

static CSIEscape csi_escape_seq;
static STREscape str_escape_seq;

int64
xwrite(int32 fd, char *s, int64 len) {
    int64 r;
    int64 left = (int64)len;

    while (left > 0) {
        r = write(fd, s, (size_t)len);
        if (r < 0) {
            return r;
        }
        left -= r;
        s += r;
    }

    return (int64)len;
}

void
xfree(void *pointer) {
    free(pointer);
    return;
}

#include "base64.c"

int32
term_line_len(Glyph *line) {
    int32 i = term.ncols - 1;

    for (; i >= 0 && !(line[i].mode & (ATTR_SET | ATTR_WRAP)); i -= 1);

    return i + 1;
}

int32
term_is_wrapped(Glyph *line) {
    int32 len = term_line_len(line);
    int32 wrapped = 0;

    if (len > 0) {
        if (line[len - 1].mode & ATTR_WRAP) {
            wrapped = 1;
        }
    }

    return wrapped;
}

char *
term_get_glyphs(char *buffer, Glyph *gp, Glyph *lgp) {
    while (gp <= lgp) {
        if (gp->mode & ATTR_WDUMMY) {
            gp += 1;
        } else {
            buffer += utf8_encode(gp->rune, buffer);
            gp += 1;
        }
    }
    return buffer;
}

void
die(char *errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(1);
}

void
exec_shell(char *cmd, char **args) {
    char *shell;
    char *arg;
    struct passwd *pw;

    errno = 0;
    pw = getpwuid(getuid());
    if (pw == NULL) {
        if (errno) {
            die("getpwuid: %s\n", strerror(errno));
        } else {
            die("who are you?\n");
        }
    }

    shell = getenv("SHELL");
    if (shell == NULL) {
        if (pw->pw_shell[0]) {
            shell = pw->pw_shell;
        } else {
            shell = cmd;
        }
    }

    if (args) {
        program = args[0];
        arg = NULL;
    } else {
        if (CONF_UTMP) {
            program = CONF_UTMP;
            arg = NULL;
        } else {
            program = shell;
            arg = NULL;
        }
    }
    DEFAULT(args, ((char *[]){program, arg, NULL}));

    unsetenv("COLUMNS");
    unsetenv("LINES");
    unsetenv("TERMCAP");
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("SHELL", shell, 1);
    setenv("HOME", pw->pw_dir, 1);
    setenv("TERM", CONF_TERM_NAME, 1);

    signal(SIGCHLD, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGALRM, SIG_DFL);

    execvp(program, args);
    _exit(1);
}

void
term_set_sixel_attr(Glyph *line, int x1, int x2) {
    for (; x1 <= x2; x1 += 1) {
        line[x1].mode |= ATTR_SIXEL;
    }
    return;
}

int32
term_attr_set(int32 attr) {
    for (int32 i = 0; i < term.nrows - 1; i += 1) {
        for (int32 j = 0; j < term.ncols - 1; j += 1) {
            if (term.line[i][j].mode & attr) {
                return 1;
            }
        }
    }

    return 0;
}

void
term_set_dirt(int32 top, int32 bot) {
    LIMIT(top, 0, term.nrows - 1);
    LIMIT(bot, 0, term.nrows - 1);

    for (int32 i = top; i <= bot; i += 1) {
        term.dirty[i] = 1;
    }
    return;
}

void
term_set_dirt_attr(int32 attr) {
    for (int32 i = 0; i < term.nrows - 1; i += 1) {
        for (int32 j = 0; j < term.ncols - 1; j += 1) {
            if (term.line[i][j].mode & attr) {
                term.dirty[i] = 1;
                break;
            }
        }
    }
    return;
}

void
term_full_dirt(void) {
    for (int32 i = 0; i < term.nrows; i += 1) {
        term.dirty[i] = 1;
    }
    return;
}

void
term_cursor(int32 mode) {
    static TCursor c[2];
    int32 alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);

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

void
tdeleteimages(void) {
    ImageList *next;

    for (ImageList *im = term.images; im; im = next) {
        next = im->next;
        delete_image(im);
    }
    return;
}

void
term_reset(void) {
    ImageList *im = term.images;
    while (im) {
        ImageList *next = im->next;
        xfree(im->pixels);
        xfree(im);
        im = next;
    }
    term.images = NULL;
    
    term.cursor.attr.mode = ATTR_NULL;
    term.cursor.attr.fg = CONF_COLOR_INDEX_FONT;
    term.cursor.attr.bg = CONF_COLOR_BG;
    term.cursor.x = 0;
    term.cursor.y = 0;
    term.cursor.state = CURSOR_DEFAULT;

    memset(term.tabs, 0, (size_t)term.ncols*SIZEOF(*term.tabs));
    for (int32 i = CONF_TAB_NSPACES; i < term.ncols; i += CONF_TAB_NSPACES) {
        term.tabs[i] = 1;
    }
    term.top_scroll_limit = 0;
    term.n_hist = 0;
    term.lines_scrolled_up = 0;
    term.bot_scroll_limit = term.nrows - 1;
    term.mode = TERM_MODE_WRAP | TERM_MODE_UTF8;
    memset(term.translation_table, CS_USA, SIZEOF(term.translation_table));
    term.charset = 0;

    selection_remove();
    for (uint32 i = 0; i < 2; i += 1) {
        term_cursor(CURSOR_SAVE); /* reset saved cursor */
        for (int32 y = 0; y < term.nrows; y += 1) {
            for (int32 x = 0; x < term.ncols; x += 1) {
                term_clear_glyph(&term.line[y][x], 0);
            }
        }
        tdeleteimages();
        term_swap_screen();
    }
    term_full_dirt();
    return;
}

/* handle it with care */
void
term_swap_screen(void) {
    static Glyph **altline;
    static int32 altcol;
    static int32 altrow;
    Glyph **tmpline = term.line;
    int32 tmpcol = term.ncols;
    int32 tmprow = term.nrows;

    term.line = altline;
    term.ncols = altcol;
    term.nrows = altrow;
    altline = tmpline;
    altcol = tmpcol;
    altrow = tmprow;
    term.mode ^= TERM_MODE_ALTSCREEN;
    return;
}

void
term_load_def_screen(int32 clear, int32 loadcursor) {
    int32 col = 0;
    int32 row = 0;
    int32 alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);

    if (alt) {
        if (clear) {
            term_clear_region(0, 0, term.ncols - 1, term.nrows - 1, 1);
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

void
term_load_alt_screen(int32 clear, int32 savecursor) {
    int32 col;
    int32 row;
    int32 def = !TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);

    if (savecursor) {
        term_cursor(CURSOR_SAVE);
    }
    if (def) {
        col = term.ncols;
        row = term.nrows;
        term_swap_screen();
        term.lines_scrolled_up = 0;
        term_resize_alt(col, row);
    }
    if (clear) {
        term_clear_region(0, 0, term.ncols - 1, term.nrows - 1, 1);
    }
    return;
}

void
term_scroll_down(int32 top, int32 n) {
    int32 bot = term.bot_scroll_limit;
    Glyph *temp;

    if (n <= 0) {
        return;
    }
    n = (int32)MIN(n, bot - top + 1);

    term_set_dirt(top, bot - n);
    term_clear_region(0, bot - n + 1, term.ncols - 1, bot, 1);

    for (int32 i = bot; i >= top + n; i -= 1) {
        temp = term.line[i];
        term.line[i] = term.line[i - n];
        term.line[i - n] = temp;
    }

    if ((selection.ob.x != -1)
        && (selection.alt == TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))) {
        selection_scroll(top, bot, n);
    }

    {
        ImageList *im = term.images;
        while (im) {
            if (im->y >= top && im->y <= bot) {
                im->y += n;
            }
            im = im->next;
        }
    }
    return;
}

void
term_scroll_up(int32 top, int32 bot, int32 n, int32 mode) {
    int32 s = 0;
    uint32 alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);
    int32 savehist = !alt && top == 0 && mode != SCROLL_NOSAVEHIST;
    Glyph *temp;

    if (n <= 0) {
        return;
    }
    n = (int32)MIN(n, bot - top + 1);

    if (savehist) {
        for (int32 i = 0; i < n; i += 1) {
            term.i_hist = (term.i_hist + 1) % HISTORY_SIZE;
            temp = term.hist[term.i_hist];
            for (int32 j = 0; j < term.ncols; j += 1) {
                term_clear_glyph(&temp[j], 1);
            }
            term.hist[term.i_hist] = term.line[i];
            term.line[i] = temp;
        }
        term.n_hist = (int32)MIN(term.n_hist + n, HISTORY_SIZE);
        s = n;
        if (term.lines_scrolled_up) {
            int32 j = term.lines_scrolled_up;
            term.lines_scrolled_up = (int32)MIN(j + n, HISTORY_SIZE);
            s = j + n - term.lines_scrolled_up;
        }
        if (mode != SCROLL_RESIZE) {
            term_full_dirt();
        }
    } else {
        term_clear_region(0, top, term.ncols - 1, top + n - 1, 1);
        term_set_dirt(top + n, bot);
    }

    for (int32 i = top; i <= bot - n; i += 1) {
        temp = term.line[i];
        term.line[i] = term.line[i + n];
        term.line[i + n] = temp;
    }

    if (selection.ob.x != -1 && selection.alt == alt) {
        if (!savehist) {
            selection_scroll(top, bot, -n);
        } else {
            if (s > 0) {
                selection_move(-s);
                if (-term.lines_scrolled_up + selection.nb.y < -term.n_hist) {
                    selection_remove();
                }
            }
        }
    }

    {
        ImageList **pim = &term.images;
        while (*pim) {
            ImageList *im = *pim;
            if (im->y >= top && im->y <= bot) {
                im->y -= n;
            }
            if (im->y < -term.n_hist) {
                *pim = im->next;
                xfree(im->pixels);
                xfree(im);
                continue;
            }
            pim = &(*pim)->next;
        }
    }
    return;
}

void
term_new_line(int32 first_col) {
    int32 y = term.cursor.y;
    int32 x_pos;

    if (y == term.bot_scroll_limit) {
        term_scroll_up(term.top_scroll_limit, term.bot_scroll_limit, 1,
                       SCROLL_SAVEHIST);
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

void
control_seq_intro_parse(void) {
    char *p = csi_escape_seq.buffer;
    char *np;
    int64 v;
    int32 sep = ';'; /* colon or semi-colon, but not both */

    csi_escape_seq.narg = 0;
    if (*p == '?') {
        csi_escape_seq.priv = 1;
        p += 1;
    }

    csi_escape_seq.buffer[csi_escape_seq.len] = '\0';
    while (p < csi_escape_seq.buffer + csi_escape_seq.len) {
        np = NULL;
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
            sep = ':'; /* allow override to colon once */
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

/* for absolute user moves, when decom is set */
void
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

void
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

void
term_set_char(uint32 u, Glyph *attr, int32 x, int32 y) {
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
    if (term.translation_table[term.charset] == CS_GRAPHIC0
        && BETWEEN(u, 0x41, 0x7e) && vt100_0[u - 0x41]) {
        utf8_decode(vt100_0[u - 0x41], &u, UTF_SIZ);
    }

    if (term.line[y][x].mode & ATTR_WIDE) {
        if (x + 1 < term.ncols) {
            term.line[y][x + 1].rune = ' ';
            term.line[y][x + 1].mode &= ~ATTR_WDUMMY;
        }
    } else {
        if (term.line[y][x].mode & ATTR_WDUMMY) {
            term.line[y][x - 1].rune = ' ';
            term.line[y][x - 1].mode &= ~ATTR_WIDE;
        }
    }

    term.dirty[y] = 1;
    term.line[y][x] = *attr;
    term.line[y][x].rune = u;
    term.line[y][x].mode |= ATTR_SET;

    if (isboxdraw(u)) {
        term.line[y][x].mode |= ATTR_BOXDRAW;
    }
    return;
}

void
term_clear_glyph(Glyph *gp, int32 usecurattr) {
    if (usecurattr) {
        gp->fg = term.cursor.attr.fg;
        gp->bg = term.cursor.attr.bg;
    } else {
        gp->fg = CONF_COLOR_INDEX_FONT;
        gp->bg = CONF_COLOR_BG;
    }
    gp->mode = ATTR_NULL;
    gp->rune = ' ';
    return;
}

void
term_clear_region(int32 x1, int32 y1, int32 x2, int32 y2, int32 usecurattr) {
    /* selection_is_selected4() takes relative coordinates */
    if (selection_is_selected4(
            x1 + term.lines_scrolled_up, y1 + term.lines_scrolled_up,
            x2 + term.lines_scrolled_up, y2 + term.lines_scrolled_up)) {
        selection_remove();
    }

    for (int32 y = y1; y <= y2; y += 1) {
        term.dirty[y] = 1;
        for (int32 x = x1; x <= x2; x += 1) {
            term_clear_glyph(&term.line[y][x], usecurattr);
        }
    }
    return;
}

void
term_delete_char(int32 n) {
    int32 src;
    int32 dst;
    int32 size;
    Glyph *line;

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
        line = term.line[term.cursor.y];
        memmove(&line[dst], &line[src], (size_t)size*SIZEOF(Glyph));
    }
    term_clear_region(dst + size, term.cursor.y, term.ncols - 1, term.cursor.y,
                      1);
    return;
}

void
term_insert_blank(int32 n) {
    int32 src;
    int32 dst;
    int32 size;
    Glyph *line;

    if (n <= 0) {
        return;
    }
    dst = (int32)MIN(term.cursor.x + n, term.ncols);
    src = term.cursor.x;
    size = term.ncols - dst;
    if (size > 0) { /* otherwise dst would point beyond the array */
        line = term.line[term.cursor.y];
        memmove(&line[dst], &line[src], (size_t)size*SIZEOF(Glyph));
    }
    term_clear_region(src, term.cursor.y, dst - 1, term.cursor.y, 1);
    return;
}

void
term_insert_blank_line(int32 n) {
    if (BETWEEN(term.cursor.y, term.top_scroll_limit, term.bot_scroll_limit)) {
        term_scroll_down(term.cursor.y, n);
    }
    return;
}

void
term_delete_line(int32 n) {
    if (BETWEEN(term.cursor.y, term.top_scroll_limit, term.bot_scroll_limit)) {
        term_scroll_up(term.cursor.y, term.bot_scroll_limit, n,
                       SCROLL_NOSAVEHIST);
    }
    return;
}

int32_t
term_def_color(int32 *attr, int32 *npar, int32 l) {
    int32_t idx = -1;
    uint32 r;
    uint32 g;
    uint32 b;

    switch (attr[*npar + 1]) {
    case 2: /* direct color in RGB space */
        if (*npar + 4 >= l) {
            fprintf(stderr, "erresc(38): Incorrect number of parameters (%d)\n",
                    *npar);
            break;
        }
        r = (uint32)attr[*npar + 2];
        g = (uint32)attr[*npar + 3];
        b = (uint32)attr[*npar + 4];
        *npar += 4;
        if (!BETWEEN(r, 0, 255) || !BETWEEN(g, 0, 255) || !BETWEEN(b, 0, 255)) {
            fprintf(stderr, "erresc: bad rgb color (%u,%u,%u)\n", r, g, b);
        } else {
            idx = (int32)TRUECOLOR(r, g, b);
        }
        break;
    case 5: /* indexed color */
        if (*npar + 2 >= l) {
            fprintf(stderr, "erresc(38): Incorrect number of parameters (%d)\n",
                    *npar);
            break;
        }
        *npar += 2;
        if (!BETWEEN(attr[*npar], 0, 255)) {
            fprintf(stderr, "erresc: bad fgcolor %d\n", attr[*npar]);
        } else {
            idx = attr[*npar];
        }
        break;
    case 0: /* implemented defined (only foreground) */
    case 1: /* transparent */
    case 3: /* direct color in CMY space */
    case 4: /* direct color in CMYK space */
    default:
        fprintf(stderr, "erresc(38): gfx attr %d unknown\n", attr[*npar]);
        break;
    }

    return idx;
}

void
term_set_attr(int32 *attr, int32 l) {
    int32_t idx;

    for (int32 i = 0; i < l; i += 1) {
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
        case 5: /* slow blink */
                /* FALLTHROUGH */
        case 6: /* rapid blink */
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
        case 39: /* set foreground color to default */
            term.cursor.attr.fg = CONF_COLOR_INDEX_FONT;
            break;
        case 48:
            idx = term_def_color(attr, &i, l);
            if (idx >= 0) {
                term.cursor.attr.bg = idx;
            }
            break;
        case 49: /* set background color to default */
            term.cursor.attr.bg = CONF_COLOR_BG;
            break;
        case 58:
            /* This starts a sequence to change the color of
             * "underline" pixels. We don't support that and
             * instead eat up a following "5;n" or "2;r;g;b". */
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
                fprintf(stderr, "erresc(default): gfx attr %d unknown\n",
                        attr[i]);
                control_seq_intro_dump();
            }
            break;
        }
    }
    return;
}

void
term_set_mode(int32 priv, int32 set, int32 *args, int32 narg) {
    for (int32 *lim = args + narg; args < lim; args += 1) {
        if (priv) {
            switch (*args) {
            case 1: /* DECCKM -- Cursor CONF_KEYS */
                x_set_mode(set, WIN_MODE_APPCURSOR);
                break;
            case 5: /* DECSCNM -- Reverse video */
                x_set_mode(set, WIN_MODE_REVERSE);
                break;
            case 6: /* DECOM -- Origin */
                MODBIT(term.cursor.state, set, CURSOR_ORIGIN);
                term_move_abs_to(0, 0);
                break;
            case 7: /* DECAWM -- Auto wrap */
                MODBIT(term.mode, set, TERM_MODE_WRAP);
                break;
            case 0:  /* Error (IGNORED) */
            case 2:  /* DECANM -- ANSI/VT52 (IGNORED) */
            case 3:  /* DECCOLM -- Column  (IGNORED) */
            case 4:  /* DECSCLM -- Scroll (IGNORED) */
            case 8:  /* DECARM -- Auto repeat (IGNORED) */
            case 18: /* DECPFF -- Printer feed (IGNORED) */
            case 19: /* DECPEX -- Printer extent (IGNORED) */
            case 42: /* DECNRCM -- National characters (IGNORED) */
            case 12: /* att610 -- Start blinking cursor (IGNORED) */
                break;
            case 25: /* DECTCEM -- Text Cursor Enable Mode */
                x_set_mode(!set, WIN_MODE_HIDE);
                break;
            case 9: /* X10 mouse compatibility mode */
                x_set_pointer_motion(0);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEX10);
                break;
            case 1000: /* 1000: report button press */
                x_set_pointer_motion(0);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEBTN);
                break;
            case 1002: /* 1002: report motion on button press */
                x_set_pointer_motion(0);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEMOTION);
                break;
            case 1003: /* 1003: enable all mouse motions */
                x_set_pointer_motion(set);
                x_set_mode(0, WIN_MODE_MOUSE);
                x_set_mode(set, WIN_MODE_MOUSEMANY);
                break;
            case 1004: /* 1004: send handler_focus events to tty */
                x_set_mode(set, WIN_MODE_FOCUS);
                break;
            case 1006: /* 1006: extended reporting mode */
                x_set_mode(set, WIN_MODE_MOUSESGR);
                break;
            case 1034: /* 1034: enable 8-bit mode for keyboard input */
                x_set_mode(set, WIN_MODE_8BIT);
                break;
            case 1049: /* swap screen & set/restore cursor as xterm */
                if (!CONF_ALLOW_ALT_SCREEN) {
                    break;
                }
                if (set) {
                    term_cursor(CURSOR_SAVE);
                } else {
                    term_cursor(CURSOR_LOAD);
                }
                _X_FALLTHROUGH;
            case 47:   /* swap screen */
            case 1047: /*swap screen, clearing alternate screen */
                if (!CONF_ALLOW_ALT_SCREEN) {
                    break;
                }
                if (set) {
                    int32 should_clear;
                    int32 should_save;
                    if (*args == 1049) {
                        should_clear = 1;
                    } else {
                        should_clear = 0;
                    }
                    if (*args == 1049) {
                        should_save = 1;
                    } else {
                        should_save = 0;
                    }
                    term_load_alt_screen(should_clear, should_save);
                } else {
                    int32 should_clear_def;
                    int32 should_load_def;
                    if (*args == 1047) {
                        should_clear_def = 1;
                    } else {
                        should_clear_def = 0;
                    }
                    if (*args == 1049) {
                        should_load_def = 1;
                    } else {
                        should_load_def = 0;
                    }
                    term_load_def_screen(should_clear_def, should_load_def);
                }
                break;
            case 1048: /* save/restore cursor (like DECSC/DECRC) */
                if (!CONF_ALLOW_ALT_SCREEN) {
                    break;
                }
                if (set) {
                    term_cursor(CURSOR_SAVE);
                } else {
                    term_cursor(CURSOR_LOAD);
                }
                break;
            case 2004: /* 2004: bracketed paste mode */
                x_set_mode(set, WIN_MODE_BRCKTPASTE);
                break;
            /* Not implemented mouse modes. See comments there. */
            case 1001: /* mouse highlight mode; can hang the
                          terminal by design when implemented. */
            case 1005: /* UTF-8 mouse mode; will confuse
                          applications not supporting UTF-8
                          and luit. */
            case 1015: /* urxvt mangled mouse mode; incompatible
                          and can be mistaken for other control
                          codes. */
                break;
            case 80: /* DECSDM -- Sixel Display Mode */
                MODBIT(term.mode, set, TERM_MODE_SIXEL_SDM);
                break;
            case 8452: /* sixel scrolling leaves cursor to right of graphic */
                MODBIT(term.mode, set, TERM_MODE_SIXEL_CUR_RT);
                break;
            default:
                fprintf(stderr, "erresc: unknown private set/reset mode %d\n",
                        *args);
                break;
            }
        } else {
            switch (*args) {
            case 0: /* Error (IGNORED) */
                break;
            case 2:
                x_set_mode(set, WIN_MODE_KBDLOCK);
                break;
            case 4: /* IRM -- Insertion-replacement */
                MODBIT(term.mode, set, TERM_MODE_INSERT);
                break;
            case 12: /* SRM -- Send/Receive */
                MODBIT(term.mode, !set, TERM_MODE_ECHO);
                break;
            case 20: /* LNM -- Linefeed/new line */
                MODBIT(term.mode, set, TERM_MODE_CRLF);
                break;
            default:
                fprintf(stderr, "erresc: unknown set/reset mode %d\n", *args);
                break;
            }
        }
    }
    return;
}

void
control_seq_intro_handle(void) {
    char buffer[256];
    int32 n;
    int pi;
    int pa;
    int32 x;

    switch (csi_escape_seq.mode[0]) {
    default:
    unknown:
        fprintf(stderr, "erresc: unknown csi ");
        control_seq_intro_dump();
        /* die(""); */
        break;
    case '@': /* ICH -- Insert <n> blank char */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_insert_blank(csi_escape_seq.arg[0]);
        break;
    case 'A': /* CUU -- Cursor <n> Up */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x, term.cursor.y - csi_escape_seq.arg[0]);
        break;
    case 'B': /* CUD -- Cursor <n> Down */
    case 'e': /* VPR --Cursor <n> Down */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x, term.cursor.y + csi_escape_seq.arg[0]);
        break;
    case 'i': /* MC -- Media Copy */
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
            fprintf(stderr,
                    "control_seq_intro_handle: Unhandled switch case.\n");
            break;
        }
        break;
    case 'c': /* DA -- Device Attributes */
        if (csi_escape_seq.arg[0] == 0) {
            tty_write(CONF_VTIDEN, (int64)strlen(CONF_VTIDEN), 0);
        }
        break;
    case 'b': /* REP -- if last char is printable print it <n> more times */
        LIMIT(csi_escape_seq.arg[0], 1, 65535);
        if (term.last_char) {
            while (csi_escape_seq.arg[0] > 0) {
                term_putc(term.last_char);
                csi_escape_seq.arg[0] -= 1;
            }
        }
        break;
    case 'C': /* CUF -- Cursor <n> Forward */
    case 'a': /* HPR -- Cursor <n> Forward */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x + csi_escape_seq.arg[0], term.cursor.y);
        break;
    case 'D': /* CUB -- Cursor <n> Backward */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(term.cursor.x - csi_escape_seq.arg[0], term.cursor.y);
        break;
    case 'E': /* CNL -- Cursor <n> Down and first col */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(0, term.cursor.y + csi_escape_seq.arg[0]);
        break;
    case 'F': /* CPL -- Cursor <n> Up and first col */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(0, term.cursor.y - csi_escape_seq.arg[0]);
        break;
    case 'g': /* TBC -- Tabulation clear */
        switch (csi_escape_seq.arg[0]) {
        case 0: /* clear current tab stop */
            term.tabs[term.cursor.x] = 0;
            break;
        case 3: /* clear all the tabs */
            memset(term.tabs, 0, (size_t)term.ncols*SIZEOF(*term.tabs));
            break;
        default:
            goto unknown;
        }
        break;
    case 'G': /* CHA -- Move to <col> */
    case '`': /* HPA */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_to(csi_escape_seq.arg[0] - 1, term.cursor.y);
        break;
    case 'H': /* CUP -- Move to <row> <col> */
    case 'f': /* HVP */
        DEFAULT(csi_escape_seq.arg[0], 1);
        DEFAULT(csi_escape_seq.arg[1], 1);
        term_move_abs_to(csi_escape_seq.arg[1] - 1, csi_escape_seq.arg[0] - 1);
        break;
    case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_put_tab(csi_escape_seq.arg[0]);
        break;
    case 'J': /* ED -- Clear screen */
        switch (csi_escape_seq.arg[0]) {
        case 0: /* below */
            term_clear_region(term.cursor.x, term.cursor.y, term.ncols - 1,
                              term.cursor.y, 1);
            if (term.cursor.y < term.nrows - 1) {
                term_clear_region(0, term.cursor.y + 1, term.ncols - 1,
                                  term.nrows - 1, 1);
            }
            break;
        case 1: /* above */
            if (term.cursor.y >= 1) {
                term_clear_region(0, 0, term.ncols - 1, term.cursor.y - 1, 1);
            }
            term_clear_region(0, term.cursor.y, term.cursor.x, term.cursor.y,
                              1);
            break;
        case 2: /* all */
            if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
                term_clear_region(0, 0, term.ncols - 1, term.nrows - 1, 1);
                tdeleteimages();
                break;
            }
            /* vte does this:
               term_scroll_up(0, term.nrows-1, term.nrows, SCROLL_SAVEHIST); */

            /* alacritty does this: */
            n = term.nrows - 1;
            while (n >= 0 && term_line_len(term.line[n]) == 0) {
                n -= 1;
            }
            for (ImageList *im = term.images; im; im = im->next) {
                n = (int32)MAX(im->y - term.lines_scrolled_up, n);
            }
            if (n >= 0) {
                term_scroll_up(0, term.nrows - 1, n + 1, SCROLL_SAVEHIST);
            }
            term_scroll_up(0, term.nrows - 1, term.nrows - n - 1,
                           SCROLL_NOSAVEHIST);
            tdeleteimages();
            break;
        case 6: /* sixels */
            tdeleteimages();
            term_full_dirt();
            break;
        default:
            goto unknown;
        }
        break;
    case 'K': /* EL -- Clear line */
        switch (csi_escape_seq.arg[0]) {
        case 0: /* right */
            term_clear_region(term.cursor.x, term.cursor.y, term.ncols - 1,
                              term.cursor.y, 1);
            break;
        case 1: /* left */
            term_clear_region(0, term.cursor.y, term.cursor.x, term.cursor.y,
                              1);
            break;
        case 2: /* all */
            term_clear_region(0, term.cursor.y, term.ncols - 1, term.cursor.y,
                              1);
            break;
        default:
            fprintf(stderr,
                    "control_seq_intro_handle: Unhandled switch case.\n");
            break;
        }
        break;
    case 'S': /* SU -- Scroll <n> line up */
        if (csi_escape_seq.priv) {
            if (csi_escape_seq.narg > 1) {
                /* XTSMGRAPHICS */
                pi = csi_escape_seq.arg[0];
                pa = csi_escape_seq.arg[1];
                if (pi == 1 && (pa == 1 || pa == 2 || pa == 4)) {
                    /* number of sixel color registers */
                    n = SNPRINTF(buffer, "\033[?1;0;%dS", DECSIXEL_PALETTE_MAX);
                    tty_write(buffer, n, 1);
                    break;
                } else {
                    if (pi == 2 && (pa == 1 || pa == 2 || pa == 4)) {
                        /* sixel graphics geometry (in pixels) */
                        long long mw;
                        long long mh;
                        mw = (long long)MIN(term.ncols*term_window.cw, DECSIXEL_WIDTH_MAX);
                        mh = (long long)MIN(term.nrows*term_window.ch, DECSIXEL_HEIGHT_MAX);
                        n = SNPRINTF(buffer, "\033[?2;0;%lld;%lldS", mw, mh);
                        tty_write(buffer, n, 1);
                        break;
                    }
                }
                /* the number of color registers and sixel geometry can't be
                 * changed */
                n = SNPRINTF(buffer, "\033[?%d;3;0S", pi); /* failure */
                tty_write(buffer, n, 1);
            }
        }
        DEFAULT(csi_escape_seq.arg[0], 1);
        /* xterm, urxvt, alacritty save this in history */
        term_scroll_up(term.top_scroll_limit, term.bot_scroll_limit,
                       csi_escape_seq.arg[0], SCROLL_SAVEHIST);
        break;
    case 'T': /* SD -- Scroll <n> line down */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_scroll_down(term.top_scroll_limit, csi_escape_seq.arg[0]);
        break;
    case 'L': /* IL -- Insert <n> blank lines */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_insert_blank_line(csi_escape_seq.arg[0]);
        break;
    case 'l': /* RM -- Reset Mode */
        term_set_mode(csi_escape_seq.priv, 0, csi_escape_seq.arg,
                      csi_escape_seq.narg);
        break;
    case 'M': /* DL -- Delete <n> lines */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_delete_line(csi_escape_seq.arg[0]);
        break;
    case 'X': /* ECH -- Erase <n> char */
        if (csi_escape_seq.arg[0] < 0) {
            return;
        }
        DEFAULT(csi_escape_seq.arg[0], 1);
        x = (int32)MIN(term.cursor.x + csi_escape_seq.arg[0], term.ncols) - 1;
        term_clear_region(term.cursor.x, term.cursor.y, x, term.cursor.y, 1);
        break;
    case 'P': /* DCH -- Delete <n> char */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_delete_char(csi_escape_seq.arg[0]);
        break;
    case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_put_tab(-csi_escape_seq.arg[0]);
        break;
    case 'd': /* VPA -- Move to <row> */
        DEFAULT(csi_escape_seq.arg[0], 1);
        term_move_abs_to(term.cursor.x, csi_escape_seq.arg[0] - 1);
        break;
    case 'h': /* SM -- Set terminal mode */
        term_set_mode(csi_escape_seq.priv, 1, csi_escape_seq.arg,
                      csi_escape_seq.narg);
        break;
    case 'm': /* SGR -- Terminal attribute (color) */
        term_set_attr(csi_escape_seq.arg, csi_escape_seq.narg);
        break;
    case 'n': /* DSR -- Device Status Report */
        switch (csi_escape_seq.arg[0]) {
        case 5: /* Status Report "OK" `0n` */
            tty_write("\033[0n", SIZEOF("\033[0n") - 1, 0);
            break;
        case 6: /* Report Cursor Position (CPR) "<row>;<column>R" */
            n = SNPRINTF(buffer,
                         "\033[%i;%iR", term.cursor.y + 1, term.cursor.x + 1);
            tty_write(buffer, (int64)n, 0);
            break;
        default:
            goto unknown;
        }
        break;
    case '$': /* DECRQM -- DEC Request Mode (private) */
        if (csi_escape_seq.mode[1] == 'p' && csi_escape_seq.priv) {
            switch (csi_escape_seq.arg[0]) {
            case 5: /* DECSCNM -- Reverse Video */
                tty_write("\033[?5;2$y", 8,
                          0); /* Report as permanently reset */
                break;
            case 80:
                /* Sixel Display Mode  */
                if (TERM_MODE_IS_SET(TERM_MODE_SIXEL_SDM)) {
                    tty_write("\033[?80;1$y", 9, 0);
                } else {
                    tty_write("\033[?80;2$y", 9, 0);
                }
                break;
            case 8452:
                /* Sixel scrolling leaves cursor to right of graphic */
                if (TERM_MODE_IS_SET(TERM_MODE_SIXEL_CUR_RT)) {
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
    case 'r': /* DECSTBM -- Set Scrolling Region */
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
    case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
        term_cursor(CURSOR_SAVE);
        break;
    case 't':
        switch (csi_escape_seq.arg[0]) {
        case 14: /* text area size in pixels */
            if (csi_escape_seq.narg > 1) {
                goto unknown;
            }
            n = SNPRINTF(buffer,
                         "\033[4;%d;%dt",
                         term.nrows*term_window.ch, term.ncols*term_window.cw);
            tty_write(buffer, n, 1);
            break;
        case 16: /* character cell size in pixels */
            n = SNPRINTF(buffer,
                         "\033[6;%d;%dt",
                         term_window.ch, term_window.cw);
            tty_write(buffer, n, 1);
            break;
        case 18: /* size of the text area in characters */
            n = SNPRINTF(buffer, "\033[8;%d;%dt", term.nrows, term.ncols);
            tty_write(buffer, n, 1);
            break;
        default:
            goto unknown;
        }
        break;
    case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
        if (csi_escape_seq.priv) {
            goto unknown;
        } else {
            term_cursor(CURSOR_LOAD);
        }
        break;
    case ' ':
        switch (csi_escape_seq.mode[1]) {
        case 'q': /* DECSCUSR -- Set Cursor Style */
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

void
control_seq_intro_dump(void) {
    uint32 c;

    fprintf(stderr, "ESC[");
    for (int64 i = 0; i < csi_escape_seq.len; i += 1) {
        c = csi_escape_seq.buffer[i] & 0xff;
        if (isprint(c)) {
            putc((int32)c, stderr);
        } else {
            if (c == '\n') {
                fprintf(stderr, "(\\n)");
            } else {
                if (c == '\r') {
                    fprintf(stderr, "(\\r)");
                } else {
                    if (c == 0x1b) {
                        fprintf(stderr, "(\\e)");
                    } else {
                        fprintf(stderr, "(%02x)", c);
                    }
                }
            }
        }
    }
    putc('\n', stderr);
    return;
}

void
control_seq_intro_reset(void) {
    memset(&csi_escape_seq, 0, SIZEOF(csi_escape_seq));
    return;
}

void
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
        fprintf(stderr, "erresc: failed to fetch %s color %d\n",
                type_str, is_osc4 ? num : index);
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

void
string_handle(void) {
    char *p = NULL;
    char *dec;
    int32 j;
    int32 narg;
    int32 par;
    ImageList *newimages = (void *)0xCD;
    ImageList *next_im;
    ImageList *tail = NULL;
    int x1_im;
    int y1_im;
    int x2_im;
    int y2_im;
    int y_line;
    int numimages;
    int cx_pos;
    int cy_pos;
    Glyph *line_ptr;
    int scr_offset;

    struct {
        int32 idx;
        char *string;
    } osc_table[] = {{CONF_COLOR_INDEX_FONT, "foreground"},
                     {CONF_COLOR_BG, "background"},
                     {CONF_COLOR_INDEX_CURSOR, "cursor"}};

    if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        scr_offset = 0;
    } else {
        scr_offset = term.lines_scrolled_up;
    }

    term.esc &= ~(ESC_STR_END | ESC_STR);
    {
        int32 c_char;
        char *p2 = str_escape_seq.buffer;

        str_escape_seq.narg = 0;
        str_escape_seq.buffer[str_escape_seq.len] = '\0';

        if (*p2 != '\0') {
            while (str_escape_seq.narg < STR_ARG_SIZ) {
                str_escape_seq.args[str_escape_seq.narg] = p2;
                str_escape_seq.narg += 1;
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

    narg = str_escape_seq.narg;
    if (narg) {
        par = atoi(str_escape_seq.args[0]);
    } else {
        par = 0;
    }

    switch (str_escape_seq.type) {
    case ']': /* OSC -- Operating System Command */
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
        case 52: /* manipulate selection data */
            if (narg > 2 && CONF_ALLOW_WINDOW_OPS) {
                dec = base64_decode(str_escape_seq.args[2]);
                if (dec) {
                    selection_set(dec, CurrentTime);
                    user_clipboard_copy(NULL);
                } else {
                    fprintf(stderr, "erresc: invalid base64\n");
                }
            }
            return;
        case 10: /* set dynamic VT100 text foreground color */
        case 11: /* set dynamic VT100 text background color */
        case 12: /* set dynamic text cursor color */
            if (narg < 2) {
                break;
            }
            p = str_escape_seq.args[1];
            j = par - 10;
            if (j < 0 || j >= LENGTH(osc_table)) {
                break; /* shouldn't be possible */
            }

            if (!strcmp(p, "?")) {
                osc_color_response(par, osc_table[j].idx, 0);
            } else {
                if (x_set_color_name(osc_table[j].idx, p)) {
                    fprintf(stderr, "erresc: invalid %s color: %s\n",
                            osc_table[j].string, p);
                } else {
                    term_full_dirt();
                }
            }
            return;
        case 4: /* color set */
            if (narg < 3) {
                break;
            }
            p = str_escape_seq.args[2];
            _X_FALLTHROUGH;
        case 104: /* color reset */
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
                        fprintf(stderr, "erresc: invalid color j=%d, p=%s\n", j, p);
                    } else {
                        fprintf(stderr, "erresc: invalid color j=%d, p=%s\n", j,
                                "(null)");
                    }
                } else {
                    term_full_dirt();
                }
            }
            return;
        case 110: /* reset dynamic VT100 text foreground color */
        case 111: /* reset dynamic VT100 text background color */
        case 112: /* reset dynamic text cursor color */
            if (narg != 1) {
                break;
            }
            j = par - 110;
            if (j < 0 || j >= LENGTH(osc_table)) {
                break; /* shouldn't be possible */
            }
            if (x_set_color_name(osc_table[j].idx, NULL)) {
                fprintf(stderr, "erresc: %s color not found\n",
                        osc_table[j].string);
            } else {
                term_full_dirt();
            }
            return;
        default:
            fprintf(stderr, "string_handle: Unhandled switch case.\n");
            break;
        }
        break;
    case 'k': /* old title set compatibility */
        x_set_title(str_escape_seq.args[0]);
        return;
    case 'P': /* DCS -- Device Control String */
        if (TERM_MODE_IS_SET(TERM_MODE_SIXEL)) {
            term.mode &= ~TERM_MODE_SIXEL;
            if (sixel_st.image.data == NULL) {
                sixel_parser_deinit(&sixel_st);
                return;
            }
            if (TERM_MODE_IS_SET(TERM_MODE_SIXEL_SDM)) {
                cx_pos = 0;
            } else {
                cx_pos = term.cursor.x;
            }
            if (TERM_MODE_IS_SET(TERM_MODE_SIXEL_SDM)) {
                cy_pos = 0;
            } else {
                cy_pos = term.cursor.y;
            }
            numimages
                = sixel_parser_finalize(&sixel_st, &newimages, cx_pos, cy_pos + scr_offset,
                                        term_window.cw, term_window.ch);

            /* Sanity check to prevent X11 BadMatch crash */
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
                char *transparent_rows = xmalloc((int64)numimages);
                ImageList *im_ptr;
                int32 i_idx;
                for (i_idx = 0, im_ptr = newimages; im_ptr; im_ptr = im_ptr->next, i_idx += 1) {
                    transparent_rows[i_idx] = (char)im_ptr->transparent;
                }
                for (im_ptr = term.images; im_ptr; im_ptr = next_im) {
                    next_im = im_ptr->next;
                    if (im_ptr->y >= y1_im && im_ptr->y < y2_im) {
                        y_line = im_ptr->y - scr_offset;
                        if (y_line >= 0 && y_line < term.nrows && term.dirty[y_line]) {
                            line_ptr = term.line[y_line];
                            j = (int32)MIN(im_ptr->x + im_ptr->cols, term.ncols);
                            for (i_idx = im_ptr->x; i_idx < j; i_idx += 1) {
                                if (line_ptr[i_idx].mode & ATTR_SIXEL) {
                                    break;
                                }
                            }
                            if (i_idx == j) {
                                delete_image(im_ptr);
                                continue;
                            }
                        }
                        if (im_ptr->x >= x1_im && im_ptr->x + im_ptr->cols <= x2_im
                            && !transparent_rows[im_ptr->y - y1_im]) {
                            delete_image(im_ptr);
                            continue;
                        }
                    }
                    tail = im_ptr;
                }
                xfree(transparent_rows);
            }

            if (tail) {
                tail->next = newimages;
                newimages->prev = tail;
            } else {
                term.images = newimages;
            }

            x2_im = (int32)MIN(x2_im, term.ncols) - 1;

            if (TERM_MODE_IS_SET(TERM_MODE_SIXEL_SDM)) {
                ImageList *im_sdm;
                int32 i_sdm;
                for (i_sdm = 0, im_sdm = newimages; im_sdm; im_sdm = next_im, i_sdm += 1) {
                    next_im = im_sdm->next;
                    if (i_sdm >= term.nrows) {
                        delete_image(im_sdm);
                        continue;
                    }
                    im_sdm->y = i_sdm + scr_offset;
                    term_set_sixel_attr(term.line[i_sdm], x1_im, x2_im);
                    term.dirty[MIN(im_sdm->y, term.nrows - 1)] = 1;
                }
            } else {
                ImageList *im_cur;
                int32 i_cur;
                for (i_cur = 0, im_cur = newimages; im_cur; im_cur = next_im, i_cur += 1) {
                    next_im = im_cur->next;
                    if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
                        scr_offset = 0;
                    } else {
                        scr_offset = term.lines_scrolled_up;
                    }
                    im_cur->y = term.cursor.y + scr_offset;
                    term_set_sixel_attr(term.line[term.cursor.y], x1_im, x2_im);
                    term.dirty[MIN(im_cur->y, term.nrows - 1)] = 1;

                    if (i_cur < numimages - 1) {
                        im_cur->next = NULL;
                        term_new_line(0);
                        im_cur->next = next_im;
                    }
                }

                if (TERM_MODE_IS_SET(TERM_MODE_SIXEL_CUR_RT)) {
                    term.cursor.x = (int32)MIN(term.cursor.x + newimages->cols,
                                               term.ncols - 1);
                } else {
                    term_new_line(1);
                }
            }
        }
        return;
    case '_': /* APC -- Application Program Command */
    case '^': /* PM -- Privacy Message */
        return;
    default:
        fprintf(stderr, "string_handle: Unhandled switch case.\n");
        break;
    }

    fprintf(stderr, "erresc: unknown string ");
    {
        uint32 c_code;

        fprintf(stderr, "ESC%c", str_escape_seq.type);
        for (uint64 i = 0; i < str_escape_seq.len; i += 1) {
            c_code = str_escape_seq.buffer[i] & 0xff;
            if (c_code == '\0') {
                putc('\n', stderr);
                return;
            } else {
                if (isprint(c_code)) {
                    putc((int32)c_code, stderr);
                } else {
                    if (c_code == '\n') {
                        fprintf(stderr, "(\\n)");
                    } else {
                        if (c_code == '\r') {
                            fprintf(stderr, "(\\r)");
                        } else {
                            if (c_code == 0x1b) {
                                fprintf(stderr, "(\\e)");
                            } else {
                                fprintf(stderr, "(%02x)", c_code);
                            }
                        }
                    }
                }
            }
        }
        fprintf(stderr, "ESC\\\n");
        return;
    }
}

void
externalpipe(Arg *arg) {
    int32 to[2];
    char buffer[UTF_SIZ];
    void (*oldsigpipe)(int32);
    Glyph *bp;
    Glyph *end;
    int32 lastpos;
    int32 newline;
    char *const *argv = arg->v;

    if (pipe(to) == -1) {
        return;
    }

    switch (fork()) {
    case -1:
        close(to[0]);
        close(to[1]);
        return;
    case 0:
        dup2(to[0], STDIN_FILENO);
        close(to[0]);
        close(to[1]);
        execvp(argv[0], argv);
        fprintf(stderr, "st: execvp %s\n", argv[0]);
        perror("failed");
        exit(0);
    default:
        break;
    }

    close(to[0]);
    oldsigpipe = signal(SIGPIPE, SIG_IGN);
    newline = 0;
    for (int32 n = 0; n <= HISTORY_SIZE + 2; n += 1) {
        int32 i_hist;
        bp = TERM_LINE_HIST(n);
        i_hist = term.ncols;

        if (TERM_LINE_HIST(n)[i_hist - 1].mode & ATTR_WRAP) {
            lastpos = i_hist;
        } else {
            while (i_hist > 0 && TERM_LINE_HIST(n)[i_hist - 1].rune == ' ') {
                i_hist -= 1;
            }
            lastpos = i_hist;
        }

        lastpos = (int32)MIN(lastpos + 1, term.ncols) - 1;
        if (lastpos < 0) {
            break;
        }
        if (lastpos == 0) {
            continue;
        }
        end = &bp[lastpos + 1];
        for (; bp < end; bp += 1) {
            if (xwrite(to[1], buffer, utf8_encode(bp->rune, buffer)) < 0) {
                break;
            }
        }
        if (TERM_LINE_HIST(n)[lastpos].mode & ATTR_WRAP) {
            newline = 1;
            continue;
        } else {
            newline = 0;
        }
        if (xwrite(to[1], "\n", 1) < 0) {
            break;
        }
        newline = 0;
    }
    if (newline) {
        (void)xwrite(to[1], "\n", 1);
    }
    close(to[1]);
    signal(SIGPIPE, oldsigpipe);
    return;
}

void
term_printer(char *s, int64 len) {
    if (io_fd != -1) {
        if (xwrite(io_fd, s, len) < 0) {
            perror("Error writing to output file");
            close(io_fd);
            io_fd = -1;
        }
    }
    return;
}

void
term_dump_sel(void) {
    char *ptr;

    ptr = selection_get();
    if (ptr) {
        term_printer(ptr, (int64)strlen(ptr));
        xfree(ptr);
    }
    return;
}

void
term_dump_line(int32 n) {
    char *string = xmalloc((int64)((term.ncols + 1)*UTF_SIZ) * SIZEOF(*string));
    char *buffer = string;
    Glyph *fgp = &term.line[n][0];
    Glyph *lgp = &fgp[term.ncols - 1];
    char *ptr;

    while (lgp > fgp && !(lgp->mode & (ATTR_SET | ATTR_WRAP))) {
        lgp -= 1;
    }
    ptr = term_get_glyphs(buffer, fgp, lgp);

    if (!(lgp->mode & ATTR_WRAP)) {
        *ptr = '\n';
        ptr += 1;
    }

    term_printer(string, ptr - buffer);
    return;
}

void
term_dump(void) {
    for (int32 i = 0; i < term.nrows; i += 1) {
        term_dump_line(i);
    }
    return;
}

void
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

void
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

void
term_def_tran(char ascii) {
    static char cs[] = "0B";
    static int32 vcs[] = {CS_GRAPHIC0, CS_USA};
    char *p;

    p = strchr(cs, ascii);
    if (p == NULL) {
        fprintf(stderr, "esc unhandled charset: ESC ( %c\n", ascii);
    } else {
        term.translation_table[term.icharset] = (char)vcs[p - cs];
    }
    return;
}

void
term_dec_test(char c) {
    if (c == '8') { /* DEC screen alignment test. */
        for (int32 x = 0; x < term.ncols; x += 1) {
            for (int32 y = 0; y < term.nrows; y += 1) {
                term_set_char('E', &term.cursor.attr, x, y);
            }
        }
    }
    return;
}

void
term_str_sequence(uchar c) {
    str_escape_seq.buffer = xrealloc(str_escape_seq.buffer, STR_BUF_SIZ);
    str_escape_seq.siz = STR_BUF_SIZ;
    str_escape_seq.len = 0;
    str_escape_seq.narg = 0;

    switch (c) {
    case 0x90: /* DCS -- Device Control String */
        c = 'P';
        term.esc |= ESC_DCS;
        break;
    case 0x9f: /* APC -- Application Program Command */
        c = '_';
        break;
    case 0x9e: /* PM -- Privacy Message */
        c = '^';
        break;
    case 0x9d: /* OSC -- Operating System Command */
        c = ']';
        break;
    default:
        fprintf(stderr, "term_str_sequence: unhandled switch case.\n");
        break;
    }
    str_escape_seq.type = (char)c;
    term.esc |= ESC_STR;
    return;
}

void
dcshandle(void) {
    uint bgcolor;
    int transparent;
    uint r = 0xCD;
    uint g = 0xCD;
    uint b = 0xCD;
    uint a = 255;

    switch (csi_escape_seq.mode[0]) {
    default:
        fprintf(stderr, "erresc: unknown csi ");
        control_seq_intro_dump();
        break;
    case 'q': /* DECSIXEL */
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
        if (sixel_parser_init(&sixel_st, transparent, (255u << 24), bgcolor, 1,
                              term_window.cw, term_window.ch) != 0) {
            perror("sixel_parser_init() failed");
        }
        term.mode |= TERM_MODE_SIXEL;
        break;
    }
    return;
}

void
term_control_code(uchar ascii) {
    switch (ascii) {
    case '\t': /* HT */
        term_put_tab(1);
        return;
    case '\b': /* BS */
        term_move_to(term.cursor.x - 1, term.cursor.y);
        return;
    case '\r': /* CR */
        term_move_to(0, term.cursor.y);
        return;
    case '\f': /* LF */
    case '\v': /* VT */
    case '\n': /* LF */
        term_new_line(TERM_MODE_IS_SET(TERM_MODE_CRLF));
        return;
    case '\a': /* BEL */
        if (term.esc & ESC_STR_END) {
            string_handle();
        } else {
            x_bell();
        }
        break;
    case '\033': /* ESC */
        control_seq_intro_reset();
        term.esc &= ~(ESC_CSI | ESC_ALTCHARSET | ESC_TEST);
        term.esc |= ESC_START;
        return;
    case '\016': /* SO (LS1 -- Locking shift 1) */
    case '\017': /* SI (LS0 -- Locking shift 0) */
        term.charset = 1 - (ascii - '\016');
        return;
    case '\032': /* SUB */
        term_set_char('?', &term.cursor.attr, term.cursor.x, term.cursor.y);
        _X_FALLTHROUGH;
    case '\030': /* CAN */
        control_seq_intro_reset();
        break;
    case '\005': /* ENQ (IGNORED) */
    case '\000': /* NUL (IGNORED) */
    case '\021': /* XON (IGNORED) */
    case '\023': /* XOFF (IGNORED) */
    case 0177:   /* DEL (IGNORED) */
        return;
    case 0x80: /* TODO: PAD */
    case 0x81: /* TODO: HOP */
    case 0x82: /* TODO: BPH */
    case 0x83: /* TODO: NBH */
    case 0x84: /* TODO: IND */
        break;
    case 0x85:            /* NEL -- Next line */
        term_new_line(1); /* always go to first col */
        break;
    case 0x86: /* TODO: SSA */
    case 0x87: /* TODO: ESA */
        break;
    case 0x88: /* HTS -- Horizontal tab stop */
        term.tabs[term.cursor.x] = 1;
        break;
    case 0x89: /* TODO: HTJ */
    case 0x8a: /* TODO: VTS */
    case 0x8b: /* TODO: PLD */
    case 0x8c: /* TODO: PLU */
    case 0x8d: /* TODO: RI */
    case 0x8e: /* TODO: SS2 */
    case 0x8f: /* TODO: SS3 */
    case 0x91: /* TODO: PU1 */
    case 0x92: /* TODO: PU2 */
    case 0x93: /* TODO: STS */
    case 0x94: /* TODO: CCH */
    case 0x95: /* TODO: MW */
    case 0x96: /* TODO: SPA */
    case 0x97: /* TODO: EPA */
    case 0x98: /* TODO: SOS */
    case 0x99: /* TODO: SGCI */
        break;
    case 0x9a: /* DECID -- Identify Terminal */
        tty_write(CONF_VTIDEN, (int64)strlen(CONF_VTIDEN), 0);
        break;
    case 0x9b: /* TODO: CSI */
    case 0x9c: /* TODO: ST */
        break;
    case 0x90: /* DCS -- Device Control String */
    case 0x9d: /* OSC -- Operating System Command */
    case 0x9e: /* PM -- Privacy Message */
    case 0x9f: /* APC -- Application Program Command */
        term_str_sequence(ascii);
        return;
    default:
        fprintf(stderr, "term_control_code: unhandled switch case.\n");
        break;
    }
    term.esc &= ~(ESC_STR_END | ESC_STR);
    return;
}

int32
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
    case 'P': /* DCS -- Device Control String */
        term.esc |= ESC_DCS;
        _X_FALLTHROUGH;
    case '_': /* APC -- Application Program Command */
    case '^': /* PM -- Privacy Message */
    case ']': /* OSC -- Operating System Command */
    case 'k': /* old title set compatibility */
        term_str_sequence(ascii);
        return 0;
    case 'n': /* LS2 -- Locking shift 2 */
    case 'o': /* LS3 -- Locking shift 3 */
        term.charset = 2 + (ascii - 'n');
        break;
    case '(': /* GZD4 -- set primary charset G0 */
    case ')': /* G1D4 -- set secondary charset G1 */
    case '*': /* G2D4 -- set tertiary charset G2 */
    case '+': /* G3D4 -- set quaternary charset G3 */
        term.icharset = ascii - '(';
        term.esc |= ESC_ALTCHARSET;
        return 0;
    case 'D': /* IND -- Linefeed */
        if (term.cursor.y == term.bot_scroll_limit) {
            term_scroll_up(term.top_scroll_limit, term.bot_scroll_limit, 1,
                           SCROLL_SAVEHIST);
        } else {
            term_move_to(term.cursor.x, term.cursor.y + 1);
        }
        break;
    case 'E':              /* NEL -- Next line */
        term_new_line(1); /* always go to first col */
        break;
    case 'H': /* HTS -- Horizontal tab stop */
        term.tabs[term.cursor.x] = 1;
        break;
    case 'M': /* RI -- Reverse index */
        if (term.cursor.y == term.top_scroll_limit) {
            term_scroll_down(term.top_scroll_limit, 1);
        } else {
            term_move_to(term.cursor.x, term.cursor.y - 1);
        }
        break;
    case 'Z': /* DECID -- Identify Terminal */
        tty_write(CONF_VTIDEN, (int64)strlen(CONF_VTIDEN), 0);
        break;
    case 'c': /* RIS -- Reset to initial state */
        term_reset();
        reset_title();
        x_load_cols();
        x_set_mode(0, WIN_MODE_HIDE);
        break;
    case '=': /* DECPAM -- Application keypad */
        x_set_mode(1, WIN_MODE_APPKEYPAD);
        break;
    case '>': /* DECPNM -- Normal keypad */
        x_set_mode(0, WIN_MODE_APPKEYPAD);
        break;
    case '7': /* DECSC -- Save Cursor */
        term_cursor(CURSOR_SAVE);
        break;
    case '8': /* DECRC -- Restore Cursor */
        term_cursor(CURSOR_LOAD);
        break;
    case '\\': /* ST -- String Terminator */
        if (term.esc & ESC_STR_END) {
            string_handle();
        }
        break;
    default:
        fprintf(stderr, "erresc: unknown sequence ESC 0x%02X '%c'\n",
                (uchar)ascii, isprint(ascii) ? ascii : '.');
        break;
    }
    return 1;
}

void
term_putc(uint32 u) {
    char c[UTF_SIZ];
    int32 control;
    int32 width = 0;
    int32 len;
    Glyph *glyph;

    control = IS_CONTROl(u);
    if (u < 127 || !TERM_MODE_IS_SET(TERM_MODE_UTF8)) {
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

    if (TERM_MODE_IS_SET(TERM_MODE_PRINT)) {
        term_printer(c, (int64)len);
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

        if (str_escape_seq.len + (uint64)len >= str_escape_seq.siz) {
            if (str_escape_seq.siz <= (SIZE_MAX - UTF_SIZ) / 2) {
                str_escape_seq.siz *= 2;
                str_escape_seq.buffer
                    = xrealloc(str_escape_seq.buffer, (int64)str_escape_seq.siz);
            } else {
                return;
            }
        }

        memmove(&str_escape_seq.buffer[str_escape_seq.len], c, (size_t)len);
        str_escape_seq.len += (uint64)len;

        if (str_escape_seq.type == 'P' && u == 'q') {
            int32 is_sixel = 1;
            for (uint32 i = 0; i < str_escape_seq.len - 1; i += 1) {
                if (str_escape_seq.buffer[i] != ';'
                    && !isdigit((uchar)str_escape_seq.buffer[i])) {
                    is_sixel = 0;
                    break;
                }
            }
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
        if (TERM_MODE_IS_SET(TERM_MODE_UTF8) && IS_CONTROL_C1(u)) {
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
                            dcshandle();
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

    if (selection_is_selected(term.cursor.x + term.lines_scrolled_up,
                              term.cursor.y + term.lines_scrolled_up)) {
        selection_clear();
    }

    glyph = &term.line[term.cursor.y][term.cursor.x];
    if (TERM_MODE_IS_SET(TERM_MODE_WRAP)) {
        if (term.cursor.state & CURSOR_WRAPNEXT) {
            glyph->mode |= ATTR_WRAP;
            term_new_line(1);
            glyph = &term.line[term.cursor.y][term.cursor.x];
        }
    }

    if (TERM_MODE_IS_SET(TERM_MODE_INSERT)) {
        if (term.cursor.x + width < term.ncols) {
            memmove(glyph + width, glyph,
                    (size_t)(term.ncols - term.cursor.x - width)*SIZEOF(Glyph));
            glyph->mode &= ~ATTR_WIDE;
        }
    }

    if (term.cursor.x + width > term.ncols) {
        if (TERM_MODE_IS_SET(TERM_MODE_WRAP)) {
            term_new_line(1);
        } else {
            term_move_to(term.ncols - width, term.cursor.y);
        }
        glyph = &term.line[term.cursor.y][term.cursor.x];
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
        if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
            term.wrap_char_width[1] = width;
        } else {
            term.wrap_char_width[0] = width;
        }
        term.cursor.state |= CURSOR_WRAPNEXT;
    }
    return;
}

int32
term_write(char *buffer, int32 buflen, int32 show_ctrl) {
    int32 charsize;
    uint32 u;
    int32 n;

    for (n = 0; n < buflen; n += charsize) {
        if (TERM_MODE_IS_SET(TERM_MODE_SIXEL) && sixel_st.state != PS_ESC) {
            charsize = sixel_parser_parse(
                &sixel_st, (unsigned char *)buffer + n, buflen - n);
            continue;
        } else {
            if (TERM_MODE_IS_SET(TERM_MODE_UTF8)) {
                charsize = (int32)utf8_decode(buffer + n, &u, (int64)(buflen - n));
                if (charsize == 0) {
                    break;
                }
            } else {
                u = buffer[n] & 0xFF;
                charsize = 1;
            }
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
                }
            }
        }
        term_putc(u);
    }
    return n;
}

void
reflow_scroll_down(int32 n) {
    int32 j;
    Glyph *temp;

    n = (int32)MIN(n, term.n_hist);
    if (n <= 0) {
        return;
    }

    for (int32 i = term.cursor.y + n; i >= n; i -= 1) {
        temp = term.line[i];
        term.line[i] = term.line[i - n];
        term.line[i - n] = temp;
    }
    for (int32 i = n - 1; i >= 0; i -= 1) {
        temp = term.line[i];
        term.line[i] = term.hist[term.i_hist];
        term.hist[term.i_hist] = temp;
        term.i_hist = (term.i_hist - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    }
    term.cursor.y += n;
    term.n_hist -= n;
    j = term.lines_scrolled_up - n;
    if (j >= 0) {
        term.lines_scrolled_up = j;
    } else {
        term.lines_scrolled_up = 0;
        if (selection.ob.x != -1 && !selection.alt) {
            selection_move(-j);
        }
    }

    {
        ImageList *im = term.images;
        while (im) {
            im->y += n;
            im = im->next;
        }
    }
    return;
}

void
term_resize(int32 col, int32 row) {
    int32 *bp;

    term.dirty = xrealloc(term.dirty, (int64)row*SIZEOF(*(term.dirty)));
    term.tabs = xrealloc(term.tabs, (int64)col*SIZEOF(*(term.tabs)));
    if (col > term.ncols) {
        bp = term.tabs + term.ncols;
        memset(bp, 0, SIZEOF(*term.tabs)*(size_t)(col - term.ncols));
        bp -= 1;
        while (bp > term.tabs && !*bp) {
            bp -= 1;
        }
        for (bp += CONF_TAB_NSPACES; bp < term.tabs + col;
             bp += CONF_TAB_NSPACES) {
            *bp = 1;
        }
    }

    if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        term_resize_alt(col, row);
    } else {
        term_resize_def(col, row);
    }
    return;
}

void
term_resize_def(int32 new_ncols, int32 new_nrows) {
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
            xfree(term.line[i]);
        }

        term.line = xrealloc(term.line, (int64)new_nrows*SIZEOF(*(term.line)));

        for (int32 i = term.nrows; i < new_nrows; i += 1) {
            term.line[i] = xmalloc((int64)new_ncols*SIZEOF(Glyph));
            for (int32 j = 0; j < new_ncols; j += 1) {
                term_clear_glyph(&term.line[i][j], 0);
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

void
term_resize_alt(int32 new_ncols, int32 new_nrows) {
    int32 i;

    if (term.ncols == new_ncols && term.nrows == new_nrows) {
        term_full_dirt();
        return;
    }
    if (selection.alt) {
        selection_remove();
    }
    i = 0;
    while (i <= term.cursor.y - new_nrows) {
        xfree(term.line[i]);
        i += 1;
    }
    if (i > 0) {
        memmove(term.line, term.line + i,
                (size_t)new_nrows*SIZEOF(*(term.line)));
        term.cursor.y = new_nrows - 1;
    }
    for (i += new_nrows; i < term.nrows; i += 1) {
        xfree(term.line[i]);
    }
    term.line = xrealloc(term.line, (int64)new_nrows*SIZEOF(*(term.line)));

    for (int32 j = 0; j < MIN(new_nrows, term.nrows); j += 1) {
        term.line[j] = xrealloc(term.line[j],
                                (int64)new_ncols*SIZEOF(*(term.line[j])));
        for (int32 k = term.ncols; k < new_ncols; k += 1) {
            term_clear_glyph(&term.line[j][k], 0);
        }
    }
    for (int32 j = (int32)MIN(new_nrows, term.nrows); j < new_nrows; j += 1) {
        term.line[j] = xmalloc((int64)new_ncols*SIZEOF(Glyph));
        for (int32 k = 0; k < new_ncols; k += 1) {
            term_clear_glyph(&term.line[j][k], 0);
        }
    }
    if (term.cursor.x >= new_ncols) {
        term.cursor.state &= ~CURSOR_WRAPNEXT;
        term.cursor.x = new_ncols - 1;
    } else {
        UPDATE_WRAP_NEXT(1, new_ncols);
    }
    term.ncols = new_ncols;
    term.nrows = new_nrows;
    term.top_scroll_limit = 0;
    term.bot_scroll_limit = new_nrows - 1;
    term_full_dirt();
    return;
}

void
term_reflow(int32 new_ncols, int32 new_nrows) {
    int32 i;
    int32 old_cursor_end_line;
    int32 new_cursor_end_line;
    int32 bottom_visible_line;
    int32 scroll_offset;
    int32 old_x_offset = 0;
    int32 old_y_index = -term.n_hist;
    int32 new_x_offset = 0;
    int32 new_y_index = -1;
    int32 len = 0;
    int32 new_cursor_y_proxy = -1; /* proxy for new y coordinate of cursor */
    int32 nlines;
    static Glyph **reflow_lines = NULL;
    Glyph *line = 0;

    /* --- determine end of current cursor line --- */
    old_cursor_end_line = term.cursor.y;
    while (old_cursor_end_line < (term.nrows - 1)) {
        int32 wrap_len = term_line_len(term.line[old_cursor_end_line]);
        if (wrap_len > 0 && (term.line[old_cursor_end_line][wrap_len - 1].mode & ATTR_WRAP)) {
            old_cursor_end_line += 1;
        } else {
            break;
        }
    }

    /* --- compute required number of lines --- */
    nlines = term.n_hist + old_cursor_end_line + 1;
    if (new_ncols < term.ncols) {
        int32 lines_per_old_line = (term.ncols + new_ncols - 1) / new_ncols;
        nlines = lines_per_old_line*nlines;

        if (nlines > (HISTORY_SIZE + RESIZE_BUFFER + new_nrows)) {
            nlines = HISTORY_SIZE + RESIZE_BUFFER + new_nrows;
            old_y_index
                = -(nlines / lines_per_old_line - old_cursor_end_line - 1);
        }
    }

    /* --- allocate reflow reflow_lines --- */
    assert(nlines <= 2*HISTORY_SIZE);
    if (reflow_lines == NULL) {
        reflow_lines = xmalloc((int64)2*HISTORY_SIZE * SIZEOF(*reflow_lines));
    }

    /* --- reflow old lines into reflow_lines --- */
    do {
        int32 space_left;
        int32 chars_left;

        if (!new_x_offset) {
            new_y_index += 1;
            reflow_lines[new_y_index] = xmalloc(
                (int64)new_ncols*SIZEOF(*(reflow_lines[new_y_index])));
        }

        if (!old_x_offset) {
            line = TERM_LINE_ABS(old_y_index);
            len = term_line_len(line);
        }

        /* update cursor tracking */
        if (old_y_index == term.cursor.y) {
            if (!old_x_offset) {
                len = (int32)MAX(len, term.cursor.x + 1);
            }

            if (new_cursor_y_proxy < 0) {
                if (term.cursor.x - old_x_offset < new_ncols - new_x_offset) {
                    term.cursor.x = new_x_offset + term.cursor.x - old_x_offset;
                    new_cursor_y_proxy = new_y_index;
                    UPDATE_WRAP_NEXT(0, new_ncols);
                }
            }
        }

        /* copy data to new reflow_lines */
        space_left = new_ncols - new_x_offset;
        chars_left = len - old_x_offset;

        if (space_left > chars_left) {
            memcpy(&reflow_lines[new_y_index][new_x_offset],
                   &line[old_x_offset], (size_t)chars_left*SIZEOF(Glyph));
            new_x_offset += chars_left;

            if (len == 0 || !(line[len - 1].mode & ATTR_WRAP)) {
                for (int32 j = new_x_offset; j < new_ncols; j += 1) {
                    term_clear_glyph(&reflow_lines[new_y_index][j], 0);
                }
                new_x_offset = 0;
            } else {
                if (new_x_offset > 0) {
                    reflow_lines[new_y_index][new_x_offset - 1].mode &= ~ATTR_WRAP;
                }
            }

            old_x_offset = 0;
            old_y_index += 1;
        } else {
            if (space_left == chars_left) {
                memcpy(&reflow_lines[new_y_index][new_x_offset],
                       &line[old_x_offset], (size_t)space_left*SIZEOF(Glyph));
                old_x_offset = 0;
                old_y_index += 1;
                new_x_offset = 0;
            } else { /* space_left < chars_left */
                memcpy(&reflow_lines[new_y_index][new_x_offset],
                       &line[old_x_offset], (size_t)space_left*SIZEOF(Glyph));
                old_x_offset += space_left;
                reflow_lines[new_y_index][new_ncols - 1].mode |= ATTR_WRAP;
                new_x_offset = 0;
            }
        }

    } while (old_y_index <= old_cursor_end_line);

    /* --- finalize last partially filled line --- */
    if (new_x_offset) {
        for (int32 j = new_x_offset; j < new_ncols; j += 1) {
            term_clear_glyph(&reflow_lines[new_y_index][j], 0);
        }
    }

    /* --- release unused old lines --- */
    for (i = new_nrows; i < term.nrows; i += 1) {
        xfree(term.line[i]);
    }

    term.line = xrealloc(term.line, (int64)new_nrows*SIZEOF(*(term.line)));

    /* --- adjust cursor and visible region --- */
    bottom_visible_line = (int32)MIN(new_y_index, new_nrows - 1);
    scroll_offset = (int32)MAX(new_nrows - term.nrows, 0);
    new_cursor_end_line
        = (int32)MIN(old_cursor_end_line + scroll_offset, bottom_visible_line);

    term.cursor.y = new_cursor_end_line - (new_y_index - new_cursor_y_proxy);

    if (term.cursor.y < 0) {
        int32 j_prev = new_cursor_end_line;
        new_cursor_end_line = (int32)MIN(new_cursor_end_line - term.cursor.y,
                                         bottom_visible_line);
        term.cursor.y += new_cursor_end_line - j_prev;

        while (term.cursor.y < 0) {
            xfree(reflow_lines[new_y_index]);
            new_y_index -= 1;
            term.cursor.y += 1;
        }
    }

    /* --- allocate additional rows if needed --- */
    for (i = new_nrows - 1; i > new_cursor_end_line; i -= 1) {
        term.line[i] = xmalloc((int64)new_ncols*SIZEOF(Glyph));
        for (int32 j = 0; j < new_ncols; j += 1) {
            term_clear_glyph(&term.line[i][j], 0);
        }
    }

    /* --- populate visible lines --- */
    for (; i >= term.nrows; i -= 1) {
        term.line[i] = reflow_lines[new_y_index];
        new_y_index -= 1;
    }

    for (; i >= 0; i -= 1) {
        xfree(term.line[i]);
        term.line[i] = reflow_lines[new_y_index];
        new_y_index -= 1;
    }

    /* --- update history reflow_lines --- */
    {
        int32 k_idx;
        k_idx = -1;
        while (new_y_index >= 0 && k_idx >= -HISTORY_SIZE) {
            int32 j_hist = (term.i_hist + k_idx + 1 + HISTORY_SIZE) % HISTORY_SIZE;
            xfree(term.hist[j_hist]);
            term.hist[j_hist] = reflow_lines[new_y_index];
            k_idx -= 1;
            new_y_index -= 1;
        }
        term.n_hist = -k_idx - 1;
    }

    term.lines_scrolled_up = (int32)MIN(term.lines_scrolled_up, term.n_hist);

    /* --- reallocate remaining history lines --- */
    for (int32 k_rem = -term.n_hist - 1; k_rem >= -HISTORY_SIZE; k_rem -= 1) {
        int32 j_rem = (term.i_hist + k_rem + 1 + HISTORY_SIZE) % HISTORY_SIZE;
        term.hist[j_rem] = xrealloc(term.hist[j_rem],
                                    (int64)new_ncols*SIZEOF(*(term.hist[j_rem])));
        if (new_ncols > term.ncols) {
            for (int32 c_col = term.ncols; c_col < new_ncols; c_col += 1) {
                term_clear_glyph(&term.hist[j_rem][c_col], 0);
            }
        }
    }
    return;
}

void
reset_title(void) {
    x_set_title(NULL);
    return;
}

void
draw(void) {
    int32 cx = term.cursor.x;
    int32 old_cursor_x = term.old_cursor_x;
    int32 old_cursor_y = term.old_cursor_y;

    if (!x_start_draw()) {
        return;
    }

    LIMIT(term.old_cursor_x, 0, term.ncols - 1);
    LIMIT(term.old_cursor_y, 0, term.nrows - 1);
    if (term.line[term.old_cursor_y][term.old_cursor_x].mode & ATTR_WDUMMY) {
        term.old_cursor_x -= 1;
    }
    if (term.line[term.cursor.y][cx].mode & ATTR_WDUMMY) {
        cx -= 1;
    }

    for (int32 y = 0; y < term.nrows; y += 1) {
        if (!term.dirty[y]) {
            continue;
        }

        term.dirty[y] = 0;
        x_draw_line(TERM_LINE(y), 0, y, term.ncols);
    }

    x_draw_cursor(cx, term.cursor.y, term.line[term.cursor.y][cx],
                  term.old_cursor_x, term.old_cursor_y,
                  term.line[term.old_cursor_y][term.old_cursor_x]);
    term.old_cursor_x = cx;
    term.old_cursor_y = term.cursor.y;
    x_finish_draw();
    if (old_cursor_x != term.old_cursor_x
        || old_cursor_y != term.old_cursor_y) {
        x_xim_spot(term.old_cursor_x, term.old_cursor_y);
    }
    return;
}

void
redraw(void) {
    term_full_dirt();
    draw();
    return;
}

#endif /* ST_C */
