#include "sp.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void fail_at(const char *file, int line, const char *msg) {
    fprintf(stderr, "FAIL %s:%d %s\n", file, line, msg);
    g_fail++;
}

#define EXPECT(cond)                                                                        \
    do {                                                                                    \
        if (!(cond)) fail_at(__FILE__, __LINE__, #cond);                                    \
    } while (0)

#define EXPECT_STR(s, want)                                                                 \
    do {                                                                                    \
        if ((s).err != SP_OK) {                                                             \
            char buf[192];                                                                  \
            snprintf(buf, sizeof(buf), "err=%s want=\"%s\"", sp_err_str((s).err), want);    \
            fail_at(__FILE__, __LINE__, buf);                                               \
        } else if (strcmp((s).ptr, (want)) != 0) {                                          \
            char buf[256];                                                                  \
            snprintf(buf, sizeof(buf), "got=\"%s\" want=\"%s\"", (s).ptr, want);            \
            fail_at(__FILE__, __LINE__, buf);                                               \
        }                                                                                   \
    } while (0)

#define EXPECT_ERR(s, e)                                                                    \
    do {                                                                                    \
        if ((s).err != (e)) {                                                               \
            char buf[160];                                                                  \
            snprintf(buf, sizeof(buf), "err=%s want=%s", sp_err_str((s).err), sp_err_str(e));\
            fail_at(__FILE__, __LINE__, buf);                                               \
        }                                                                                   \
    } while (0)

#define EXPECT_VIEW(v, want)                                                                \
    do {                                                                                    \
        size_t n = strlen(want);                                                            \
        if ((v).len != n || memcmp((v).ptr ? (v).ptr : "", want, n) != 0)                   \
            fail_at(__FILE__, __LINE__, want);                                              \
    } while (0)

static void expect_clean(sp_arena *a, sp_path_style st, const char *in, const char *out) {
    EXPECT_STR(sp_path_clean(a, sp_view_cstr(in), st), out);
}

static void test_core(void) {
    sp_arena *a = sp_arena_create(64, 0);
    char *p1, *p2;
    sp_mark m;
    sp_str s;
    uint16_t *u16 = NULL;
    size_t n = 0;
    char overlong[] = {(char)0xC0, (char)0x80};
    char euro[4];

    EXPECT(a != NULL);
    EXPECT(strcmp(sp_version(), "1.0.0") == 0);
    p1 = (char *)sp_arena_alloc(a, 32, 8);
    EXPECT(p1 && ((uintptr_t)p1 & 7u) == 0);
    memset(p1, 'A', 32);
    m = sp_arena_mark(a);
    p2 = (char *)sp_arena_alloc(a, 4096, 16);
    EXPECT(p2 != NULL);
    sp_arena_rewind(a, m);
    EXPECT(p1[0] == 'A' && p1[31] == 'A');
    EXPECT(!sp_arena_oom(a));

    EXPECT(sp_utf8_valid(SP_LIT("日本語")));
    EXPECT(sp_utf8_count(SP_LIT("日本語")) == 3);
    EXPECT(!sp_utf8_valid(sp_view_make(overlong, 2)));
    EXPECT(sp_utf8_count(sp_view_make(overlong, 2)) == (size_t)-1);
    EXPECT(sp_utf8_encode(euro, 0x20ACu) == 3);

    s = sp_concat(a, SP_LIT("foo"), SP_LIT("bar"));
    EXPECT_STR(s, "foobar");
    s = sp_sprintf(a, "%s-%d", "n", 42);
    EXPECT_STR(s, "n-42");
    EXPECT(sp_view_make(NULL, 8).len == 0);

    EXPECT(sp_utf8_to_utf16(a, SP_LIT("AΩ"), &u16, &n) == SP_OK);
    EXPECT(n == 2 && u16[0] == 'A' && u16[1] == 0x03A9);

    sp_arena_destroy(a);
    a = sp_arena_create(32, SP_ARENA_FIXED);
    EXPECT(sp_arena_alloc(a, 16, 1));
    EXPECT(!sp_arena_alloc(a, 64, 1));
    EXPECT(sp_arena_oom(a));
    sp_arena_destroy(a);
}

static void test_paths(void) {
    static const struct {
        sp_path_style st;
        const char *in;
        const char *out;
    } clean[] = {
        {SP_POSIX, "", "."},
        {SP_POSIX, "/", "/"},
        {SP_POSIX, "abc/..", "."},
        {SP_POSIX, "/abc/def/../../..", "/"},
        {SP_POSIX, "abc/../../def", "../def"},
        {SP_POSIX, "abc//def//ghi", "abc/def/ghi"},
        {SP_POSIX, "//abc", "/abc"},
        {SP_POSIX, "/../a/c", "/a/c"},
        {SP_WINDOWS, "abc/def", "abc\\def"},
        {SP_WINDOWS, "C:/a/../b", "C:\\b"},
        {SP_WINDOWS, "C:", "C:."},
        {SP_WINDOWS, "C:\\", "C:\\"},
        {SP_WINDOWS, "\\\\host\\share\\..\\x", "\\\\host\\share\\x"},
        {SP_WINDOWS, "\\\\host\\share\\", "\\\\host\\share"},
        {SP_WINDOWS, "\\\\?\\C:\\foo\\..\\bar", "\\\\?\\C:\\bar"},
        {SP_WINDOWS, "a\\..\\..\\b", "..\\b"},
    };
    static const struct {
        const char *from;
        const char *to;
        sp_path_style st;
        const char *out;
        sp_err err;
    } rel[] = {
        {"/a/b/c", "/a/d", SP_POSIX, "../../d", SP_OK},
        {"/a/b", "/a/b", SP_POSIX, ".", SP_OK},
        {"/", "/a/b", SP_POSIX, "a/b", SP_OK},
        {"/a", "b", SP_POSIX, NULL, SP_ERR_INVALID},
        {"C:\\a\\b", "C:\\a\\c", SP_WINDOWS, "..\\c", SP_OK},
    };
    sp_arena *a = sp_arena_create(2048, 0);
    size_t i;
    sp_view cat[2];
    sp_str s;

    for (i = 0; i < sizeof(clean) / sizeof(clean[0]); i++)
        expect_clean(a, clean[i].st, clean[i].in, clean[i].out);

    EXPECT_STR(sp_path_join2(a, SP_POSIX, SP_LIT("/a"), SP_LIT("/b")), "/b");
    EXPECT_STR(sp_path_join2(a, SP_WINDOWS, SP_LIT("C:\\A"), SP_LIT("\\B")), "C:\\B");
    EXPECT_STR(sp_path_join2(a, SP_WINDOWS, SP_LIT("C:\\A"), SP_LIT("D:\\B")), "D:\\B");
    cat[0] = SP_LIT("a");
    cat[1] = SP_LIT("/b");
    EXPECT_STR(sp_path_cat(a, SP_POSIX, cat, 2), "a/b");

    EXPECT_VIEW(sp_path_dir(SP_LIT("a/b/c"), SP_POSIX), "a/b");
    EXPECT_VIEW(sp_path_base(SP_LIT("a/b/c/"), SP_POSIX), "c");
    EXPECT_VIEW(sp_path_ext(SP_LIT("a/b.tar.gz"), SP_POSIX), ".gz");
    EXPECT_VIEW(sp_path_stem(SP_LIT(".bashrc"), SP_POSIX), ".bashrc");
    EXPECT(sp_path_is_abs(SP_LIT("C:\\x"), SP_WINDOWS));
    EXPECT(!sp_path_is_abs(SP_LIT("C:x"), SP_WINDOWS));
    EXPECT(sp_path_eq(SP_LIT("A/B"), SP_LIT("a\\b"), SP_WINDOWS));

    for (i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
        s = sp_path_rel(a, sp_view_cstr(rel[i].from), sp_view_cstr(rel[i].to), rel[i].st);
        if (rel[i].err != SP_OK) EXPECT_ERR(s, rel[i].err);
        else EXPECT_STR(s, rel[i].out);
    }

    EXPECT(sp_path_is_under(a, SP_LIT("/var/www"), SP_LIT("/var/www/x"), SP_POSIX));
    EXPECT(!sp_path_is_under(a, SP_LIT("/var/www"), SP_LIT("/var/www-data/x"), SP_POSIX));
    EXPECT(sp_path_is_under(a, SP_LIT("C:\\app"), SP_LIT("c:\\APP\\x"), SP_WINDOWS));

    EXPECT_STR(sp_path_join_under(a, SP_LIT("/var/www"), SP_LIT("foo/../bar"), SP_POSIX,
                                  SP_SEC_DEFAULT),
               "/var/www/bar");
    EXPECT_ERR(sp_path_join_under(a, SP_LIT("/var/www"), SP_LIT("../etc/passwd"), SP_POSIX,
                                  SP_SEC_DEFAULT),
               SP_ERR_TRAVERSAL);
    EXPECT_ERR(sp_path_join_under(a, SP_LIT("/var/www"), SP_LIT("/etc/passwd"), SP_POSIX,
                                  SP_SEC_DEFAULT),
               SP_ERR_ABSOLUTE);
    {
        char nul[] = {'a', 0, 'b'};
        EXPECT_ERR(sp_path_join_under(a, SP_LIT("/r"), sp_view_make(nul, 3), SP_POSIX,
                                      SP_SEC_DEFAULT),
                   SP_ERR_NUL);
    }
    EXPECT_STR(sp_path_join_under(a, SP_LIT("C:\\app"), SP_LIT("x\\y"), SP_WINDOWS,
                                  SP_SEC_DEFAULT),
               "C:\\app\\x\\y");
    EXPECT_ERR(sp_path_join_under(a, SP_LIT("C:\\app"), SP_LIT("foo:stream"), SP_WINDOWS,
                                  SP_SEC_DEFAULT),
               SP_ERR_INVALID);
    EXPECT_ERR(sp_path_join_under(a, SP_LIT("C:\\app"), SP_LIT("foo."), SP_WINDOWS,
                                  SP_SEC_DEFAULT),
               SP_ERR_INVALID);
    EXPECT_ERR(sp_path_join_under(a, SP_LIT("/tmp"), SP_LIT("a\nb"), SP_POSIX,
                                  SP_SEC_UTF8 | SP_SEC_CTRL),
               SP_ERR_INVALID);

#ifdef _WIN32
    EXPECT(sp_path_native() == SP_WINDOWS);
#else
    EXPECT(sp_path_native() == SP_POSIX);
#endif
    sp_arena_destroy(a);
}

int main(void) {
    test_core();
    test_paths();
    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    puts("ok");
    return 0;
}
