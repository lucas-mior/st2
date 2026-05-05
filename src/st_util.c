#include "util.c"

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
timediff(struct timespec t1, struct timespec t2) {
    double diff;
    diff = ((double)(t1.tv_sec - t2.tv_sec)*1000 + (double)(t1.tv_nsec - t2.tv_nsec)/1E6);
    return diff;
}

