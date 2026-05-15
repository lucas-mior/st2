#if !defined(USER_C)
#define USER_C

#include <termios.h>
#include "st.h"
#include "util.c"
#include "config.h"
#include "selection.c"
#include "utf8.c"
#include "x.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_user 1
#elif !defined(TESTING_user)
#define TESTING_user 0
#endif

static void
user_clipboard_copy(union Arg *arg) {
    Atom clipboard;
    (void)arg;

    free2(xsel.clipboard, xsel.clipboard_len + 1);
    xsel.clipboard = NULL;
    xsel.clipboard_len = 0;

    if (xsel.primary != NULL) {
        xsel.clipboard_len = strlen32(xsel.primary);
        xsel.clipboard = malloc2(xsel.clipboard_len + 1);
        memcpy64(xsel.clipboard, xsel.primary, xsel.clipboard_len + 1);

        clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
        XSetSelectionOwner(x_window.display,
                           clipboard, x_window.win,
                           CurrentTime);
    }
    return;
}

static void
user_clipboard_paste(union Arg *arg) {
    Atom clipboard;
    (void)arg;

    clipboard = XInternAtom(x_window.display, "CLIPBOARD", 0);
    XConvertSelection(x_window.display, clipboard, xsel.xtarget, clipboard,
                      x_window.win, CurrentTime);
    return;
}

static void
user_selection_paste(union Arg *arg) {
    (void)arg;
    XConvertSelection(x_window.display,
                      XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
                      x_window.win, CurrentTime);
    return;
}

static void
user_change_alpha(union Arg *arg) {
    if ((CONF_ALPHA > 0 && arg->f < 0) || (CONF_ALPHA < 1 && arg->f > 0)) {
        CONF_ALPHA += arg->f;
    }
    if (CONF_ALPHA < 0) {
        CONF_ALPHA = 0;
    }
    if (CONF_ALPHA > 1) {
        CONF_ALPHA = 1;
    }

    x_load_colors();
    redraw();
    return;
}

static void
user_toggle_numlock(union Arg *arg) {
    (void)arg;
    term_window.mode ^= WIN_MODE_NUMLOCK;
    return;
}

static void
clear_image_pixmaps(ImageList *list) {
    for (ImageList *image = list; image; image = image->next) {
        if (image->pixmap) {
            XFreePixmap(x_window.display, (Drawable)image->pixmap);
        }
        if (image->clipmask) {
            XFreePixmap(x_window.display, (Drawable)image->clipmask);
        }
        image->pixmap = NULL;
        image->clipmask = NULL;
    }
    return;
}

static void
zoom_abs(union Arg *arg) {
    x_unload_fonts();
    x_load_fonts(used_font, arg->f);
    x_load_spare_fonts();

    clear_image_pixmaps(term.images);
    clear_image_pixmaps(term.images_alt);

    x_configure_resize(0, 0);
    redraw();
    x_hints();
    return;
}

static void
user_zoom(union Arg *arg) {
    union Arg larg;

    larg.f = used_font_size + arg->f;
    if (larg.f >= 1.0f) {
        zoom_abs(&larg);
    }
    return;
}

static void
user_zoom_reset(union Arg *arg) {
    union Arg larg;
    (void)arg;

    if (default_font_size > 0) {
        larg.f = default_font_size;
        zoom_abs(&larg);
    }
    return;
}

static void
user_tty_send(union Arg *arg) {
    tty_write(arg->s, strlen32(arg->s), 1);
    return;
}

static void
user_scroll_down(union Arg *a) {
    int64 n = a->i;

    if (!term.scrolled_up || term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        return;
    }

    if (n < 0) {
        n = MAX(term.nrows / -n, 1);
    }

    if (n <= term.scrolled_up) {
        term.scrolled_up -= n;
    } else {
        n = term.scrolled_up;
        term.scrolled_up = 0;
    }
    if (selection.ob.x != -1 && !selection.alt) {
        selection_move_y((int32)-n); /* negate change in term.scrolled_up */
    }
    term_full_dirt();
    return;
}

static void
user_scroll_up(union Arg *a) {
    int64 n = a->i;

    if (!term.n_hist || term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        return;
    }

    if (n < 0) {
        n = MAX(term.nrows / -n, 1);
    }

    if (term.scrolled_up + n <= term.n_hist) {
        term.scrolled_up += n;
    } else {
        n = term.n_hist - term.scrolled_up;
        term.scrolled_up = term.n_hist;
    }

    if (selection.ob.x != -1 && !selection.alt) {
        selection_move_y((int32)n); /* negate change in term.scrolled_up */
    }
    term_full_dirt();
    return;
}

static void
user_smart_scroll_up(union Arg *arg) {
    /* 
     * If Altscreen is on, or if the app has enabled Application Cursor Keys
     * (a very strong signal that a TUI like less or vim is running).
     */
    if (term_mode_is_set(TERM_MODE_ALTSCREEN)
        || win_mode_is_set(WIN_MODE_APPCURSOR)) {
        user_tty_send(&(union Arg){.s = "\031"}); /* Send Ctrl-Y */
    } else {
        user_scroll_up(arg);
    }
    return;
}

static void
user_smart_scroll_down(union Arg *arg) {
    if (term_mode_is_set(TERM_MODE_ALTSCREEN)
        || win_mode_is_set(WIN_MODE_APPCURSOR)) {
        user_tty_send(&(union Arg){.s = "\005"}); /* Send Ctrl-E */
    } else {
        if (term.scrolled_up > 0) {
            user_scroll_down(arg);
        }
    }
    return;
}

static void
user_send_break(union Arg *arg) {
    if (tcsendbreak(command_fd, 0)) {
        error("Error sending break.\n");
    }
    (void)arg;
    return;
}

static void
user_toggle_printer(union Arg *arg) {
    term.mode ^= TERM_MODE_PRINT;
    (void)arg;
    return;
}

static void
user_print_screen(union Arg *arg) {
    term_dump();
    (void)arg;
    return;
}

static void
user_print_sel(union Arg *arg) {
    term_dump_sel();
    (void)arg;
    return;
}

static void
dump_for_editor(int32 fd, int32 *out_row, int32 *out_col) {
    char buffer[UTF_SIZ];

    for (int32 y = -term.n_hist; y < term.nrows; y += 1) {
        StGlyph *line = term_line_abs(y);
        int32 last_pos = term.ncols - 1;

        while ((last_pos >= 0)
                && !(line[last_pos].mode & (ATTR_SET | ATTR_WRAP))) {
            last_pos -= 1;
        }
        last_pos += 1;

        if (y == term.cursor.y) {
            last_pos = (int32)MAX(last_pos, term.cursor.x + 1);
        }

        for (int32 x = 0; x < last_pos; x += 1) {
            if (!(line[x].mode & ATTR_WDUMMY)) {
                xwrite(fd, buffer, utf8_encode(line[x].rune, buffer));
            }
        }
        
        if ((last_pos == 0)
                || !(line[last_pos - 1].mode & ATTR_WRAP)
                || (y == term.nrows - 1)) {
            xwrite(fd, "\n", 1);
        }
    }

    if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        *out_row = term.n_hist + 1;
        *out_col = 1;
    } else {
        *out_row = term.n_hist + term.cursor.y + 1;
        *out_col = term.cursor.x + 1;
    }
    
    return;
}

static void
user_vim_select(union Arg *arg) {
    char tmp_file[] = "/tmp/st_vimselect_XXXXXX";
    int32 fd;
    int32 target_row;
    int32 target_col;
    pid_t child;

    (void)arg;

    if ((fd = mkstemp(tmp_file)) < 0) {
        error("Error creating temporary file: %s\n", strerror(errno));
        return;
    }

    dump_for_editor(fd, &target_row, &target_col);
    XCLOSE(&fd);

    if (term_mode_is_set(TERM_MODE_ALTSCREEN)) {
        target_row = term.n_hist + 1;
        target_col = 1;
    } else {
        target_row = term.n_hist + term.cursor.y + 1;
        target_col = term.cursor.x + 1;
    }

    switch (child = fork()) {
    case -1:
        error("fork failed: %s\n", strerror(errno));
        break;
    case 0:
        {
            char geometry[32];
            char window[32];
            char cursor[64];
            char delete_command[128];
            char cmd[4096];
            char *argv[64];
            int32 argc = 0;

            SNPRINTF(geometry,
                     "%dx%d", term.ncols, term.nrows);
            SNPRINTF(window,
                     "%lu", x_window.win);
            SNPRINTF(cursor,
                     "call cursor(%d, %d)", target_row, target_col);
            SNPRINTF(delete_command,
                     "autocmd VimLeave * call delete('%s')", tmp_file);
            
            argv[argc++] = program_path;
            argv[argc++] = "-w";
            argv[argc++] = window;
            argv[argc++] = "-g";
            argv[argc++] = geometry;
            argv[argc++] = "-f";
            argv[argc++] = used_font;
            argv[argc++] = "-e";
            argv[argc++] = "vim";
            argv[argc++] = "-c" ;
            argv[argc++] = "set nonumber norelativenumber wrap";
            argv[argc++] = "-c";
            argv[argc++] = "set laststatus=0 buftype=nowrite";
            argv[argc++] = "-c";
            argv[argc++] = cursor;
            argv[argc++] = "-c";
            argv[argc++] = delete_command;
            argv[argc++] = tmp_file;
            argv[argc++] = NULL;

            execvp(argv[0], argv);
            STRING_FROM_ARRAY(cmd, " ", argv, argc);
            error("Error executing\n%s\n%s", cmd, strerror(errno));
            _exit(1);
        }
    default:
        XSetInputFocus(x_window.display, x_window.win,
                       RevertToParent, CurrentTime);
        XRaiseWindow(x_window.display, x_window.win);

        XWarpPointer(x_window.display, None, x_window.win,
                     0, 0, 0, 0,
                     term_window.w / 2, term_window.h / 2);

        XFlush(x_window.display);
        if (waitpid(child, NULL, 0) < 0) {
            error("Error waiting for child: %s.\n", strerror(errno));
        }
        break;
    }

    return;
}

static void
dump_terminal_to_fd(int32 fd) {
    int32 newline = 0;
    void (*oldsigpipe)(int32) = signal(SIGPIPE, SIG_IGN);

    for (int32 n = 0; n <= (HISTORY_SIZE + 2); n += 1) {
        StGlyph *line = term_line_hist(n);
        StGlyph *end;
        char buffer[UTF_SIZ];
        int32 i_hist = term.ncols;
        int32 last_pos;

        if (term_line_hist(n)[i_hist - 1].mode & ATTR_WRAP) {
            last_pos = i_hist - 1;
        } else {
            while ((i_hist > 0) && term_line_hist(n)[i_hist - 1].rune == ' ') {
                i_hist -= 1;
            }
            last_pos = i_hist - 1;
        }

        if (last_pos < 0) {
            continue;
        }

        end = &line[last_pos + 1];
        while (line < end) {
            if (!(line->mode & ATTR_WDUMMY)) {
                if (xwrite(fd, buffer, utf8_encode(line->rune, buffer)) < 0) {
                    goto cleanup;
                }
            }
            line += 1;
        }

        if (term_line_hist(n)[last_pos].mode & ATTR_WRAP) {
            newline = 1;
            continue;
        }

        newline = 0;
        if (xwrite(fd, "\n", 1) < 0) {
            break;
        }
    }

    if (newline) {
        (void)xwrite(fd, "\n", 1);
    }

cleanup:
    signal(SIGPIPE, oldsigpipe);
    return;
}

static void
exec_external_pipe(int32 argc, char **argv) {
    int32 pipe[2];

    xpipe(pipe);

    switch (fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        xdup2(pipe[0], STDIN_FILENO);
        XCLOSE(&pipe[0]);
        XCLOSE(&pipe[1]);
        execvp(argv[0], argv);
        {
            char cmd[4096];
            STRING_FROM_ARRAY(cmd, " ", argv, argc);
            error("Error executing\n%s\n: %s.\n", cmd, strerror(errno));
            _exit(1);
        }
    default:
        XCLOSE(&pipe[0]);
        dump_terminal_to_fd(pipe[1]);
        XCLOSE(&pipe[1]);
        break;
    }

    return;
}

static void
user_external_pipe(union Arg *arg) {
    exec_external_pipe(0, arg->v);
    return;
}

#include "gen/copy_output.h"
#include "gen/copy_url.h"

static void
user_copy_output(union Arg *arg) {
    char winid[32];
    char *argv[32];
    int32 argc = 0;

    (void)arg;

    SNPRINTF(winid, "%lu", x_window.win);

    argv[argc++] = "sh";
    argv[argc++] = "-c";
    argv[argc++] = (char *)st_copy_output;
    argv[argc++] = winid;
    argv[argc++] = NULL;

    (void)st_copy_output_len;

    exec_external_pipe(argc, argv);
    return;
}

static void
user_url_select(union Arg *arg) {
    char winid[32];
    char *argv[32];
    int32 argc = 0;
    char *mode;

    if (arg->i == 'c') {
        mode = "c";
    } else {
        mode = "o";
    }

    SNPRINTF(winid, "%lu", x_window.win);

    argv[argc++] = "sh";
    argv[argc++] = "-c";
    argv[argc++] = (char *)st_copy_url;
    argv[argc++] = winid;
    argv[argc++] = mode;
    argv[argc++] = NULL;

    (void)st_copy_url_len;

    exec_external_pipe(argc, argv);
    return;
}

static void
user_toggle_colorscheme(union Arg *arg) {
    static int32 is_light = 0;
    (void)arg;

    if (is_light == 0) {
        for (int32 i = 0; i < LENGTH(CONF_COLORS); i += 1) {
            if (CONF_COLORS_LIGHT[i]) {
                CONF_COLORS[i] = CONF_COLORS_LIGHT[i];
            }
        }
        is_light = 1;
    } else {
        for (int32 i = 0; i < LENGTH(CONF_COLORS); i += 1) {
            if (CONF_COLORS_DARK[i]) {
                CONF_COLORS[i] = CONF_COLORS_DARK[i];
            }
        }
        is_light = 0;
    }

    x_load_colors();
    redraw();
    return;
}

#if TESTING_user

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "assert.c"
#include "st.c"
#include "x.c"

int
main(void) {
    /* 1. Rigid Initialization of Global State */
    {
        XGCValues xgc_values = {.graphics_exposures = False};

        x_window.display = XOpenDisplay(NULL);
        if (!x_window.display) {
            error("can't open display\n");
            exit(EXIT_FAILURE);
        }
        x_window.screen = XDefaultScreen(x_window.display);
        x_window.visual = DefaultVisual(x_window.display, x_window.screen);
        x_window.depth = DefaultDepth(x_window.display, x_window.screen);
        x_window.color_map = DefaultColormap(x_window.display, x_window.screen);
        
        /* Initialize pointers to NULL to prevent xrealloc/XFree crashes */
        x_window.font_spec_buf = NULL;
        x_window.drawable = None;
        
        x_window.win = XCreateSimpleWindow(x_window.display, 
                                           RootWindow(x_window.display, x_window.screen), 
                                           0, 0, 100, 100, 0, 0, 0);

        xsel.xtarget = XInternAtom(x_window.display, "UTF8_STRING", 0);

        CONF_NCOLS = 80;
        CONF_NROWS = 24;
        term_allocate();

        /* Essential for x_configure_resize and x_resize math */
        term_window.cw = 10;
        term_window.ch = 20;
        term_window.w = 800;
        term_window.h = 600;

        /* Create initial drawable and Xft context */
        x_window.drawable = XCreatePixmap(x_window.display, x_window.win, 
                                          (uint32)term_window.w, (uint32)term_window.h, 
                                          (uint32)x_window.depth);
        
        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable, 
                                          x_window.visual, x_window.color_map);
        
        if (!x_window.xft_draw) {
            error("XftDrawCreate failed\n");
            exit(EXIT_FAILURE);
        }

        draw_context.graphics = XCreateGC(x_window.display, x_window.win, 
                                          GCGraphicsExposures, &xgc_values);

        term_reset();
        
        default_font_size = 12.0;
        used_font_size = 12.0;
        used_font = CONF_FONT;
        x_load_fonts(used_font, 0);
    }

    /* 2. Logic & State Tests */
    {
        term_window.mode = 0;
        user_toggle_numlock(NULL);
        ASSERT_EQUAL((int32)term_window.mode, WIN_MODE_NUMLOCK);
        
        term.mode = 0;
        user_toggle_printer(NULL);
        ASSERT_EQUAL((int32)term.mode, TERM_MODE_PRINT);
    }

    {
        union Arg a;
        term.n_hist = 10;
        term.scrolled_up = 0;
        a.i = 5;
        user_scroll_up(&a);
        ASSERT_EQUAL(term.scrolled_up, 5);
        user_scroll_down(&a);
        ASSERT_EQUAL(term.scrolled_up, 0);
    }

    /* 3. Functional Tests (Parent process) */
    {
        union Arg a;
        
        /* Clipboard */
        xsel.primary = xstrdup("test_data");
        user_clipboard_copy(NULL);
        ASSERT(xsel.clipboard != NULL);

        /* Visuals & Scaling */
        a.f = 0.1;
        user_change_alpha(&a);
        
        /* This triggers x_resize via x_configure_resize(0, 0) */
        a.f = 2.0; 
        user_zoom(&a);
        user_zoom_reset(NULL);
    }

    /* 4. Subsystem Isolation (Forked) */
    {
        if (fork() == 0) {
            command_fd = -1; 
            user_send_break(NULL);
            exit(EXIT_SUCCESS);
        }
        wait(NULL);

        if (fork() == 0) {
            io_fd = open("/dev/null", O_WRONLY);
            user_print_screen(NULL);
            user_print_sel(NULL);
            exit(EXIT_SUCCESS);
        }
        wait(NULL);

        if (fork() == 0) {
            union Arg a = {.s = "test"};
            user_tty_send(&a);
            exit(EXIT_SUCCESS);
        }
        wait(NULL);
    }

    /* Cleanup */
    if (x_window.xft_draw) {
        XftDrawDestroy(x_window.xft_draw);
    }
    if (x_window.drawable) {
        XFreePixmap(x_window.display, x_window.drawable);
    }
    if (x_window.display) {
        XCloseDisplay(x_window.display);
    }
    
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_user */

#endif /* USER_C */
