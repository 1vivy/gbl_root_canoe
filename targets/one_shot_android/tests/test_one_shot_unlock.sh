#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-one-shot.$$
TOOLKIT=$TMP/toolkit
STUBS=$TMP/stubs
BY_NAME=$TMP/by-name
PERSIST=$TMP/persist
EVENT_LOG=$TMP/events.log
REAL_SHA256SUM=$(command -v sha256sum)
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_eq() { [ "$1" = "$2" ] || fail "$3 (got '$1', want '$2')"; }

mkdir -p "$TOOLKIT/bin" "$TOOLKIT/efisp/tools" "$STUBS"
cp "$ROOT/targets/one_shot_android/resources/install-canoe.sh" "$TOOLKIT/install-canoe.sh"
printf 'new-bds\n' > "$TOOLKIT/BDS.efi"
printf 'efi-tool\n' > "$TOOLKIT/efisp/tools/BLTools.efi"

cat > "$STUBS/id" <<'EOF'
#!/bin/sh
[ "$1" = -u ] || exit 2
printf '%s\n' "${TEST_UID:-0}"
EOF
cat > "$STUBS/getprop" <<'EOF'
#!/bin/sh
[ "$1" = ro.boot.slot_suffix ] || exit 2
printf '%s\n' "${TEST_SLOT_SUFFIX:-_a}"
EOF
cat > "$TOOLKIT/bin/canoe-image" <<'EOF'
#!/bin/sh
printf 'canoe-image:%s\n' "$*" >> "$EVENT_LOG"
case "$1" in
    abl-check)
        if [ "${ABL_VULNERABLE:-1}" = 1 ]; then
            printf '{"ok":true,"result":{"sha256":"stub","gbl_patched":true}}\n'
        else
            printf '{"ok":true,"result":{"sha256":"stub","gbl_patched":false}}\n'
        fi
        ;;
    build)
        shift
        staged=
        while [ "$#" -gt 0 ]; do
            case "$1" in --staged) staged=$2; shift 2 ;; *) shift ;; esac
        done
        mkdir -p "$staged/tools"
        printf 'loader\n' > "$staged/boot.efi"
        printf 'profile\n' > "$staged/boot.efi.gm2p"
        printf 'tzmap\n' > "$staged/boot.efi.tzmap"
        printf 'tool\n' > "$staged/tools/BLTools.efi"
        ;;
    *) exit 2 ;;
esac
EOF
cat > "$TOOLKIT/bin/canoe-provision" <<'EOF'
#!/bin/sh
printf 'canoe-provision:%s\n' "$*" >> "$EVENT_LOG"
operation=$1
shift
persist=
while [ "$#" -gt 0 ]; do
    case "$1" in --persist-directory) persist=$2; shift 2 ;; *) shift ;; esac
done
case "$operation" in
    create) [ ! -e "$persist/efisp.fat" ] || exit 1; printf 'fat\n' > "$persist/efisp.fat" ;;
    remove) rm -f "$persist/efisp.fat" ;;
    *) exit 2 ;;
esac
EOF
cat > "$TOOLKIT/bin/canoe-bootmgr" <<'EOF'
#!/bin/sh
printf 'canoe-bootmgr:%s\n' "$*" >> "$EVENT_LOG"
EOF
# No patch_abl stub: the installer must never patch or rewrite the slot ABL.
cat > "$STUBS/losetup" <<'EOF'
#!/bin/sh
case "$1" in
    -f) printf '%s\n' "$TEST_LOOP" ;;
    -d)
        if [ "${DETACH_FAILURE:-0}" = 1 ]; then
            exit 1
        fi
        if [ "${AUTO_DETACH:-0}" = 1 ] && [ -f "$TEST_LOOP.unmounted" ]; then
            printf 'loop-auto-detach:%s\n' "$2" >> "$EVENT_LOG"
            exit 1
        fi
        : > "$TEST_LOOP.detached"
        printf 'loop-detach:%s\n' "$2" >> "$EVENT_LOG"
        ;;
    *)
        if [ "$#" -eq 1 ]; then
            [ "${AUTO_DETACH:-0}" != 1 ] || [ ! -f "$TEST_LOOP.unmounted" ]
        else
            printf 'loop-attach:%s:%s\n' "$1" "$2" >> "$EVENT_LOG"
        fi
        ;;
esac
EOF
cat > "$STUBS/mount" <<'EOF'
#!/bin/sh
for argument do target=$argument; done
mkdir -p "$target"
printf 'mount:%s\n' "$target" >> "$EVENT_LOG"
EOF
cat > "$STUBS/umount" <<'EOF'
#!/bin/sh
: > "$TEST_LOOP.unmounted"
if [ "${AUTO_DETACH:-0}" = 1 ]; then
    : > "$TEST_LOOP.detached"
fi
printf 'umount:%s\n' "$1" >> "$EVENT_LOG"
EOF
cat > "$STUBS/sync" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$STUBS/sha256sum" <<'EOF'
#!/bin/sh
printf 'hash:%s\n' "$1" >> "$EVENT_LOG"
exec "$REAL_SHA256SUM" "$@"
EOF
cat > "$STUBS/dd" <<'EOF'
#!/bin/sh
input=
output=
for argument do
    case "$argument" in
        if=*) input=${argument#if=} ;;
        of=*) output=${argument#of=} ;;
    esac
done
[ -n "$input" ] && [ -n "$output" ] || exit 2
case "$output" in
    "$CANOE_BY_NAME_DIR"/*)
        [ -f "$TEST_LOOP.unmounted" ] || exit 3
        [ -f "$TEST_LOOP.detached" ] || exit 3
        printf 'write:%s\n' "$output" >> "$EVENT_LOG"
        ;;
esac
cp "$input" "$output"
if [ "${CORRUPT_READBACK:-0}" = 1 ] &&
   [ "$output" = "$CURRENT_WORK_DIR/efisp.readback" ]; then
    printf 'corrupt\n' >> "$output"
fi
EOF
chmod +x "$TOOLKIT/install-canoe.sh" "$TOOLKIT/bin/"* "$STUBS/"*

reset_case() {
    rm -rf "$BY_NAME" "$PERSIST" "$TMP/work" "$TMP/loop0.detached" "$TMP/loop0.unmounted" "$TMP/out" "$TMP/err"
    mkdir -p "$BY_NAME" "$PERSIST"
    : > "$EVENT_LOG"
    printf 'active-abl-before\n' > "$BY_NAME/abl_a"
    printf 'inactive-abl-before\n' > "$BY_NAME/abl_b"
    printf 'vbmeta-before\n' > "$BY_NAME/vbmeta_a"
    printf 'efisp-before\n' > "$BY_NAME/efisp"
    cp "$BY_NAME/abl_a" "$TMP/abl.expected"
    cp "$BY_NAME/efisp" "$TMP/efisp.expected"
}
run_installer() {
    set +e
    cp "$BY_NAME/abl_b" "$TMP/abl_b.expected"
    TEST_UID=${TEST_UID:-0} ABL_VULNERABLE=${ABL_VULNERABLE:-1} \
      AUTO_DETACH=${AUTO_DETACH:-0} DETACH_FAILURE=${DETACH_FAILURE:-0} \
      CORRUPT_READBACK=${CORRUPT_READBACK:-0} TEST_SLOT_SUFFIX=_a \
      TEST_LOOP="$TMP/loop0" EVENT_LOG="$EVENT_LOG" \
      REAL_SHA256SUM="$REAL_SHA256SUM" CANOE_BY_NAME_DIR="$BY_NAME" \
      CURRENT_WORK_DIR="$TMP/work" PATH="$STUBS:$PATH" \
      sh "$TOOLKIT/install-canoe.sh" "$@" > "$TMP/out" 2> "$TMP/err"
    STATUS=$?
    set -e
}
base_args() {
    printf '%s\n' --mode 1 --persist-mount "$PERSIST" --work-dir "$TMP/work"
}

reset_case
run_installer $(base_args)
assert_eq 0 "$STATUS" 'dry run failed'
cmp "$TMP/abl.expected" "$BY_NAME/abl_a" || fail 'dry run changed active ABL'
cmp "$TMP/efisp.expected" "$BY_NAME/efisp" || fail 'dry run changed efisp'
[ ! -e "$TMP/work" ] || fail 'dry run created the work directory'
case "$(cat "$EVENT_LOG")" in *write:*) fail 'dry run attempted a raw write' ;; esac
pass 'dry run writes nothing'

reset_case
run_installer --persist-mount "$PERSIST" --work-dir "$TMP/work"
[ "$STATUS" -ne 0 ] || fail 'missing mode was accepted'
case "$(cat "$EVENT_LOG")" in *write:*) fail 'missing mode reached a raw write' ;; esac
pass 'missing mode is refused'

reset_case
ABL_VULNERABLE=0 run_installer $(base_args)
assert_eq 0 "$STATUS" 'stock active-slot ABL blocked the plan'
case "$(cat "$TMP/out")" in *'Boot prerequisite:'*'signed vulnerable ABL separately'*) ;; *) fail 'stock-ABL boot prerequisite was not disclosed' ;; esac
[ ! -e "$TMP/work" ] || fail 'stock-ABL plan created the work directory'
case "$(cat "$EVENT_LOG")" in *write:*) fail 'stock-ABL plan reached a raw write' ;; esac
pass 'stock active-slot ABL can supply the plan without a raw write'

reset_case
TEST_UID=2000 run_installer $(base_args)
[ "$STATUS" -ne 0 ] || fail 'non-root execution was accepted'
case "$(cat "$EVENT_LOG")" in *write:*) fail 'non-root execution reached a raw write' ;; esac
pass 'root absence is refused'

reset_case
CORRUPT_READBACK=1 run_installer $(base_args) --apply
[ "$STATUS" -ne 0 ] || fail 'corrupt efisp readback was accepted'
case "$(cat "$TMP/err")" in *'no prior efisp image was saved'*) ;; *) fail 'raw-write failure lacked recovery warning' ;; esac
cmp "$TMP/abl.expected" "$BY_NAME/abl_a" || fail 'readback failure changed active ABL'
pass 'readback mismatch reports no built-in prior-state rollback'

reset_case
AUTO_DETACH=1 run_installer $(base_args) --apply
assert_eq 0 "$STATUS" 'auto-released loop prevented completion'
assert_file "$TMP/work/efisp.readback"
cmp "$TOOLKIT/BDS.efi" "$BY_NAME/efisp" || fail 'auto-released loop prevented the efisp write'
case "$(cat "$EVENT_LOG")" in *'loop-auto-detach:'*) ;; *) fail 'auto-release path was not exercised' ;; esac
pass 'auto-released loop allows complete installation and efisp readback'

reset_case
DETACH_FAILURE=1 run_installer $(base_args) --apply
[ "$STATUS" -ne 0 ] || fail 'attached loop detach failure was accepted'
assert_file "$PERSIST/efisp.fat"
cmp "$TMP/efisp.expected" "$BY_NAME/efisp" || fail 'detach failure reached raw efisp write'
pass 'bound loop detach failure preserves container and leaves efisp untouched'

reset_case
ABL_VULNERABLE=0 run_installer $(base_args) --apply
assert_eq 0 "$STATUS" 'stock active-slot ABL installation failed'
assert_file "$TMP/work/efisp.readback"
[ ! -e "$TMP/work/efisp.before.img" ] || fail 'an old efisp image was saved'
[ ! -e "$TMP/work/abl_a.before.img" ] || fail 'an ABL rollback copy was saved'
printf '%s\n' "$BY_NAME/efisp" > "$TMP/writes.expected"
sed -n 's/^write://p' "$EVENT_LOG" > "$TMP/writes.actual"
cmp "$TMP/writes.expected" "$TMP/writes.actual" || fail 'efisp was not the only raw write'
cmp "$TOOLKIT/BDS.efi" "$BY_NAME/efisp" || fail 'efisp did not receive BDS'
cmp "$TMP/abl.expected" "$BY_NAME/abl_a" ||
    fail 'the active-slot ABL was written; a patched ABL is unsigned and XBL rejects it, costing an EDL recovery'
cmp "$TMP/abl_b.expected" "$BY_NAME/abl_b" || fail 'the inactive ABL changed'
pass 'stock-ABL preparation writes efisp once with readback, leaving both ABLs untouched'

echo 'all one-shot unlock fixtures passed'
