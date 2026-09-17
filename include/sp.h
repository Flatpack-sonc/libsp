#ifndef SP_H
#define SP_H

/*
 * libsp — UTF-8 arena strings and lexical paths.
 *
 * Contract:
 *   - Public text is UTF-8. Successful owned strings are NUL-terminated.
 *   - Arena pointers stay valid until reset, destroy, or rewind past them.
 *   - Arenas are not thread-safe. Immutable views may be shared.
 *   - Path APIs are lexical: no syscalls, no symlink resolution.
 *   - Embedded NUL in a path is always an error (OS truncation).
 *   - Size arithmetic never wraps; overflow is SP_ERR_OVERFLOW.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SP_VERSION_MAJOR 1
#define SP_VERSION_MINOR 0
#define SP_VERSION_PATCH 0
#define SP_VERSION "1.0.0"

#if defined(_WIN32) && defined(SP_SHARED)
#if defined(SP_BUILD)
#define SP_API __declspec(dllexport)
#else
#define SP_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define SP_API __attribute__((visibility("default")))
#else
#define SP_API
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SP_PRINTF(f, a) __attribute__((format(printf, f, a)))
#define SP_NODISCARD __attribute__((warn_unused_result))
#else
#define SP_PRINTF(f, a)
#define SP_NODISCARD
#endif

typedef enum sp_err {
    SP_OK = 0,
    SP_ERR_OOM = 1,
    SP_ERR_UTF8 = 2,
    SP_ERR_OVERFLOW = 3,
    SP_ERR_INVALID = 4,
    SP_ERR_NUL = 5,
    SP_ERR_TRAVERSAL = 6,
    SP_ERR_ABSOLUTE = 7,
    SP_ERR_RANGE = 8
} sp_err;

typedef enum sp_path_style {
    SP_POSIX = 0,
    SP_WINDOWS = 1
} sp_path_style;

typedef struct sp_view {
    const char *ptr;
    size_t len;
} sp_view;

typedef struct sp_str {
    char *ptr;
    size_t len;
    sp_err err;
} sp_str;

typedef struct sp_arena sp_arena;

typedef struct sp_mark {
    void *block;
    size_t pos;
    size_t used;
    int oom;
} sp_mark;

typedef struct sp_buf {
    sp_arena *arena;
    char *ptr;
    size_t len;
    size_t cap;
    sp_err err;
} sp_buf;

typedef struct sp_path_info {
    sp_view volume;
    sp_view rest;
    int rooted;
    int unc;
    int drive_rel;
} sp_path_info;

enum {
    SP_ARENA_GROW = 0,
    SP_ARENA_FIXED = 1u << 0
};

enum {
    SP_SEC_UTF8 = 1u << 0,
    SP_SEC_CTRL = 1u << 1,
    SP_SEC_DEFAULT = SP_SEC_UTF8
};

#define SP_LIT(s) sp_view_make((s), (sizeof(s) - 1u))

SP_API const char *sp_version(void);
SP_API const char *sp_err_str(sp_err e);
SP_API int sp_ok(sp_str s);

SP_API sp_view sp_view_make(const char *ptr, size_t len);
SP_API sp_view sp_view_cstr(const char *s);
SP_API int sp_view_eq(sp_view a, sp_view b);
SP_API int sp_view_eq_cstr(sp_view a, const char *s);
SP_API int sp_view_starts_with(sp_view a, sp_view pre);
SP_API int sp_view_ends_with(sp_view a, sp_view suf);
SP_API int sp_view_contains_byte(sp_view a, unsigned char b);
SP_API int sp_view_is_empty(sp_view a);

SP_API SP_NODISCARD sp_arena *sp_arena_create(size_t first_block, unsigned flags);
SP_API void sp_arena_destroy(sp_arena *a);
SP_API void sp_arena_reset(sp_arena *a);
SP_API sp_mark sp_arena_mark(const sp_arena *a);
SP_API void sp_arena_rewind(sp_arena *a, sp_mark m);
SP_API void *sp_arena_alloc(sp_arena *a, size_t size, size_t align);
SP_API int sp_arena_oom(const sp_arena *a);
SP_API size_t sp_arena_used(const sp_arena *a);

SP_API int sp_utf8_valid(sp_view s);
SP_API size_t sp_utf8_count(sp_view s); /* (size_t)-1 if invalid */
SP_API int sp_utf8_next(sp_view s, size_t *off, uint32_t *cp);
SP_API int sp_utf8_encode(char out[4], uint32_t cp);
SP_API sp_err sp_utf8_to_utf16(sp_arena *a, sp_view utf8, uint16_t **out, size_t *nunits);
SP_API sp_str sp_utf16_to_utf8(sp_arena *a, const uint16_t *p, size_t nunits);

SP_API sp_str sp_str_empty(void);
SP_API sp_str sp_str_error(sp_err e);
SP_API sp_view sp_str_view(sp_str s);
SP_API sp_str sp_str_from(sp_arena *a, const char *s);
SP_API sp_str sp_str_from_n(sp_arena *a, const char *s, size_t n);
SP_API sp_str sp_str_from_view(sp_arena *a, sp_view v);
SP_API sp_str sp_str_dup_malloc(sp_view v); /* free(ptr) */
SP_API sp_str sp_concat(sp_arena *a, sp_view x, sp_view y);
SP_API sp_str sp_join_views(sp_arena *a, const sp_view *parts, size_t n, sp_view sep);
SP_API sp_str sp_sprintf(sp_arena *a, const char *fmt, ...) SP_PRINTF(2, 3);
SP_API sp_str sp_vsprintf(sp_arena *a, const char *fmt, va_list ap);
SP_API int sp_str_eq(sp_str a, sp_str b);

SP_API void sp_buf_init(sp_buf *b, sp_arena *a);
SP_API void sp_buf_clear(sp_buf *b);
SP_API void sp_buf_append(sp_buf *b, sp_view v);
SP_API void sp_buf_append_cstr(sp_buf *b, const char *s);
SP_API void sp_buf_append_char(sp_buf *b, char c);
SP_API void sp_buf_printf(sp_buf *b, const char *fmt, ...) SP_PRINTF(2, 3);
SP_API sp_str sp_buf_finish(sp_buf *b);

SP_API sp_path_style sp_path_native(void);
SP_API int sp_path_is_sep(char c, sp_path_style style);
SP_API char sp_path_pref_sep(sp_path_style style);
SP_API sp_path_info sp_path_parse(sp_view path, sp_path_style style);
SP_API int sp_path_is_abs(sp_view path, sp_path_style style);
SP_API int sp_path_is_rooted(sp_view path, sp_path_style style);
SP_API int sp_path_has_volume(sp_view path, sp_path_style style);
SP_API int sp_path_eq(sp_view a, sp_view b, sp_path_style style);

SP_API sp_str sp_path_clean(sp_arena *a, sp_view path, sp_path_style style);
SP_API sp_str sp_path_join(sp_arena *a, sp_path_style style, const sp_view *parts, size_t n);
SP_API sp_str sp_path_join2(sp_arena *a, sp_path_style style, sp_view x, sp_view y);
SP_API sp_str sp_path_join3(sp_arena *a, sp_path_style style, sp_view x, sp_view y, sp_view z);
SP_API sp_str sp_path_cat(sp_arena *a, sp_path_style style, const sp_view *parts, size_t n);

SP_API sp_view sp_path_dir(sp_view path, sp_path_style style);
SP_API sp_view sp_path_base(sp_view path, sp_path_style style);
SP_API sp_view sp_path_ext(sp_view path, sp_path_style style);
SP_API sp_view sp_path_stem(sp_view path, sp_path_style style);

SP_API sp_str sp_path_rel(sp_arena *a, sp_view from, sp_view to, sp_path_style style);
SP_API int sp_path_is_under(sp_arena *a, sp_view parent, sp_view child, sp_path_style style);
SP_API sp_err sp_path_check(sp_view path, sp_path_style style, unsigned sec_flags);

/*
 * Join untrusted `user` under trusted `root`.
 * Always rejects embedded NUL. SP_SEC_UTF8 / SP_SEC_CTRL as requested.
 * Rejects absolute/volume/rooted user paths and any escape of root.
 * Windows: also rejects <>:"|?* , ADS `:`, and components ending in `.` or space.
 */
SP_API sp_str sp_path_join_under(sp_arena *a, sp_view root, sp_view user,
                                 sp_path_style style, unsigned sec_flags);

#ifdef __cplusplus
}
#endif

#endif /* SP_H */
