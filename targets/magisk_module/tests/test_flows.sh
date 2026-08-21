#!/bin/sh
# Fixture coverage for the Magisk/KernelSU module flow contract.
# Run from the repository root with: sh targets/magisk_module/tests/test_flows.sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-module-flow.$$
MOD="$TMP/module"
BIN="$TMP/fakebin"
BY_NAME="$TMP/by-name"
PERSIST="$TMP/persist"
EFISP="$PERSIST/efisp"
MODE_MISMATCH_USED="$TMP/mode-mismatch.used"
PAIR_TARGET=""
PAIR_SYNC_FAILED="$TMP/pair-sync-failed"
RAW_SYNC_FAILED="$TMP/raw-sync-failed"
RAW_SYNC_REACHED="$TMP/raw-sync-reached"
STATIC_SYNC_FAILED="$TMP/static-sync-failed"
DEBUG_SYNC_FAILED="$TMP/debug-sync-failed"
ABL_SYNC_FAILED="$TMP/abl-sync-failed"
LOG="$TMP/flow.log"
MODE_STATE="$TMP/mode.state"
mkdir -p "$MOD/bin" "$MOD/efisp/tools" "$BIN" "$BY_NAME" "$EFISP"
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_eq() { [ "$1" = "$2" ] || fail "$3 (got '$1', want '$2')"; }
assert_contains() { case "$1" in *"$2"*) ;; *) fail "$3" ;; esac; }

# Build a hermetic command surface. The real script still supplies all flow
# ordering and transaction logic; these commands only model Android services.
cat > "$BIN/getprop" <<'EOF'
#!/bin/sh
[ "$1" = ro.boot.slot_suffix ] && { echo _a; exit 0; }
exit 0
EOF
cat > "$BIN/blockdev" <<'EOF'
#!/bin/sh
case "$1" in
  --getsize64) echo 2097152 ;;
  --getss) echo 512 ;;
  --getro) echo "${EFISP_READONLY:-0}" ;;
  --setrw) echo "setrw $2" >> "$FLOW_LOG" ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/dd" <<'EOF'
#!/bin/sh
in= out=
for arg in "$@"; do
  case "$arg" in if=*) in=${arg#*=} ;; of=*) out=${arg#*=} ;; esac
done
printf 'dd %s -> %s\n' "$in" "$out" >> "$FLOW_LOG"
[ -n "$in" ] && [ -n "$out" ] && [ -f "$in" ] && /bin/cp "$in" "$out"
EOF
cat > "$BIN/grep" <<'EOF'
#!/bin/sh
case "$*" in *persist*) exit 0 ;; esac
exec /usr/bin/grep "$@"
EOF
cat > "$BIN/extractfv" <<'EOF'
#!/bin/sh
out=.
abl=
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o) out=$2; shift 2 ;;
    -v) abl=$2; shift 2 ;;
    *) shift ;;
  esac
done
printf 'linux-loader:%s\n' "$abl" > "$out/LinuxLoader.efi"
printf 'extractfv %s\n' "$abl" >> "$FLOW_LOG"
EOF
cat > "$BIN/patch_abl" <<'EOF'
#!/bin/sh
cat "$1" > "$2"
# GBL_VULNERABLE=1 models an ABL that still carries the GBL bug (patch
# succeeds); 0 models a fixed ABL, which is what drives the downgrade path.
if [ "${GBL_VULNERABLE:-1}" != 1 ]; then
  printf 'Warning: Failed to patch ABL GBL\n'
fi
printf 'patch_abl %s -> %s\n' "$1" "$2" >> "$FLOW_LOG"
EOF
cat > "$BIN/abl_tzmap" <<'EOF'
#!/bin/sh
cmd=$1
shift
case "$cmd" in
  derive)
    abl=$1
    shift
    out=
    while [ "$#" -gt 0 ]; do
      [ "$1" = -o ] && { out=$2; shift 2; continue; }
      shift
    done
    echo "tzmap derive $abl" >> "$FLOW_LOG"
    [ -f "$abl" ] || exit 1
    {
      printf 'tzmap-from='
      cat "$abl"
      printf '\n'
    } > "$out"
    ;;
  validate)
    input=$1
    echo "tzmap validate $input" >> "$FLOW_LOG"
    [ "${FAIL_TZMAP_VALIDATE:-0}" = 1 ] && exit 1
    [ -s "$input" ]
    ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/patch_tools" <<'EOF'
#!/bin/sh
echo "patch_tools $*" >> "$FLOW_LOG"
EOF
cat > "$BIN/cp" <<'EOF'
#!/bin/sh
case "${FAIL_PAIR_CP:-0}:$*" in
  1:*.canoe.new.gm2p*) exit 1 ;;
esac
exec /bin/cp "$@"
EOF
cat > "$BIN/mv" <<'EOF'
#!/bin/sh
if [ "${FAIL_PAIR_MV:-0}" = 1 ]; then
  case "$1:$2" in
    *.canoe.new.gm2p:*boot.efi.gm2p) exit 1 ;;
  esac
fi
exec /bin/mv "$@"
EOF
cat > "$BIN/sync" <<'EOF'
#!/bin/sh
last=$(/usr/bin/tail -n 1 "$FLOW_LOG")
if printf '%s\n' "$last" | /usr/bin/grep -q " -> .*/by-name/efisp$"; then
  if [ ! -e "$RAW_SYNC_REACHED" ]; then
    : > "$RAW_SYNC_REACHED"
    if [ "${FAIL_BDS_SYNC:-0}" = 1 ] &&
       [ ! -e "$RAW_SYNC_FAILED" ]; then
      : > "$RAW_SYNC_FAILED"
      exit 1
    fi
  fi
elif [ "${FAIL_STATIC_SYNC:-0}" = 1 ] &&
     [ -e "$RAW_SYNC_REACHED" ] &&
     [ ! -e "$STATIC_SYNC_FAILED" ]; then
  : > "$STATIC_SYNC_FAILED"
  exit 1
fi
if printf '%s\n' "$last" | /usr/bin/grep -q " -> .*/by-name/abl" &&
   [ "${FAIL_ABL_SYNC:-0}" = 1 ] &&
   [ ! -e "$ABL_SYNC_FAILED" ]; then
  : > "$ABL_SYNC_FAILED"
  exit 1
fi
if [ "${FAIL_DEBUG_SYNC:-0}" = 1 ] &&
   [ ! -e "$DEBUG_SYNC_FAILED" ]; then
  : > "$DEBUG_SYNC_FAILED"
  exit 1
fi
if [ "${FAIL_COMMIT_SYNC:-0}" = 1 ] &&
   [ -f "$PAIR_TARGET/.canoe.pair.txn" ] &&
   /usr/bin/grep -q '^CANOEP1|committed|' "$PAIR_TARGET/.canoe.pair.txn" &&
   [ ! -e "$PAIR_SYNC_FAILED" ]; then
  : > "$PAIR_SYNC_FAILED"
  exit 1
fi
exit 0
EOF

chmod +x "$BIN/cp"

cat > "$BIN/mode2_profile" <<'EOF'
#!/bin/sh
cmd=$1; shift
case "$cmd" in
  derive)
    vbmeta= out=
    while [ "$#" -gt 0 ]; do
      case "$1" in --vbmeta) vbmeta=$2; shift 2 ;; --out) out=$2; shift 2 ;; *) shift ;; esac
    done
    echo "derive $vbmeta" >> "$FLOW_LOG"
    [ "${FAIL_DERIVE:-0}" = 1 ] && exit 1
    [ -f "$vbmeta" ] || exit 1
    printf 'profile-from=%s\n' "$vbmeta" > "$out"
    ;;
  validate)
    input=
    while [ "$#" -gt 0 ]; do [ "$1" = --input ] && { input=$2; shift 2; continue; }; shift; done
    echo "validate $input" >> "$FLOW_LOG"
    [ "${FAIL_VALIDATE:-0}" = 1 ] && exit 1
    [ -s "$input" ]
    ;;
  mode-read)
    [ "${FAIL_MODE_READ:-0}" = 1 ] && exit 1
    echo "mode-read" >> "$FLOW_LOG"
    if [ "${FORCE_MODE_DEFAULTED:-0}" = 1 ]; then
      echo 'MODE=1|MODE_DEFAULTED=1'
    elif [ -f "$MODE_STATE" ]; then
      cat "$MODE_STATE"
    else
      echo 'MODE=1|MODE_DEFAULTED=1'
    fi
    ;;
  mode-write)
    mode=
    while [ "$#" -gt 0 ]; do [ "$1" = --mode ] && { mode=$2; shift 2; continue; }; shift; done
    echo "mode-write $mode" >> "$FLOW_LOG"
    [ "${FAIL_MODE_WRITE:-0}" = 1 ] && exit 1
    if [ "${FAIL_MODE_WRITE_MISMATCH:-0}" = 1 ] &&
       [ ! -e "$MODE_MISMATCH_USED" ]; then
      mode=0
      : > "$MODE_MISMATCH_USED"
    fi
    printf 'MODE=%s|MODE_DEFAULTED=0\n' "$mode" > "$MODE_STATE"
    ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/getevent" <<'EOF'
#!/bin/sh
index=0
[ -f "$KEY_STATE" ] && index=$(cat "$KEY_STATE")
case "$index" in
  0) echo KEY_VOLUMEDOWN ;;
  1|2) echo KEY_VOLUMEUP ;;
  3) [ "${FINAL_INSTALL_DOWN:-0}" = 1 ] &&
       echo KEY_VOLUMEDOWN || echo KEY_VOLUMEUP ;;
  4) [ "${REPO_CONFIRM_UP:-0}" = 1 ] &&
       echo KEY_VOLUMEUP || echo KEY_VOLUMEDOWN ;;
  *) echo KEY_VOLUMEDOWN ;;
esac
echo $((index + 1)) > "$KEY_STATE"
EOF
cat > "$BIN/ksud" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$BIN"/*
cp "$BIN/extractfv" "$BIN/patch_abl" "$BIN/patch_tools" \
  "$BIN/mode2_profile" "$BIN/abl_tzmap" "$MOD/bin/"

# Install fixture package and same-slot source partitions.
printf 'BOOTENTRIES fixture\n' > "$MOD/efisp/BOOTENTRIES"
printf 'tool\n' > "$MOD/efisp/tools/ENTRIES"
printf 'BDS fixture\n' > "$MOD/BDS.efi"
printf 'abl-a\n' > "$BY_NAME/abl_a"
printf 'abl-b\n' > "$BY_NAME/abl_b"
: > "$BY_NAME/efisp"
printf 'vbmeta-a\n' > "$BY_NAME/vbmeta_a"
printf 'vbmeta-b\n' > "$BY_NAME/vbmeta_b"
printf 'old-live\n' > "$EFISP/boot.efi"
printf 'old-live-profile\n' > "$EFISP/boot.efi.gm2p"
printf 'old-live-tzmap\n' > "$EFISP/boot.efi.tzmap"
sed -e "s#BY_NAME_DIR=\"/dev/block/by-name\"#BY_NAME_DIR=\"$BY_NAME\"#" \
    -e "s#PERSIST_MNT=\"/mnt/vendor/persist\"#PERSIST_MNT=\"$PERSIST\"#" \
    "$ROOT/targets/magisk_module/module/bin/bl_flasher.sh" > "$MOD/bin/bl_flasher.sh"
chmod +x "$MOD/bin/bl_flasher.sh"
run() {
  MODDIR="$MOD" FLOW_LOG="$LOG" MODE_STATE="$MODE_STATE" \
    MODE_MISMATCH_USED="$MODE_MISMATCH_USED" PAIR_TARGET="$EFISP" \
    PAIR_SYNC_FAILED="$PAIR_SYNC_FAILED" RAW_SYNC_FAILED="$RAW_SYNC_FAILED" \
    RAW_SYNC_REACHED="$RAW_SYNC_REACHED" STATIC_SYNC_FAILED="$STATIC_SYNC_FAILED" \
    DEBUG_SYNC_FAILED="$DEBUG_SYNC_FAILED" ABL_SYNC_FAILED="$ABL_SYNC_FAILED" \
    FAIL_DERIVE="${FAIL_DERIVE:-0}" FAIL_MODE_READ="${FAIL_MODE_READ:-0}" \
    FAIL_MODE_WRITE="${FAIL_MODE_WRITE:-0}" FAIL_MODE_WRITE_MISMATCH="${FAIL_MODE_WRITE_MISMATCH:-0}" \
    FAIL_COMMIT_SYNC="${FAIL_COMMIT_SYNC:-0}" FAIL_BDS_SYNC="${FAIL_BDS_SYNC:-0}" \
    FAIL_STATIC_SYNC="${FAIL_STATIC_SYNC:-0}" FAIL_DEBUG_SYNC="${FAIL_DEBUG_SYNC:-0}" \
    FAIL_ABL_SYNC="${FAIL_ABL_SYNC:-0}" FAIL_TZMAP_VALIDATE="${FAIL_TZMAP_VALIDATE:-0}" \
    FORCE_MODE_DEFAULTED="${FORCE_MODE_DEFAULTED:-0}" FAIL_VALIDATE="${FAIL_VALIDATE:-0}" \
    EFISP_READONLY="${EFISP_READONLY:-0}" FAIL_PAIR_CP="${FAIL_PAIR_CP:-0}" \
    FAIL_PAIR_MV="${FAIL_PAIR_MV:-0}" GBL_VULNERABLE="${GBL_VULNERABLE:-0}" \
    PATH="$BIN:$PATH" \
    sh "$MOD/bin/bl_flasher.sh" "$@"
}

CUSTOMIZE_FIXTURE="$MOD/customize-fixture.sh"
{
  cat <<'EOF'
ui_print() { :; }
set_perm_recursive() { :; }
set_perm() { :; }
abort() { echo \"ABORT=$*\" >&2; exit 1; }
EOF
  sed -e "s#BY_NAME_DIR=/dev/block/by-name#BY_NAME_DIR=$BY_NAME#" \
      -e "s#PERSIST_MNT=/mnt/vendor/persist#PERSIST_MNT=$PERSIST#" \
      "$ROOT/targets/magisk_module/module/customize.sh"
} > "$CUSTOMIZE_FIXTURE"
run_customize() {
  MODPATH="$MOD" FLOW_LOG="$LOG" MODE_STATE="$MODE_STATE" \
    MODE_MISMATCH_USED="$MODE_MISMATCH_USED" PAIR_TARGET="$EFISP" \
    PAIR_SYNC_FAILED="$PAIR_SYNC_FAILED" RAW_SYNC_FAILED="$RAW_SYNC_FAILED" \
    RAW_SYNC_REACHED="$RAW_SYNC_REACHED" STATIC_SYNC_FAILED="$STATIC_SYNC_FAILED" \
    DEBUG_SYNC_FAILED="$DEBUG_SYNC_FAILED" ABL_SYNC_FAILED="$ABL_SYNC_FAILED" \
    KEY_STATE="$TMP/key.state" FAIL_DERIVE="${FAIL_DERIVE:-0}" \
    FAIL_MODE_READ="${FAIL_MODE_READ:-0}" FAIL_MODE_WRITE="${FAIL_MODE_WRITE:-0}" \
    FAIL_MODE_WRITE_MISMATCH="${FAIL_MODE_WRITE_MISMATCH:-0}" \
    FAIL_COMMIT_SYNC="${FAIL_COMMIT_SYNC:-0}" FAIL_BDS_SYNC="${FAIL_BDS_SYNC:-0}" \
    FAIL_STATIC_SYNC="${FAIL_STATIC_SYNC:-0}" FAIL_DEBUG_SYNC="${FAIL_DEBUG_SYNC:-0}" \
    FAIL_ABL_SYNC="${FAIL_ABL_SYNC:-0}" FORCE_NO_GBL="${FORCE_NO_GBL:-0}" \
    GBL_VULNERABLE="${GBL_VULNERABLE:-1}" \
    FINAL_INSTALL_DOWN="${FINAL_INSTALL_DOWN:-0}" REPO_CONFIRM_UP="${REPO_CONFIRM_UP:-0}" \
    FAIL_PAIR_CP="${FAIL_PAIR_CP:-0}" FAIL_PAIR_MV="${FAIL_PAIR_MV:-0}" \
    PATH="$BIN:$PATH" sh "$CUSTOMIZE_FIXTURE"
}
# An unmarked pair snapshot fails closed before customize's optional patch,
# repository downgrade, paired install, or raw BDS write.
printf 'stale-customize-old\n' > "$EFISP/.canoe.old.live.efi"
: > "$LOG"
printf '0\n' > "$TMP/key.state"
customize_stale_rc=0
run_customize >/dev/null 2>&1 || customize_stale_rc=$?
[ "$customize_stale_rc" -ne 0 ] || fail "customize accepted stale pair state"
! /usr/bin/grep -q patch_tools "$LOG" || fail "customize patched before stale recovery"
! /usr/bin/grep -q '^setrw\|^dd ' "$LOG" || fail "customize wrote after stale recovery failure"
[ -e "$EFISP/.canoe.old.live.efi" ] || fail "customize consumed stale snapshot"
rm -f "$EFISP/.canoe.old.live.efi"
pass "customize stale pair state fails closed"

# The final NO choice is module/WebUI-only even when an optional vendor_boot
# patch was selected at the earlier prompt.
: > "$LOG"
printf '0\n' > "$TMP/key.state"
live_before=$(cat "$EFISP/boot.efi")
profile_before=$(cat "$EFISP/boot.efi.gm2p")
FAIL_DERIVE=1 FAIL_MODE_READ=1 FINAL_INSTALL_DOWN=1 run_customize >/dev/null
assert_eq "$(cat "$EFISP/boot.efi")" "$live_before" \
  "customize NO changed live EFI"
assert_eq "$(cat "$EFISP/boot.efi.gm2p")" "$profile_before" \
  "customize NO changed live profile"
! /usr/bin/grep -q 'patch_tools\|^setrw\|^dd ' "$LOG" ||
  fail "customize NO performed a boot-chain write"
pass "customize NO is module-only"
! /usr/bin/grep -q '^derive \|^mode-read' "$LOG" ||
  fail "customize NO performed a boot-chain preflight"



# Fresh install preflights the current-slot ABL/vbmeta and raw mode before any
# optional partition patch or live write. A profile failure leaves everything.
: > "$LOG"
printf '0\n' > "$TMP/key.state"
fresh_rc=0
FAIL_DERIVE=1 run_customize >/dev/null 2>&1 || fresh_rc=$?
[ "$fresh_rc" -ne 0 ] || fail "fresh profile failure was accepted"
! /usr/bin/grep -q patch_tools "$LOG" || fail "fresh patch_tools preceded profile preflight"
! /usr/bin/grep -q '^setrw\\|^dd ' "$LOG" || fail "fresh profile failure wrote a live target"
rm -f "$RAW_SYNC_FAILED"
: > "$LOG"
printf '0\n' > "$TMP/key.state"
sync_rc=0
FAIL_DERIVE=0 FAIL_BDS_SYNC=1 run_customize >/dev/null 2>&1 || sync_rc=$?
[ "$sync_rc" -ne 0 ] || fail "fresh install accepted failed BDS sync"
assert_eq "$(cat "$EFISP/boot.efi")" 'old-live' \
  "failed BDS sync did not restore live EFI"
assert_eq "$(cat "$EFISP/boot.efi.gm2p")" 'old-live-profile' \
  "failed BDS sync did not restore live profile"
[ ! -e "$EFISP/.canoe.pair.txn" ] || fail "failed BDS sync retained transaction marker"
! /usr/bin/grep -q '^mode-write' "$LOG" ||
  fail "failed BDS sync wrote preferred mode"
pass "fresh install BDS sync failure restores pair"

assert_eq "$(cat "$EFISP/boot.efi")" 'old-live' "fresh profile failure changed live EFI"

: > "$LOG"
printf '0\n' > "$TMP/key.state"
FAIL_DERIVE=0 run_customize >/dev/null
assert_contains "$(cat "$LOG")" "derive $BY_NAME/vbmeta_a" "fresh install used the wrong vbmeta slot"
derive_line=$(/usr/bin/grep -n '^derive ' "$LOG" | cut -d: -f1 | head -n1)
mode_line=$(/usr/bin/grep -n '^mode-read' "$LOG" | cut -d: -f1 | head -n1)
patch_line=$(/usr/bin/grep -n '^patch_tools' "$LOG" | cut -d: -f1 | head -n1)
[ "$derive_line" -lt "$patch_line" ] && [ "$mode_line" -lt "$patch_line" ] ||
  fail "fresh optional patch ran before pair/mode preflight"
assert_eq "$(cat "$EFISP/boot_backup.efi")" 'old-live' "fresh install did not rotate live EFI"
assert_eq "$(cat "$EFISP/boot_backup.efi.gm2p")" 'old-live-profile' "fresh install did not rotate live profile"
assert_eq "$(cat "$EFISP/boot_backup.efi.tzmap")" 'old-live-tzmap' "fresh install did not rotate live tzmap"
assert_file "$EFISP/boot.efi"
assert_file "$EFISP/boot.efi.gm2p"
assert_file "$EFISP/boot.efi.tzmap"
assert_contains "$(cat "$MODE_STATE")" 'MODE=1' "fresh install did not materialize default Mode 1"
pass "fresh install preflight ordering and paired install"
# A selected optional patch must not run before the user can decline a required
# ABL downgrade. Without the ordering fix, patch_tools appears before abort.
: > "$LOG"
printf '0\n' > "$TMP/key.state"
optional_live_before=$(cat "$EFISP/boot.efi")
optional_patch_rc=0
GBL_VULNERABLE=0 FORCE_NO_GBL=1 REPO_CONFIRM_UP=0 run_customize >/dev/null 2>&1 ||
  optional_patch_rc=$?
[ "$optional_patch_rc" -ne 0 ] || fail "declined ABL downgrade was accepted"
assert_eq "$(cat "$EFISP/boot.efi")" "$optional_live_before" \
  "declined ABL downgrade changed the live pair"
! /usr/bin/grep -q patch_tools "$LOG" ||
  fail "optional patch ran before declined ABL downgrade"
! /usr/bin/grep -q '^setrw\|^dd ' "$LOG" ||
  fail "declined ABL downgrade performed a live write"
pass "declined ABL downgrade leaves optional patch untouched"

# Reset the pair so OTA rotation assertions have a stable independent fixture.
printf 'old-live\n' > "$EFISP/boot.efi"
printf 'old-live-profile\n' > "$EFISP/boot.efi.gm2p"
printf 'old-live-tzmap\n' > "$EFISP/boot.efi.tzmap"
printf 'old-backup\n' > "$EFISP/boot_backup.efi"
printf 'old-backup-profile\n' > "$EFISP/boot_backup.efi.gm2p"
printf 'old-backup-tzmap\n' > "$EFISP/boot_backup.efi.tzmap"
: > "$BY_NAME/efisp"
# Pair preflight: the current-slot ABL/vbmeta pair is derived before optional
# tools; failed profile construction is terminal status 3 and does not copy
# target ABL.
: > "$LOG"
FAIL_DERIVE=1 run flash 'update-efisp,vendor_boot=1' >/dev/null 2>&1 || rc=$?
[ "${rc:-0}" -eq 3 ] || fail "profile failure did not return terminal status 3"
! /usr/bin/grep -q 'dd .*abl_b' "$LOG" || fail "target ABL copied after profile failure"
assert_eq "$(cat "$EFISP/boot.efi")" 'old-live' "profile failure changed live EFI"
! /usr/bin/grep -q patch_tools "$LOG" || fail "optional patch_tools ran before profile preflight"
pass "same-slot profile preflight and terminal status 3"
FAIL_DERIVE=0

# A hard raw-mode read failure is a preflight failure: no persist pair, BDS,
# target ABL, or optional partition may be touched.
live_before=$(cat "$EFISP/boot.efi")
profile_before=$(cat "$EFISP/boot.efi.gm2p")
: > "$LOG"
mode_rc=0
FAIL_MODE_READ=1 run flash 'update-efisp,vendor_boot=1' >/dev/null 2>&1 || mode_rc=$?
FAIL_MODE_READ=0
assert_eq "$mode_rc" 3 "mode-read failure was not terminal"
assert_eq "$(cat "$EFISP/boot.efi")" "$live_before" "mode-read failure changed live EFI"
assert_eq "$(cat "$EFISP/boot.efi.gm2p")" "$profile_before" "mode-read failure changed live profile"
! /usr/bin/grep -q '^setrw\|^dd \|patch_tools' "$LOG" ||
  fail "mode-read failure wrote a live target"
pass "OTA raw-mode preflight blocks live writes"

# Successful rotation keeps complete live/backup pairs. The target ABL is
# replaced by the current slot, so every derived artifact must use current
# slot inputs before patch_tools.
: > "$LOG"
run flash 'update-efisp,vendor_boot=1' >/dev/null
assert_contains "$(cat "$LOG")" "derive $BY_NAME/vbmeta_a" "current-slot vbmeta was not used"
assert_contains "$(cat "$LOG")" "dd $BY_NAME/abl_a -> $BY_NAME/abl_b" \
  "flashed ABL did not come from the current slot"
assert_contains "$(cat "$EFISP/boot.efi")" "$BY_NAME/abl_a" \
  "boot.efi was not derived from the flashed ABL"
assert_contains "$(cat "$EFISP/boot.efi.gm2p")" "$BY_NAME/vbmeta_a" \
  "gm2p was not derived from the flashed vbmeta"
assert_contains "$(cat "$EFISP/boot.efi.tzmap")" "$BY_NAME/abl_a" \
  "tzmap was not derived from the flashed ABL"
assert_contains "$(cat "$LOG")" patch_tools "optional patch_tools was not run"
assert_eq "$(cat "$EFISP/boot_backup.efi")" 'old-live' "live EFI was not rotated"
assert_eq "$(cat "$EFISP/boot_backup.efi.gm2p")" 'old-live-profile' "live profile was not rotated"
assert_eq "$(cat "$EFISP/boot_backup.efi.tzmap")" 'old-live-tzmap' "live tzmap was not rotated"
assert_file "$EFISP/boot.efi.gm2p"
assert_file "$EFISP/boot.efi.tzmap"
old_target=$(cat "$BY_NAME/abl_b")
: > "$LOG"
FAIL_PAIR_CP=1 run flash update-efisp >/dev/null 2>&1 || :

assert_eq "$(cat "$EFISP/boot_backup.efi")" 'old-live' "pair rollback changed backup EFI"
assert_eq "$(cat "$EFISP/boot_backup.efi.gm2p")" 'old-live-profile' "pair rollback changed backup profile"
FAIL_PAIR_CP=0
tzmap_before=$(cat "$EFISP/boot.efi.tzmap")
backup_tzmap_before=$(cat "$EFISP/boot_backup.efi.tzmap")
live_before=$(cat "$EFISP/boot.efi")
profile_before=$(cat "$EFISP/boot.efi.gm2p")
backup_before=$(cat "$EFISP/boot_backup.efi")
backup_profile_before=$(cat "$EFISP/boot_backup.efi.gm2p")
FAIL_PAIR_MV=1 run flash update-efisp >/dev/null 2>&1 || :
FAIL_PAIR_MV=0
assert_eq "$(cat "$EFISP/boot.efi")" "$live_before" "mid-transaction rollback changed live EFI"
assert_eq "$(cat "$EFISP/boot.efi.gm2p")" "$profile_before" "mid-transaction rollback changed live profile"
assert_eq "$(cat "$EFISP/boot_backup.efi")" "$backup_before" "mid-transaction rollback changed backup EFI"
assert_eq "$(cat "$EFISP/boot_backup.efi.gm2p")" "$backup_profile_before" "mid-transaction rollback changed backup profile"
assert_eq "$(cat "$EFISP/boot.efi.tzmap")" "$tzmap_before" "mid-transaction rollback changed live tzmap"
assert_eq "$(cat "$EFISP/boot_backup.efi.tzmap")" "$backup_tzmap_before" "mid-transaction rollback changed backup tzmap"
# A target-slot ABL sync failure is terminal and cannot report a completed
# cross-slot flash.
rm -f "$ABL_SYNC_FAILED"
: > "$LOG"
FAIL_ABL_SYNC=1 run flash update-efisp >/dev/null 2>&1 || :
FAIL_ABL_SYNC=0
status=$(run status)
assert_contains "$status" 'STATE=error' "ABL sync failure was reported as success"
! /usr/bin/grep -q '刷写 .*完成' "$LOG" ||
  fail "ABL sync failure emitted a flash-success marker"
run flash update-efisp >/dev/null

# A post-rename durability failure publishes a committed marker but must not
# delete snapshots or trigger rollback over the committed pair.
rm -f "$PAIR_SYNC_FAILED"
commit_sync_rc=0
FAIL_COMMIT_SYNC=1 run flash update-efisp >/dev/null 2>&1 || commit_sync_rc=$?
FAIL_COMMIT_SYNC=0
[ "$commit_sync_rc" -eq 3 ] || fail "committed-pending pair was not terminal"
assert_contains "$(cat "$EFISP/.canoe.pair.txn")" 'CANOEP1|committed|' \
  "committed-pending marker was not retained"
[ -e "$EFISP/.canoe.old.live.efi" ] || fail "committed-pending live snapshot was deleted"
[ -e "$EFISP/.canoe.old.live.gm2p" ] || fail "committed-pending profile snapshot was deleted"
[ -e "$EFISP/.canoe.old.live.tzmap" ] || fail "committed-pending tzmap snapshot was deleted"
run flash update-efisp >/dev/null
[ ! -e "$EFISP/.canoe.pair.txn" ] || fail "committed-pending marker was not recovered"
pass "committed marker durability preserves recovery snapshots"
# A committed marker with a staged sidecar completes the sidecar publish before
# a pair-free BDS update, so recovery cannot leave a mixed EFI/profile/tzmap set.
rm -f "$EFISP/boot.efi.tzmap"
printf 'staged-committed-tzmap\n' > "$EFISP/.canoe.new.tzmap"
printf 'CANOEP1|committed|1|1|0|1|1|0\n' > "$EFISP/.canoe.pair.txn"
run flash update-bds-tools >/dev/null
assert_eq "$(cat "$EFISP/boot.efi.tzmap")" 'staged-committed-tzmap' \
  "committed recovery did not publish staged tzmap"
[ ! -e "$EFISP/.canoe.new.tzmap" ] || fail "committed recovery left staged tzmap"
pass "committed staged tzmap recovery preserves pair integrity"



# A prepared marker is sufficient to recover an interrupted transaction before
# a new install is attempted. The exact pre-transaction pair is restored.
printf 'interrupted-new-live\n' > "$EFISP/boot.efi"
printf 'interrupted-new-profile\n' > "$EFISP/boot.efi.gm2p"
printf 'interrupted-new-tzmap\n' > "$EFISP/boot.efi.tzmap"
printf 'prepared-old-live\n' > "$EFISP/.canoe.old.live.efi"
printf 'prepared-old-profile\n' > "$EFISP/.canoe.old.live.gm2p"
printf 'prepared-old-tzmap\n' > "$EFISP/.canoe.old.live.tzmap"
printf 'prepared-old-backup\n' > "$EFISP/.canoe.old.backup.efi"
printf 'prepared-old-backup-profile\n' > "$EFISP/.canoe.old.backup.gm2p"
printf 'prepared-old-backup-tzmap\n' > "$EFISP/.canoe.old.backup.tzmap"
printf 'CANOEP1|prepared|1|1|1|1|1|1\n' > "$EFISP/.canoe.pair.txn"
FAIL_PAIR_CP=1 run flash update-efisp >/dev/null 2>&1 || :
FAIL_PAIR_CP=0
assert_eq "$(cat "$EFISP/boot.efi")" 'prepared-old-live' \
  "prepared recovery did not restore live EFI"
assert_eq "$(cat "$EFISP/boot.efi.gm2p")" 'prepared-old-profile' \
  "prepared recovery did not restore live profile"
assert_eq "$(cat "$EFISP/boot_backup.efi")" 'prepared-old-backup' \
  "prepared recovery did not restore backup EFI"
assert_eq "$(cat "$EFISP/boot_backup.efi.gm2p")" 'prepared-old-backup-profile' \
  "prepared recovery did not restore backup profile"
assert_eq "$(cat "$EFISP/boot.efi.tzmap")" 'prepared-old-tzmap' \
  "prepared recovery did not restore live tzmap"
assert_eq "$(cat "$EFISP/boot_backup.efi.tzmap")" 'prepared-old-backup-tzmap' \
  "prepared recovery did not restore backup tzmap"
[ ! -e "$EFISP/.canoe.pair.txn" ] || fail "prepared marker was not cleared"
[ ! -e "$EFISP/.canoe.old.live.efi" ] || fail "prepared live temp was not cleared"
pass "durable prepared transaction recovery"

# Old transaction files without a valid marker fail closed before target ABL
# copying or optional partition patching, and remain intact.
live_before=$(cat "$EFISP/boot.efi")
printf 'unmarked-old-live\n' > "$EFISP/.canoe.old.live.efi"
: > "$LOG"
run flash 'update-efisp,vendor_boot=1' >/dev/null 2>&1 || :
assert_eq "$(cat "$EFISP/.canoe.old.live.efi")" 'unmarked-old-live' \
  "unmarked old temp was deleted"
assert_eq "$(cat "$EFISP/boot.efi")" "$live_before" \
  "unmarked transaction changed live EFI"
! /usr/bin/grep -q 'dd .*abl_\|patch_tools' "$LOG" ||
  fail "unmarked transaction reached a live mutation"
rm -f "$EFISP/.canoe.old.live.efi"
pass "unmarked transaction fails closed"
# A legacy six-field marker is rejected rather than being misparsed as a pair
# whose profile and TrustZone sidecars have different ownership.
live_before=$(cat "$EFISP/boot.efi")
printf 'stale-six-field-live\n' > "$EFISP/.canoe.old.live.efi"
printf 'CANOEP1|prepared|1|1|1|1\n' > "$EFISP/.canoe.pair.txn"
stale_marker_rc=0
run flash update-efisp >/dev/null 2>&1 || stale_marker_rc=$?
[ "$stale_marker_rc" -ne 0 ] || fail "stale six-field marker was accepted"
assert_eq "$(cat "$EFISP/boot.efi")" "$live_before" \
  "stale six-field marker changed live EFI"
assert_file "$EFISP/.canoe.old.live.efi"
rm -f "$EFISP/.canoe.old.live.efi" "$EFISP/.canoe.pair.txn"
pass "stale six-field marker fails closed"


# New-only staging artifacts are restartable because no old snapshot exists.
printf 'staged-new-efi\n' > "$EFISP/.canoe.new.efi"
printf 'staged-new-profile\n' > "$EFISP/.canoe.new.gm2p"
printf 'staged-new-tzmap\n' > "$EFISP/.canoe.new.tzmap"
run flash update-efisp >/dev/null
[ ! -e "$EFISP/.canoe.new.efi" ] || fail "new-only staging EFI blocked retry"
[ ! -e "$EFISP/.canoe.new.gm2p" ] || fail "new-only staging profile blocked retry"
[ ! -e "$EFISP/.canoe.new.tzmap" ] || fail "new-only staging tzmap blocked retry"
pass "new-only staging recovery"

# A successfully read malformed/blank record is materialized as Mode 1. A raw
# mode-write failure restores the paired files and aborts before target ABL or
# optional partition work.
rm -f "$MODE_STATE"
: > "$LOG"
run flash update-efisp >/dev/null
assert_contains "$(cat "$MODE_STATE")" 'MODE=1|MODE_DEFAULTED=0' "OTA did not materialize Mode 1"
printf 'MODE=2|MODE_DEFAULTED=0\n' > "$MODE_STATE"
live_before=$(cat "$EFISP/boot.efi")
profile_before=$(cat "$EFISP/boot.efi.gm2p")
backup_before=$(cat "$EFISP/boot_backup.efi")
backup_profile_before=$(cat "$EFISP/boot_backup.efi.gm2p")
tzmap_before=$(cat "$EFISP/boot.efi.tzmap")
backup_tzmap_before=$(cat "$EFISP/boot_backup.efi.tzmap")
: > "$LOG"
mode_rc=0
FAIL_MODE_WRITE=1 run flash 'update-efisp,vendor_boot=1' >/dev/null 2>&1 || mode_rc=$?
FAIL_MODE_WRITE=0
assert_eq "$mode_rc" 3 "mode-write failure was not terminal"
assert_eq "$(cat "$EFISP/boot.efi")" "$live_before" "mode-write failure changed live EFI"
assert_eq "$(cat "$EFISP/boot.efi.gm2p")" "$profile_before" "mode-write failure changed live profile"
assert_eq "$(cat "$EFISP/boot_backup.efi")" "$backup_before" "mode-write failure changed backup EFI"
assert_eq "$(cat "$EFISP/boot_backup.efi.gm2p")" "$backup_profile_before" "mode-write failure changed backup profile"
assert_eq "$(cat "$EFISP/boot.efi.tzmap")" "$tzmap_before" "mode-write failure changed live tzmap"
assert_eq "$(cat "$EFISP/boot_backup.efi.tzmap")" "$backup_tzmap_before" "mode-write failure changed backup tzmap"
mode_reads=$(/usr/bin/grep -c '^mode-read' "$LOG" || true)
[ "$mode_reads" -ge 2 ] || fail "mode-write failure skipped mandatory reread"
! /usr/bin/grep -q 'dd .*abl_a.*abl_b\|patch_tools' "$LOG" ||
  fail "mode-write failure continued to target ABL or optional patching"
pass "OTA mode materialization and failed-write rollback"
: > "$LOG"
run flash 'debug,vendor_boot=1' >/dev/null
assert_file "$MOD/tmp/efisp/boot.efi"
assert_file "$MOD/tmp/efisp/boot.efi.gm2p"
assert_file "$MOD/tmp/efisp/boot.efi.tzmap"
# Debug efisp output is durable only after its final sync succeeds.
rm -f "$DEBUG_SYNC_FAILED"
: > "$LOG"
debug_sync_rc=0
FAIL_DEBUG_SYNC=1 run flash 'debug,vendor_boot=1' >/dev/null 2>&1 || debug_sync_rc=$?
FAIL_DEBUG_SYNC=0
status=$(run status)
assert_contains "$status" 'STATE=error' "debug accepted a failed sync"
pass "debug efisp sync failure is terminal"
! /usr/bin/grep -q '^dd ' "$LOG" || fail "debug mode performed a raw write"
# skip-efisp remains profile-free even when an optional partition is selected.
: > "$LOG"
run flash 'skip-efisp,vendor_boot=1' >/dev/null
! /usr/bin/grep -q '^derive ' "$LOG" || fail "skip-efisp built a profile"
pass "pair rotation, rollback, and debug no-write"

# BDS/tools is pair-free and must not consume even new-only staging files.
printf 'staged-bds-efi\n' > "$EFISP/.canoe.new.efi"
printf 'staged-bds-profile\n' > "$EFISP/.canoe.new.gm2p"
printf 'staged-bds-tzmap\n' > "$EFISP/.canoe.new.tzmap"
: > "$LOG"
run flash update-bds-tools >/dev/null 2>&1 || :
status=$(run status)
assert_contains "$status" 'STATE=error' "BDS accepted new-only pair staging"
! /usr/bin/grep -q '^setrw\|^dd ' "$LOG" || fail "BDS wrote with new-only pair staging"
[ -e "$EFISP/.canoe.new.efi" ] || fail "BDS consumed staged EFI"
[ -e "$EFISP/.canoe.new.gm2p" ] || fail "BDS consumed staged profile"
[ -e "$EFISP/.canoe.new.tzmap" ] || fail "BDS consumed staged tzmap"
rm -f "$EFISP/.canoe.new.efi" "$EFISP/.canoe.new.gm2p" "$EFISP/.canoe.new.tzmap"
pass "BDS new-only pair state fails closed"

# BDS/tools refuses to touch raw or static state while an unmarked pair
# transaction is present.
printf 'stale-bds-old\n' > "$EFISP/.canoe.old.live.efi"
rm -f "$EFISP/BOOTENTRIES"
: > "$LOG"
run flash update-bds-tools >/dev/null 2>&1 || :
status=$(run status)
assert_contains "$status" 'STATE=error' "BDS accepted stale pair state"
! /usr/bin/grep -q '^setrw\|^dd ' "$LOG" || fail "BDS wrote with stale pair state"
[ ! -e "$EFISP/BOOTENTRIES" ] || fail "BDS copied static tree with stale pair state"
[ -e "$EFISP/.canoe.old.live.efi" ] || fail "BDS consumed stale snapshot"
rm -f "$EFISP/.canoe.old.live.efi"
pass "BDS stale pair state fails closed"


# BDS/tools requires the boot/profile pair, but an absent optional TrustZone
# map is accepted because firmware has a built-in fallback.
rm -f "$EFISP/boot.efi.gm2p"
: > "$LOG"
run flash update-bds-tools >/dev/null 2>&1 || :
status=$(run status)
assert_contains "$status" 'STATE=error' "BDS accepted a missing Mode2 profile"
! /usr/bin/grep -q '^setrw\|^dd ' "$LOG" ||
  fail "BDS wrote raw state without a Mode2 profile"
printf 'old-live-profile\n' > "$EFISP/boot.efi.gm2p"
: > "$LOG"
FAIL_VALIDATE=1 run flash update-bds-tools >/dev/null 2>&1 || :
FAIL_VALIDATE=0
status=$(run status)
assert_contains "$status" 'STATE=error' "BDS accepted an invalid Mode2 profile"
! /usr/bin/grep -q '^setrw\|^dd ' "$LOG" ||
  fail "BDS wrote raw state with an invalid Mode2 profile"
rm -f "$EFISP/boot.efi.tzmap"
: > "$LOG"
run flash update-bds-tools >/dev/null
status=$(run status)
assert_contains "$status" 'STATE=success' "BDS rejected an absent optional tzmap"
printf 'old-live-tzmap\n' > "$EFISP/boot.efi.tzmap"
: > "$LOG"
FAIL_TZMAP_VALIDATE=1 run flash update-bds-tools >/dev/null 2>&1 || :
FAIL_TZMAP_VALIDATE=0
status=$(run status)
assert_contains "$status" 'STATE=error' "BDS accepted an invalid optional tzmap"
! /usr/bin/grep -q '^setrw\|^dd ' "$LOG" ||
  fail "BDS wrote raw state with an invalid optional tzmap"
printf 'old-live-tzmap\n' > "$EFISP/boot.efi.tzmap"
pass "BDS profile preflight and optional tzmap fallback"

old_live=$(cat "$EFISP/boot.efi")
old_profile=$(cat "$EFISP/boot.efi.gm2p")
old_tzmap=$(cat "$EFISP/boot.efi.tzmap")
abl_before=$(cat "$BY_NAME/abl_a")
: > "$LOG"
run flash update-bds-tools >/dev/null
assert_eq "$(cat "$EFISP/boot.efi")" "$old_live" "BDS tools changed live EFI"
assert_eq "$(cat "$EFISP/boot.efi.gm2p")" "$old_profile" "BDS tools changed sidecar"
assert_eq "$(cat "$EFISP/boot.efi.tzmap")" "$old_tzmap" "BDS tools changed TrustZone map"
assert_eq "$(cat "$BY_NAME/abl_a")" "$abl_before" "BDS tools touched ABL"
assert_contains "$(cat "$MODE_STATE")" 'MODE=2' "BDS tools did not preserve mode"
! /usr/bin/grep -q 'dd .*abl_' "$LOG" || fail "BDS tools wrote an ABL"
# Static efisp tree publication is also durable only after its sync succeeds.
rm -f "$STATIC_SYNC_FAILED"
: > "$LOG"
FAIL_STATIC_SYNC=1 run flash update-bds-tools >/dev/null 2>&1 || :
FAIL_STATIC_SYNC=0
status=$(run status)
assert_contains "$status" 'STATE=error' "BDS accepted a failed static-tree sync"
run flash update-bds-tools >/dev/null
pass "BDS static-tree sync failure is terminal"

FAIL_MODE_WRITE=1 run flash update-bds-tools >/dev/null 2>&1 || :
FAIL_MODE_WRITE=0
status=$(run status)
assert_contains "$status" 'STATE=error' "BDS mode-write failure was not terminal"
assert_eq "$(cat "$EFISP/boot.efi")" "$old_live" "failed BDS mode write changed EFI"
assert_eq "$(cat "$BY_NAME/abl_a")" "$abl_before" "failed BDS mode touched ABL"
assert_contains "$status" 'PREFERRED_MODE=2' "mode-write failure did not refresh actual mode"

# A successful-but-wrong write and a defaulted reread both fail before the
# static BOOTENTRIES/tools copy, while preserving the installed pair.
rm -f "$EFISP/BOOTENTRIES"
FAIL_MODE_WRITE_MISMATCH=1 run flash update-bds-tools >/dev/null 2>&1 || :
assert_contains "$(cat "$MODE_STATE")" 'MODE=2|MODE_DEFAULTED=0' \
  "mode mismatch did not restore original Mode 2"
FAIL_MODE_WRITE_MISMATCH=0
status=$(run status)
assert_contains "$status" 'STATE=error' "silent mode mismatch was accepted"
[ ! -e "$EFISP/BOOTENTRIES" ] || fail "BDS copied static tree before mode verification"
assert_eq "$(cat "$BY_NAME/abl_a")" "$abl_before" "mode mismatch touched ABL"
printf 'MODE=2|MODE_DEFAULTED=0\n' > "$MODE_STATE"

# A read that reports a defaulted mode is healed by persisting the default
# explicitly, so a stored Mode 2 is replaced by the default the read observed.
FORCE_MODE_DEFAULTED=1 run flash update-bds-tools >/dev/null 2>&1 || :
FORCE_MODE_DEFAULTED=0
assert_contains "$(cat "$MODE_STATE")" 'MODE=1|MODE_DEFAULTED=0' \
  "defaulted mode read was not healed to an explicit default"
status=$(run status)
# A missing live EFI must preserve an existing backup EFI while removing stale
# profile and TrustZone sidecars.
rm -f "$EFISP/boot.efi" "$EFISP/boot.efi.gm2p" "$EFISP/boot.efi.tzmap"
printf 'preserved-backup-efi\n' > "$EFISP/boot_backup.efi"
printf 'stale-backup-profile\n' > "$EFISP/boot_backup.efi.gm2p"
printf 'stale-backup-tzmap\n' > "$EFISP/boot_backup.efi.tzmap"
run flash update-efisp >/dev/null
assert_contains "$(cat "$MODE_STATE")" 'MODE=1|MODE_DEFAULTED=0' \
  "update-efisp must not change the preferred mode"
[ ! -e "$EFISP/boot_backup.efi.tzmap" ] || fail "stale backup tzmap was retained"
assert_eq "$(cat "$EFISP/boot_backup.efi")" 'preserved-backup-efi' \
  "backup EFI was deleted when live EFI was absent"
[ ! -e "$EFISP/boot_backup.efi.gm2p" ] || fail "stale backup sidecar was retained"
assert_file "$EFISP/boot.efi.gm2p"
pass "absent live EFI preserves backup EFI"


# start-mode follows the task protocol, status reports the actual raw mode,
# invalid input and missing Mode2 sidecars fail before mode-write.
# The preceding block healed the record to the explicit default, so re-establish
# Mode 2 for the start-mode assertions below.
printf 'MODE=2|MODE_DEFAULTED=0\n' > "$MODE_STATE"
status=$(run status)
assert_contains "$status" 'PREFERRED_MODE=2' "status omitted preferred mode"
assert_contains "$status" 'MODE_DEFAULTED=0' "status omitted mode default marker"
FAIL_MODE_READ=1 unreadable_status=$(run status)
FAIL_MODE_READ=0
assert_contains "$unreadable_status" 'PREFERRED_MODE=|MODE_DEFAULTED=|MODE_READ_ERROR=1' \
  "hard mode-read failure was reported as a valid default"
start=$(run start-mode 1)
case "$start" in STARTED=1\|TASK_ID=*|FINISHED=success\|TASK_ID=*) ;; *) fail "unexpected start-mode output: $start" ;; esac
status=$(run status)
assert_contains "$status" 'PREFERRED_MODE=1' "start-mode did not persist mode"
FAIL_MODE_WRITE=1 mode_fail=$(run start-mode 0 2>/dev/null || :)
FAIL_MODE_WRITE=0
case "$mode_fail" in STARTED=1\|TASK_ID=*|FINISHED=error\|TASK_ID=*) ;; *) fail "unexpected failed start-mode output: $mode_fail" ;; esac
status=$(run status)
assert_contains "$status" 'STATE=error' "failed mode write was not terminal"
assert_contains "$status" 'PREFERRED_MODE=1' "failed mode write did not reread actual mode"
mode_writes_before=$(/usr/bin/grep -c '^mode-write ' "$LOG" || true)
invalid=$(run start-mode 9 2>/dev/null || :)
assert_contains "$invalid" 'ERROR=invalid mode' "invalid mode was accepted"
mode_writes_after=$(/usr/bin/grep -c '^mode-write ' "$LOG" || true)
assert_eq "$mode_writes_after" "$mode_writes_before" "invalid mode performed a raw write"
invalid_writes_before=$(/usr/bin/grep -c '^setrw\|^dd ' "$LOG" || true)
invalid_action=$(run start unknown-action 2>/dev/null || :)
assert_contains "$invalid_action" 'ERROR=invalid action' "invalid flash action was accepted"
run flash unknown-action >/dev/null 2>&1 || :
invalid_writes_after=$(/usr/bin/grep -c '^setrw\|^dd ' "$LOG" || true)
assert_eq "$invalid_writes_after" "$invalid_writes_before" \
  "invalid flash action performed a live write"
read_only_writes_before=$(/usr/bin/grep -c '^mode-write ' "$LOG" || true)
chmod a-w "$BY_NAME/efisp"
read_only=$(run start-mode 0 2>/dev/null || :)
chmod u+w "$BY_NAME/efisp"
assert_contains "$read_only" 'ERROR=preferred mode preflight failed' \
  "read-only efisp passed start-mode preflight"
read_only_writes_after=$(/usr/bin/grep -c '^mode-write ' "$LOG" || true)
assert_eq "$read_only_writes_after" "$read_only_writes_before" \
  "read-only start-mode performed a raw write"

malformed_writes_before=$(/usr/bin/grep -c '^mode-write ' "$LOG" || true)
FAIL_VALIDATE=1
malformed=$(run start-mode 2 2>/dev/null || :)
FAIL_VALIDATE=0
assert_contains "$malformed" 'ERROR=preferred mode preflight failed' \
  "malformed Mode2 profile passed preflight"
malformed_writes_after=$(/usr/bin/grep -c '^mode-write ' "$LOG" || true)
assert_eq "$malformed_writes_after" "$malformed_writes_before" \
  "malformed Mode2 profile performed a raw write"
rm -f "$EFISP/boot.efi.gm2p"
missing=$(run start-mode 2 2>/dev/null || :)
assert_contains "$missing" 'ERROR_CODE=MODE2_PROFILE_MISSING' "Mode2 missing sidecar was accepted"
printf 'stale-task\n' > "$MOD/tmp/task_id"
printf 'success\n' > "$MOD/tmp/state"
printf 'stale success\n' > "$MOD/tmp/message"
missing=$(run start-mode 2 2>/dev/null || :)
assert_contains "$missing" 'ERROR_CODE=MODE2_PROFILE_MISSING' "Mode2 missing sidecar was accepted"
status=$(run status)
assert_contains "$status" 'STATE=error' "failed start-mode preflight kept success state"
assert_contains "$status" 'MODE2_PROFILE_MISSING' \
  "failed start-mode state omitted the preflight error code"
case "$status" in *TASK_ID=stale-task*) fail "failed start-mode kept stale task id" ;; esac
pass "start-mode/status protocol and Mode2 preflight"

# A live owner makes even terminal state busy; losing starts and clear-log leave
# the active task's state, task id, and log untouched. A dead owner is reclaimed.
printf 'profile\n' > "$EFISP/boot.efi.gm2p"
runtime="$MOD/tmp"
rm -rf "$runtime/flash.lock"
rm -f "$runtime/flash.pid"
mkdir -p "$runtime/flash.lock"
sleep 30 &
holder=$!
echo "$holder" > "$runtime/flash.lock/owner.pid"
echo held-task > "$runtime/flash.lock/task_id"
echo held-task > "$runtime/task_id"
echo success > "$runtime/state"
echo held-message > "$runtime/message"
echo held-log > "$runtime/flash.log"
busy=$(run start-mode 0)
assert_contains "$busy" 'ALREADY_RUNNING=1' "concurrent start-mode was accepted"
busy_status=$(run status)
assert_contains "$busy_status" "RUNNING=1|PID=$holder|STATE=success" \
  "live lock owner was hidden by terminal state"
clear_busy=$(run clear-log)
assert_contains "$clear_busy" 'BUSY=1' "clear-log ignored live task owner"
assert_eq "$(cat "$runtime/task_id")" held-task "busy start clobbered task id"
assert_eq "$(cat "$runtime/state")" success "busy start clobbered state"
assert_eq "$(cat "$runtime/flash.log")" held-log "busy start or clear clobbered log"
kill "$holder"
wait "$holder" 2>/dev/null || :
recovered=$(run start-mode 0)
case "$recovered" in STARTED=1\|TASK_ID=*|FINISHED=success\|TASK_ID=*) ;; *)
  fail "stale lock was not reclaimed: $recovered" ;;
esac
assert_contains "$(cat "$MODE_STATE")" 'MODE=0|MODE_DEFAULTED=0' \
  "recovered mode worker did not run"
pass "atomic task lock, busy state, and stale lock recovery"

echo 'all module flow fixtures passed'
