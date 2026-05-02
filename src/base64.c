#if !defined(BASE64_C)
#define BASE64_C

#include <ctype.h>
#include "st.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_base64 1
#elif !defined(TESTING_base64)
#define TESTING_base64 0
#endif

static char
base64_decode_getc(char **src) {
    while (**src && !isprint((uchar)**src)) {
        (*src)++;
    }
    /* emulate padding if string ends */
    if (**src) {
        return *((*src)++);
    } else {
        return '=';
    }
}

static char *
base64_decode(char *src) {
    int64 in_len = (int64)strlen32(src);
    char *result;
    char *dst;
    static char base64_digits[256] = {
        [43] = 62, 0,  0,  0,  63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0,
        0,         0,  -1, 0,  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
        10,        11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        0,         0,  0,  0,  0,  0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
        36,        37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51};

    if (in_len % 4) {
        in_len += 4 - (in_len % 4);
    }
    result = xmalloc(in_len / 4*3 + 1);
    dst = result;
    while (*src) {
        int32 a = base64_digits[(uchar)base64_decode_getc(&src)];
        int32 b = base64_digits[(uchar)base64_decode_getc(&src)];
        int32 c = base64_digits[(uchar)base64_decode_getc(&src)];
        int32 d = base64_digits[(uchar)base64_decode_getc(&src)];

        /* invalid input. 'a' can be -1, e.g. if src is "\n" (c-string) */
        if (a == -1 || b == -1) {
            break;
        }

        *dst = (char)((a << 2) | ((b & 0x30) >> 4));
        dst += 1;
        if (c == -1) {
            break;
        }
        *dst = (char)(((b & 0x0f) << 4) | ((c & 0x3c) >> 2));
        dst += 1;
        if (d == -1) {
            break;
        }
        *dst = (char)(((c & 0x03) << 6) | d);
        dst += 1;
    }
    *dst = '\0';
    return result;
}

#if TESTING_base64

#include <stdbool.h>
#include <stdlib.h>

#include "assert.c"

int
main(void) {
	ASSERT(true);
	exit(EXIT_SUCCESS);
}

#endif /* TESTING_base64 */

#endif /* BASE64_C */
