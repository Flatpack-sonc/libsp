#include "sp.h"
#include <stdint.h>
#include <string.h>

/* Standalone driver: also usable as libFuzzer target (LLVMFuzzerTestOneInput). */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    sp_arena *a;
    sp_view v;
    sp_str s;
    uint16_t *u16;
    size_t n;

    if (!data) return 0;
    a = sp_arena_create(4096, 0);
    if (!a) return 0;
    v = sp_view_make((const char *)data, size);

    (void)sp_utf8_valid(v);
    (void)sp_utf8_count(v);
    (void)sp_path_check(v, SP_POSIX, SP_SEC_DEFAULT | SP_SEC_CTRL);
    (void)sp_path_check(v, SP_WINDOWS, SP_SEC_DEFAULT | SP_SEC_CTRL);
    s = sp_path_clean(a, v, SP_POSIX);
    (void)s;
    s = sp_path_clean(a, v, SP_WINDOWS);
    (void)s;
    (void)sp_path_dir(v, SP_POSIX);
    (void)sp_path_base(v, SP_WINDOWS);
    (void)sp_path_is_abs(v, SP_POSIX);
    (void)sp_path_is_under(a, SP_LIT("/var/www"), v, SP_POSIX);
    s = sp_path_join_under(a, SP_LIT("/root"), v, SP_POSIX, SP_SEC_DEFAULT);
    (void)s;
    s = sp_path_join_under(a, SP_LIT("C:\\root"), v, SP_WINDOWS, SP_SEC_DEFAULT);
    (void)s;
    u16 = NULL;
    n = 0;
    if (sp_utf8_to_utf16(a, v, &u16, &n) == SP_OK && u16) {
        s = sp_utf16_to_utf8(a, u16, n);
        (void)s;
    }
    sp_arena_destroy(a);
    return 0;
}

#ifndef SP_FUZZ_NO_MAIN
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    uint8_t buf[4096];
    size_t n = 0;
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) return 1;
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
    } else {
        static const char *samples[] = {
            "", ".", "..", "/", "a/../../b", "C:\\a\\..\\b", "\\\\s\\sh\\x",
            "\xC0\x80", "/var/www", "../etc/passwd"
        };
        static const char nulpath[] = {'f', 'o', 'o', '\0', 'b', 'a', 'r'};
        size_t i;
        for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
            LLVMFuzzerTestOneInput((const uint8_t *)samples[i], strlen(samples[i]));
        }
        LLVMFuzzerTestOneInput((const uint8_t *)nulpath, sizeof(nulpath));
        return 0;
    }
    return LLVMFuzzerTestOneInput(buf, n);
}
#endif
