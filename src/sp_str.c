#include "sp_internal.h"

static const char *const sp_err_tab[] = {
    "ok",
    "out of memory",
    "invalid utf-8",
    "size overflow",
    "invalid argument",
    "embedded nul",
    "path traversal",
    "absolute path not allowed",
    "range"
};

const char *sp_err_str(sp_err e) {
    unsigned u = (unsigned)e;
    if (u >= (unsigned)(sizeof(sp_err_tab) / sizeof(sp_err_tab[0]))) return "unknown";
    return sp_err_tab[u];
}

int sp_ok(sp_str s) {
    return s.err == SP_OK;
}

sp_view sp_view_make(const char *ptr, size_t len) {
    sp_view v;
    if (!ptr) {
        v.ptr = "";
        v.len = 0;
        return v;
    }
    v.ptr = ptr;
    v.len = len;
    return v;
}

sp_view sp_view_cstr(const char *s) {
    if (!s) return sp_view_make("", 0);
    return sp_view_make(s, strlen(s));
}

int sp_view_is_empty(sp_view a) {
    return a.len == 0;
}

int sp_view_eq(sp_view a, sp_view b) {
    if (a.len != b.len) return 0;
    if (a.len == 0) return 1;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

int sp_view_eq_cstr(sp_view a, const char *s) {
    return sp_view_eq(a, sp_view_cstr(s));
}

int sp_view_starts_with(sp_view a, sp_view pre) {
    if (pre.len > a.len) return 0;
    if (pre.len == 0) return 1;
    return memcmp(a.ptr, pre.ptr, pre.len) == 0;
}

int sp_view_ends_with(sp_view a, sp_view suf) {
    if (suf.len > a.len) return 0;
    if (suf.len == 0) return 1;
    return memcmp(a.ptr + (a.len - suf.len), suf.ptr, suf.len) == 0;
}

int sp_view_contains_byte(sp_view a, unsigned char b) {
    return a.len && memchr(a.ptr, b, a.len) != NULL;
}

sp_str sp_str_empty(void) {
    return sp__owned((char *)"", 0);
}

sp_str sp_str_error(sp_err e) {
    sp_str s = sp_str_empty();
    s.err = e == SP_OK ? SP_ERR_INVALID : e;
    return s;
}

sp_view sp_str_view(sp_str s) {
    if (s.err != SP_OK) return sp_view_make("", 0);
    return sp_view_make(s.ptr, s.len);
}

int sp_str_eq(sp_str a, sp_str b) {
    if (a.err != b.err) return 0;
    if (a.err != SP_OK) return 1;
    return sp_view_eq(sp_str_view(a), sp_str_view(b));
}

sp_str sp_str_from_n(sp_arena *a, const char *s, size_t n) {
    char *p;
    if (!a) return sp_str_error(SP_ERR_INVALID);
    if (!s && n) return sp_str_error(SP_ERR_INVALID);
    p = sp__arena_strdup(a, s ? s : "", n);
    return sp__owned(p, n);
}

sp_str sp_str_from(sp_arena *a, const char *s) {
    if (!s) return sp_str_from_n(a, "", 0);
    return sp_str_from_n(a, s, strlen(s));
}

sp_str sp_str_from_view(sp_arena *a, sp_view v) {
    return sp_str_from_n(a, v.ptr, v.len);
}

sp_str sp_str_dup_malloc(sp_view v) {
    char *p;
    size_t total;
    if (!sp_add_ok(v.len, 1, &total)) return sp_str_error(SP_ERR_OVERFLOW);
    p = (char *)malloc(total);
    if (!p) return sp_str_error(SP_ERR_OOM);
    if (v.len && v.ptr) memcpy(p, v.ptr, v.len);
    p[v.len] = '\0';
    return sp__owned(p, v.len);
}

sp_str sp_concat(sp_arena *a, sp_view x, sp_view y) {
    size_t n;
    char *p;
    if (!a) return sp_str_error(SP_ERR_INVALID);
    if ((x.len && !x.ptr) || (y.len && !y.ptr)) return sp_str_error(SP_ERR_INVALID);
    if (!sp_add_ok(x.len, y.len, &n)) return sp_str_error(SP_ERR_OVERFLOW);
    p = sp__arena_strdup(a, NULL, n);
    if (!p) return sp_str_error(SP_ERR_OOM);
    if (x.len) memcpy(p, x.ptr, x.len);
    if (y.len) memcpy(p + x.len, y.ptr, y.len);
    p[n] = '\0';
    return sp__owned(p, n);
}

sp_str sp_join_views(sp_arena *a, const sp_view *parts, size_t n, sp_view sep) {
    size_t i, total = 0, seps;
    char *p, *w;
    if (!a) return sp_str_error(SP_ERR_INVALID);
    if (n && !parts) return sp_str_error(SP_ERR_INVALID);
    if (n == 0) return sp_str_from_n(a, "", 0);
    seps = n - 1u;
    for (i = 0; i < n; i++) {
        if (!sp_add_ok(total, parts[i].len, &total)) return sp_str_error(SP_ERR_OVERFLOW);
    }
    if (sep.len) {
        size_t extra;
        if (!sp_mul_ok(sep.len, seps, &extra) || !sp_add_ok(total, extra, &total))
            return sp_str_error(SP_ERR_OVERFLOW);
    }
    p = sp__arena_strdup(a, NULL, total);
    if (!p) return sp_str_error(SP_ERR_OOM);
    w = p;
    for (i = 0; i < n; i++) {
        if (i && sep.len) {
            memcpy(w, sep.ptr, sep.len);
            w += sep.len;
        }
        if (parts[i].len) {
            memcpy(w, parts[i].ptr, parts[i].len);
            w += parts[i].len;
        }
    }
    *w = '\0';
    return sp__owned(p, total);
}

sp_str sp_vsprintf(sp_arena *a, const char *fmt, va_list ap) {
    va_list aq;
    int n;
    char *p;
    if (!a || !fmt) return sp_str_error(SP_ERR_INVALID);
    va_copy(aq, ap);
    n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0) return sp_str_error(SP_ERR_INVALID);
    p = sp__arena_strdup(a, NULL, (size_t)n);
    if (!p) return sp_str_error(SP_ERR_OOM);
    vsnprintf(p, (size_t)n + 1u, fmt, ap);
    return sp__owned(p, (size_t)n);
}

sp_str sp_sprintf(sp_arena *a, const char *fmt, ...) {
    va_list ap;
    sp_str s;
    va_start(ap, fmt);
    s = sp_vsprintf(a, fmt, ap);
    va_end(ap);
    return s;
}

void sp_buf_init(sp_buf *b, sp_arena *a) {
    if (!b) return;
    b->arena = a;
    b->ptr = NULL;
    b->len = 0;
    b->cap = 0;
    b->err = a ? SP_OK : SP_ERR_INVALID;
}

void sp_buf_clear(sp_buf *b) {
    if (!b) return;
    b->len = 0;
    if (b->ptr) b->ptr[0] = '\0';
}

static int sp_buf_reserve(sp_buf *b, size_t extra) {
    size_t need, ncap;
    char *np;
    if (!b || b->err != SP_OK) return 0;
    if (!sp_add_ok(b->len, extra, &need)) {
        b->err = SP_ERR_OVERFLOW;
        return 0;
    }
    if (!sp_add_ok(need, 1, &need)) {
        b->err = SP_ERR_OVERFLOW;
        return 0;
    }
    if (need <= b->cap) return 1;
    ncap = b->cap ? b->cap : 64u;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2u) {
            ncap = need;
            break;
        }
        ncap *= 2u;
    }
    np = (char *)sp_arena_alloc(b->arena, ncap, 1);
    if (!np) {
        b->err = SP_ERR_OOM;
        return 0;
    }
    if (b->len && b->ptr) memcpy(np, b->ptr, b->len);
    b->ptr = np;
    b->cap = ncap;
    return 1;
}

void sp_buf_append(sp_buf *b, sp_view v) {
    if (!b || b->err != SP_OK) return;
    if (v.len && !v.ptr) {
        b->err = SP_ERR_INVALID;
        return;
    }
    if (!sp_buf_reserve(b, v.len)) return;
    if (v.len) memcpy(b->ptr + b->len, v.ptr, v.len);
    b->len += v.len;
    b->ptr[b->len] = '\0';
}

void sp_buf_append_cstr(sp_buf *b, const char *s) {
    sp_buf_append(b, sp_view_cstr(s));
}

void sp_buf_append_char(sp_buf *b, char c) {
    sp_view v;
    v.ptr = &c;
    v.len = 1;
    sp_buf_append(b, v);
}

void sp_buf_printf(sp_buf *b, const char *fmt, ...) {
    va_list ap, aq;
    int n;
    if (!b || b->err != SP_OK || !fmt) {
        if (b && b->err == SP_OK) b->err = SP_ERR_INVALID;
        return;
    }
    va_start(ap, fmt);
    va_copy(aq, ap);
    n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0) {
        b->err = SP_ERR_INVALID;
        va_end(ap);
        return;
    }
    if (!sp_buf_reserve(b, (size_t)n)) {
        va_end(ap);
        return;
    }
    vsnprintf(b->ptr + b->len, (size_t)n + 1u, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

sp_str sp_buf_finish(sp_buf *b) {
    if (!b) return sp_str_error(SP_ERR_INVALID);
    if (b->err != SP_OK) return sp_str_error(b->err);
    if (!b->ptr) return sp_str_from_n(b->arena, "", 0);
    b->ptr[b->len] = '\0';
    return sp__owned(b->ptr, b->len);
}
