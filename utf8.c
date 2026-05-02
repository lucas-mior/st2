int64
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

uint32
utf8_decode_byte(char c, int64 *i) {
    for (*i = 0; *i < LENGTH(utf8_mask); ++(*i)) {
        if (((uchar)c & utf8_mask[*i]) == utf8_byte[*i]) {
            return (uchar)c & ~utf8_mask[*i];
        }
    }

    return 0;
}

int64
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

char
utf8_encode_byte(uint32 u, int64 i) {
    return (char)(utf8_byte[i] | (u & (uint32)~utf8_mask[i]));
}

int64
utf8_validate(uint32 *u, int64 i) {
    if (!BETWEEN(*u, utf8_min[i], utf8_max[i]) || BETWEEN(*u, 0xD800, 0xDFFF)) {
        *u = UTF_INVALID;
    }
    for (i = 1; *u > utf8_max[i]; i += 1)
        ;

    return i;
}
