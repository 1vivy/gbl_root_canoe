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

# A dirty image is fail-closed unless recovery is explicitly requested. Setting
# RECOVER as well as the state bit makes e2fsck enter its journal-replay path.
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
) 9>"$locked" &
locker=$!
sleep 0.1
expect_code 6 "$BIN" inspect "$locked"
wait "$locker"

printf 'PASS: %s feature variants; read/write/rename/remove/list/mkdir round trips; dirty journal recovery; mounted and unknown-feature fail-closed checks\n' "$variants"
