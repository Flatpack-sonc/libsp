#include "sp.h"
#include <stdio.h>

int main(void) {
    sp_arena *a = sp_arena_create(4096, 0);
    sp_str cfg, safe, bad;

    if (!a) return 1;

    cfg = sp_path_join3(a, SP_POSIX, SP_LIT("/home/app"), SP_LIT(".config"), SP_LIT("tool.toml"));
    printf("config  %s\n", cfg.ptr);

    safe = sp_path_join_under(a, SP_LIT("/var/www"), SP_LIT("img/logo.png"), SP_POSIX,
                              SP_SEC_DEFAULT);
    printf("allowed %s\n", safe.ptr);

    bad = sp_path_join_under(a, SP_LIT("/var/www"), SP_LIT("../../etc/passwd"), SP_POSIX,
                             SP_SEC_DEFAULT);
    printf("blocked %s\n", sp_err_str(bad.err));

    sp_arena_destroy(a);
    return (safe.err == SP_OK && bad.err == SP_ERR_TRAVERSAL) ? 0 : 1;
}
