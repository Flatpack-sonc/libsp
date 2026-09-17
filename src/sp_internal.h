#ifndef SP_INTERNAL_H
#define SP_INTERNAL_H

#include "sp.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#define SP_UNUSED(x) (void)(x)

static inline int sp_add_ok(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) return 0;
    *out = a + b;
    return 1;
}

static inline int sp_mul_ok(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) return 0;
    *out = a * b;
    return 1;
}

static inline size_t sp_align_up(size_t x, size_t a) {
    size_t m;
    if (a <= 1) return x;
    m = a - 1u;
    if (x > SIZE_MAX - m) return SIZE_MAX;
    return (x + m) & ~m;
}

static inline int sp_ascii_eq_ci(char a, char b) {
    unsigned char xa = (unsigned char)a;
    unsigned char xb = (unsigned char)b;
    if (xa >= 'A' && xa <= 'Z') xa = (unsigned char)(xa - 'A' + 'a');
    if (xb >= 'A' && xb <= 'Z') xb = (unsigned char)(xb - 'A' + 'a');
    return xa == xb;
}

sp_str sp__owned(char *p, size_t n);
char *sp__arena_strdup(sp_arena *a, const char *s, size_t n);

#endif
