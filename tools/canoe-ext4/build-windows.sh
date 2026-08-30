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
        "$SOURCE/configure" \
            --host=x86_64-w64-mingw32 \
            --prefix="$PREFIX" \
            --disable-nls \
            --disable-fsck \
            --disable-e2scrub \
            --disable-fuse2fs \
            --disable-libblkid \
            --disable-libuuid
    )
fi
# The helper only consumes libext2fs and libcom_err. Their dependencies are
# built by the e2fsprogs makefiles before the two requested archive targets.
make -C "$BUILD" lib/et/libcom_err.la lib/ext2fs/libext2fs.la
make -C "$BUILD" install-libLTLIBRARIES install-data-local >/dev/null 2>&1 || true

INCLUDE_FLAGS="-I$BUILD/lib -I$SOURCE/lib -I$BUILD/lib/ext2fs -I$SOURCE/lib/ext2fs"
LIB_FLAGS="$BUILD/lib/ext2fs/.libs/libext2fs.a $BUILD/lib/et/.libs/libcom_err.a"
# Windows uses libext2fs' windows_io_manager (selected by default_io_manager);
# recovery of a dirty image remains unavailable unless a Windows e2fsck port is
# supplied, while clean read/write operations use the same helper contract.
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 \
    $INCLUDE_FLAGS -o "$OUT" "$ROOT/canoe-ext4.c" $LIB_FLAGS \
    -lz -lws2_32
printf 'Built %s\n' "$OUT"
