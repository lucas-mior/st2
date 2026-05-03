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

static int64
xwrite(int32 fd, char *s, int64 len) {
    int64 r;
    int64 left = (int64)len;

    while (left > 0) {
        r = write64(fd, s, len);
        if (r < 0) {
            return r;
        }
        left -= r;
        s += r;
    }

    return (int64)len;
}

#include "base64.c"

static int32
term_line_len(StGlyph *line) {
    int32 i = term.ncols - 1;

    for (; i >= 0 && !(line[i].mode & (ATTR_SET | ATTR_WRAP)); i -= 1);

    return i + 1;
}

static int32
term_is_wrapped(StGlyph *line) {
    int32 len = term_line_len(line);
    int32 wrapped = 0;

    if (len > 0) {
        if (line[len - 1].mode & ATTR_WRAP) {
            wrapped = 1;
        }
    }

    return wrapped;
}

static char *
term_get_glyphs(char *buffer, StGlyph *gp, StGlyph *lgp) {
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

static void
exec_shell(char *cmd, char **args) {
    char *shell;
    char *arg;
    struct passwd *pw;

    errno = 0;
    pw = getpwuid(getuid());
    if (pw == NULL) {
        if (errno) {
            error("getpwuid: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            error("who are you?\n");
            exit(EXIT_FAILURE);
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

static void
term_set_sixel_attr(StGlyph *line, int x1, int x2) {
    for (; x1 <= x2; x1 += 1) {
        line[x1].mode |= ATTR_SIXEL;
    }
    return;
}

static int32
term_attr_set(enum GlyphAttribute attr) {
    for (int32 i = 0; i < term.nrows - 1; i += 1) {
        for (int32 j = 0; j < term.ncols - 1; j += 1) {
            if (term.lines[i][j].mode & attr) {
                return 1;
            }
        }
    }

    return 0;
}

static void
term_set_dirt(int32 top, int32 bot) {
    LIMIT(top, 0, term.nrows - 1);
    LIMIT(bot, 0, term.nrows - 1);

    for (int32 i = top; i <= bot; i += 1) {
        term.dirty[i] = 1;
    }
    return;
}

static void
term_set_dirt_attr(enum GlyphAttribute attr) {
    for (int32 i = 0; i < term.nrows - 1; i += 1) {
        for (int32 j = 0; j < term.ncols - 1; j += 1) {
            if (term.lines[i][j].mode & attr) {
                term.dirty[i] = 1;
                break;
            }
        }
    }
    return;
}

static void
term_full_dirt(void) {
    for (int32 i = 0; i < term.nrows; i += 1) {
        term.dirty[i] = 1;
    }
    return;
}

static void
tdeleteimages(void) {
    ImageList *next;

    for (ImageList *im = term.images; im; im = next) {
        next = im->next;
        delete_image(im);
    }
    return;
}

static void
term_reset(void) {
    ImageList *im = term.images;
    while (im) {
        ImageList *next = im->next;
        free(im->pixels);
        free(im);
        im = next;
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
        term.tabs[i] = 1;
    }
    term.top_scroll_limit = 0;
    term.n_hist = 0;
    term.lines_scrolled_up = 0;
    term.bot_scroll_limit = term.nrows - 1;
    term.mode = TERM_MODE_WRAP | TERM_MODE_UTF8;
    memset64(term.translation_table, CS_USA, SIZEOF(term.translation_table));
    term.charset = 0;

    selection_remove();
    for (uint32 i = 0; i < 2; i += 1) {
        term_cursor(CURSOR_SAVE); /* reset saved cursor */
        for (int32 y = 0; y < term.nrows; y += 1) {
            for (int32 x = 0; x < term.ncols; x += 1) {
                term_clear_glyph(&term.lines[y][x], 0);
            }
        }
        tdeleteimages();
        term_swap_screen();
    }
    term_full_dirt();
    return;
}

/* handle it with care */
static void
term_swap_screen(void) {
    static StGlyph **altline;
    static int32 altcol;
    static int32 altrow;
    StGlyph **tmpline = term.lines;
    int32 tmpcol = term.ncols;
    int32 tmprow = term.nrows;

    term.lines = altline;
    term.ncols = altcol;
    term.nrows = altrow;
    altline = tmpline;
    altcol = tmpcol;
    altrow = tmprow;
    term.mode ^= TERM_MODE_ALTSCREEN;
    return;
}

static void
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

static void
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

static void
term_scroll_down(int32 top, int32 n) {
    int32 bot = term.bot_scroll_limit;
    StGlyph *temp;

    if (n <= 0) {
        return;
    }
    n = (int32)MIN(n, bot - top + 1);

    term_set_dirt(top, bot - n);
    term_clear_region(0, bot - n + 1, term.ncols - 1, bot, 1);

    for (int32 i = bot; i >= top + n; i -= 1) {
        temp = term.lines[i];
        term.lines[i] = term.lines[i - n];
        term.lines[i - n] = temp;
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

static void
term_scroll_up(int32 top, int32 bot, int32 n, enum ScrollMode mode) {
    int32 s = 0;
    uint32 alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);
    int32 savehist = !alt && top == 0 && mode != SCROLL_NOSAVEHIST;
    StGlyph *temp;

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
            term.hist[term.i_hist] = term.lines[i];
            term.lines[i] = temp;
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
        temp = term.lines[i];
        term.lines[i] = term.lines[i + n];
        term.lines[i + n] = temp;
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
            int32 height_in_rows = (im->height + im->ch - 1) / im->ch;

            /* FIX: Allow images in history (y < 0) to keep moving if we are saving history */
            if (im->y <= bot && (im->y >= top || (savehist && im->y < 0))) {
                im->y -= n;
            }

            /* FIX: Only delete if the BOTTOM of the image is past the history limit */
            if (im->y + height_in_rows < -term.n_hist) {
                *pim = im->next;
                if (im->pixmap) {
                    XFreePixmap(x_window.display, (Pixmap)im->pixmap);
                }
                if (im->clipmask) {
                    XFreePixmap(x_window.display, (Pixmap)im->clipmask);
                }
                free(im->pixels);
                free(im);
                continue;
            }
            pim = &(*pim)->next;
        }
    }
    return;
}

static void
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
    if (term.translation_table[term.charset] == CS_GRAPHIC0
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

    term.dirty[y] = 1;
    term.lines[y][x] = *attr;
    term.lines[y][x].rune = u;
    term.lines[y][x].mode |= ATTR_SET;

    if (isboxdraw(u)) {
        term.lines[y][x].mode |= ATTR_BOXDRAW;
    }
    return;
}

static void
term_clear_glyph(StGlyph *gp, int32 usecurattr) {
    if (usecurattr) {
        gp->fg = term.cursor.attr.fg;
        gp->bg = term.cursor.attr.bg;
    } else {
        gp->fg = CONF_COLOR_INDEX_FONT;
        gp->bg = CONF_COLOR_BG;
    }
    gp->mode = ATTR_NONE;
    gp->rune = ' ';
    return;
}

static void
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
            term_clear_glyph(&term.lines[y][x], usecurattr);
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
    term_clear_region(dst + size, term.cursor.y, term.ncols - 1, term.cursor.y,
                      1);
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
    term_clear_region(src, term.cursor.y, dst - 1, term.cursor.y, 1);
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
        term_scroll_up(term.cursor.y, term.bot_scroll_limit, n,
                       SCROLL_NOSAVEHIST);
    }
    return;
}

static void
externalpipe(union Arg *arg) {
    int32 to[2];
    char buffer[UTF_SIZ];
    void (*oldsigpipe)(int32);
    StGlyph *bp;
    StGlyph *end;
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

static void
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

static void
term_dump_sel(void) {
    char *ptr;

    ptr = selection_get();
    if (ptr) {
        term_printer(ptr, strlen32(ptr));
        free(ptr);
    }
    return;
}

static void
term_dump_line(int32 n) {
    char *string = xmalloc((int64)((term.ncols + 1)*UTF_SIZ) * SIZEOF(*string));
    char *buffer = string;
    StGlyph *fgp = &term.lines[n][0];
    StGlyph *lgp = &fgp[term.ncols - 1];
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

static void
term_dump(void) {
    for (int32 i = 0; i < term.nrows; i += 1) {
        term_dump_line(i);
    }
    return;
}

static void
reflow_scroll_down(int32 n) {
    int32 j;
    StGlyph *temp;

    n = (int32)MIN(n, term.n_hist);
    if (n <= 0) {
        return;
    }

    for (int32 i = term.cursor.y + n; i >= n; i -= 1) {
        temp = term.lines[i];
        term.lines[i] = term.lines[i - n];
        term.lines[i - n] = temp;
    }
    for (int32 i = n - 1; i >= 0; i -= 1) {
        temp = term.lines[i];
        term.lines[i] = term.hist[term.i_hist];
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

static void
term_resize(int32 col, int32 row) {
    int32 *bp;

    term.dirty = xrealloc(term.dirty, (int64)row*SIZEOF(*(term.dirty)));
    term.tabs = xrealloc(term.tabs, (int64)col*SIZEOF(*(term.tabs)));
    if (col > term.ncols) {
        bp = term.tabs + term.ncols;
        memset64(bp, 0, SIZEOF(*term.tabs)*(col - term.ncols));
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

static void
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
            free(term.lines[i]);
        }

        term.lines = xrealloc(term.lines, (int64)new_nrows*SIZEOF(*(term.lines)));

        for (int32 i = term.nrows; i < new_nrows; i += 1) {
            term.lines[i] = xmalloc((int64)new_ncols*SIZEOF(StGlyph));
            for (int32 j = 0; j < new_ncols; j += 1) {
                term_clear_glyph(&term.lines[i][j], 0);
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

static void
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
        free(term.lines[i]);
        i += 1;
    }
    if (i > 0) {
        memmove64(term.lines, term.lines + i,
                  new_nrows*SIZEOF(*(term.lines)));
        term.cursor.y = new_nrows - 1;
    }
    for (i += new_nrows; i < term.nrows; i += 1) {
        free(term.lines[i]);
    }
    term.lines = xrealloc(term.lines, (int64)new_nrows*SIZEOF(*(term.lines)));

    for (int32 j = 0; j < MIN(new_nrows, term.nrows); j += 1) {
        term.lines[j] = xrealloc(term.lines[j],
                                (int64)new_ncols*SIZEOF(*(term.lines[j])));
        for (int32 k = term.ncols; k < new_ncols; k += 1) {
            term_clear_glyph(&term.lines[j][k], 0);
        }
    }
    for (int32 j = (int32)MIN(new_nrows, term.nrows); j < new_nrows; j += 1) {
        term.lines[j] = xmalloc((int64)new_ncols*SIZEOF(StGlyph));
        for (int32 k = 0; k < new_ncols; k += 1) {
            term_clear_glyph(&term.lines[j][k], 0);
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

static void
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
    static StGlyph **reflow_lines = NULL;
    StGlyph *line = 0;

    /* --- determine end of current cursor line --- */
    old_cursor_end_line = term.cursor.y;
    while (old_cursor_end_line < (term.nrows - 1)) {
        int32 wrap_len = term_line_len(term.lines[old_cursor_end_line]);
        if (wrap_len > 0 && (term.lines[old_cursor_end_line][wrap_len - 1].mode & ATTR_WRAP)) {
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
            memcpy64(&reflow_lines[new_y_index][new_x_offset],
                   &line[old_x_offset], chars_left*SIZEOF(StGlyph));
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
                memcpy64(&reflow_lines[new_y_index][new_x_offset],
                       &line[old_x_offset], space_left*SIZEOF(StGlyph));
                old_x_offset = 0;
                old_y_index += 1;
                new_x_offset = 0;
            } else { /* space_left < chars_left */
                memcpy64(&reflow_lines[new_y_index][new_x_offset],
                       &line[old_x_offset], space_left*SIZEOF(StGlyph));
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
        free(term.lines[i]);
    }

    term.lines = xrealloc(term.lines, (int64)new_nrows*SIZEOF(*(term.lines)));

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
            free(reflow_lines[new_y_index]);
            new_y_index -= 1;
            term.cursor.y += 1;
        }
    }

    /* --- allocate additional rows if needed --- */
    for (i = new_nrows - 1; i > new_cursor_end_line; i -= 1) {
        term.lines[i] = xmalloc((int64)new_ncols*SIZEOF(StGlyph));
        for (int32 j = 0; j < new_ncols; j += 1) {
            term_clear_glyph(&term.lines[i][j], 0);
        }
    }

    /* --- populate visible lines --- */
    for (; i >= term.nrows; i -= 1) {
        term.lines[i] = reflow_lines[new_y_index];
        new_y_index -= 1;
    }

    for (; i >= 0; i -= 1) {
        free(term.lines[i]);
        term.lines[i] = reflow_lines[new_y_index];
        new_y_index -= 1;
    }

    /* --- update history reflow_lines --- */
    {
        int32 k_idx;
        k_idx = -1;
        while (new_y_index >= 0 && k_idx >= -HISTORY_SIZE) {
            int32 j_hist = (term.i_hist + k_idx + 1 + HISTORY_SIZE) % HISTORY_SIZE;
            free(term.hist[j_hist]);
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

static void
reset_title(void) {
    x_set_title(NULL);
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
        term.old_cursor_x -= 1;
    }
    if (term.lines[term.cursor.y][cx].mode & ATTR_WDUMMY) {
        cx -= 1;
    }

    for (int32 y = 0; y < term.nrows; y += 1) {
        if (!term.dirty[y]) {
            continue;
        }

        term.dirty[y] = 0;
        x_draw_line(TERM_LINE(y), 0, y, term.ncols);
    }

    x_draw_cursor(cx, term.cursor.y, term.lines[term.cursor.y][cx],
                  term.old_cursor_x, term.old_cursor_y,
                  term.lines[term.old_cursor_y][term.old_cursor_x]);
    term.old_cursor_x = cx;
    term.old_cursor_y = term.cursor.y;

    /* x_finish_draw() */ {
        int32 bw = term_window.hborderpx;
        int32 bh = term_window.vborderpx;
        int32 term_h = term.nrows * term_window.ch;
        GC gc = NULL;
        ImageList *next;

        XSetClipMask(x_window.display, draw_context.graphics, None);

        for (ImageList *im = term.images; im; im = next) {
            int32 rel_y = im->y + term.lines_scrolled_up;
            int32 height_in_rows = (im->height + im->ch - 1) / im->ch;
            int32 width = im->width;
            int32 height = im->height;
            next = im->next;

            /* Check if ANY part of the image is on the visible screen */
            if (im->x >= term.ncols || rel_y >= term.nrows || rel_y + height_in_rows <= 0) {
                continue;
            }

            if (im->pixmap == NULL) {
                XImage ximage;
                im->pixmap = (void *)XCreatePixmap(x_window.display, x_window.win,
                                                   (uint32)width, (uint32)height,
                                                   (uint32)x_window.depth);

                if (im->transparent) {
                    im->clipmask = (void *)sixel_create_clipmask((char *)im->pixels,
                                                                 width, height);
                }

                ximage.format = ZPixmap;
                ximage.data = (char *)im->pixels;
                ximage.width = width;
                ximage.height = height;
                ximage.depth = x_window.depth;
                ximage.bits_per_pixel = 32;
                ximage.bytes_per_line = width*4;
                ximage.byte_order = ImageByteOrder(x_window.display);
                ximage.bitmap_unit = 32;
                ximage.bitmap_pad = 32;

                XPutImage(x_window.display, (Drawable)im->pixmap,
                          draw_context.graphics, &ximage, 0, 0, 0, 0, (uint32)width,
                          (uint32)height);
            }

            if (gc == NULL) {
                XGCValues gcvalues;
                memset64(&gcvalues, 0, SIZEOF(gcvalues));
                gcvalues.graphics_exposures = False;
                gc = XCreateGC(x_window.display, x_window.win, GCGraphicsExposures, &gcvalues);
            }

            {
                int32 draw_w = width;
                int32 draw_h = height;
                int32 src_y = 0;
                int32 dest_y = bh + rel_y * term_window.ch;

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
                    if (im->transparent && im->clipmask) {
                        /* Mask origin must track the logical position (rel_y) */
                        XSetClipOrigin(x_window.display, gc, bw + im->x * term_window.cw, bh + rel_y * term_window.ch);
                        XSetClipMask(x_window.display, gc, (Pixmap)im->clipmask);
                    } else {
                        XSetClipMask(x_window.display, gc, None);
                    }

                    XCopyArea(x_window.display, (Drawable)im->pixmap, x_window.drawable, gc,
                              0, src_y, (uint32)draw_w, (uint32)draw_h,
                              bw + im->x * term_window.cw, dest_y);
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
zoom_abs(union Arg *arg) {
    int32 i;
    ImageList *im;
    x_unload_fonts();
    x_load_fonts(usedfont, arg->f);
    x_load_spare_fonts();

    for (im = term.images, i = 0; i < 2; i += 1, im = term.images_alt) {
        for (; im; im = im->next) {
            if (im->pixmap) {
                XFreePixmap(x_window.display, (Drawable)im->pixmap);
            }
            if (im->clipmask) {
                XFreePixmap(x_window.display, (Drawable)im->clipmask);
            }
            im->pixmap = NULL;
            im->clipmask = NULL;
        }
    }

    cresize(0, 0);
    redraw();
    x_hints();
    return;
}

static int32
xevent_col(XEvent *xevent) {
    int32 x = xevent->xbutton.x - term_window.hborderpx;
    LIMIT(x, 0, term_window.tty_width - 1);
    return x / term_window.cw;
}

static int32
xevent_row(XEvent *xevent) {
    int32 y = xevent->xbutton.y - term_window.vborderpx;
    LIMIT(y, 0, term_window.tty_height - 1);
    return y / term_window.ch;
}

static void
mouse_select(XEvent *xevent, int32 done) {
    enum SelectionType seltype = SELECTION_NORMAL;
    uint32 state = xevent->xbutton.state & ~(Button1Mask | CONF_FORCE_MOUSE_MOD);

    for (enum SelectionType type = 1; type < LENGTH(CONF_SELECTION_MASKS); type += 1) {
        if (match_mask_state(CONF_SELECTION_MASKS[type], state)) {
            seltype = type;
            break;
        }
    }
    selection_extend(xevent_col(xevent), xevent_row(xevent), seltype, done);
    if (done) {
        selection_set(selection_get(), xevent->xbutton.time);
    }
    return;
}

static void
mouse_report(XEvent *xevent) {
    int32 len;
    int32 button;
    int32 code;
    int32 x = xevent_col(xevent);
    int32 y = xevent_row(xevent);
    int32 state = (int32)xevent->xbutton.state;
    char buffer[40];
    static int32 ox;
    static int32 oy;

    if (xevent->type == MotionNotify) {
        if (x == ox && y == oy) {
            return;
        }
        if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMOTION)
            && !TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMANY)) {
            return;
        }
        /* WIN_MODE_MOUSEMOTION: no reporting if no button is pressed */
        if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSEMOTION) && buttons == 0) {
            return;
        }
        /* Set button to lowest-numbered pressed button, or 12 if no
         * buttons are pressed. */
        for (button = 1;
             button <= 11 && !(buttons & (1 << (button - 1)));
             button += 1) {
        }
        code = 32;
    } else {
        button = (int32)xevent->xbutton.button;
        /* Only buttons 1 through 11 can be encoded */
        if (button < 1 || button > 11) {
            return;
        }
        if (xevent->type == ButtonRelease) {
            /* WIN_MODE_MOUSEX10: no button release reporting */
            if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSEX10)) {
                return;
            }
            /* Don't send release events for the scroll wheel */
            if (button == 4 || button == 5) {
                return;
            }
        }
        code = 0;
    }

    ox = x;
    oy = y;

    /* Encode button into code. If no button is pressed for a motion event in
     * WIN_MODE_MOUSEMANY, then encode it as a release. */
    if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSESGR) && xevent->type == ButtonRelease) {
        code += 3;
    } else if (button == 12) {
        code += 3;
    } else if (button >= 8) {
        code += 128 + button - 8;
    } else if (button >= 4) {
        code += 64 + button - 4;
    } else {
        code += button - 1;
    }

    if (!TERM_WINDOW_IS_SET(WIN_MODE_MOUSEX10)) {
        if (state & ShiftMask) {
            code += 4;
        }
        if (state & Mod1Mask) {
            code += 8;
        }
        if (state & ControlMask) {
            code += 16;
        }
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_MOUSESGR)) {
        char c;
        if (xevent->type == ButtonRelease) {
            c = 'm';
        } else {
            c = 'M';
        }
        len = SNPRINTF(buffer, "\033[<%d;%d;%d%c",
                               code, x + 1, y + 1, c);
    } else if (x < 223 && y < 223) {
        len = SNPRINTF(buffer, "\033[M%c%c%c",
                               32 + code, 32 + x + 1, 32 + y + 1);
    } else {
        return;
    }

    tty_write(buffer, (int64)len, 0);
    return;
}

static uint32
button_mask(uint32 button) {
    if (button == Button1) {
        return Button1Mask;
    }
    if (button == Button2) {
        return Button2Mask;
    }
    if (button == Button3) {
        return Button3Mask;
    }
    if (button == Button4) {
        return Button4Mask;
    }
    if (button == Button5) {
        return Button5Mask;
    }
    return 0;
}

static int32
mouse_action(XEvent *xevent, uint32 release) {
    MouseShortcut *mouse_shortcut;
    /* ignore Button<N>mask for Button<N> - it's set on release */
    uint32 state = xevent->xbutton.state & ~button_mask(xevent->xbutton.button);

    for (mouse_shortcut = CONF_MOUSE_SHORTCUTS;
         mouse_shortcut < CONF_MOUSE_SHORTCUTS + LENGTH(CONF_MOUSE_SHORTCUTS);
         mouse_shortcut += 1) {
        if (mouse_shortcut->release == release
            && mouse_shortcut->button == xevent->xbutton.button) {
            if (match_mask_state(mouse_shortcut->mod, state)) {
                mouse_shortcut->func(&(mouse_shortcut->arg));
                return 1;
            }
            if (match_mask_state(mouse_shortcut->mod, state & ~CONF_FORCE_MOUSE_MOD)) {
                mouse_shortcut->func(&(mouse_shortcut->arg));
                return 1;
            }
        }
    }

    return 0;
}

static void
cresize(int32 width, int32 height) {
    int32 col;
    int32 row;

    if (width != 0) {
        term_window.w = width;
    }
    if (height != 0) {
        term_window.h = height;
    }

    col = (term_window.w - 2*CONF_BORDER_PIXELS) / term_window.cw;
    row = (term_window.h - 2*CONF_BORDER_PIXELS) / term_window.ch;
    col = (int32)MAX(1, col);
    row = (int32)MAX(1, row);

    term_window.hborderpx = (term_window.w - col*term_window.cw) / 2;
    term_window.vborderpx = (term_window.h - row*term_window.ch) / 2;

    term_resize(col, row);
    x_resize(col, row);
    tty_resize(term_window.tty_width, term_window.tty_height);
    return;
}

static int32
match_mask_state(uint32 mask, uint32 state) {
    if (mask == XK_ANY_MOD) {
        return 1;
    }
    if (mask == (state & ~CONF_IGNORE_MOD)) {
        return 1;
    }
    return 0;
}

static void __attribute((noreturn)) 
usage(void) {
    error("usage: %s [-aiv] [-c class] [-f font] [-g geometry]"
          " [-n name] [-o file]\n"
          "          [-T title] [-t title] [-w windowid]"
          " [[-e] command [args ...]]\n"
          "       %s [-aiv] [-c class] [-f font] [-g geometry]"
          " [-n name] [-o file]\n"
          "          [-T title] [-t title] [-w windowid] -l line"
          " [CONF_STTY_ARGS ...]\n",
          argv0, argv0);
    exit(EXIT_FAILURE);
}

#if TESTING_st

#include <stdbool.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "assert.c"
#include "user.c"
#include "x.c"

int
main(void) {
    x_window.display = XOpenDisplay(NULL);
    if (x_window.display) {
        XGCValues xgc_values;
        x_window.screen = XDefaultScreen(x_window.display);
        x_window.visual = DefaultVisual(x_window.display, x_window.screen);
        x_window.depth = DefaultDepth(x_window.display, x_window.screen);
        x_window.color_map = DefaultColormap(x_window.display, x_window.screen);
        x_window.win = XCreateSimpleWindow(x_window.display, RootWindow(x_window.display, x_window.screen), 0, 0, 1, 1, 0, 0, 0);
        memset64(&xgc_values, 0, SIZEOF(xgc_values));
        xgc_values.graphics_exposures = False;
        draw_context.graphics = XCreateGC(x_window.display, x_window.win, GCGraphicsExposures, &xgc_values);
        x_window.drawable = XCreatePixmap(x_window.display, x_window.win, 1, 1, (uint32)x_window.depth);
        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable, x_window.visual, x_window.color_map);
    }

    CONF_NUMBER_COLS = 10;
    CONF_NUMBER_ROWS = 5;
    term.ncols = CONF_NUMBER_COLS;
    term.nrows = CONF_NUMBER_ROWS;
    term.dirty = xmalloc(term.nrows * SIZEOF(*term.dirty));
    term.tabs = xmalloc(term.ncols * SIZEOF(*term.tabs));
    for (int32 i = 0; i < 2; i += 1) {
        term.lines = xmalloc(term.nrows * SIZEOF(*term.lines));
        for (int32 j = 0; j < term.nrows; j += 1) {
            term.lines[j] = xmalloc(term.ncols * SIZEOF(*term.lines[j]));
        }
        term_swap_screen();
    }
    for (int32 i = 0; i < HISTORY_SIZE; i += 1) {
        term.hist[i] = xmalloc(term.ncols * SIZEOF(StGlyph));
    }
    term_reset();

    {
        StGlyph line[10];
        int32 len;
        term.ncols = 10;
        for (int32 i = 0; i < 10; i += 1) { line[i].mode = ATTR_NONE; }
        len = term_line_len(line);
        ASSERT_EQUAL(len, 0);
        line[4].mode = ATTR_SET;
        len = term_line_len(line);
        ASSERT_EQUAL(len, 5);
    }

    {
        StGlyph line[10];
        int32 wrapped;
        term.ncols = 10;
        for (int32 i = 0; i < 10; i += 1) { line[i].mode = ATTR_NONE; }
        line[4].mode = ATTR_SET;
        wrapped = term_is_wrapped(line);
        ASSERT_EQUAL(wrapped, 0);
        line[4].mode = ATTR_SET | ATTR_WRAP;
        wrapped = term_is_wrapped(line);
        ASSERT_EQUAL(wrapped, 1);
    }

    {
        StGlyph line[2];
        char buf[10];
        char *ptr;
        line[0].rune = 'A';
        line[0].mode = ATTR_SET;
        line[1].rune = 'B';
        line[1].mode = ATTR_SET;
        ptr = term_get_glyphs(buf, &line[0], &line[1]);
        *ptr = '\0';
        ASSERT(strcmp(buf, "AB") == 0);
    }

    {
        StGlyph line[10];
        for (int32 i = 0; i < 10; i += 1) { line[i].mode = ATTR_NONE; }
        term_set_sixel_attr(line, 2, 5);
        ASSERT(line[2].mode & ATTR_SIXEL);
        ASSERT(line[5].mode & ATTR_SIXEL);
    }

    {
        int32 res;
        term_reset();
        res = term_attr_set(ATTR_BOLD);
        ASSERT_EQUAL(res, 0);
        term.lines[0][0].mode |= ATTR_BOLD;
        res = term_attr_set(ATTR_BOLD);
        ASSERT_EQUAL(res, 1);
    }

    {
        term_full_dirt();
        ASSERT(term.dirty[0]);
        term.dirty[0] = 0;
        term_set_dirt(0, 0);
        ASSERT(term.dirty[0]);
        term.dirty[0] = 0;
        term.lines[0][0].mode |= ATTR_ITALIC;
        term_set_dirt_attr(ATTR_ITALIC);
        ASSERT(term.dirty[0]);
    }

    {
        int32 mode = term.mode & TERM_MODE_ALTSCREEN;
        term_swap_screen();
        ASSERT((term.mode & TERM_MODE_ALTSCREEN) != mode);
        term_swap_screen();
    }

    {
        term_move_to(5, 2);
        ASSERT_EQUAL(term.cursor.x, 5);
        ASSERT_EQUAL(term.cursor.y, 2);
        term_move_abs_to(1, 1);
        ASSERT_EQUAL(term.cursor.x, 1);
        ASSERT_EQUAL(term.cursor.y, 1);
    }

    {
        StGlyph attr_val;
        attr_val.mode = ATTR_BOLD;
        attr_val.fg = 1;
        attr_val.bg = 2;
        term_set_char('X', &attr_val, 0, 0);
        ASSERT_EQUAL(term.lines[0][0].rune, 'X');
        ASSERT(term.lines[0][0].mode & ATTR_BOLD);
    }

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

    {
        XEvent ev;
        int32 col;
        ev.type = ButtonPress;
        ev.xbutton.x = term_window.hborderpx + 5;
        ev.xbutton.y = term_window.vborderpx + 5;
        term_window.cw = 10;
        term_window.ch = 20;
        term_window.tty_width = 100;
        term_window.tty_height = 100;
        col = xevent_col(&ev);
        ASSERT_EQUAL(col, 0);
    }

    {
        ASSERT(match_mask_state(XK_ANY_MOD, 0));
        ASSERT(match_mask_state(ShiftMask, ShiftMask));
    }

    {
        term_load_alt_screen(1, 1);
        ASSERT(term.mode & TERM_MODE_ALTSCREEN);
        term_load_def_screen(1, 1);
        ASSERT(!(term.mode & TERM_MODE_ALTSCREEN));
    }

    {
        term_reset();
        term_new_line(1);
        ASSERT_EQUAL(term.cursor.y, 1);
        ASSERT_EQUAL(term.cursor.x, 0);
    }

    {
        uint32 mask;
        mask = button_mask(Button1);
        ASSERT_EQUAL(mask, Button1Mask);

        mask = button_mask(Button2);
        ASSERT_EQUAL(mask, Button2Mask);

        mask = button_mask(Button3);
        ASSERT_EQUAL(mask, Button3Mask);

        mask = button_mask(Button4);
        ASSERT_EQUAL(mask, Button4Mask);

        mask = button_mask(Button5);
        ASSERT_EQUAL(mask, Button5Mask);

    }

    {
        StGlyph glyph_val;
        term_clear_glyph(&glyph_val, 0);
        ASSERT(glyph_val.mode == ATTR_NONE);
        ASSERT_EQUAL(glyph_val.rune, ' ');
        ASSERT_EQUAL(glyph_val.fg, CONF_COLOR_INDEX_FONT);
        ASSERT_EQUAL(glyph_val.bg, CONF_COLOR_BG);
    }

    XCloseDisplay(x_window.display);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_st */

#endif /* ST_C */
