#!/bin/sh
# Host fixture coverage for the canoe host-side driver scripts.
# Run from the repository root with:
#   sh targets/toolkit_linux/tests/test_canoe_scripts.sh
#
# Scope: the HOST drivers and the two preparation pathways. The install
# transaction itself lives in tools/canoe-device/canoe_device_install.sh and is
# covered directly by test_canoe_device_install.sh, which exercises its rollback
# paths natively instead of through a stub. What is checked here is that the
# drivers derive, validate, stage and invoke correctly:
#
#   A  standalone prep derives a valid triplet with no package and no graft
#   B  staging pushes the full set, invokes the device script and rotates
#   C  a failed push aborts before the transaction runs at all
#   D  --skip-bds installs the tree and leaves efisp untouched
#   E  --abl without --vbmeta is rejected
#   F  the package pathway grafts, substitutes in place and is idempotent
#   G  --slot inactive derives from the non-active slot; bad values rejected
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-scripts.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
sha()  { if [ -f "$1" ]; then sha256sum "$1" | cut -c1-12; else echo ABSENT; fi; }

RES="$ROOT/targets/toolkit_linux/resources"
TK="$TMP/tk"
DEV="$TMP/dev"

# ---------------------------------------------------------------- fixtures --
# build.sh is not under test here (test_build_scripts.sh owns it), so the derive
# tools are replaced with generators that emit correctly sized outputs.
make_toolkit() {
  rm -rf "$TK"
  cp -R "$RES" "$TK"
  mkdir -p "$TK/bin" "$TK/Platform-Tools" "$TK/images"
  cp "$ROOT/tools/canoe-device/canoe_device_install.sh" "$TK/canoe_device_install.sh"
  chmod +x "$TK/canoe_device_install.sh"

  cat > "$TK/build.sh" <<'EOS'
#!/bin/sh
set -eu
cd "$(dirname "$0")"
[ -f ./images/abl.img ] || { echo "ERROR: missing images/abl.img" >&2; exit 1; }
[ -f ./images/vbmeta.img ] || { echo "ERROR: missing images/vbmeta.img" >&2; exit 1; }
mkdir -p ./efisp
printf 'PATCHED-ABL-FROM-%s' "$(sha256sum ./images/abl.img | cut -c1-8)" > ./efisp/boot.efi
dd if=/dev/zero bs=1 count=120 2>/dev/null | tr '\0' 'G' > ./efisp/boot.efi.gm2p
dd if=/dev/zero bs=1 count=256 2>/dev/null | tr '\0' 'T' > ./efisp/boot.efi.tzmap
cp ./images/abl.img ./ABL_original.efi
if [ "${STUB_NO_GBL:-0}" = 1 ]; then
  echo "Warning: Failed to patch ABL GBL" > ./patch_log.txt
else
  echo "GBL patched" > ./patch_log.txt
fi
echo "stub build.sh done"
EOS
  chmod +x "$TK/build.sh"
  printf 'MZSTUB-BDS-IMAGE-CONTENT' > "$TK/BDS.efi"

  # vbmetabackup/vbmetaport stubs: the real ones are covered by their own build.
  cat > "$TK/bin/vbmetabackup" <<'EOS'
#!/bin/sh
set -eu
out=./vbmetas; img=""; name=""
while [ $# -gt 0 ]; do
  case "$1" in
    -o) out=$2; shift 2 ;;
    -f) img=$2; shift 2 ;;
    -n) name=$2; shift 2 ;;
    *) shift ;;
  esac
done
[ -n "$img" ] || { echo "stub needs -f" >&2; exit 1; }
[ -f "$img" ] || { echo "no such image: $img" >&2; exit 1; }
[ -n "$name" ] || name=$(basename "$img" .img)
mkdir -p "$out"
printf 'AVB0STUBVBMETA' > "$out/$name.vbmeta"
EOS
  cat > "$TK/bin/vbmetaport" <<'EOS'
#!/bin/sh
set -eu
src=$1; target=$2; out=$3
[ -f "$src" ] && [ -f "$target" ] || { echo "missing input" >&2; exit 1; }
cp "$target" "$out"
printf 'GRAFTED' | dd of="$out" bs=1 seek=0 conv=notrunc 2>/dev/null
EOS
  chmod +x "$TK/bin/vbmetabackup" "$TK/bin/vbmetaport"
  cp "$ROOT/targets/toolkit_linux/tests/stub_adb.py" "$TK/Platform-Tools/adb"
  chmod +x "$TK/Platform-Tools/adb"
}

make_device() {
  with_existing=$1
  rm -rf "$DEV"
  mkdir -p "$DEV/persist/efisp/tools" "$DEV/tmp"
  printf '/dev/block/by-name/persist %s/persist ext4 rw 0 0\n' "$DEV" > "$DEV/proc_mounts"
  printf 'androidboot.slot_suffix=_a rootwait\n' > "$DEV/cmdline"
  printf 'STOCK-ABL-IMAGE' > "$DEV/abl_a.bin"
  printf 'STOCK-VBMETA-IMAGE' > "$DEV/vbmeta_a.bin"
  { printf 'MZOLDBDS'; dd if=/dev/zero bs=1024 count=2048 2>/dev/null; } > "$DEV/efisp.bin"
  if [ "$with_existing" = yes ]; then
    printf 'OLD-LIVE' > "$DEV/persist/efisp/boot.efi"
    dd if=/dev/zero bs=1 count=120 2>/dev/null | tr '\0' 'L' > "$DEV/persist/efisp/boot.efi.gm2p"
    dd if=/dev/zero bs=1 count=256 2>/dev/null | tr '\0' 'L' > "$DEV/persist/efisp/boot.efi.tzmap"
    printf 'OLD-BACKUP' > "$DEV/persist/efisp/boot_backup.efi"
    printf 'Android:boot.efi\nOLD-MENU\n' > "$DEV/persist/efisp/BOOTENTRIES"
  fi
}

stage() {
  ( cd "$TK" && STUB_DEV="$DEV" STUB_FAIL="${1:-}" STUB_CORRUPT=0 \
      ./canoe_stage.sh ${2:-} >"$TMP/out" 2>"$TMP/err" )
}

# ------------------------------------------------------------------- tests --
make_toolkit

# A: standalone prep, no package, no graft
make_device yes
( cd "$TK" && STUB_DEV="$DEV" STUB_NO_GBL=1 ./canoe_prep_device.sh >"$TMP/out" 2>"$TMP/err" ) ||
  fail "canoe_prep_device.sh failed: $(cat "$TMP/err")"
[ "$(wc -c < "$TK/efisp/boot.efi.gm2p")" = 120 ] || fail 'A: gm2p is not 120 bytes'
[ "$(wc -c < "$TK/efisp/boot.efi.tzmap")" = 256 ] || fail 'A: tzmap is not 256 bytes'
grep -q 'fastboot flash abl' "$TMP/out" ||
  fail 'A: a non-vulnerable source ABL must point at the fastboot ABL step'
[ ! -f "$TK/images/abl.img" ] || fail 'A: pulled images were not cleaned up'
pass 'standalone prep derives a valid triplet with no package and no graft'

# B: staging pushes the full set, invokes the device script and rotates
make_device yes
live_before=$(sha "$DEV/persist/efisp/boot.efi")
new=$(sha "$TK/efisp/boot.efi")
stage || fail "B: canoe_stage.sh failed: $(cat "$TMP/err")"
grep -q 'canoe_device_install.sh' "$DEV/adb.log" ||
  fail 'B: the device-side transaction script was never pushed or invoked'
grep -q 'CANOE-MARK: efisp-verified' "$TMP/out" ||
  fail 'B: the device script did not report a verified BDS write'
[ "$(sha "$DEV/persist/efisp/boot.efi")" = "$new" ] || fail 'B: boot.efi is not the new image'
[ "$(sha "$DEV/persist/efisp/boot_backup.efi")" = "$live_before" ] ||
  fail 'B: previous generation was not demoted to boot_backup.efi'
[ -f "$DEV/persist/efisp/BOOTENTRIES" ] || fail 'B: BOOTENTRIES was not installed'
head -c 2 "$DEV/efisp.bin" | grep -q 'MZ' || fail 'B: efisp was not rewritten'
[ -f "$TK/work/efisp-backup.img" ] || fail 'B: the efisp backup was not pulled to the host'
[ -z "$(find "$DEV/persist/efisp" -maxdepth 1 -name '.canoe.*' -print -quit)" ] ||
  fail 'B: staging or snapshot files were left behind'
pass 'staging pushes the full set, invokes the device script and rotates'

# C: a failed push must abort before the transaction runs
make_device yes
live_before=$(sha "$DEV/persist/efisp/boot.efi")
efisp_before=$(sha "$DEV/efisp.bin")
if stage '.canoe.stage/boot.efi'; then fail 'C: expected failure'; fi
grep -q 'FAULT' "$DEV/adb.log" || fail 'C: vacuous, the fault never fired'
! grep -q 'CANOE-MARK: committed' "$TMP/out" ||
  fail 'C: the transaction ran despite a failed push'
[ "$(sha "$DEV/persist/efisp/boot.efi")" = "$live_before" ] || fail 'C: live generation changed'
[ "$(sha "$DEV/efisp.bin")" = "$efisp_before" ] || fail 'C: efisp changed'
pass 'a failed push aborts before the transaction runs'

# D: --skip-bds
make_device yes
efisp_before=$(sha "$DEV/efisp.bin")
stage '' '--skip-bds' || fail "D: canoe_stage.sh --skip-bds failed: $(cat "$TMP/err")"
[ "$(sha "$DEV/efisp.bin")" = "$efisp_before" ] || fail 'D: efisp was written despite --skip-bds'
[ -f "$DEV/persist/efisp/boot.efi" ] || fail 'D: tree was not installed'
! grep -q 'CANOE-MARK: efisp-verified' "$TMP/out" || fail 'D: reported a BDS write'
pass '--skip-bds installs the tree and leaves efisp untouched'

# E: paired-argument guard
make_device yes
if ( cd "$TK" && STUB_DEV="$DEV" ./canoe_prep_device.sh --abl "$DEV/abl_a.bin" \
       >"$TMP/out" 2>"$TMP/err" ); then
  fail 'E: --abl without --vbmeta was accepted'
fi
grep -q 'must be given together' "$TMP/err" || fail 'E: wrong rejection message'
pass '--abl without --vbmeta is rejected'

# G: --slot inactive derives from the slot a sideload just wrote
make_device yes
printf 'OTHER-ABL-IMAGE' > "$DEV/abl_b.bin"
printf 'OTHER-VBMETA-IMAGE' > "$DEV/vbmeta_b.bin"
( cd "$TK" && STUB_DEV="$DEV" ./canoe_prep_device.sh --slot inactive >"$TMP/out" 2>"$TMP/err" ) ||
  fail "G: canoe_prep_device.sh --slot inactive failed: $(cat "$TMP/err")"
want="PATCHED-ABL-FROM-$(printf 'OTHER-ABL-IMAGE' | sha256sum | cut -c1-8)"
grep -q "$want" "$TK/efisp/boot.efi" || fail 'G: did not derive from the inactive slot abl'
grep -q 'sourcing from the inactive slot _b' "$TMP/out" ||
  fail 'G: did not report the inactive slot'
if ( cd "$TK" && STUB_DEV="$DEV" ./canoe_prep_device.sh --slot nonsense >"$TMP/out" 2>"$TMP/err" ); then
  fail 'G: --slot nonsense was accepted'
fi
grep -q 'must be _a, _b, active or inactive' "$TMP/err" || fail 'G: wrong rejection message'
pass '--slot inactive derives from the non-active slot; bad values rejected'

# F: package pathway grafts, substitutes and is idempotent
PKG="$TMP/pkg"
rm -rf "$PKG"; mkdir -p "$PKG"
printf 'PKG-STOCK-ABL' > "$PKG/abl.img"
printf 'PKG-STOCK-VBMETA' > "$PKG/vbmeta.img"
printf 'PKG-STOCK-RECOVERY-PADDED' > "$PKG/recovery.img"
printf 'CUSTOM-RECOVERY-IMAGE----' > "$TMP/custom_recovery.img"
printf 'VULNERABLE-ABL' > "$TMP/vuln_abl.img"
stock_recovery=$(sha "$PKG/recovery.img")
stock_abl=$(sha "$PKG/abl.img")
( cd "$TK" && ./canoe_prep.sh --pkg "$PKG" --recovery "$TMP/custom_recovery.img" \
    --abl "$TMP/vuln_abl.img" --in-place >"$TMP/out" 2>"$TMP/err" ) ||
  fail "F: canoe_prep.sh failed: $(cat "$TMP/err")"
[ "$(sha "$PKG/recovery.img.canoe-orig")" = "$stock_recovery" ] ||
  fail 'F: stock recovery was not backed up'
[ "$(sha "$PKG/abl.img.canoe-orig")" = "$stock_abl" ] || fail 'F: stock abl was not backed up'
[ "$(sha "$PKG/abl.img")" = "$(sha "$TMP/vuln_abl.img")" ] ||
  fail 'F: vulnerable ABL was not substituted'
[ "$(sha "$PKG/recovery.img")" = "$(sha "$TK/work/grafted_recovery.img")" ] ||
  fail 'F: grafted recovery was not substituted'
( cd "$TK" && ./canoe_prep.sh --pkg "$PKG" --recovery "$TMP/custom_recovery.img" \
    --abl "$TMP/vuln_abl.img" --in-place >"$TMP/out" 2>"$TMP/err" ) ||
  fail "F: rerun failed: $(cat "$TMP/err")"
[ "$(sha "$PKG/recovery.img.canoe-orig")" = "$stock_recovery" ] ||
  fail 'F: rerun clobbered the recovery backup'
[ "$(sha "$PKG/abl.img.canoe-orig")" = "$stock_abl" ] ||
  fail 'F: rerun clobbered the abl backup'
pass 'package pathway grafts, substitutes in place and is idempotent'

echo 'all canoe script fixtures passed'
