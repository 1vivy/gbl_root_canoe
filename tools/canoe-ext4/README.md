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
| 4 | Dirty filesystem; rerun with `--recover` (recovery is never implicit) |
| 5 | Source is mounted |
| 6 | I/O, locking, recovery, or filesystem I/O failure |
| 7 | Requested path does not exist |
| 8 | Operation rejected (wrong type, non-empty directory, collision, limit) |

## Build

On Linux, install the e2fsprogs development package (`libext2fs-dev` on
Debian/Ubuntu), then run `make`. The normal binary links dynamically to
`libext2fs` and `libcom_err`; `make static` requests a fully static link when
the host supplies suitable archives.

`build-windows.sh` cross-builds `canoe-ext4.exe` for x86_64 Windows. It
deliberately produces no placeholder binary when MinGW or the e2fsprogs source
tree is absent. It needs two inputs, because MinGW supplies no system
`libuuid`/`libblkid`/`zlib` and the build enables e2fsprogs' bundled uuid and
blkid instead:

| Variable | Meaning |
| --- | --- |
| `E2FSPROGS_SRC` | Checked-out e2fsprogs tree (release tarball unpacked is fine); configured in a sibling build directory, never modified. |
| `ZLIB_PREFIX` | Prefix holding `include/zlib.h` and `lib/libz.a` built for MinGW; `libext2fs` links `-lz` unconditionally. |
| `BUILD_CC` | Build-machine compiler for configure's own probes; defaults to `cc`. Leave it unless the default is unusable. |

```sh
git clone --depth 1 --branch v1.47.3 https://github.com/tytso/e2fsprogs.git /tmp/e2fsprogs
git clone --depth 1 https://github.com/madler/zlib.git /tmp/zlib
make -C /tmp/zlib -f win32/Makefile.gcc PREFIX=x86_64-w64-mingw32- libz.a
mkdir -p /tmp/zlib-mingw/include /tmp/zlib-mingw/lib
cp /tmp/zlib/zlib.h /tmp/zlib/zconf.h /tmp/zlib-mingw/include/
cp /tmp/zlib/libz.a /tmp/zlib-mingw/lib/
E2FSPROGS_SRC=/tmp/e2fsprogs ZLIB_PREFIX=/tmp/zlib-mingw sh build-windows.sh
```

The first configure run is slow (every MinGW probe compiles); results are
cached in the build directory, so reruns are fast. On Windows the helper uses
`libext2fs`' `windows_io_manager` against `\\.\PhysicalDrive<N>`. The e2fsprogs
journal replay objects (`debugfs/journal.c`, `e2fsck/revoke.c`, and
`e2fsck/recovery.c`) are linked into the helper and use the manager's
read/write, block-size, and flush callbacks. Consequently dirty-source
recovery is available on Windows under the same explicit `--recover`
authorization as Linux; discard/zeroout are not part of the replay path.
