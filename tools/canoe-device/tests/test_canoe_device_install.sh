#!/bin/sh
# Host fixture coverage for the device-side install transaction.
# Run from the repository root with:
#   sh targets/toolkit_linux/tests/test_canoe_device_install.sh
#
# tools/canoe-device/canoe_device_install.sh is the single implementation of the
# install transaction; both host drivers (canoe_stage.sh and canoe_stage.bat) push
# it to the device and invoke it. Because every absolute device path arrives as an
# argument, the transaction runs unmodified against ordinary directories and a
# regular file standing in for the efisp block device, so these cases exercise the
# real code rather than a reimplementation.
#
# Failures are injected by shadowing `mv`, `cmp` and `dd` on PATH.
#
#   1  success with a previous generation: rotation, menu install, BDS verified
#   2  invalid staged set aborts before touching anything
#   3  BDS verification failure rolls back the triplet AND the menu tree
#   4  first install creates no bogus backup
#   5  failed first install leaves nothing behind
#   6  commit failure restores everything
#   7  BDS write failure restores efisp and the pair
#   8  tree-only mode leaves the block device untouched
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
SCRIPT="$ROOT/tools/canoe-device/canoe_device_install.sh"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-device.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
sha()  { if [ -f "$1" ]; then sha256sum "$1" | cut -c1-12; else echo ABSENT; fi; }
temps() { find "$1" -maxdepth 1 -name '.canoe.*' -print 2>/dev/null | wc -l | tr -d ' '; }

[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

# setup <case> <existing yes|no> <gm2p bytes> <tzmap bytes>
setup() {
  C="$TMP/$1"; existing=$2; gm2p=$3; tzmap=$4
  rm -rf "$C"
  ST="$C/stage"; D="$C/efisp"; DEV="$C/efisp.bin"; BK="$C/efisp-backup.img"
  mkdir -p "$ST/tools" "$D/tools"
  printf 'NEW-BOOT-EFI-PAYLOAD' > "$ST/boot.efi"
  dd if=/dev/zero bs=1 count="$gm2p" 2>/dev/null | tr '\0' 'G' > "$ST/boot.efi.gm2p"
  dd if=/dev/zero bs=1 count="$tzmap" 2>/dev/null | tr '\0' 'T' > "$ST/boot.efi.tzmap"
  printf 'Android:boot.efi\nNEW-MENU\n' > "$ST/BOOTENTRIES"
  printf 'NEW-BLTOOLS' > "$ST/tools/BLTools.efi"
  { printf 'MZ'; dd if=/dev/zero bs=1024 count=12 2>/dev/null; } > "$ST/BDS.efi"
  if [ "$existing" = yes ]; then
    printf 'OLD-LIVE-EFI' > "$D/boot.efi"
    dd if=/dev/zero bs=1 count=120 2>/dev/null | tr '\0' 'L' > "$D/boot.efi.gm2p"
    dd if=/dev/zero bs=1 count=256 2>/dev/null | tr '\0' 'L' > "$D/boot.efi.tzmap"
    printf 'OLD-BACKUP-EFI' > "$D/boot_backup.efi"
    dd if=/dev/zero bs=1 count=120 2>/dev/null | tr '\0' 'B' > "$D/boot_backup.efi.gm2p"
    dd if=/dev/zero bs=1 count=256 2>/dev/null | tr '\0' 'B' > "$D/boot_backup.efi.tzmap"
    printf 'Android:boot.efi\nOLD-MENU\n' > "$D/BOOTENTRIES"
    printf 'OLD-BLTOOLS' > "$D/tools/BLTools.efi"
  fi
  { printf 'MZOLDBDS'; dd if=/dev/zero bs=1024 count=2048 2>/dev/null; } > "$DEV"
}

# shadow <case> <name> <body>  -- put a failing stand-in first on PATH
shadow() {
  mkdir -p "$TMP/$1/shadow"
  printf '%s\n' "$3" > "$TMP/$1/shadow/$2"
  chmod +x "$TMP/$1/shadow/$2"
  SHADOW="$TMP/$1/shadow"
}

run_install() {
  rc=0
  if [ -n "${SHADOW:-}" ]; then
    ( PATH="$SHADOW:$PATH"; export PATH; sh "$SCRIPT" "$@" ) >"$OUT" 2>"$ERR" || rc=$?
  else
    sh "$SCRIPT" "$@" >"$OUT" 2>"$ERR" || rc=$?
  fi
  return "$rc"
}

# ------------------------------------------------------------------ case 1 --
setup c1 yes 120 256
OUT="$TMP/c1/out"; ERR="$TMP/c1/err"; SHADOW=""
live=$(sha "$D/boot.efi"); menu=$(sha "$D/BOOTENTRIES"); tool=$(sha "$D/tools/BLTools.efi")
new=$(sha "$ST/boot.efi")
run_install "$ST" "$D" "$DEV" "$BK" || fail "1: transaction failed: $(cat "$ERR")"
[ "$(sha "$D/boot.efi")" = "$new" ] || fail '1: boot.efi is not the new image'
[ "$(sha "$D/boot_backup.efi")" = "$live" ] || fail '1: previous live was not demoted'
[ "$(sha "$D/BOOTENTRIES")" != "$menu" ] || fail '1: BOOTENTRIES was not installed'
[ "$(sha "$D/tools/BLTools.efi")" != "$tool" ] || fail '1: tools/ was not installed'
[ "$(temps "$D")" = 0 ] || fail '1: snapshot files were left behind'
[ -s "$BK" ] || fail '1: no efisp backup was taken'
grep -q 'CANOE-MARK: efisp-verified' "$OUT" || fail '1: BDS was not verified'
pass 'success rotates the generation, installs the menu tree and verifies the BDS'

# ------------------------------------------------------------------ case 2 --
setup c2 yes 119 256
OUT="$TMP/c2/out"; ERR="$TMP/c2/err"; SHADOW=""
live=$(sha "$D/boot.efi"); menu=$(sha "$D/BOOTENTRIES"); dev=$(sha "$DEV")
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '2: accepted a 119-byte gm2p'; fi
grep -q '120 bytes' "$ERR" || fail '2: wrong rejection message'
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '2: live changed'
[ "$(sha "$D/BOOTENTRIES")" = "$menu" ] || fail '2: menu changed'
[ "$(sha "$DEV")" = "$dev" ] || fail '2: device changed'
pass 'an invalid staged set aborts before touching anything'

# ------------------------------------------------------------------ case 3 --
setup c3 yes 120 256
OUT="$TMP/c3/out"; ERR="$TMP/c3/err"
live=$(sha "$D/boot.efi"); back=$(sha "$D/boot_backup.efi")
menu=$(sha "$D/BOOTENTRIES"); tool=$(sha "$D/tools/BLTools.efi"); dev=$(sha "$DEV")
shadow c3 cmp '#!/bin/sh
exit 1'
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '3: accepted a failed verification'; fi
grep -q 'verification' "$ERR" || fail '3: wrong failure message'
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '3: live triplet not restored'
[ "$(sha "$D/boot_backup.efi")" = "$back" ] || fail '3: backup not restored'
[ "$(sha "$D/BOOTENTRIES")" = "$menu" ] || fail '3: BOOTENTRIES not restored'
[ "$(sha "$D/tools/BLTools.efi")" = "$tool" ] || fail '3: tools/ not restored'
[ "$(sha "$DEV")" = "$dev" ] || fail '3: efisp not restored'
[ "$(temps "$D")" = 0 ] || fail '3: snapshot files were left behind'
pass 'a BDS verification failure rolls back the triplet and the menu tree'

# ------------------------------------------------------------------ case 4 --
setup c4 no 120 256
OUT="$TMP/c4/out"; ERR="$TMP/c4/err"; SHADOW=""
new=$(sha "$ST/boot.efi")
run_install "$ST" "$D" "$DEV" "$BK" || fail "4: first install failed: $(cat "$ERR")"
[ "$(sha "$D/boot.efi")" = "$new" ] || fail '4: boot.efi not installed'
[ "$(sha "$D/boot_backup.efi")" = ABSENT ] || fail '4: invented a backup'
grep -q 'CANOE-MARK: first-install' "$OUT" || fail '4: first install not reported'
pass 'a first install creates no bogus backup'

# ------------------------------------------------------------------ case 5 --
setup c5 no 120 256
OUT="$TMP/c5/out"; ERR="$TMP/c5/err"
dev=$(sha "$DEV")
shadow c5 cmp '#!/bin/sh
exit 1'
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '5: accepted a failed verification'; fi
[ "$(sha "$D/boot.efi")" = ABSENT ] || fail '5: partial boot.efi left behind'
[ "$(sha "$D/boot_backup.efi")" = ABSENT ] || fail '5: invented a backup'
[ "$(sha "$D/BOOTENTRIES")" = ABSENT ] || fail '5: left a menu with no loader'
[ "$(sha "$DEV")" = "$dev" ] || fail '5: efisp not restored'
pass 'a failed first install leaves nothing behind'

# ------------------------------------------------------------------ case 6 --
setup c6 yes 120 256
OUT="$TMP/c6/out"; ERR="$TMP/c6/err"
live=$(sha "$D/boot.efi"); back=$(sha "$D/boot_backup.efi")
menu=$(sha "$D/BOOTENTRIES"); dev=$(sha "$DEV")
shadow c6 mv '#!/bin/sh
exit 1'
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '6: accepted a failed commit'; fi
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '6: live not restored'
[ "$(sha "$D/boot_backup.efi")" = "$back" ] || fail '6: backup not restored'
[ "$(sha "$D/BOOTENTRIES")" = "$menu" ] || fail '6: menu not restored'
[ "$(sha "$DEV")" = "$dev" ] || fail '6: device changed'
[ "$(temps "$D")" = 0 ] || fail '6: snapshot files were left behind'
pass 'a commit failure restores everything'

# ------------------------------------------------------------------ case 7 --
setup c7 yes 120 256
OUT="$TMP/c7/out"; ERR="$TMP/c7/err"
live=$(sha "$D/boot.efi"); menu=$(sha "$D/BOOTENTRIES"); dev=$(sha "$DEV")
# Succeed for the backup read, fail only for the write to the stand-in device.
shadow c7 dd "#!/bin/sh
out=
for a in \"\$@\"; do
  case \"\$a\" in of=*) out=\${a#of=} ;; esac
done
case \"\$out\" in
  *efisp.bin) exit 1 ;;
esac
exec $(command -v dd) \"\$@\""
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '7: accepted a failed write'; fi
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '7: live not restored'
[ "$(sha "$D/BOOTENTRIES")" = "$menu" ] || fail '7: menu not restored'
[ "$(sha "$DEV")" = "$dev" ] || fail '7: efisp changed despite a failed write'
pass 'a BDS write failure restores efisp and the pair'

# ------------------------------------------------------------------ case 8 --
setup c8 yes 120 256
OUT="$TMP/c8/out"; ERR="$TMP/c8/err"; SHADOW=""
new=$(sha "$ST/boot.efi"); dev=$(sha "$DEV")
run_install "$ST" "$D" || fail "8: tree-only install failed: $(cat "$ERR")"
[ "$(sha "$D/boot.efi")" = "$new" ] || fail '8: tree not installed'
[ "$(sha "$DEV")" = "$dev" ] || fail '8: device written without being asked'
! grep -q 'CANOE-MARK: efisp-verified' "$OUT" || fail '8: reported a BDS write'
pass 'tree-only mode leaves the block device untouched'

echo 'all canoe device-install fixtures passed'
