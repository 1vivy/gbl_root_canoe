#!/bin/sh
# Focused device-module flow coverage for the 7.x install contract.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-module-flow.$$
MOD="$TMP/module"
BIN="$TMP/bin"
BY_NAME="$TMP/by-name"
PERSIST="$TMP/persist"
EFISP="$PERSIST/efisp"
SUPPLIED="$TMP/supplied"
LOG="$MOD/tmp/flash.log"
QUESTION_LOG="$TMP/question.log"
if [ "${KEEP_TMP:-0}" = 1 ]; then
  trap ':' EXIT INT TERM HUP
else
  trap 'rm -rf "$TMP"' EXIT INT TERM HUP
fi

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_no_entry() {
  if printf '%s\n' "$1" | /usr/bin/grep -q "^entry $2$"; then
    fail "$3"
  fi
}
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_eq() { [ "$1" = "$2" ] || fail "$3 (got '$1', want '$2')"; }
assert_contains() { case "$1" in *"$2"*) ;; *) fail "$3" ;; esac; }
assert_not_contains() { case "$1" in *"$2"*) fail "$3" ;; esac; }

mkdir -p "$MOD/bin" "$MOD/efisp/tools" "$BIN" "$BY_NAME" "$EFISP" "$SUPPLIED"
mkdir -p "$ROOT/tools/canoe-bootmgr/target"
cargo build --quiet --locked --manifest-path "$ROOT/tools/canoe-bootmgr/Cargo.toml"
cp "$ROOT/tools/canoe-bootmgr/target/debug/canoe-bootmgr" "$MOD/bin/canoe-bootmgr"
chmod +x "$MOD/bin/canoe-bootmgr"
cp "$ROOT/targets/magisk_module/module/bin/canoe_vendor_boot.sh" "$MOD/bin/canoe_vendor_boot.sh"
chmod +x "$MOD/bin/canoe_vendor_boot.sh"
printf 'BDS fixture\n' > "$MOD/BDS.efi"
printf 'tool\n' > "$MOD/efisp/tools/BLTools.efi"

make_profile() {
  profile_path=$1
  profile_name=$2
  awk -v value="profile-$profile_name" \
    'BEGIN { printf "%-56s%-32s%-32s", substr(value, 1, 56), "fixture-signer", "fixture-tail" }' \
    > "$profile_path"
}
make_tzmap() {
  awk -v value="tzmap-$1" 'BEGIN { printf "%-256s", value }' > "$2"
}

printf 'abl-a-v1-vulnerable\n' > "$BY_NAME/abl_a"
printf 'abl-b-v1-vulnerable\n' > "$BY_NAME/abl_b"
printf 'vbmeta-a\n' > "$BY_NAME/vbmeta_a"
printf 'vbmeta-b\n' > "$BY_NAME/vbmeta_b"
printf 'old-live\n' > "$EFISP/boot.efi"
make_profile "$EFISP/boot.efi.gm2p" old-live
make_tzmap old-live "$EFISP/boot.efi.tzmap"
printf 'old-backup\n' > "$EFISP/boot_backup.efi"
make_profile "$EFISP/boot_backup.efi.gm2p" old-backup
make_tzmap old-backup "$EFISP/boot_backup.efi.tzmap"
truncate -s 2097152 "$BY_NAME/efisp"

# Plant a stale passthrough row and a hand-added row; the transaction must
# migrate only the former while preserving the latter.
"$MOD/bin/canoe-bootmgr" --boot-root "$EFISP" entry set \
  --id android-b --title 'Android (slot B)' --image boot_b.efi \
  --role inactive --mode 2 >/dev/null
"$MOD/bin/canoe-bootmgr" --boot-root "$EFISP" entry set \
  --id lineage --title 'Lineage custom' --image lineage.efi \
  --role other --mode 1 >/dev/null
printf 'stale-b\n' > "$EFISP/boot_b.efi"
make_profile "$EFISP/boot_b.efi.gm2p" stale-b
make_tzmap stale-b "$EFISP/boot_b.efi.tzmap"

cat > "$BIN/getprop" <<'EOF'
#!/bin/sh
case "$1" in
  ro.boot.slot_suffix) echo "${SLOT_SUFFIX:-_a}" ;;
  ro.product.name) echo test-device ;;
  ro.product.model) echo Test-Model ;;
  ro.board.platform) echo sm8850 ;;
esac
EOF
cat > "$BIN/bootctl" <<'EOF'
#!/bin/sh
echo "${BOOTCTL_SLOT:-1}"
EOF
cat > "$BIN/blockdev" <<'EOF'
#!/bin/sh
case "$1" in
  --getsize64) echo 2097152 ;;
  --getss) echo 512 ;;
  --setrw) exit 0 ;;
  --getro) echo 0 ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/grep" <<'EOF'
#!/bin/sh
case "$*" in
  *persist*) exit 0 ;;
  *) exec /usr/bin/grep "$@" ;;
esac
EOF
cat > "$BIN/dd" <<'EOF'
#!/bin/sh
in= out=
for arg in "$@"; do
  case "$arg" in if=*) in=${arg#*=} ;; of=*) out=${arg#*=} ;; esac
done
printf 'dd %s -> %s\n' "$in" "$out" >> "${FLOW_LOG:?}"
if [ "$out" = "${BY_NAME_DIR:-}/efisp" ] && [ -f "$out" ]; then
  old_size=$(stat -c '%s' "$out")
  temp_out="$out.dd.$$"
  /usr/bin/dd "$@" of="$temp_out" >/dev/null 2>&1
  /usr/bin/dd if="$temp_out" of="$out" conv=notrunc >/dev/null 2>&1
  truncate -s "$old_size" "$out"
  rm -f "$temp_out"
else
  exec /usr/bin/dd "$@"
fi
EOF
cat > "$BIN/sync" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$BIN/extractfv" <<'EOF'
#!/bin/sh
out=.
abl=
while [ "$#" -gt 0 ]; do
  case "$1" in -o) out=$2; shift 2 ;; -v) abl=$2; shift 2 ;; *) shift ;; esac
done
printf 'extractfv source=%s\n' "$abl" >> "${FLOW_LOG:?}"
printf 'loader-from=%s\n' "$abl" > "$out/LinuxLoader.efi"
cat "$abl" >> "$out/LinuxLoader.efi"
EOF
cat > "$BIN/patch_abl" <<'EOF'
#!/bin/sh
printf 'patch_abl source=%s\n' "$1" >> "${FLOW_LOG:?}"
if /usr/bin/grep -q nonvulnerable "$1"; then
  echo 'Warning: Failed to patch ABL GBL'
fi
/bin/cp "$1" "$2"
EOF
cat > "$BIN/mode2_profile" <<'EOF'
#!/bin/sh
cmd=$1; shift
case "$cmd" in
  derive)
    vbmeta= out=
    while [ "$#" -gt 0 ]; do
      case "$1" in --vbmeta) vbmeta=$2; shift 2 ;; --out) out=$2; shift 2 ;; *) shift ;; esac
done
    printf 'mode2_profile source=%s\n' "$vbmeta" >> "${FLOW_LOG:?}"
    value="profile-from=$(cat "$vbmeta")"
    signer=fixture-signer
    if /usr/bin/grep -q supplied "$vbmeta"; then signer=supplied-signer
    elif /usr/bin/grep -q custom "$vbmeta"; then signer=custom-signer
    fi
    awk -v value="$value" -v signer="$signer" \
      'BEGIN { printf "%-56s%-32s%-32s", substr(value, 1, 56), signer, "fixture-tail" }' \
      > "$out"
    ;;
  validate)
    input=
    while [ "$#" -gt 0 ]; do
      case "$1" in --input) input=$2; shift 2 ;; *) shift ;; esac
done
    [ "$(wc -c < "$input" | tr -d '[:space:]')" = 120 ]
    ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/abl_tzmap" <<'EOF'
#!/bin/sh
cmd=$1; shift
case "$cmd" in
  derive)
    abl=$1; shift; out=
    while [ "$#" -gt 0 ]; do
      case "$1" in -o) out=$2; shift 2 ;; *) shift ;; esac
done
    printf 'abl_tzmap source=%s\n' "$abl" >> "${FLOW_LOG:?}"
    awk 'BEGIN { printf "%-256s", "fixture-tzmap" }' > "$out"
    ;;
  validate)
    [ "$(wc -c < "$1" | tr -d '[:space:]')" = 256 ]
    ;;
  verify)
    sidecar= abl=
    while [ "$#" -gt 0 ]; do
      case "$1" in --sidecar) sidecar=$2; shift 2 ;; --abl) abl=$2; shift 2 ;; *) shift ;; esac
done
    [ -s "$sidecar" ] && [ -s "$abl" ]
    ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$BIN"/*
cp "$BIN/extractfv" "$BIN/patch_abl" "$BIN/mode2_profile" "$BIN/abl_tzmap" "$MOD/bin/"

run() {
  MODDIR="$MOD" BY_NAME_DIR="$BY_NAME" PERSIST_MNT="$PERSIST" EFISP_DIR="$EFISP" \
    RUNTIME_DIR="$MOD/tmp" LOG_FILE="$LOG" SUPPLIED_DIR="$SUPPLIED" \
    FLOW_LOG="$LOG" PATH="$BIN:$PATH" \
    sh "$MOD/bin/bl_flasher.sh" "$@"
}
cp "$ROOT/targets/magisk_module/module/bin/bl_flasher.sh" "$MOD/bin/bl_flasher.sh"
chmod +x "$MOD/bin/bl_flasher.sh"

run flash update-efisp > "$TMP/initial.out"
cfg=$(cat "$EFISP/canoe.cfg")
assert_contains "$cfg" 'entry android-b' 'target-slot entry missing'
assert_contains "$cfg" 'role active' 'target-slot role missing'
assert_contains "$cfg" 'entry android-backup' 'backup entry missing'
assert_contains "$cfg" 'image boot_backup.efi' 'backup image missing'
assert_contains "$cfg" 'entry lineage' 'hand-added row was not preserved'
assert_no_entry "$cfg" android-a 'legacy active row survived migration'
[ ! -e "$EFISP/boot.efi" ] || fail 'legacy loader survived migration'
assert_file "$EFISP/boot_b.efi"
assert_file "$EFISP/boot_b.efi.gm2p"
assert_file "$EFISP/boot_b.efi.tzmap"
pass 'module pair install writes target and backup rows while preserving custom state'


: > "$LOG"
printf 'target-vulnerable\n' > "$BY_NAME/abl_b"
run flash update-efisp >/dev/null
log=$(cat "$LOG")
assert_contains "$log" 'canoe: abl source=partition vbmeta source=partition' \
  'partition provenance was not logged'
assert_contains "$log" "extractfv source=$BY_NAME/abl_b" \
  'default derivation did not read target ABL partition'
assert_contains "$log" "mode2_profile source=$BY_NAME/vbmeta_b" \
  'default derivation did not read target vbmeta partition'
assert_not_contains "$log" 'source=supplied' 'default derivation unexpectedly used supplied images'
pass 'default image provenance uses the target partitions'

printf 'supplied-nonvulnerable\n' > "$SUPPLIED/abl.img"
printf 'supplied-vbmeta\n' > "$SUPPLIED/vbmeta.img"
: > "$LOG"
run flash update-efisp,abl=supplied,vbmeta=supplied >/dev/null
log=$(cat "$LOG")
assert_contains "$log" 'canoe: abl source=supplied vbmeta source=supplied' \
  'supplied provenance was not logged'
assert_contains "$log" "extractfv source=$SUPPLIED/abl.img" \
  'supplied ABL was not passed to extractfv'
assert_contains "$log" "mode2_profile source=$SUPPLIED/vbmeta.img" \
  'supplied vbmeta was not passed to mode2_profile'
assert_contains "$log" "dd $BY_NAME/abl_a -> $BY_NAME/abl_b" \
  'ABL flash did not source the current partition'
pass 'supplied derivation images are accepted while ABL flashing remains partition-to-partition'

cp "$BY_NAME/abl_b" "$TMP/abl-b-before-empty"
cp "$EFISP/canoe.cfg" "$TMP/cfg-before-empty"
: > "$SUPPLIED/abl.img"
: > "$LOG"
if run flash update-efisp,abl=supplied >/dev/null 2>&1; then
  fail 'empty supplied ABL was accepted'
fi
cmp "$TMP/abl-b-before-empty" "$BY_NAME/abl_b" || fail 'empty supplied ABL changed a partition'
cmp "$TMP/cfg-before-empty" "$EFISP/canoe.cfg" || fail 'empty supplied ABL changed canoe.cfg'
assert_not_contains "$(cat "$LOG")" "dd $BY_NAME/abl_a -> $BY_NAME/abl_b" \
  'empty supplied ABL reached the flash step'
pass 'empty supplied images are refused before any write'
printf 'supplied-nonvulnerable\n' > "$SUPPLIED/abl.img"

cat > "$BIN/bootctl" <<'EOF'
#!/bin/sh
echo 1
EOF
chmod +x "$BIN/bootctl"
printf 'target-nonvulnerable\n' > "$BY_NAME/abl_b"
: > "$LOG"
run flash update-efisp >/dev/null
cfg=$(cat "$EFISP/canoe.cfg")
assert_contains "$cfg" 'entry android-b' 'next-slot probe did not label the target slot'
assert_not_contains "$(cat "$LOG")" 'next-slot probe unavailable' \
  'available next-slot probe was reported unavailable'
pass 'bootctl next-slot probe labels the next active row'
assert_contains "$(cat "$LOG")" "extractfv source=$BY_NAME/abl_b" \
  'GBL probe did not inspect the target ABL'
assert_not_contains "$(cat "$LOG")" "mode2_profile source=$BY_NAME/vbmeta_b" \
  'GBL probe unexpectedly derived a target profile'
[ ! -e "$MOD/tmp/boot.efi" ] || fail 'GBL probe left a staged loader'
[ ! -e "$MOD/tmp/boot.efi.gm2p" ] || fail 'GBL probe left a profile sidecar'
[ ! -e "$MOD/tmp/boot.efi.tzmap" ] || fail 'GBL probe left a tzmap sidecar'
pass 'GBL probe uses only the target ABL and leaves no staged outputs'

rm -f "$BIN/bootctl"
printf 'target-nonvulnerable\n' > "$BY_NAME/abl_b"
: > "$LOG"
if run flash update-efisp >/dev/null 2>&1; then
  fail 'missing bootctl metadata was accepted'
fi
assert_contains "$(cat "$LOG")" 'bootctl/GPT' \
  'missing bootctl refusal was not logged'
pass 'missing bootctl metadata refuses a running-slot relabel'

cat > "$BIN/bootctl" <<'EOF'
#!/bin/sh
echo 1
EOF
chmod +x "$BIN/bootctl"

printf 'target-nonvulnerable\n' > "$BY_NAME/abl_b"
: > "$LOG"
run flash update-efisp >/dev/null
assert_contains "$(cat "$LOG")" "dd $BY_NAME/abl_a -> $BY_NAME/abl_b" \
  'non-vulnerable target did not copy the current ABL'
pass 'non-vulnerable target ABL is replaced from the current partition'

printf 'target-vulnerable\n' > "$BY_NAME/abl_b"
: > "$LOG"
run flash update-efisp >/dev/null
assert_not_contains "$(cat "$LOG")" "dd $BY_NAME/abl_a -> $BY_NAME/abl_b" \
  'vulnerable target ABL was unnecessarily overwritten'
pass 'vulnerable target ABL is not overwritten'

printf 'custom-vbmeta\n' > "$BY_NAME/vbmeta_b"
: > "$LOG"
run flash update-efisp >/dev/null
cfg=$(cat "$EFISP/canoe.cfg")
active_block=$(awk '/^entry android-b$/{seen=1} seen{print} /^$/{if(seen) exit}' "$EFISP/canoe.cfg")
assert_contains "$(cat "$LOG")" '"signer_changed":true' \
  'partition signer change was not reported'
assert_contains "$(cat "$LOG")" 'Mode 2' \
  'Mode 2 was not downgraded after a partition signer change'
assert_contains "$active_block" 'mode 1' \
  'partition signer change left the active row in Mode 2'
pass 'partition signer changes are reported and Mode 2 is downgraded'

"$MOD/bin/canoe-bootmgr" --boot-root "$EFISP" entry mode --id android-b --mode 2 >/dev/null
: > "$LOG"
run flash update-efisp,abl=supplied,vbmeta=supplied >/dev/null
assert_contains "$(cat "$LOG")" '"signer_changed":true' \
  'supplied signer change was not reported'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'entry android-b' \
  'supplied signer change removed target entry'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'mode 2' \
  'supplied signer change unexpectedly downgraded Mode 2'
pass 'supplied signer changes proceed without a Mode 2 downgrade'
printf 'vbmeta-b\n' > "$BY_NAME/vbmeta_b"
printf 'supplied-vbmeta\n' > "$SUPPLIED/vbmeta.img"

# Synthetic vendor_boot partition: only the 40-byte token append may change.
VENDOR="$BY_NAME/vendor_boot_a"
VENDOR_BEFORE="$TMP/vendor-before.img"
truncate -s 4096 "$VENDOR"
printf VNDRBOOT | dd of="$VENDOR" bs=1 seek=0 conv=notrunc 2>/dev/null
printf 'console=ttyS0' | dd of="$VENDOR" bs=1 seek=28 conv=notrunc 2>/dev/null
cp "$VENDOR" "$VENDOR_BEFORE"
patch_output=$(BY_NAME_DIR="$BY_NAME" RUNTIME_DIR="$MOD/tmp" FLOW_LOG="$LOG" \
  PATH="$BIN:$PATH" sh "$MOD/bin/canoe_vendor_boot.sh" a)
assert_eq "$patch_output" patched 'vendor_boot patch did not report a change'
diff_count=$(cmp -l "$VENDOR_BEFORE" "$VENDOR" | wc -l | tr -d '[:space:]')
assert_eq "$diff_count" 40 'vendor_boot patch changed the wrong number of bytes'
if cmp -l "$VENDOR_BEFORE" "$VENDOR" | awk '$1 - 1 < 28 || $1 - 1 > 2075 { bad=1 } END { exit bad }'; then :; else
  fail 'vendor_boot patch changed bytes outside the cmdline field'
fi
cp "$VENDOR" "$TMP/vendor-after-first.img"
patch_output=$(BY_NAME_DIR="$BY_NAME" RUNTIME_DIR="$MOD/tmp" FLOW_LOG="$LOG" \
  PATH="$BIN:$PATH" sh "$MOD/bin/canoe_vendor_boot.sh" a)
assert_eq "$patch_output" 'already patched' 'vendor_boot patch was not idempotent'
cmp "$TMP/vendor-after-first.img" "$VENDOR" || fail 'second vendor_boot patch changed the image'
pass 'vendor_boot patch appends exactly 40 bytes and is idempotent'

VENDOR_BAD="$BY_NAME/vendor_boot_b"
truncate -s 4096 "$VENDOR_BAD"
printf BADMAGIC | dd of="$VENDOR_BAD" bs=1 seek=0 conv=notrunc 2>/dev/null
cp "$VENDOR_BAD" "$TMP/vendor-bad-before.img"
if BY_NAME_DIR="$BY_NAME" RUNTIME_DIR="$MOD/tmp" FLOW_LOG="$LOG" PATH="$BIN:$PATH" \
  sh "$MOD/bin/canoe_vendor_boot.sh" b >/dev/null 2>&1; then
  fail 'bad vendor_boot magic was accepted'
fi
cmp "$TMP/vendor-bad-before.img" "$VENDOR_BAD" || fail 'bad magic changed vendor_boot'

truncate -s 4096 "$VENDOR_BAD"
printf VNDRBOOT | dd of="$VENDOR_BAD" bs=1 seek=0 conv=notrunc 2>/dev/null
awk 'BEGIN { for (i = 0; i < 2048; i++) printf "x" }' > "$TMP/full-field"
dd if="$TMP/full-field" of="$VENDOR_BAD" bs=1 seek=28 conv=notrunc 2>/dev/null
cp "$VENDOR_BAD" "$TMP/vendor-full-before.img"
if BY_NAME_DIR="$BY_NAME" RUNTIME_DIR="$MOD/tmp" FLOW_LOG="$LOG" PATH="$BIN:$PATH" \
  sh "$MOD/bin/canoe_vendor_boot.sh" b >/dev/null 2>&1; then
  fail 'overfull vendor_boot cmdline was accepted'
fi
cmp "$TMP/vendor-full-before.img" "$VENDOR_BAD" || fail 'overfull cmdline changed vendor_boot'
pass 'vendor_boot patch refuses invalid magic and a full cmdline without writing'

run_questionnaire() {
  question_name=$1
  question_keys=$2
  question_mod="$TMP/$question_name-module"
  question_bin="$TMP/$question_name-bin"
  question_persist="$TMP/$question_name-persist"
  question_efisp="$question_persist/efisp"
  question_log="$TMP/$question_name.log"
  question_count="$TMP/$question_name.count"
  question_supplied_dir=${3:-}
  mkdir -p "$question_mod/bin" "$question_mod/efisp/tools" \
    "$question_bin" "$question_efisp"
  cp "$MOD/bin/canoe-bootmgr" "$question_mod/bin/canoe-bootmgr"
  cp "$ROOT/targets/magisk_module/module/customize.sh" "$question_mod/customize.sh"
  cp "$ROOT/targets/magisk_module/module/bin/canoe_vendor_boot.sh" \
    "$question_mod/bin/canoe_vendor_boot.sh"
  cp "$BIN/extractfv" "$BIN/patch_abl" "$BIN/mode2_profile" "$BIN/abl_tzmap" \
    "$question_mod/bin/"
  cp "$MOD/BDS.efi" "$question_mod/BDS.efi"
  cp -r "$MOD/efisp/tools/." "$question_mod/efisp/tools/"
  cat > "$question_bin/getevent" <<'EOF'
#!/bin/sh
number=$(cat "${QUESTION_COUNT:?}" 2>/dev/null || echo 0)
printf '%s\n' $((number + 1)) > "$QUESTION_COUNT"
key=$(printf '%s' "${QUESTION_KEYS:?}" | cut -c "$((number + 1))")
case "$key" in
  U) echo KEY_VOLUMEUP ;;
  D) echo KEY_VOLUMEDOWN ;;
esac
EOF
  cat > "$question_bin/ksud" <<'EOF'
#!/bin/sh
exit 0
EOF
  cat > "$question_bin/reboot" <<'EOF'
#!/bin/sh
exit 0
EOF
  cat > "$question_mod/question-wrapper.sh" <<'EOF'
#!/bin/sh
ui_print() { printf '%s\n' "$*" >> "${QUESTION_LOG:?}"; }
abort() { printf 'ABORT: %s\n' "$*" >> "${QUESTION_LOG:?}"; exit 1; }
set_perm_recursive() { :; }
set_perm() { :; }
EOF
  cat "$question_mod/customize.sh" >> "$question_mod/question-wrapper.sh"
  chmod +x "$question_bin/"* "$question_mod/question-wrapper.sh" "$question_mod/customize.sh" \
    "$question_mod/bin/"*
  : > "$question_count"
  : > "$question_log"
  QUESTION_COUNT="$question_count" QUESTION_KEYS="$question_keys" \
    QUESTION_LOG="$question_log" FLOW_LOG="$question_log" \
    MODPATH="$question_mod" BY_NAME_DIR="$BY_NAME" \
    PERSIST_MNT="$question_persist" EFISP_DIR="$question_efisp" \
    RUNTIME_DIR="$question_mod/tmp" PATH="$question_bin:$BIN:$PATH" \
    sh "$question_mod/question-wrapper.sh" > "$TMP/$question_name.stdout" 2>&1
}

run_questionnaire questionnaire-mode2 DUUD
assert_contains "$(cat "$TMP/questionnaire-mode2-persist/efisp/canoe.cfg")" 'mode 2' \
  'Mode 2 questionnaire selection did not reach the device transaction'
assert_contains "$(cat "$TMP/questionnaire-mode2.log")" \
  'Select boot mode: Vol+ = keep Mode 1, Vol- = Mode 2' \
  'Mode 2 second-stage question was not shown'
assert_not_contains "$(cat "$TMP/questionnaire-mode2.log")" \
  'Mode 1: patch vendor_boot? Vol+ = yes, Vol- = no' 'Mode 2 unexpectedly asked the vendor_boot question'
assert_contains "$(cat "$TMP/questionnaire-mode2.log")" \
  'Data format is required. On a first-time installation it is not optional' 'format-data explanation was not printed'
pass 'questionnaire reaches Mode 2 and skips Mode 1-only questions'

run_questionnaire questionnaire-mode1 DUUUUU
assert_contains "$(cat "$TMP/questionnaire-mode1-persist/efisp/canoe.cfg")" 'mode 1' \
  'Mode 1 questionnaire selection did not reach the device transaction'
assert_contains "$(cat "$TMP/questionnaire-mode1.log")" \
  'Mode 1: patch vendor_boot? Vol+ = yes, Vol- = no' \
  'Mode 1 vendor_boot question was not shown'
assert_contains "$(cat "$TMP/questionnaire-mode1.stdout")" patched \
  'Mode 1 vendor_boot selection did not invoke the patcher'
pass 'questionnaire reaches Mode 1 and enables the vendor_boot patch'

echo 'all module flow fixtures passed'
