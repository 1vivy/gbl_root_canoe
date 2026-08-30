# canoe-ext4

`canoe-ext4` is a bounded, headless ext4 reader/writer backed by e2fsprogs'
`libext2fs`. It accepts an ext4 partition image or an exported block-device
path and never requires a filesystem mount.

## Commands

Options must precede the command. `--recover` explicitly authorizes journal
recovery for a dirty source. `--mkdir-p` (or `-p`) gives `write` and `mkdir`
parents semantics.

```
canoe-ext4 inspect SOURCE [--path PATH]
canoe-ext4 read SOURCE PATH
canoe-ext4 write SOURCE PATH < BYTES
canoe-ext4 mkdir SOURCE PATH
canoe-ext4 remove SOURCE PATH
canoe-ext4 rename SOURCE OLD_PATH NEW_PATH
canoe-ext4 list SOURCE DIRECTORY
```

`inspect` emits one JSON object containing `state`, `free_blocks`,
`free_bytes`, `block_size`, numeric feature bitfields, and feature-name arrays.
With `--path`, it also includes `path` and `path_exists`. `list` emits a JSON
array of `{name,inode,type}` objects. `read` writes only file bytes to stdout;
recovery/status diagnostics are on stderr.

Writes are limited to 64 MiB per invocation. Every invocation takes an
exclusive lock on the source and refuses a source listed as mounted in
`/proc/self/mountinfo` (tests may provide `CANOE_EXT4_MOUNTINFO`). Mutation
opens are fail-closed for unsupported feature bits and dirty state. They run
`e2fsck`'s libext2fs-equivalent journal recovery boundary before mutation,
then flush libext2fs, stop its filesystem/journal handle, fsync the source fd,
and close it before returning success.

## Exit codes

| Code | Meaning |
| ---: | --- |
| 0 | Success |
| 2 | Usage or invalid path |
| 3 | Unsupported/unknown filesystem feature |
| 4 | Dirty filesystem; rerun with `--recover` |
| 5 | Source is mounted |
| 6 | I/O, locking, recovery, or filesystem I/O failure |
| 7 | Requested path does not exist |
| 8 | Operation rejected (wrong type, non-empty directory, collision, limit) |

## Build

On Linux, install the e2fsprogs development package (`libext2fs-dev` on
Debian/Ubuntu), then run `make`. The normal binary links dynamically to
`libext2fs` and `libcom_err`; `make static` requests a fully static link when
the host supplies suitable archives. `build-windows.sh` documents and drives
a source build of e2fsprogs/libext2fs followed by an x86_64 MinGW helper build.
It deliberately produces no placeholder binary when MinGW or the e2fsprogs
source tree is absent.
