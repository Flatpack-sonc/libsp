#include "sp.h"
#include <stdio.h>

int main() {
    sp_arena *a = sp_arena_create(256, 0);
    if (!a) return 1;
    sp_str s = sp_path_join2(a, SP_POSIX, SP_LIT("a"), SP_LIT("b"));
    int rc = (s.err == SP_OK && s.len == 3) ? 0 : 1;
    sp_arena_destroy(a);
    return rc;
}
