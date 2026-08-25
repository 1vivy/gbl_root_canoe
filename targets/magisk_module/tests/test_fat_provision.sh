#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-fat-provision.$$
PERSIST=$TMP/persist
BIN=$TMP/bin
RUNTIME=$TMP/runtime
FAT=$PERSIST/efisp.fat
IMAGE=$TMP/host-image.fat
SCRIPT=$ROOT/targets/magisk_module/module/bin/canoe_fat_provision.sh
SIZE=8192
EXTENT_SOURCE=filefrag

trap 'rm -rf "$TMP"' EXIT INT TERM HUP
mkdir -p "$PERSIST" "$BIN" "$RUNTIME"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_not_file() { [ ! -e "$1" ] || fail "unexpected file: $1"; }
assert_contains() { case "$1" in *"$2"*) ;; *) fail "$3 (got '$1')" ;; esac; }
run_script() {
  PERSIST_MNT="$PERSIST" FAT_FILE="$FAT" FAT_SIZE="$SIZE" PERSIST_FREE_BYTES=1048576 \
    EXTENT_SOURCE="$EXTENT_SOURCE" FIEMAP_HELPER="$BIN/fiemap" RUNTIME_DIR="$RUNTIME" \
    PATH="$BIN:/usr/bin:/bin" sh "$SCRIPT" "$@"
}

cat > "$BIN/filefrag" <<'EOF'
#!/bin/sh
printf 'Filesystem type is: fake\n 0: 0..1: 100..101: 2: last,eof\n'
EOF
cat > "$BIN/debugfs" <<'EOF'
#!/bin/sh
printf 'debugfs fake invocation: %s\n 0: 0..1: 300..301: 2: last,eof\n' "$*"
EOF
cat > "$BIN/fiemap" <<'EOF'
#!/bin/sh
printf '400:2\n'
EOF
chmod +x "$BIN/filefrag" "$BIN/debugfs" "$BIN/fiemap"

# Creation uses ordinary zero writes, not a sparse seek, and writes a valid trailer.
EXTENT_SOURCE=filefrag run_script provision "$SIZE" > "$TMP/create.out"
assert_file "$FAT"
allocated=$(stat -c %b "$FAT")
[ "$allocated" -gt 0 ] || fail "zero-filled file has no allocated blocks"
assert_contains "$(cat "$TMP/create.out")" 'STAMP_VALID=1' 'creation did not verify trailer'
pass 'provision creates a fully allocated file and verifies its stamp'

# The free-space guard runs before creation and leaves no partial file.
rm -f "$FAT"
if PERSIST_MNT="$PERSIST" FAT_FILE="$FAT" FAT_SIZE="$SIZE" PERSIST_FREE_BYTES=1 \
   EXTENT_SOURCE=filefrag RUNTIME_DIR="$RUNTIME" PATH="$BIN:/usr/bin:/bin" \
   sh "$SCRIPT" provision "$SIZE" > "$TMP/space.out" 2>&1; then fail 'insufficient space was accepted'; fi
assert_not_file "$FAT"
pass 'insufficient space leaves no file behind'

# An image must match exactly and is copied in place after zero allocation.
rm -f "$FAT"
dd if=/dev/zero of="$IMAGE" bs=1 count="$SIZE" 2>/dev/null
printf 'IMAGE-DATA' | dd of="$IMAGE" bs=1 conv=notrunc 2>/dev/null
EXTENT_SOURCE=filefrag run_script provision "$SIZE" "$IMAGE" > "$TMP/image.out"
assert_contains "$(dd if="$FAT" bs=1 count=10 2>/dev/null)" 'IMAGE-DATA' 'image was not copied in place'
rm -f "$FAT"
if EXTENT_SOURCE=filefrag run_script provision "$SIZE" "$TMP/missing-size-image" > "$TMP/mismatch.out" 2>&1; then fail 'missing image was accepted'; fi
assert_not_file "$FAT"
dd if=/dev/zero of="$TMP/wrong-size" bs=1 count=512 2>/dev/null
if EXTENT_SOURCE=filefrag run_script provision "$SIZE" "$TMP/wrong-size" > "$TMP/mismatch.out" 2>&1; then fail 'wrong-size image was accepted'; fi
assert_not_file "$FAT"
pass 'in-place image copy accepts exact size and refuses mismatches'

# Exercise each resolver independently and assert the selected path is reported.
EXTENT_SOURCE=debugfs run_script provision "$SIZE" > "$TMP/debugfs.out"
assert_contains "$(cat "$TMP/debugfs.out")" 'EXTENT_SOURCE=debugfs' 'debugfs resolver was not selected'
rm -f "$FAT"
EXTENT_SOURCE=fiemap run_script provision "$SIZE" > "$TMP/fiemap.out"
assert_contains "$(cat "$TMP/fiemap.out")" 'EXTENT_SOURCE=fiemap' 'FIEMAP resolver was not selected'
pass 'filefrag, debugfs, and FIEMAP extent sources are selectable'

# A changed physical run simulates relocation: verify reports a mismatch, then
# restamp repairs it; repeated verify/restamp remain idempotent.
EXTENT_SOURCE=fiemap run_script verify "$SIZE" > "$TMP/verify1.out" || fail 'valid stamp did not verify'
assert_contains "$(cat "$TMP/verify1.out")" 'STAMP_MATCH=1' 'valid stamp was not reported'
cat > "$BIN/fiemap" <<'EOF'
#!/bin/sh
printf '500:2\n'
EOF
chmod +x "$BIN/fiemap"
if EXTENT_SOURCE=fiemap run_script verify "$SIZE" > "$TMP/relocated.out"; then fail 'relocated run was accepted'; fi
assert_contains "$(cat "$TMP/relocated.out")" 'STAMP_MATCH=0' 'relocation mismatch was not reported'
EXTENT_SOURCE=fiemap run_script restamp > "$TMP/restamp.out"
assert_contains "$(cat "$TMP/restamp.out")" 'STAMP_VALID=1' 'restamp did not repair the stamp'
EXTENT_SOURCE=fiemap run_script verify "$SIZE" > "$TMP/verify2.out"
assert_contains "$(cat "$TMP/verify2.out")" 'STAMP_MATCH=1' 'repeated verify was not idempotent'
EXTENT_SOURCE=fiemap run_script restamp > "$TMP/restamp2.out"
assert_contains "$(cat "$TMP/restamp2.out")" 'STAMP_VALID=1' 'repeated restamp was not idempotent'
pass 'trailer mismatch, restamp, and repeated verify are safe'

# Verify detects a sparse file separately from a stamp mismatch.
rm -f "$FAT"
mkdir -p "$PERSIST"
: > "$FAT"
dd if=/dev/zero of="$FAT" bs=1 count=1 seek=$((SIZE - 1)) 2>/dev/null
if EXTENT_SOURCE=filefrag run_script verify "$SIZE" > "$TMP/sparse.out"; then fail 'sparse file was accepted'; fi
assert_contains "$(cat "$TMP/sparse.out")" 'NON_SPARSE=0' 'sparse file was not detected'
pass 'verify reports sparse files'

echo 'ok - FAT provisioning coverage complete'
