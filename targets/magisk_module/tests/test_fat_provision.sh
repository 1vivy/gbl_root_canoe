#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-fat-provision.$$
PERSIST=$TMP/persist
SOURCE=$PERSIST/efisp
BIN=$TMP/bin
RUNTIME=$TMP/runtime
STATE=$TMP/state
CAPTURE=$TMP/capture
FAT=$PERSIST/efisp.fat
SCRIPT=$ROOT/targets/magisk_module/module/bin/canoe_fat_provision.sh
HELPER=$BIN/fiemap
SIZE=2129920
VOLUME_SECTORS=4152
trap 'rm -rf "$TMP"' EXIT INT TERM HUP
mkdir -p "$SOURCE/tools" "$BIN" "$RUNTIME" "$STATE" "$CAPTURE"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_not_exists() { [ ! -e "$1" ] || fail "unexpected path: $1"; }
assert_contains() { case "$1" in *"$2"*) ;; *) fail "$3 (got '$1')" ;; esac; }
run_script() {
  PERSIST_MNT="$PERSIST" SOURCE_EFISP="$SOURCE" FAT_FILE="$FAT" FAT_SIZE="$SIZE" \
  FIEMAP_HELPER="${FIEMAP_HELPER:-$HELPER}" RUNTIME_DIR="$RUNTIME" PATH="$BIN:/usr/bin:/bin" sh "$SCRIPT" "$@"
}

cat > "$BIN/fiemap" <<'EOF'
#!/bin/sh
printf 'blocksize:4096\n%s\n' "${FIEMAP_RUN:-1000:520}"
EOF
cat > "$BIN/losetup" <<'EOF'
#!/bin/sh
printf 'losetup %s\n' "$*" >> "$FLOW_LOG"
case "$1" in
  -f) printf '/dev/block/loop7\n' ;;
  -d) rm -f "$LOOP_STATE" ;;
  *) [ "${FAIL_ATTACH:-0}" -eq 0 ] || exit 1; : > "$LOOP_STATE" ;;
esac
EOF
cat > "$BIN/newfs_msdos" <<'EOF'
#!/bin/sh
printf 'newfs_msdos %s\n' "$*" >> "$FLOW_LOG"
[ "${FAIL_FORMAT:-0}" -eq 0 ]
EOF
cat > "$BIN/fsck_msdos" <<'EOF'
#!/bin/sh
printf 'fsck_msdos %s\n' "$*" >> "$FLOW_LOG"
[ "${FAIL_FSCK:-0}" -eq 0 ]
EOF
cat > "$BIN/mount" <<'EOF'
#!/bin/sh
printf 'mount %s\n' "$*" >> "$FLOW_LOG"
mkdir -p "$6"
printf '%s\n' "$6" > "$MOUNT_STATE"
EOF
cat > "$BIN/umount" <<'EOF'
#!/bin/sh
printf 'umount %s\n' "$*" >> "$FLOW_LOG"
if [ -n "${CAPTURE_DIR:-}" ]; then
  mkdir -p "$CAPTURE_DIR"
  /bin/cp -pr "$1/." "$CAPTURE_DIR/"
fi
rm -f "$MOUNT_STATE"
[ "${FAIL_UMOUNT:-0}" -eq 0 ]
EOF
cat > "$BIN/cp" <<'EOF'
#!/bin/sh
case "${FAIL_COPY:-0}:$*" in
  1:*tools*) exit 1 ;;
esac
printf 'cp %s\n' "$*" >> "$FLOW_LOG"
exec /bin/cp "$@"
EOF
cat > "$BIN/stat" <<'EOF'
#!/bin/sh
if [ "${FAIL_ALLOC:-0}" -eq 1 ] && [ "$2" = '%b' ] && [ "$3" = "$FAT_FILE" ]; then
  printf '0\n'
  exit 0
fi
exec /usr/bin/stat "$@"
EOF
chmod +x "$BIN"/*
: > "$TMP/flow.log"
FLOW_LOG=$TMP/flow.log LOOP_STATE=$STATE/loop MOUNT_STATE=$STATE/mount \
CAPTURE_DIR=$CAPTURE export FLOW_LOG LOOP_STATE MOUNT_STATE CAPTURE_DIR

printf 'BOOTENTRIES fixture\n' > "$SOURCE/BOOTENTRIES"
printf 'boot fixture\n' > "$SOURCE/boot.efi"
printf 'profile fixture\n' > "$SOURCE/boot.efi.gm2p"
printf 'tzmap fixture\n' > "$SOURCE/boot.efi.tzmap"
printf 'backup fixture\n' > "$SOURCE/boot_backup.efi"
printf 'backup profile fixture\n' > "$SOURCE/boot_backup.efi.gm2p"
printf 'backup tzmap fixture\n' > "$SOURCE/boot_backup.efi.tzmap"
printf 'tool fixture\n' > "$SOURCE/tools/ENTRIES"

# Given a size that is not aligned to the trailer's block contract, provision refuses it.
if run_script provision 8193 > "$TMP/alignment.out" 2>&1; then fail 'non-4096 size was accepted'; fi
assert_contains "$(cat "$TMP/alignment.out")" '4096-byte multiple' 'alignment refusal missing'
assert_not_exists "$FAT"
pass 'non-4096 size is refused before creation'

# Given a volume below the computed FAT16 cluster floor, no image is created.
if run_script provision 2097152 > "$TMP/floor.out" 2>&1; then fail 'below-floor size was accepted'; fi
assert_contains "$(cat "$TMP/floor.out")" 'FAT16 minimum' 'cluster-floor refusal missing'
assert_not_exists "$FAT"
pass 'FAT16 cluster floor is computed and enforced'

# Given too little persist space, the guard runs before the zero-fill.
if PERSIST_FREE_BYTES=1 run_script provision > "$TMP/space.out" 2>&1; then fail 'insufficient space was accepted'; fi
assert_contains "$(cat "$TMP/space.out")" 'insufficient persist free space' 'space refusal missing'
assert_not_exists "$FAT"
pass 'insufficient space leaves no file behind'

# Given enough space, the file is filled by real zero writes and checked for allocation.
run_script provision > "$TMP/provision.out"
assert_file "$FAT"
allocated=$(/usr/bin/stat -c %b "$FAT")
[ "$allocated" -ge $((SIZE / 512)) ] || fail 'zero-filled file is short-allocated'
assert_contains "$(cat "$TMP/provision.out")" 'STAMP_VALID=1' 'provision did not verify the trailer'
assert_contains "$(cat "$TMP/flow.log")" 'newfs_msdos -F 16 -c 1 -L CANOEFAT -s 4152 /dev/block/loop7' 'format did not protect trailer boundary'
assert_contains "$(cat "$TMP/flow.log")" 'mount -t vfat -o rw /dev/block/loop7' 'mount did not use loop node'
assert_contains "$(cat "$TMP/flow.log")" 'fsck_msdos -n /dev/block/loop7' 'fsck did not run on loop node'
assert_not_exists "$STATE/loop"
assert_not_exists "$STATE/mount"
assert_contains "$(cat "$CAPTURE/BOOTENTRIES")" 'BOOTENTRIES fixture' 'BOOTENTRIES was not copied'
assert_contains "$(cat "$CAPTURE/boot.efi")" 'boot fixture' 'boot.efi was not copied'
assert_contains "$(cat "$CAPTURE/boot.efi.gm2p")" 'profile fixture' 'profile was not copied'
assert_contains "$(cat "$CAPTURE/tools/ENTRIES")" 'tool fixture' 'tools tree was not copied'
pass 'device format, allocation check, FAT boundary, and tree copy'

# A mid-copy failure still unmounts, detaches, and removes the incomplete file.
rm -f "$FAT" "$STATE/loop" "$STATE/mount"
rm -rf "$CAPTURE"
if FAIL_COPY=1 run_script provision > "$TMP/copy-fail.out" 2>&1; then fail 'copy failure was accepted'; fi
assert_not_exists "$FAT"
assert_not_exists "$STATE/loop"
assert_not_exists "$STATE/mount"
assert_contains "$(cat "$TMP/flow.log")" 'umount' 'copy failure did not release mount point'
assert_contains "$(cat "$TMP/flow.log")" 'losetup -d /dev/block/loop7' 'copy failure did not release loop node'
pass 'mid-way failure cleans mount and loop resources'

# Recreate a valid file, then relocation of the resolved run invalidates the stamp.
run_script provision > /dev/null
if FIEMAP_RUN=2000:520 run_script verify > "$TMP/relocated.out"; then fail 'relocated extent run was accepted'; fi
assert_contains "$(cat "$TMP/relocated.out")" 'STAMP_MATCH=0' 'relocation mismatch was not reported'
assert_contains "$(cat "$TMP/relocated.out")" 'FAT_BOUNDARY_MATCH=1' 'boundary status was lost on mismatch'
pass 'verify reports relocated extents and preserves boundary diagnostics'

# Restamp uses the relocated runs and remains idempotent.
FIEMAP_RUN=2000:520 run_script restamp > "$TMP/restamp.out"
assert_contains "$(cat "$TMP/restamp.out")" 'STAMP_VALID=1' 'restamp did not repair relocation'
FIEMAP_RUN=2000:520 run_script verify > "$TMP/verify.out"
assert_contains "$(cat "$TMP/verify.out")" 'STAMP_MATCH=1' 'restamped file did not verify'
FIEMAP_RUN=2000:520 run_script restamp > "$TMP/restamp2.out"
assert_contains "$(cat "$TMP/restamp2.out")" 'STAMP_VALID=1' 'repeated restamp was not idempotent'
pass 'relocation mismatch and idempotent restamp are covered'

# A missing helper has a direct diagnosis when no explicit fallback is selected.
# Invoked directly rather than through run_script: a prefix assignment on a
# shell function is not reliably visible inside it, so the override has to be
# part of the command's own environment.
if EXTENT_SOURCE=fiemap PERSIST_MNT="$PERSIST" SOURCE_EFISP="$SOURCE" FAT_FILE="$FAT" \
   FAT_SIZE="$SIZE" FIEMAP_HELPER="$TMP/missing-fiemap" RUNTIME_DIR="$RUNTIME" \
   PATH="$BIN:/usr/bin:/bin" sh "$SCRIPT" verify > "$TMP/helper-missing.out" 2>&1; then
  fail 'missing helper was accepted'
fi
assert_contains "$(cat "$TMP/helper-missing.out")" 'fiemap helper missing' 'missing helper diagnosis was obscure'
pass 'missing FIEMAP helper is reported clearly'

# The allocation guard rejects a post-fill file whose allocated blocks are short.
rm -f "$FAT"
if FAIL_ALLOC=1 run_script provision > "$TMP/alloc.out" 2>&1; then fail 'short allocation was accepted'; fi
assert_contains "$(cat "$TMP/alloc.out")" 'allocation is short' 'short allocation refusal missing'
assert_not_exists "$FAT"
pass 'post-fill allocation shortfall is refused'

# The host-image argument is gone: formatting always starts from device-side zero fill.
if run_script provision "$SIZE" "$TMP/host-image" > "$TMP/image.out" 2>&1; then fail 'host image argument was accepted'; fi
assert_contains "$(cat "$TMP/image.out")" 'usage: provision [size]' 'host image dependency remains'

sh -n "$SCRIPT"
echo 'ok - FAT provisioning coverage complete'
