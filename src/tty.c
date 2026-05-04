#if !defined(TTY_C)
#define TTY_C

#include <pty.h>
#include <pwd.h>
#include "st.h"
#include "config.h"
#include "handlers.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_tty 1
#elif !defined(TESTING_tty)
#define TESTING_tty 0
#endif

static void
stty(char **args) {
    char cmd[_POSIX_ARG_MAX];
    char *s;
    int64 n;
    char *exec_args[256];
    int32 exec_argc = 0;
    char *token;
    pid_t pid2;

    if ((n = strlen32(CONF_STTY_ARGS)) > SIZEOF(cmd) - 1) {
        error("incorrect stty parameters\n");
        exit(EXIT_FAILURE);
    }
    memcpy64(cmd, CONF_STTY_ARGS, n + 1);

    token = strtok(cmd, " ");
    while (token != NULL) {
        if (exec_argc >= 255) {
            break;
        }
        exec_args[exec_argc] = token;
        exec_argc += 1;
        token = strtok(NULL, " ");
    }

    for (char **p = args; p && (s = *p); p += 1) {
        if (exec_argc >= 255) {
            break;
        }
        exec_args[exec_argc] = s;
        exec_argc += 1;
    }
    exec_args[exec_argc] = NULL;

    switch (pid2 = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        execvp(exec_args[0], exec_args);
        perror("Couldn't call stty");
        exit(EXIT_FAILURE);
    default:
        waitpid(pid2, NULL, 0);
        break;
    }

    return;
}

static void __attribute((noreturn))
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

    if ((shell = getenv("SHELL")) == NULL) {
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

static int32
tty_new(char *line, char *cmd, char *out, char **args) {
    int32 amaster;
    int32 aslave;

    if (out) {
        term.mode |= TERM_MODE_PRINT;
        io_fd = (!strcmp(out, "-")) ? 1 : open(out, O_WRONLY | O_CREAT, 0666);
        if (io_fd < 0) {
            error("Error opening %s:%s\n", out, strerror(errno));
        }
    }

    if (line) {
        if ((command_fd = open(line, O_RDWR)) < 0) {
            error("open line '%s' failed: %s\n", line, strerror(errno));
            exit(EXIT_FAILURE);
        }
        dup2(command_fd, 0);
        stty(args);
        return command_fd;
    }

    /* seems to work fine on linux, openbsd and freebsd */
    if (openpty(&amaster, &aslave, NULL, NULL, NULL) < 0) {
        error("openpty failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    switch (pid = fork()) {
    case -1:
        error("fork failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    case 0:
        close(io_fd);
        close(amaster);
        setsid();
        dup2(aslave, 0);
        dup2(aslave, 1);
        dup2(aslave, 2);
        if (ioctl(aslave, TIOCSCTTY, NULL) < 0) {
            error("ioctl TIOCSCTTY failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (aslave > 2) {
            close(aslave);
        }
#ifdef __OpenBSD__
        if (pledge("stdio getpw proc exec", NULL) == -1) {
            error("pledge\n");
            exit(EXIT_FAILURE);
        }
#endif
        exec_shell(cmd, args);
    default:
#ifdef __OpenBSD__
        if (pledge("stdio rpath tty proc exec", NULL) == -1) {
            error("pledge\n");
            exit(EXIT_FAILURE);
        }
#endif
        close(aslave);
        command_fd = amaster;
        signal(SIGCHLD, handler_sigchld);
        break;
    }
    return command_fd;
}

static int64
tty_read(void) {
    static char buffer[BUFSIZ];
    static int32 copied = 0;
    int64 ret;
    int32 written;

    /* append read bytes to unprocessed bytes */
    if (copied >= LENGTH(buffer)) {
        copied = 0;
    }
    ret = read64(command_fd, buffer + copied, LENGTH(buffer) - copied);

    switch (ret) {
    case 0:
        exit(0);
    case -1:
        error("couldn't read from CONF_SHELl: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    default: {
        copied += ret;
        written = term_write(buffer, copied, 0);
        copied -= written;
        /* keep any incomplete UTF-8 byte sequence for the next call */
        if (copied > 0) {
            memmove64(buffer, buffer + written, copied);
        }
        return ret;
    }
    }
}

static void
tty_write(char *s, int64 n, int32 may_echo) {
    user_scroll_down(&((union Arg){.i = term.scrolled_up}));

    if (may_echo && term_mode_is_set(TERM_MODE_ECHOO)) {
        term_write(s, (int32)n, 1);
    }

    if (!term_mode_is_set(TERM_MODE_CRLF)) {
        tty_write_raw(s, n);
        return;
    }

    /* This is similar to how the kernel handles ONLCR for ttys */
    while (n > 0) {
        char *next;
        if (*s == '\r') {
            next = s + 1;
            tty_write_raw("\r\n", 2);
        } else {
            next = memchr64(s, '\r', n);
            DEFAULT(next, s + n);
            tty_write_raw(s, (int64)(next - s));
        }
        n -= (int64)(next - s);
        s = next;
    }
    return;
}

static void
tty_write_raw(char *s, int64 n) {
    int64 lim = 256;

    /*
     * Remember that we are using a pty, which might be a modem line.
     * Writing too much will clog the line. That's why we are doing this dance.
     */
    while (n > 0) {
        fd_set write_fd;
        fd_set read_fd;
        FD_ZERO(&write_fd);
        FD_ZERO(&read_fd);
        FD_SET(command_fd, &write_fd);
        FD_SET(command_fd, &read_fd);

        /* Check if we can write. */
        if (pselect(command_fd + 1, &read_fd, &write_fd, NULL, NULL, NULL)
            < 0) {
            if (errno == EINTR) {
                continue;
            }
            error("select failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (FD_ISSET(command_fd, &write_fd)) {
            /*
             * Only write the bytes written by tty_write() or the
             * default of 256. This seems to be a reasonable value
             * for a serial line. Bigger values might clog the I/O.
             */
            int64 size = ((n < lim) ? n : lim);
            int64 r;
            if ((r = write64(command_fd, s, size)) < 0) {
                error("write error on tty: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            if (r < n) {
                /*
                 * We weren't able to write out everything.
                 * This means the buffer is getting full
                 * again. Empty it.
                 */
                if (n < lim) {
                    lim = tty_read();
                }
                n -= r;
                s += r;
            } else {
                /* All bytes have been written. */
                break;
            }
        }
        if (FD_ISSET(command_fd, &read_fd)) {
            lim = tty_read();
        }
    }
    return;
}

static void
tty_resize(int32 tty_width, int32 tty_height) {
    struct winsize winsize;

    winsize.ws_row = (uint16)term.nrows;
    winsize.ws_col = (uint16)term.ncols;
    winsize.ws_xpixel = (uint16)tty_width;
    winsize.ws_ypixel = (uint16)tty_height;
    if (ioctl(command_fd, TIOCSWINSZ, &winsize) < 0) {
        error("Couldn't set window size: %s\n", strerror(errno));
    }
    return;
}

static void
tty_hangup(void) {
    /* Send SIGHUP to CONF_SHELl */
    kill(pid, SIGHUP);
    return;
}

#if TESTING_tty

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "assert.c"
#include "st.c"
#include "x.c"
#include "user.c"

static void
mock_term_init(void) {
    term.nrows = 24;
    term.ncols = 80;
    term.dirts = xmalloc(term.nrows*SIZEOF(*term.dirts));
    for (int32 i = 0; i < term.nrows; i += 1) {
        term.dirts[i] = false;
    }

    term.lines = xmalloc(term.nrows*SIZEOF(*term.lines));
    for (int32 i = 0; i < term.nrows; i += 1) {
        term.lines[i] = xmalloc(term.ncols*SIZEOF(StGlyph));
    }

    term.scrolled_up = 0;
    term.mode = 0;
    selection.ob.x = -1;
    selection.alt = 0;
    return;
}

int
main(void) {
    {
        int32 master;
        int32 slave;

        if (openpty(&master, &slave, NULL, NULL, NULL) == 0) {
            command_fd = master;
            term.nrows = 24;
            term.ncols = 80;
            tty_resize(800, 600);
            close(master);
            close(slave);
        }
        ASSERT(true);
    }

    if (fork() == 0) {
        int32 master;
        int32 slave;
        char *args[] = {"-a", NULL};

        if (openpty(&master, &slave, NULL, NULL, NULL) == 0) {
            dup2(slave, 0);
            stty(args);
            close(master);
            close(slave);
        }
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        char *args[] = {"true", NULL};

        tty_new(NULL, "true", NULL, args);
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        int32 master;
        int32 slave;

        mock_term_init();
        if (openpty(&master, &slave, NULL, NULL, NULL) == 0) {
            command_fd = master;
            write(slave, "test", 4);
            tty_read();
            close(master);
            close(slave);
        }
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        int32 master;
        int32 slave;

        mock_term_init();
        if (openpty(&master, &slave, NULL, NULL, NULL) == 0) {
            command_fd = master;
            tty_write_raw("hello", 5);
            close(master);
            close(slave);
        }
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    if (fork() == 0) {
        int32 master;
        int32 slave;

        mock_term_init();
        if (openpty(&master, &slave, NULL, NULL, NULL) == 0) {
            command_fd = master;
            tty_write("hello\rworld", 11, 0);
            close(master);
            close(slave);
        }
        exit(EXIT_SUCCESS);
    }
    wait(NULL);

    {
        pid = fork();
        if (pid == 0) {
            sleep(1);
            exit(EXIT_SUCCESS);
        }
        tty_hangup();
        wait(NULL);
        ASSERT(true);
    }

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_tty */

#endif /* TTY_C */
