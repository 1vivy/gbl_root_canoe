# canoe-ext4

`canoe-ext4` is a bounded, headless ext4 reader/writer backed by e2fsprogs'
`libext2fs`. It accepts an ext4 partition image or an exported block-device
path and never requires a filesystem mount. On Windows, raw sources such as
`\\.\PhysicalDriveN` are opened through Win32 handles; filesystem metadata
queries cannot establish whether those device objects exist. The OS access
check, independent superblock probe, and libext2fs validation still apply.

## Commands

Options must precede the command. `--recover` explicitly authorizes journal
recovery for a dirty source. `--mkdir-p` (or `-p`) gives `write` and `mkdir`
parents semantics.

New directories use libext2fs's permission umask without adding append-only,
immutable, or compression flags. Existing directories' attributes are preserved;
the helper does not clear protection flags as an implicit repair.

```
canoe-ext4 inspect SOURCE [--path PATH]
canoe-ext4 read SOURCE PATH
canoe-ext4 write SOURCE PATH < BYTES
canoe-ext4 mkdir SOURCE PATH
canoe-ext4 remove SOURCE PATH
canoe-ext4 rename SOURCE OLD_PATH NEW_PATH
canoe-ext4 sync SOURCE MANIFEST DESIRED_ROOT EXPECTED_ROOT
canoe-ext4 list SOURCE DIRECTORY
```

`inspect` emits one JSON object containing `state`, `free_blocks`,
`free_bytes`, `block_size`, numeric feature bitfields, and feature-name arrays.
With `--path`, it also includes `path` and `path_exists`. `list` emits a JSON
array of `{name,inode,type}` objects. `read` writes only file bytes to stdout;
recovery/status diagnostics are on stderr.

Individual file contents are limited to 64 MiB. `sync` accepts at most 4096
entries and retains at most 256 MiB of rollback snapshots. Every invocation takes
an exclusive lock on the source and refuses a source listed as mounted in
`/proc/self/mountinfo` (tests may provide `CANOE_EXT4_MOUNTINFO`). Mutation
opens are fail-closed for unsupported feature bits and dirty state. They run
`e2fsck`'s libext2fs-equivalent journal recovery boundary before mutation,
then flush libext2fs, stop its filesystem/journal handle, fsync the source fd,
and close it before returning success.

`sync` applies a reviewed multi-file change under one source lock and filesystem
owner. It verifies expected entries, snapshots affected contents, applies the
desired tree, and restores those contents on an ordinary apply failure before
flushing and closing. A successful rollback is logical recovery, not a claim of
power-loss atomicity.

The manifest contains space-separated `desired expected target_hex local_hex`
records, each terminated by a newline and strictly ordered by decoded target
path. Entry states are `a` (absent), `d` (directory), and `f` (file); expected
state alone may be `u` (unobserved). Target paths are absolute inside the ext4
filesystem; local paths are relative to the desired and expected host roots.
Both paths are hex-encoded so whitespace cannot change field boundaries.

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
the host supplies suitable archives, producing `canoe-ext4-static`. Linux toolkit
packaging uses this separate static output so recipients do not need the build
machine's libext2fs ABI. Install static development archives, or supply
`EXT2FS_CFLAGS` and `LDLIBS` for an independently built e2fsprogs tree.

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
