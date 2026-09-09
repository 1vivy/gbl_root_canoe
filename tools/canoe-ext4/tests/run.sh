#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOL_ROOT=$(CDPATH= cd -- "$ROOT/.." && pwd)
BIN=${CANOE_EXT4_BIN:-"$TOOL_ROOT/canoe-ext4"}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-ext4-tests.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
assert_eq() {
    expected=$1
    actual=$2
    message=$3
    test "$expected" = "$actual" || fail "$message (expected $expected, got $actual)"
}
assert_contains() {
    value=$1
    needle=$2
    message=$3
    case "$value" in *"$needle"*) ;; *) fail "$message" ;; esac
}
expect_code() {
    expected=$1
    shift
    set +e
    "$@"
    actual=$?
    set -e
    assert_eq "$expected" "$actual" "exit code for $*"
}

if [ ! -x "$BIN" ]; then
    make -C "$TOOL_ROOT" canoe-ext4 >/dev/null
fi
sh "$ROOT/build_corpus.sh" >/dev/null
IMAGE_DIR="$ROOT/corpus/images"
SEED_HASH=$(sha256sum "$ROOT/corpus/seed-files/seed.txt" | awk '{print $1}')
NEW_HASH=$(printf '%s\n' 'helper-created content.' | sha256sum | awk '{print $1}')
OVERWRITE_HASH=$(printf '%s\n' 'overwritten by canoe-ext4.' | sha256sum | awk '{print $1}')

variants=0
for image in "$IMAGE_DIR"/*.img; do
    variants=$((variants + 1))
    work="$TMP/$(basename "$image")"
    cp "$image" "$work"
    inspect=$($BIN inspect "$work" --path /persist/seed.txt)
    assert_contains "$inspect" '"state":"clean"' "clean state $(basename "$image")"
    assert_contains "$inspect" '"path_exists":true' "seed existence $(basename "$image")"
    got=$($BIN read "$work" /persist/seed.txt | sha256sum | awk '{print $1}')
    assert_eq "$SEED_HASH" "$got" "seed hash $(basename "$image")"

    printf '%s\n' 'overwritten by canoe-ext4.' | $BIN write "$work" /persist/config/settings.conf >/dev/null
    got=$($BIN read "$work" /persist/config/settings.conf | sha256sum | awk '{print $1}')
    assert_eq "$OVERWRITE_HASH" "$got" "overwrite hash $(basename "$image")"

    printf '%s\n' 'helper-created content.' | $BIN --mkdir-p write "$work" \
        /persist/canoe-ext4/newdir/new.txt >/dev/null
    got=$($BIN read "$work" /persist/canoe-ext4/newdir/new.txt | sha256sum | awk '{print $1}')
    assert_eq "$NEW_HASH" "$got" "new file hash $(basename "$image")"
    listing=$($BIN list "$work" /persist/canoe-ext4/newdir)
    assert_contains "$listing" '"name":"new.txt"' "list new file $(basename "$image")"

    $BIN rename "$work" /persist/config/settings.conf /persist/config/settings.renamed >/dev/null
    renamed_inspect=$($BIN inspect "$work" --path /persist/config/settings.conf)
    assert_contains "$renamed_inspect" '"path_exists":false' \
        "rename source remains $(basename "$image")"
    $BIN remove "$work" /persist/config/delete-me >/dev/null
    $BIN remove "$work" /persist/canoe-ext4/newdir/new.txt >/dev/null
    $BIN remove "$work" /persist/canoe-ext4/newdir >/dev/null
    $BIN remove "$work" /persist/canoe-ext4 >/dev/null
    $BIN --mkdir-p mkdir "$work" /persist/canoe-ext4/empty/sub >/dev/null
    $BIN remove "$work" /persist/canoe-ext4/empty/sub >/dev/null
    $BIN remove "$work" /persist/canoe-ext4/empty >/dev/null
    $BIN remove "$work" /persist/canoe-ext4 >/dev/null
    e2fsck -fn "$work" >/dev/null 2>&1 || fail "e2fsck consistency $(basename "$image")"
done
assert_eq 20 "$variants" 'feature-variant count'

# A dirty image is fail-closed unless recovery is explicitly requested. The
# incompatibility RECOVER bit plus invalid state makes the journal replay path.
dirty="$TMP/dirty.img"
cp "$IMAGE_DIR/16m-1024b-baseline.img" "$dirty"
debugfs -w -R 'set_super_value state 0' "$dirty" >/dev/null 2>&1
debugfs -w -R 'set_super_value feature_incompat 0x2c6' "$dirty" >/dev/null 2>&1
set +e
printf '%s\n' 'should not write' | $BIN write "$dirty" /persist/config/settings.conf \
    >/dev/null 2>"$TMP/dirty-no-recovery.err"
code=$?
set -e
assert_eq 4 "$code" 'dirty image without recovery'
set +e
printf '%s\n' 'recovered write' | $BIN --recover write "$dirty" /persist/config/settings.conf \
    >/dev/null 2>"$TMP/dirty-recovery.err"
code=$?
set -e
assert_eq 0 "$code" 'dirty image with recovery'
recovery_log=$(<"$TMP/dirty-recovery.err")
assert_contains "$recovery_log" 'journal_recovery=completed' 'journal recovery marker'
assert_contains "$recovery_log" 'journal_replay=performed' 'journal replay marker'

# When a Windows cross binary is supplied, repeat the same dirty-journal
# replay case under Wine and prove the replay entry point is present in the
# executable.  The normal host-only suite intentionally does not require Wine.
WINDOWS_BIN=${CANOE_EXT4_WINDOWS_BIN:-}
if [ -n "$WINDOWS_BIN" ]; then
    if [ -z "${WINEPATH:-}" ] && [ -d /usr/x86_64-w64-mingw32/bin ]; then
        WINEPATH=/usr/x86_64-w64-mingw32/bin
        export WINEPATH
    fi
    command -v x86_64-w64-mingw32-nm >/dev/null 2>&1 ||
        fail "cross nm is required for Windows recovery verification"
    symbols=$(x86_64-w64-mingw32-nm "$WINDOWS_BIN")
    assert_contains "$symbols" 'ext2fs_run_ext3_journal' \
        'Windows journal replay symbol'
    command -v wine >/dev/null 2>&1 ||
        fail "Wine is required for Windows recovery verification"
    windows_dirty="$TMP/windows-dirty.img"
    cp "$IMAGE_DIR/16m-1024b-baseline.img" "$windows_dirty"
    debugfs -w -R 'set_super_value state 0' "$windows_dirty" >/dev/null 2>&1
    debugfs -w -R 'set_super_value feature_incompat 0x2c6' "$windows_dirty" >/dev/null 2>&1
    set +e
    printf '%s\n' 'windows recovered write' |
        wine "$WINDOWS_BIN" --recover write "$windows_dirty" /persist/config/settings.conf \
            >/dev/null 2>"$TMP/windows-dirty-recovery.err"
    code=$?
    set -e
    assert_eq 0 "$code" 'Windows dirty image with recovery'
    printf '\000\012\015\032\377binary\012' > "$TMP/windows-binary.in"
    wine "$WINDOWS_BIN" --recover write "$windows_dirty" /persist/binary.bin < "$TMP/windows-binary.in" >/dev/null
    wine "$WINDOWS_BIN" read "$windows_dirty" /persist/binary.bin > "$TMP/windows-binary.out"
    cmp "$TMP/windows-binary.in" "$TMP/windows-binary.out" || fail 'Windows binary streams changed image bytes'
    windows_recovery_log=$(<"$TMP/windows-dirty-recovery.err")
    assert_contains "$windows_recovery_log" 'journal_recovery=completed' \
        'Windows journal recovery marker'
    assert_contains "$windows_recovery_log" 'journal_replay=performed' \
        'Windows journal replay marker'
    windows_content=$(wine "$WINDOWS_BIN" read "$windows_dirty" \
        /persist/config/settings.conf | tr -d '\r')
    assert_eq 'windows recovered write' "$windows_content" \
        'Windows recovered write content'
fi

# The mountinfo override is intentionally supported only for deterministic
# tests; production reads /proc/self/mountinfo.
mounted="$TMP/mounted.img"
cp "$IMAGE_DIR/16m-1024b-baseline.img" "$mounted"
printf '1 2 0:99 / /fake rw - ext4 %s rw\n' "$mounted" > "$TMP/mountinfo"
expect_code 5 env CANOE_EXT4_MOUNTINFO="$TMP/mountinfo" "$BIN" inspect "$mounted"

unknown="$TMP/unknown.img"
cp "$IMAGE_DIR/16m-1024b-baseline.img" "$unknown"
debugfs -w -R 'set_super_value feature_incompat 0x800002c2' "$unknown" >/dev/null 2>&1
expect_code 3 "$BIN" inspect "$unknown"

# A second process holding the source lock must prevent a helper invocation.
locked="$TMP/locked.img"
cp "$IMAGE_DIR/16m-1024b-baseline.img" "$locked"
(
    flock -n 9
    sleep 3
) 9<>"$locked" &
locker=$!
sleep 0.1
set +e
"$BIN" inspect "$locked" >/dev/null 2>"$TMP/locked.err"
code=$?
set -e
assert_eq 6 "$code" 'locked source exit code'
lock_error=$(<"$TMP/locked.err")
assert_contains "$lock_error" 'errno=' 'locked source exposes OS lock errno'
wait "$locker"

# A transient media probe should finish without forcing the operator to retry.
(
    flock -n 9
    touch "$TMP/probe-ready"
    sleep 0.3
) 9<>"$locked" &
locker=$!
while [ ! -e "$TMP/probe-ready" ]; do sleep 0.01; done
"$BIN" inspect "$locked" >/dev/null
wait "$locker"

# Every regular file this helper creates must be extent-mapped, and a
# block-mapped file from an older generation must be re-mapped when it is
# overwritten. The BDS reads the boot root with Ext4Pkg's read-only driver,
# which refuses an inode without EXT4_EXTENTS_FL: a block-mapped canoe.cfg or
# boot_a.efi opens and then fails every read, which the firmware reports as a
# missing image. Only the inode layout distinguishes the two, so bytes read back
# through this helper or a kernel mount cannot stand in for this check.
inode_flags() {
    debugfs -R "stat $2" "$1" 2>/dev/null | sed -n 's/.*Flags: \(0x[0-9a-f]*\).*/\1/p' | head -1
}

# Kernel users must be able to replace and delete entries in helper-created
# directories. libext2fs itself ignores append-only flags when unlinking, so
# its successful remove round trips above do not prove this contract.
directories="$TMP/directories.img"
cp "$IMAGE_DIR/16m-1024b-baseline.img" "$directories"
"$BIN" mkdir "$directories" /direct
"$BIN" --mkdir-p mkdir "$directories" /nested/child
printf '%s\n' 'writable payload' |
    "$BIN" --mkdir-p write "$directories" /written/child/payload
for directory in /direct /nested /nested/child /written /written/child; do
    flags=$(inode_flags "$directories" "$directory")
    test -n "$flags" || fail "missing inode flags for $directory"
    assert_eq 0 "$((flags & 0x1ff))" \
        "unexpected protection/compression/legacy flags on $directory"
done

extents="$TMP/extents.img"
cp "$IMAGE_DIR/16m-1024b-baseline.img" "$extents"
printf '%s\n' 'extent mapped payload' | "$BIN" --mkdir-p write "$extents" /efisp/boot_a.efi
created_flags=$(inode_flags "$extents" /efisp/boot_a.efi)
case "$created_flags" in
    0x8*) ;;
    *) fail "created file is not extent-mapped (flags $created_flags)" ;;
esac

# The legacy shape: a file created while the volume had no extents feature,
# which the feature being enabled afterwards leaves block-mapped. Overwriting it
# must re-map it instead of preserving a file the loader cannot read.
legacy="$TMP/legacy.img"
dd if=/dev/zero of="$legacy" bs=1M count=16 status=none
mkfs.ext4 -q -F -O ^extent,^64bit -b 1024 "$legacy"
printf '%s\n' 'legacy generation' | "$BIN" write "$legacy" /canoe.cfg
legacy_before=$(inode_flags "$legacy" /canoe.cfg)
assert_eq '0x0' "$legacy_before" 'file on a no-extents volume is block-mapped'
tune2fs -O extent "$legacy" >/dev/null 2>&1 || fail 'cannot enable extents on the legacy image'
assert_eq '0x0' "$(inode_flags "$legacy" /canoe.cfg)" 'enabling extents must not re-map existing files'
printf '%s\n' 'overwritten by canoe-ext4.' | "$BIN" write "$legacy" /canoe.cfg
legacy_after=$(inode_flags "$legacy" /canoe.cfg)
case "$legacy_after" in
    0x8*) ;;
    *) fail "overwritten file stayed block-mapped (flags $legacy_after)" ;;
esac
overwritten=$("$BIN" read "$legacy" /canoe.cfg | sha256sum | awk '{print $1}')
assert_eq "$OVERWRITE_HASH" "$overwritten" 'remapped overwrite keeps its bytes'
e2fsck -fn "$legacy" >/dev/null 2>&1 || fail 'remapped image fails e2fsck'

printf 'PASS: %s feature variants; read/write/rename/remove/list/mkdir round trips; dirty journal recovery; mounted and unknown-feature fail-closed checks\n' "$variants"
