#include "sp_internal.h"

struct sp_block {
    struct sp_block *next;
    size_t cap;
    size_t pos;
};

struct sp_arena {
    struct sp_block *head;
    struct sp_block *tail;
    size_t first_cap;
    unsigned flags;
    int oom;
    size_t used;
};

static struct sp_block *sp__block_new(size_t cap) {
    size_t bytes;
    struct sp_block *b;
    if (cap < 64u) cap = 64u;
    if (!sp_add_ok(sizeof(struct sp_block), cap, &bytes)) return NULL;
    b = (struct sp_block *)malloc(bytes);
    if (!b) return NULL;
    b->next = NULL;
    b->cap = cap;
    b->pos = 0;
    return b;
}

sp_arena *sp_arena_create(size_t first_block, unsigned flags) {
    sp_arena *a = (sp_arena *)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->first_cap = first_block ? first_block : 4096u;
    a->flags = flags;
    a->head = a->tail = sp__block_new(a->first_cap);
    if (!a->head) {
        free(a);
        return NULL;
    }
    return a;
}

void sp_arena_destroy(sp_arena *a) {
    struct sp_block *b, *n;
    if (!a) return;
    for (b = a->head; b; b = n) {
        n = b->next;
        free(b);
    }
    free(a);
}

void sp_arena_reset(sp_arena *a) {
    struct sp_block *b, *n;
    if (!a) return;
    if (!a->head) return;
    for (b = a->head->next; b; b = n) {
        n = b->next;
        free(b);
    }
    a->head->next = NULL;
    a->head->pos = 0;
    a->tail = a->head;
    a->oom = 0;
    a->used = 0;
}

static void sp__oom(sp_arena *a) {
    if (a) a->oom = 1;
}

void *sp_arena_alloc(sp_arena *a, size_t size, size_t align) {
    struct sp_block *b;
    size_t pos, need;
    char *base;

    if (!a || a->oom) return NULL;
    if (align == 0) align = 1;
    /* power-of-two aligns only */
    if ((align & (align - 1u)) != 0) align = 16;

    if (size == 0) size = 1;

    b = a->tail;
    pos = sp_align_up(b->pos, align);
    if (pos == SIZE_MAX || !sp_add_ok(pos, size, &need)) {
        sp__oom(a);
        return NULL;
    }

    if (need > b->cap) {
        size_t cap;
        if (a->flags & SP_ARENA_FIXED) {
            sp__oom(a);
            return NULL;
        }
        cap = b->cap;
        if (cap > SIZE_MAX / 2u) cap = SIZE_MAX;
        else cap *= 2u;
        if (cap < need) cap = need;
        if (cap < a->first_cap) cap = a->first_cap;
        b = sp__block_new(cap);
        if (!b) {
            sp__oom(a);
            return NULL;
        }
        a->tail->next = b;
        a->tail = b;
        pos = sp_align_up(0, align);
        if (pos == SIZE_MAX || !sp_add_ok(pos, size, &need) || need > b->cap) {
            sp__oom(a);
            return NULL;
        }
    }

    base = (char *)(b + 1);
    b->pos = need;
    if (size > SIZE_MAX - a->used) a->used = SIZE_MAX;
    else a->used += size;
    return base + pos;
}

sp_mark sp_arena_mark(const sp_arena *a) {
    sp_mark m;
    memset(&m, 0, sizeof(m));
    if (!a || !a->tail) return m;
    m.block = a->tail;
    m.pos = a->tail->pos;
    m.used = a->used;
    m.oom = a->oom;
    return m;
}

void sp_arena_rewind(sp_arena *a, sp_mark m) {
    struct sp_block *b, *n, *keep;
    if (!a || !m.block) return;
    keep = (struct sp_block *)m.block;
    for (b = a->head; b && b != keep; b = b->next) {
    }
    if (b != keep) return;
    for (b = keep->next; b; b = n) {
        n = b->next;
        free(b);
    }
    keep->next = NULL;
    keep->pos = m.pos;
    a->tail = keep;
    a->used = m.used;
    a->oom = m.oom;
}

const char *sp_version(void) {
    return SP_VERSION;
}

int sp_arena_oom(const sp_arena *a) {
    return a ? a->oom : 1;
}

size_t sp_arena_used(const sp_arena *a) {
    return a ? a->used : 0;
}

char *sp__arena_strdup(sp_arena *a, const char *s, size_t n) {
    char *p;
    size_t total;
    if (!sp_add_ok(n, 1, &total)) {
        sp__oom(a);
        return NULL;
    }
    p = (char *)sp_arena_alloc(a, total, 1);
    if (!p) return NULL;
    if (n && s) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

sp_str sp__owned(char *p, size_t n) {
    sp_str s;
    s.ptr = p;
    s.len = n;
    s.err = p ? SP_OK : SP_ERR_OOM;
    if (!p) {
        s.ptr = (char *)"";
        s.len = 0;
    }
    return s;
}

sp_path_style sp_path_native(void) {
#ifdef _WIN32
    return SP_WINDOWS;
#else
    return SP_POSIX;
#endif
}
