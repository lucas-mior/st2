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

#if TESTING_st

#include <stdbool.h>
#include <stdlib.h>

#include "assert.c"

int
main(void) {
	ASSERT(true);
	exit(EXIT_SUCCESS);
}

#endif /* TESTING_st */
