# libsp

C11 library for **UTF-8 strings** and **lexical paths**. Built to stop the usual native path bugs: `PATH_MAX` buffers, mixed ACP/UTF-16, `../` escapes, and embedded NULs.

```c
#include <sp.h>

sp_arena *a = sp_arena_create(4096, 0);
sp_str p = sp_path_join_under(a, SP_LIT("/var/www"), sp_view_cstr(user),
                              SP_POSIX, SP_SEC_DEFAULT);
if (!sp_ok(p)) {
    /* SP_ERR_TRAVERSAL, SP_ERR_NUL, SP_ERR_UTF8, SP_ERR_ABSOLUTE, ... */
}
sp_arena_destroy(a);
```

## Why

Joining `root + "/" + user` with `snprintf` is a CVE factory. libsp keeps every string length-aware, UTF-8, and arena-allocated, and `sp_path_join_under` refuses anything that would walk out of a trusted root.

## Install

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

Or vendor `include/sp.h` and `src/*.c`. Makefile: `make test SANITIZE=1`.

## Paths

| Function | Model |
|----------|--------|
| `sp_path_clean` | Go `filepath.Clean` (`.` / `..` / duplicate seps) |
| `sp_path_join` | Python `os.path.join` (absolute / volume resets) |
| `sp_path_cat` | Go `filepath.Join` (concat then clean) |
| `sp_path_join_under` | Trusted root + untrusted relative |
| `sp_path_is_under` | Cleaned prefix with a separator boundary |

POSIX uses `/` only. Windows treats `/` and `\` as separators and emits `\`. UNC, `C:`, `C:\`, and `\\?\` long paths are parsed.

`dir` / `base` / `ext` / `stem` are views into the original buffer. `.bashrc` has an empty extension.

## Contract

- Arena memory is never realloc'd while live. `sp_arena_mark` / `sp_arena_rewind` drop allocations after a checkpoint.
- Arenas are not thread-safe.
- Paths are lexical: no `stat`, no symlinks, same result on every OS.
- `join_under` is lexical. A symlink inside the root can still leave the tree; use `openat` / `O_NOFOLLOW` if you need that.
- Windows equality is ASCII case-fold plus separator folding, not full Unicode case mapping.

## Layout

```
include/sp.h     public API
src/             arena, strings, UTF-8, paths
tests/test_sp.c  unit tests
examples/        join_under demo
fuzz/            libFuzzer-compatible driver
```

## License

MIT. See [LICENSE](LICENSE).
