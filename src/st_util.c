#if !defined(ST_UTIL_C)
#define ST_UTIL_C

#include "cbase.h"
#include "util.c"
#include "st.h"
#include "arg.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_st_util 1
#elif !defined(TESTING_st_util)
#define TESTING_st_util 0
#endif

static int64
xwrite(int32 fd, char *s, int64 len) {
    int64 r;
    int64 left = len;

    while (left > 0) {
        r = write64(fd, s, len);
        if (r < 0) {
            return r;
        }
        left -= r;
        s += r;
    }

    return len;
}

static double
timediff_ms(struct timespec t1, struct timespec t2) {
    double diff;
    diff = ((double)(t1.tv_sec - t2.tv_sec)*1000 + (double)(t1.tv_nsec - t2.tv_nsec)/1E6);
    return diff;
}

static noreturn void
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

#if 0 == TESTING_st_util
static inline void
st_util_functions_sink(void) {
    (void)st_util_functions_sink;
    (void)xwrite;
    (void)timediff_ms;
    (void)usage;
    return;
}
#endif
#if TESTING_st_util
#define CBASE_IMPLEMENT
#include "cbase.h"

int
main(void) {
	ASSERT(true);
	exit(EXIT_SUCCESS);
}

#endif /* TESTING_st_util */

#endif /* ST_UTIL_C */
