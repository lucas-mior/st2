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
    while (**src && (!isprint((uchar)**src) || isspace((uchar)**src))) {
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
base64_decode(char *src, int32 *allocated_length) {
    int64 in_len = strlen32(src);
    int64 alloc_size;
    char *result;
    char *dst;
    static char base64_digits[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };

    if (in_len % 4) {
        in_len += 4 - (in_len % 4);
    }
    alloc_size = in_len / 4 * 3 + 1;
    result = malloc2(alloc_size);
    if (allocated_length) {
        *allocated_length = (int32)alloc_size;
    }
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
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "st.c"
#include "user.c"

int
main(void) {
    {
        char *src = "A";
        char c1;
        char c2;
        char c3;

        c1 = base64_decode_getc(&src);
        ASSERT_EQUAL(c1, 'A');
        
        c2 = base64_decode_getc(&src);
        ASSERT_EQUAL(c2, '=');
        
        c3 = base64_decode_getc(&src);
        ASSERT_EQUAL(c3, '=');
    }

    {
        char *decoded;
        int32 len;

        decoded = base64_decode("", &len);
        ASSERT_EQUAL(decoded, "");
        free2(decoded, len);

        decoded = base64_decode("SGVsbG8=", &len);
        ASSERT_EQUAL(decoded, "Hello");
        free2(decoded, len);

        decoded = base64_decode("YW55IGNhcm5hbCBwbGVhc3VyZS4=", &len);
        ASSERT_EQUAL(decoded, "any carnal pleasure.");
        free2(decoded, len);

        decoded = base64_decode(" \n\r", &len);
        ASSERT_EQUAL(decoded, "");
        free2(decoded, len);

        /* Expose the space bug: space evaluates to 0 ('A') instead of being skipped. 
         * "SGVsb G8=" evaluates improperly instead of skipping the space to decode "Hello". */
        decoded = base64_decode("SGVsb G8=", &len);
        ASSERT_EQUAL(decoded, "Hello");
        free2(decoded, len);

        /* Expose the unmapped character bug: '!' evaluates to 0 ('A') instead of -1. 
         * The presence of an invalid character should abort the decode (returning ""), 
         * but instead it silently corrupts the data. */
        decoded = base64_decode("S!VsbG8=", &len);
        ASSERT_EQUAL(decoded, "");
        free2(decoded, len);
    }

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_base64 */

#endif /* BASE64_C */
