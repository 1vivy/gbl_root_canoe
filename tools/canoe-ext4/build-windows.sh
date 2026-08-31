#!/bin/sh
# Build e2fsprogs/libext2fs and canoe-ext4 for Windows x64.
#
# This script intentionally does not download source or emit a fake binary.
# Set E2FSPROGS_SRC to a checked-out e2fsprogs tree (a release tarball unpacked
# there is fine). The tree is configured in a sibling build directory so the
# source checkout is not modified.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CC=${CC:-x86_64-w64-mingw32-gcc}
SOURCE=${E2FSPROGS_SRC:-}
BUILD=${E2FSPROGS_BUILD:-"$ROOT/.e2fsprogs-mingw-build"}
PREFIX=${E2FSPROGS_PREFIX:-"$BUILD/install"}
OUT=${OUT:-"$ROOT/canoe-ext4.exe"}
# zlib for the mingw link (the helper compresses nothing, but libext2fs pulls
# in -lz unconditionally on this build): a prefix with include/ and lib/.
ZLIB_PREFIX=${ZLIB_PREFIX:-}
ZLIB_FLAGS=""
if [ -n "$ZLIB_PREFIX" ]; then
    ZLIB_FLAGS="-I$ZLIB_PREFIX/include -L$ZLIB_PREFIX/lib"
fi

if ! command -v "$CC" >/dev/null 2>&1; then
    printf '%s\n' "MinGW compiler $CC is not installed; no Windows binary was built." >&2
    exit 0
fi
if [ -z "$SOURCE" ]; then
    printf '%s\n' 'Set E2FSPROGS_SRC to a checked-out e2fsprogs tree to build libext2fs; no binary was built.' >&2
    exit 0
fi
if [ ! -f "$SOURCE/configure" ]; then
    printf 'E2FSPROGS_SRC is not an e2fsprogs source tree: %s\n' "$SOURCE" >&2
    exit 2
fi

mkdir -p "$BUILD"
if [ ! -f "$BUILD/Makefile" ]; then
    (
        cd "$BUILD"
        BUILD_CC="${BUILD_CC:-cc}" "$SOURCE/configure" \
            --cache-file="$BUILD/config.cache" \
            --build="$(cc -dumpmachine)" \
            --host=x86_64-w64-mingw32 \
            --prefix="$PREFIX" \
            --disable-nls \
            --disable-fsck \
            --disable-e2scrub \
            --disable-fuse2fs \
            --enable-libblkid \
            --enable-libuuid
    )
fi
# The journal replay entry point lives in the debugfs/e2fsck sources rather
# than the libext2fs archive itself.  Build those three objects and link them
# into the helper; they use only libext2fs' block read/write/flush callbacks.
make -C "$BUILD/lib/uuid"
make -C "$BUILD/lib/blkid"
JOURNAL_CFLAGS="-I$BUILD/lib/uuid -I$BUILD/lib/blkid -I$BUILD/lib -I$SOURCE/include/mingw -I$SOURCE/lib/ext2fs -I$SOURCE/debugfs -I$SOURCE/e2fsck -I$SOURCE/lib -DHAVE_CONFIG_H -DDEBUGFS -Dunix_io_manager=windows_io_manager"
make -C "$BUILD/lib/ext2fs" \
    DEBUGFS_CFLAGS="$JOURNAL_CFLAGS" \
    journal.o revoke.o recovery.o

INCLUDE_FLAGS="-I$BUILD/lib -I$SOURCE/lib -I$BUILD/lib/ext2fs -I$SOURCE/lib/ext2fs"
LIB_FLAGS="$BUILD/lib/ext2fs/journal.o $BUILD/lib/ext2fs/revoke.o $BUILD/lib/ext2fs/recovery.o $BUILD/lib/ext2fs/libext2fs.a $BUILD/lib/et/libcom_err.a $BUILD/lib/uuid/libuuid.a $BUILD/lib/blkid/libblkid.a"
# Windows uses libext2fs' windows_io_manager for both ordinary operations and
# the linked journal replay path.  windows_io_manager implements the callbacks
# replay requires: read, write, set block size, and flush.
"$CC" -static -static-libgcc -std=c11 -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 \
    $INCLUDE_FLAGS -o "$OUT" "$ROOT/canoe-ext4.c" $LIB_FLAGS \
    $ZLIB_FLAGS -lz -lws2_32
printf 'Built %s\n' "$OUT"
