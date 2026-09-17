# Security

`sp_path_join_under` is a **lexical** check. It stops `../`, absolute user paths, embedded NULs, invalid UTF-8 (when requested), and Windows ADS / trailing-dot tricks.

It does not follow the filesystem. If the process can hit a symlink under the root, the kernel may still walk outside the tree. Pair with `openat`, `O_NOFOLLOW`, or a capability sandbox.

Report issues privately if you have a crash or bypass in path joining; otherwise use the tracker.
