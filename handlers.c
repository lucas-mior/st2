#if !defined(HANDLERS_C)
#define HANDLERS_C

#include "st.h"

void
handler_sigchld(int32 unused) {
    int32 stat;
    pid_t p;
    (void)unused;

    if ((p = waitpid(pid, &stat, WNOHANG)) < 0) {
        die("waiting for pid %hd failed: %s\n", pid, strerror(errno));
    }

    if (pid != p) {
        if (p == 0 && wait(&stat) < 0) {
            die("wait: %s\n", strerror(errno));
        }

        /* reinstall handler_sigchld handler */
        signal(SIGCHLD, handler_sigchld);
        return;
    }

    if (WIFEXITED(stat) && WEXITSTATUS(stat)) {
        die("child exited with status %d\n", WEXITSTATUS(stat));
    } else if (WIFSIGNALED(stat)) {
        die("child terminated due to signal %d\n", WTERMSIG(stat));
    }
    _exit(0);
}

#endif
