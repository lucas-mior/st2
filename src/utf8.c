#if !defined(UTF8_C)
#define UTF8_C

#include "st.h"
#include "util.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_utf8 1
#elif !defined(TESTING_utf8)
#define TESTING_utf8 0
#endif

static uchar utf8_byte[UTF_SIZ + 1] = {0x80, 0, 0xC0, 0xE0, 0xF0};
static uchar utf8_mask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static uint32 utf8_min[UTF_SIZ + 1] = {0, 0, 0x80, 0x800, 0x10000};
static uint32 utf8_max[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

static uint32
utf8_decode_byte(char c, int64 *i) {
    for (*i = 0; *i < LENGTH(utf8_mask); ++(*i)) {
        if (((uchar)c & utf8_mask[*i]) == utf8_byte[*i]) {
            return (uchar)c & ~utf8_mask[*i];
        }
    }

    return 0;
}

static char
utf8_encode_byte(uint32 u, int64 i) {
    return (char)(utf8_byte[i] | (u & (uint32)~utf8_mask[i]));
}

static int64
utf8_validate(uint32 *u, int64 i) {
    if (!BETWEEN(*u, utf8_min[i], utf8_max[i]) || BETWEEN(*u, 0xD800, 0xDFFF)) {
        *u = UTF_INVALID;
    }
    for (i = 1; *u > utf8_max[i]; i += 1)
        ;

    return i;
}

static int64
utf8_decode(char *c, uint32 *u, int64 clen) {
    int64 len;
    int64 type;
    uint32 rune_decoded;

    *u = UTF_INVALID;
    if (!clen) {
        return 0;
    }
    rune_decoded = utf8_decode_byte(c[0], &len);
    if (!BETWEEN(len, 1, UTF_SIZ)) {
        return 1;
    }
    {
        int64 j = 1;
        for (int64 i = 1; i < clen && j < len; i += 1, j += 1) {
            rune_decoded = (rune_decoded << 6) | utf8_decode_byte(c[i], &type);
            if (type != 0) {
                return j;
            }
        }
        if (j < len) {
            return 0;
        }
    }
    *u = rune_decoded;
    utf8_validate(u, len);

    return len;
}

static int64
utf8_encode(uint32 u, char *c) {
    int64 len;

    len = utf8_validate(&u, 0);
    if (len > UTF_SIZ) {
        return 0;
    }

    for (int64 i = len - 1; i != 0; --i) {
        c[i] = utf8_encode_byte(u, 0);
        u >>= 6;
    }
    c[0] = utf8_encode_byte(u, len);

    return len;
}

#if TESTING_utf8

#include <stdbool.h>
#include <stdlib.h>

#include "assert.c"

int
main(void) {
    {
        char buf[UTF_SIZ + 1];
        uint32 u;
        int64 len;

        /* Test 1-byte ASCII */
        len = utf8_encode(0x41, buf);
        ASSERT_EQUAL(len, 1);
        ASSERT_EQUAL(buf[0], 'A');
        len = utf8_decode(buf, &u, 1);
        ASSERT_EQUAL(len, 1);
        ASSERT_EQUAL(u, 0x41);

        /* Test 2-byte (e.g. U+00F1 n with tilde) */
        len = utf8_encode(0xF1, buf);
        ASSERT_EQUAL(len, 2);
        len = utf8_decode(buf, &u, 2);
        ASSERT_EQUAL(len, 2);
        ASSERT_EQUAL(u, 0xF1);

        /* Test 3-byte (e.g. U+20AC Euro sign) */
        len = utf8_encode(0x20AC, buf);
        ASSERT_EQUAL(len, 3);
        len = utf8_decode(buf, &u, 3);
        ASSERT_EQUAL(len, 3);
        ASSERT_EQUAL(u, 0x20AC);

        /* Test 4-byte */
        len = utf8_encode(0x10348, buf);
        ASSERT_EQUAL(len, 4);
        len = utf8_decode(buf, &u, 4);
        ASSERT_EQUAL(len, 4);
        ASSERT_EQUAL(u, 0x10348);

        /* Empty string decode */
        len = utf8_decode(buf, &u, 0);
        ASSERT_EQUAL(len, 0);
        ASSERT_EQUAL(u, UTF_INVALID);
    }

    {
        int64 idx;
        uint32 decoded;
        
        decoded = utf8_decode_byte('A', &idx);
        ASSERT_EQUAL(idx, 1); 
        ASSERT_EQUAL(decoded, 0x41);
    }

    {
        char encoded;
        
        encoded = utf8_encode_byte(0x41, 1);
        ASSERT_EQUAL(encoded, 'A');
    }

    {
        uint32 u;
        int64 len;

        /* Valid character */
        u = 0x41;
        len = utf8_validate(&u, 0);
        ASSERT_EQUAL(len, 1);
        ASSERT_EQUAL(u, 0x41);

        /* Invalid surrogate half */
        u = 0xD800;
        len = utf8_validate(&u, 3);
        ASSERT_EQUAL(u, UTF_INVALID);
    }

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_utf8 */

#endif /* UTF8_C */
