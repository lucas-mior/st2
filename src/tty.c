#if !defined(TTY_C)
#define TTY_C

#include <pty.h>
#include "st.h"
#include "config.def.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_tty 1
#elif !defined(TESTING_tty)
#define TESTING_tty 0
#endif

static void
stty(char **args) {
    char cmd[_POSIX_ARG_MAX], *q, *s;
    int64 n;
    int64 siz;

    if ((n = strlen32(CONF_STTY_ARGS)) > SIZEOF(cmd) - 1) {
        error("incorrect stty parameters\n");
        exit(EXIT_FAILURE);
    }
    memcpy64(cmd, CONF_STTY_ARGS, n);
    q = cmd + n;
    siz = SIZEOF(cmd) - n;
    for (char **p = args; p && (s = *p); p += 1) {
        if ((n = strlen32(s)) > siz - 1) {
            error("stty parameter length too int64\n");
            exit(EXIT_FAILURE);
        }
        *q++ = ' ';
        memcpy64(q, s, n);
        q += n;
        siz -= n + 1;
    }
    *q = '\0';
    if (system(cmd) != 0) {
        perror("Couldn't call stty");
    }
    return;
}

static int32
tty_new(char *line, char *cmd, char *out, char **args) {
    int32 amaster;
    int32 aslave;

    if (out) {
        term.mode |= TERM_MODE_PRINT;
        io_fd = (!strcmp(out, "-")) ? 1 : open(out, O_WRONLY | O_CREAT, 0666);
        if (io_fd < 0) {
            fprintf(stderr, "Error opening %s:%s\n", out, strerror(errno));
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
    int32 ret;
    int32 written;

    /* append read bytes to unprocessed bytes */
    ret = (int32)read(command_fd, buffer + copied,
                      (size_t)(LENGTH(buffer) - copied));

    switch (ret) {
    case 0:
        exit(0);
    case -1:
        error("couldn't read from CONF_SHELl: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    default:
        copied += ret;
        written = term_write(buffer, copied, 0);
        copied -= written;
        /* keep any incomplete UTF-8 byte sequence for the next call */
        if (copied > 0) {
            memmove(buffer, buffer + written, (size_t)copied);
        }
        return (int64)ret;
    }
}

static void
tty_write(char *s, int64 n, int32 may_echo) {
    char *next;

    user_scroll_down(&((Arg){.i = term.lines_scrolled_up}));

    if (may_echo && TERM_MODE_IS_SET(TERM_MODE_ECHOO)) {
        term_write(s, (int32)n, 1);
    }

    if (!TERM_MODE_IS_SET(TERM_MODE_CRLF)) {
        tty_write_raw(s, n);
        return;
    }

    /* This is similar to how the kernel handles ONLCR for ttys */
    while (n > 0) {
        if (*s == '\r') {
            next = s + 1;
            tty_write_raw("\r\n", 2);
        } else {
            next = memchr(s, '\r', (size_t)n);
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
    fd_set write_fd;
    fd_set read_fd;
    int64 r;
    int64 lim = 256;

    /*
     * Remember that we are using a pty, which might be a modem line.
     * Writing too much will clog the line. That's why we are doing this dance.
     */
    while (n > 0) {
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
            size_t size = (size_t)((n < lim) ? n : lim);
            if ((r = write(command_fd, s, size)) < 0) {
                goto write_error;
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

write_error:
    error("write error on tty: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
}

static void
tty_resize(int32 tty_width, int32 tty_height) {
    struct winsize winsize;

    winsize.ws_row = (uint16)term.nrows;
    winsize.ws_col = (uint16)term.ncols;
    winsize.ws_xpixel = (uint16)tty_width;
    winsize.ws_ypixel = (uint16)tty_height;
    if (ioctl(command_fd, TIOCSWINSZ, &winsize) < 0) {
        fprintf(stderr, "Couldn't set window size: %s\n", strerror(errno));
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

#include "assert.c"

int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_tty */

#endif /* TTY_C */
