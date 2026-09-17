#include "sp_internal.h"

/*
 * Strict UTF-8: no overlongs, no surrogates, no > U+10FFFF, no 0xFF/0xFE.
 */

int sp_utf8_encode(char out[4], uint32_t cp) {
    if (!out) return -1;
    if (cp <= 0x7Fu) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FFu) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp >= 0xD800u && cp <= 0xDFFFu) return -1;
    if (cp <= 0xFFFFu) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    if (cp <= 0x10FFFFu) {
        out[0] = (char)(0xF0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        return 4;
    }
    return -1;
}

int sp_utf8_next(sp_view s, size_t *off, uint32_t *cp) {
    const unsigned char *p;
    size_t i, n, need;
    uint32_t c, minv;

    if (!off || !cp) return 0;
    i = *off;
    if (i > s.len) return 0;
    if (i == s.len) return 0;
    if (!s.ptr) return 0;

    p = (const unsigned char *)s.ptr;
    n = s.len;
    c = p[i];

    if (c <= 0x7Fu) {
        *cp = c;
        *off = i + 1u;
        return 1;
    }
    if (c < 0xC2u || c > 0xF4u) return 0;

    if (c < 0xE0u) {
        need = 2;
        minv = 0x80u;
        c &= 0x1Fu;
    } else if (c < 0xF0u) {
        need = 3;
        minv = 0x800u;
        c &= 0x0Fu;
    } else {
        need = 4;
        minv = 0x10000u;
        c &= 0x07u;
    }

    if (i + need > n) return 0;
    {
        size_t k;
        for (k = 1; k < need; k++) {
            unsigned char t = p[i + k];
            if ((t & 0xC0u) != 0x80u) return 0;
            c = (c << 6) | (uint32_t)(t & 0x3Fu);
        }
    }
    if (c < minv) return 0;
    if (c >= 0xD800u && c <= 0xDFFFu) return 0;
    if (c > 0x10FFFFu) return 0;
    /* UTF-8 forbids 0xFF/0xFE already via lead-byte range. */
    *cp = c;
    *off = i + need;
    return 1;
}

int sp_utf8_valid(sp_view s) {
    size_t i = 0;
    uint32_t cp;
    if (s.len && !s.ptr) return 0;
    while (i < s.len) {
        if (!sp_utf8_next(s, &i, &cp)) return 0;
    }
    return 1;
}

size_t sp_utf8_count(sp_view s) {
    size_t i = 0, n = 0;
    uint32_t cp;
    if (s.len && !s.ptr) return (size_t)-1;
    while (i < s.len) {
        if (!sp_utf8_next(s, &i, &cp)) return (size_t)-1;
        n++;
    }
    return n;
}

sp_err sp_utf8_to_utf16(sp_arena *a, sp_view utf8, uint16_t **out, size_t *nunits) {
    size_t i = 0, cap = 0, w = 0;
    uint32_t cp;
    uint16_t *buf;

    if (!a || !out || !nunits) return SP_ERR_INVALID;
    *out = NULL;
    *nunits = 0;
    if (!sp_utf8_valid(utf8)) return SP_ERR_UTF8;

    /* worst case: 1 code point → 2 units */
    if (!sp_add_ok(utf8.len, 1, &cap)) return SP_ERR_OVERFLOW;
    if (!sp_mul_ok(cap, sizeof(uint16_t), &cap)) return SP_ERR_OVERFLOW;
    buf = (uint16_t *)sp_arena_alloc(a, cap, sizeof(uint16_t));
    if (!buf) return SP_ERR_OOM;

    i = 0;
    while (i < utf8.len) {
        if (!sp_utf8_next(utf8, &i, &cp)) return SP_ERR_UTF8;
        if (cp <= 0xFFFFu) {
            buf[w++] = (uint16_t)cp;
        } else {
            cp -= 0x10000u;
            buf[w++] = (uint16_t)(0xD800u + (cp >> 10));
            buf[w++] = (uint16_t)(0xDC00u + (cp & 0x3FFu));
        }
    }
    buf[w] = 0;
    *out = buf;
    *nunits = w;
    return SP_OK;
}

sp_str sp_utf16_to_utf8(sp_arena *a, const uint16_t *p, size_t nunits) {
    sp_buf b;
    size_t i;
    if (!a) return sp_str_error(SP_ERR_INVALID);
    if (nunits && !p) return sp_str_error(SP_ERR_INVALID);
    sp_buf_init(&b, a);

    for (i = 0; i < nunits; i++) {
        uint32_t cp = p[i];
        char tmp[4];
        int n;
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            uint32_t lo;
            if (i + 1u >= nunits) return sp_str_error(SP_ERR_UTF8);
            lo = p[i + 1u];
            if (lo < 0xDC00u || lo > 0xDFFFu) return sp_str_error(SP_ERR_UTF8);
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            i++;
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return sp_str_error(SP_ERR_UTF8);
        }
        n = sp_utf8_encode(tmp, cp);
        if (n < 0) return sp_str_error(SP_ERR_UTF8);
        sp_buf_append(&b, sp_view_make(tmp, (size_t)n));
    }
    return sp_buf_finish(&b);
}
