#!/bin/sh
# Host fixture coverage for the device-side install transaction.
# Run from the repository root with:
#   sh tools/canoe-device/tests/test_canoe_device_install.sh
#
# tools/canoe-device/canoe_device_install.sh is the single implementation of the
# install transaction; the host driver (`canoe install`, shared by the Linux and
# the Windows toolkit) pushes it to the device and invokes it. It stays a shell
# script because it runs on the device. Because every absolute device path
# arrives as an argument, the transaction runs unmodified against ordinary
# directories and a regular file standing in for the efisp block device, so
# these cases exercise the real code rather than a reimplementation.
#
# Failures are injected by shadowing `mv`, `cmp` and `dd` on PATH.
#
#   1  success with a previous generation: rotation, menu install, BDS verified
#   2  invalid staged set aborts before touching anything
#   3  BDS verification failure rolls back the triplet, menu tree and stamp
#   4  first install creates no bogus backup
#   5  failed first install leaves nothing behind, including the stamp
#   6  commit failure restores everything
#   7  BDS write failure restores efisp and the pair
#   8  tree-only mode leaves the block device untouched and stamps `-`
#   9  foreign file is set aside and reported
#  10  foreign file is preserved when the destination name collides
#  11  foreign disclosure rolls back with the transaction
#  12  the requested mode reaches canoe.cfg, and a bad mode is refused
#  13  passthrough row and loader are migrated while hand-added rows survive
#  14  a config naming a missing backup loader loses that entry
#  15  a changed signer is refused before touching the boot root
#  16  an explicitly supplied changed signer is accepted
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
SCRIPT="$ROOT/tools/canoe-device/canoe_device_install.sh"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-device.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
sha()  { if [ -f "$1" ]; then sha256sum "$1" | cut -c1-12; else echo ABSENT; fi; }
sha_full() { sha256sum "$1" | cut -d ' ' -f1; }
temps() { find "$1" -maxdepth 1 -name '.canoe.*' ! -name '.canoe.gen' ! -name '.canoe.foreign' -print 2>/dev/null | wc -l | tr -d ' '; }

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
  set_signer "$ST/boot.efi.gm2p"
  printf 'NEW-BLTOOLS' > "$ST/tools/BLTools.efi"
  { printf 'MZ'; dd if=/dev/zero bs=1024 count=12 2>/dev/null; } > "$ST/BDS.efi"
  if [ "$existing" = yes ]; then
    printf 'OLD-LIVE-EFI' > "$D/boot.efi"
    dd if=/dev/zero bs=1 count=120 2>/dev/null | tr '\0' 'L' > "$D/boot.efi.gm2p"
    dd if=/dev/zero bs=1 count=256 2>/dev/null | tr '\0' 'L' > "$D/boot.efi.tzmap"
    set_signer "$D/boot.efi.gm2p"
    printf 'OLD-BACKUP-EFI' > "$D/boot_backup.efi"
    dd if=/dev/zero bs=1 count=120 2>/dev/null | tr '\0' 'B' > "$D/boot_backup.efi.gm2p"
    dd if=/dev/zero bs=1 count=256 2>/dev/null | tr '\0' 'B' > "$D/boot_backup.efi.tzmap"
    printf 'version 1\ngeneration 9\ndefault android-a\nmode 1\ndevinfo-repair asneeded\n\nentry android-a\n  title Old A\n  image boot.efi\n  mode 1\n  role active\n\nentry android-backup\n  title Old backup\n  image boot_backup.efi\n  mode 1\n  role backup\n' > "$D/canoe.cfg"
    printf 'OLD-BLTOOLS' > "$D/tools/BLTools.efi"
  fi
  { printf 'MZOLDBDS'; dd if=/dev/zero bs=1024 count=2048 2>/dev/null; } > "$DEV"
}

set_signer() {
  dd if=/dev/zero bs=1 count=32 2>/dev/null | tr '\0' S |
    dd of="$1" bs=1 seek=56 conv=notrunc 2>/dev/null
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
live=$(sha "$D/boot.efi"); tool=$(sha "$D/tools/BLTools.efi")
new=$(sha "$ST/boot.efi")
run_install "$ST" "$D" "$DEV" "$BK" || fail "1: transaction failed: $(cat "$ERR")"
[ "$(sha "$D/boot.efi")" = "$new" ] || fail '1: boot.efi is not the new image'
[ "$(sha "$D/boot_backup.efi")" = "$live" ] || fail '1: previous live was not demoted'
[ "$(sha "$D/tools/BLTools.efi")" != "$tool" ] || fail '1: tools/ was not installed'
[ "$(temps "$D")" = 0 ] || fail '1: snapshot files were left behind'
[ -s "$BK" ] || fail '1: no efisp backup was taken'
expected_gen="CANOEG1|$(sha_full "$ST/BDS.efi")|$(sha_full "$D/boot.efi")|$(sha_full "$D/boot.efi.gm2p")|$(sha_full "$D/boot.efi.tzmap")"
[ "$(cat "$D/.canoe.gen")" = "$expected_gen" ] || fail '1: generation stamp does not describe installed bytes'
grep -q '^version 1$' "$D/canoe.cfg" || fail '1: canoe.cfg version missing'
grep -q '^entry android-a$' "$D/canoe.cfg" || fail '1: active entry missing'
grep -q '^  role active$' "$D/canoe.cfg" || fail '1: active role missing'
grep -q '^entry android-backup$' "$D/canoe.cfg" || fail '1: backup entry missing'
grep -q '^  role backup$' "$D/canoe.cfg" || fail '1: backup role missing'
grep -q 'CANOE-MARK: efisp-verified' "$OUT" || fail '1: BDS was not verified'
pass 'success rotates the generation, installs the tools tree and verifies the BDS'

# ------------------------------------------------------------------ case 2 --
setup c2 yes 119 256
OUT="$TMP/c2/out"; ERR="$TMP/c2/err"; SHADOW=""
live=$(sha "$D/boot.efi"); tool=$(sha "$D/tools/BLTools.efi"); dev=$(sha "$DEV")
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '2: accepted a 119-byte gm2p'; fi
grep -q '120 bytes' "$ERR" || fail '2: wrong rejection message'
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '2: live changed'
[ "$(sha "$D/tools/BLTools.efi")" = "$tool" ] || fail '2: tools changed'
[ "$(sha "$DEV")" = "$dev" ] || fail '2: device changed'
pass 'an invalid staged set aborts before touching anything'

# ------------------------------------------------------------------ case 3 --
setup c3 yes 120 256
printf 'CANOEG1|old-generation\n' > "$D/.canoe.gen"
OUT="$TMP/c3/out"; ERR="$TMP/c3/err"
live=$(sha "$D/boot.efi"); back=$(sha "$D/boot_backup.efi")
cfg=$(sha "$D/canoe.cfg")
tool=$(sha "$D/tools/BLTools.efi"); dev=$(sha "$DEV")
shadow c3 cmp '#!/bin/sh
case "$*" in *canoe-signer*) exit 0 ;; esac
exit 1'
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '3: accepted a failed verification'; fi
grep -q 'verification' "$ERR" || fail '3: wrong failure message'
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '3: live triplet not restored'
[ "$(sha "$D/boot_backup.efi")" = "$back" ] || fail '3: backup not restored'
[ "$(sha "$D/canoe.cfg")" = "$cfg" ] || fail '3: canoe.cfg not restored'
[ "$(cat "$D/.canoe.gen")" = 'CANOEG1|old-generation' ] || fail '3: generation stamp not restored'
[ "$(sha "$D/tools/BLTools.efi")" = "$tool" ] || fail '3: tools/ not restored'
[ "$(sha "$DEV")" = "$dev" ] || fail '3: efisp not restored'
[ "$(temps "$D")" = 0 ] || fail '3: snapshot files were left behind'
pass 'a BDS verification failure rolls back the triplet and the tools tree'

# ------------------------------------------------------------------ case 4 --
setup c4 no 120 256
OUT="$TMP/c4/out"; ERR="$TMP/c4/err"; SHADOW=""
new=$(sha "$ST/boot.efi")
run_install "$ST" "$D" "$DEV" "$BK" || fail "4: first install failed: $(cat "$ERR")"
[ "$(sha "$D/boot.efi")" = "$new" ] || fail '4: boot.efi not installed'
[ -s "$D/canoe.cfg" ] || fail '4: canoe.cfg not installed'
grep -q '^entry android-a$' "$D/canoe.cfg" || fail '4: active entry missing'
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
[ "$(sha "$D/.canoe.gen")" = ABSENT ] || fail '5: generation stamp left behind'
[ "$(sha "$D/canoe.cfg")" = ABSENT ] || fail '5: partial canoe.cfg left behind'
[ "$(sha "$D/boot.efi")" = ABSENT ] || fail '5: partial boot.efi left behind'
[ "$(sha "$D/boot_backup.efi")" = ABSENT ] || fail '5: invented a backup'
[ "$(sha "$D/tools/BLTools.efi")" = ABSENT ] || fail '5: left tools content behind'
[ "$(sha "$DEV")" = "$dev" ] || fail '5: efisp not restored'
pass 'a failed first install leaves nothing behind'

# ------------------------------------------------------------------ case 6 --
setup c6 yes 120 256
OUT="$TMP/c6/out"; ERR="$TMP/c6/err"
live=$(sha "$D/boot.efi"); back=$(sha "$D/boot_backup.efi")
cfg=$(sha "$D/canoe.cfg")
tool=$(sha "$D/tools/BLTools.efi"); dev=$(sha "$DEV")
shadow c6 mv '#!/bin/sh
exit 1'
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '6: accepted a failed commit'; fi
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '6: live not restored'
[ "$(sha "$D/boot_backup.efi")" = "$back" ] || fail '6: backup not restored'
[ "$(sha "$D/canoe.cfg")" = "$cfg" ] || fail '6: canoe.cfg not restored'
[ "$(sha "$D/tools/BLTools.efi")" = "$tool" ] || fail '6: tools not restored'
[ "$(sha "$DEV")" = "$dev" ] || fail '6: device changed'
[ "$(temps "$D")" = 0 ] || fail '6: snapshot files were left behind'
pass 'a commit failure restores everything'

# ------------------------------------------------------------------ case 7 --
setup c7 yes 120 256
OUT="$TMP/c7/out"; ERR="$TMP/c7/err"
live=$(sha "$D/boot.efi"); tool=$(sha "$D/tools/BLTools.efi"); dev=$(sha "$DEV")
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
[ "$(sha "$D/tools/BLTools.efi")" = "$tool" ] || fail '7: tools not restored'
[ "$(sha "$DEV")" = "$dev" ] || fail '7: efisp changed despite a failed write'
pass 'a BDS write failure restores efisp and the pair'

# ------------------------------------------------------------------ case 8 --
setup c8 yes 120 256
OUT="$TMP/c8/out"; ERR="$TMP/c8/err"; SHADOW=""
new=$(sha "$ST/boot.efi"); dev=$(sha "$DEV")
run_install "$ST" "$D" || fail "8: tree-only install failed: $(cat "$ERR")"
[ "$(cut -d '|' -f1 "$D/.canoe.gen")" = 'CANOEG1' ] || fail '8: generation stamp missing'
[ "$(cut -d '|' -f2 "$D/.canoe.gen")" = '-' ] || fail '8: tree-only stamp has a BDS digest'
[ "$(sha "$D/boot.efi")" = "$new" ] || fail '8: tree not installed'
[ "$(sha "$DEV")" = "$dev" ] || fail '8: device written without being asked'
! grep -q 'CANOE-MARK: efisp-verified' "$OUT" || fail '8: reported a BDS write'
pass 'tree-only mode leaves the block device untouched'

# ------------------------------------------------------------------ case 9 --
setup c9 yes 120 256
printf 'STALE-CHAINLOAD-TREE' > "$D/stale-chainload.bin"
OUT="$TMP/c9/out"; ERR="$TMP/c9/err"; SHADOW=""
run_install "$ST" "$D" || fail "9: foreign-file install failed: $(cat "$ERR")"
[ ! -e "$D/stale-chainload.bin" ] || fail '9: foreign file remained in the boot root'
[ "$(cat "$D/.canoe.foreign/stale-chainload.bin")" = STALE-CHAINLOAD-TREE ] ||
  fail '9: foreign file was not preserved'
grep -q 'set aside foreign entry stale-chainload.bin' "$OUT" ||
  fail '9: foreign-file disclosure was not reported'
pass 'foreign files are set aside and reported'

# ----------------------------------------------------------------- case 10 --
setup c10 yes 120 256
mkdir -p "$D/.canoe.foreign"
printf 'OLD-FOREIGN' > "$D/.canoe.foreign/stale-tree"
printf 'NEW-FOREIGN' > "$D/stale-tree"
OUT="$TMP/c10/out"; ERR="$TMP/c10/err"; SHADOW=""
run_install "$ST" "$D" || fail "10: foreign collision install failed: $(cat "$ERR")"
[ "$(cat "$D/.canoe.foreign/stale-tree")" = OLD-FOREIGN ] ||
  fail '10: existing foreign entry was overwritten'
[ "$(cat "$D/.canoe.foreign/stale-tree.1")" = NEW-FOREIGN ] ||
  fail '10: colliding foreign entry was not suffixed'
pass 'foreign files never overwrite an existing set-aside entry'
# ----------------------------------------------------------------- case 11 --
setup c11 yes 120 256
printf 'ROLLBACK-FOREIGN' > "$D/foreign-before-failure"
OUT="$TMP/c11/out"; ERR="$TMP/c11/err"
shadow c11 cmp '#!/bin/sh
exit 1'
if run_install "$ST" "$D" "$DEV" "$BK"; then fail '11: accepted a failed verification'; fi
[ "$(cat "$D/foreign-before-failure")" = ROLLBACK-FOREIGN ] ||
  fail '11: foreign file was not restored on rollback'
[ ! -e "$D/.canoe.foreign" ] || fail '11: rollback left a new foreign directory'
pass 'foreign-file disclosure rolls back with the transaction'
# ----------------------------------------------------------------- case 12 --
# The mode the caller asked for has to reach the file. It used to not: the host
# generated a canoe.cfg that this script then regenerated from scratch, so
# `canoe install --mode 0` landed `mode 1`.
setup c12 no 120 256
OUT="$TMP/c12/out"; ERR="$TMP/c12/err"; SHADOW=""
CANOE_MODE=0 CANOE_ACTIVE_SLOT=_b run_install "$ST" "$D" ||
  fail "12: install failed: $(cat "$ERR")"
cfg_value() { awk -v key="$1" '$1 == key { print $2; exit }' "$D/canoe.cfg"; }
cfg_entry() {
  awk -v wanted="$1" -v key="$2" '
    $1 == "entry" { here = ($2 == wanted); next }
    here && $1 == key { print $2; exit }
  ' "$D/canoe.cfg"
}
[ "$(cfg_entry android-b mode)" = 0 ] || fail '12: requested mode did not reach the entry'
[ "$(cfg_value mode)" = 0 ] || fail '12: first install did not adopt the requested fallback'
[ "$(cfg_value default)" = android-b ] || fail '12: active slot is not the default'
[ "$(cfg_entry android-b role)" = active ] || fail '12: active role missing'
grep -q 'CANOE-MARK: entry-set id=android-b role=active mode=0' "$OUT" ||
  fail '12: no entry receipt from the shared writer'
if CANOE_MODE=7 run_install "$ST" "$D"; then fail '12: accepted an invalid mode'; fi
pass 'the requested mode reaches canoe.cfg, and a bad mode is refused'

# ----------------------------------------------------------------- case 13 --
# A passthrough loader is no longer a supported row. Migration removes its
# loader, matching sidecars and row while preserving the operator's own row.
setup c13 yes 120 256
printf '\nentry lineage\n  title LineageOS\n  image roms/lineage.efi\n  mode 2\n  role other\n\nentry android-b\n  title Android (slot B)\n  image boot_b.efi\n  mode 0\n  role inactive\n' >> "$D/canoe.cfg"
printf 'OTHER-SLOT-LOADER' > "$D/boot_b.efi"
printf 'OTHER-SLOT-GM2P' > "$D/boot_b.efi.gm2p"
printf 'OTHER-SLOT-TZMAP' > "$D/boot_b.efi.tzmap"
OUT="$TMP/c13/out"; ERR="$TMP/c13/err"; SHADOW=""
run_install "$ST" "$D" || fail "13: install failed: $(cat "$ERR")"
grep -q '^entry lineage$' "$D/canoe.cfg" || fail '13: custom entry was erased'
grep -q '^  image roms/lineage.efi$' "$D/canoe.cfg" || fail '13: custom image was rewritten'
! grep -q '^entry android-b$' "$D/canoe.cfg" || fail '13: passthrough row survived'
grep -q '^  title LineageOS$' "$D/canoe.cfg" || fail '13: custom title was rewritten'
grep -q '^  mode 2$' "$D/canoe.cfg" || fail '13: custom mode was rewritten'
grep -q '^  role other$' "$D/canoe.cfg" || fail '13: custom role was rewritten'
[ ! -e "$D/boot_b.efi" ] || fail '13: passthrough loader survived'
[ ! -e "$D/boot_b.efi.gm2p" ] || fail '13: passthrough gm2p survived'
[ ! -e "$D/boot_b.efi.tzmap" ] || fail '13: passthrough tzmap survived'
grep -q 'CANOE-MARK: passthrough-row-migrated id=android-b' "$OUT" ||
  fail '13: passthrough migration was not reported'
grep -q '^entry android-backup$' "$D/canoe.cfg" || fail '13: backup entry missing'
pass 'a passthrough row and loader are migrated while a hand-added row survives'

# ----------------------------------------------------------------- case 14 --
# A config that names a backup loader the boot root no longer holds must lose
# that entry, or the menu offers a row the BDS cannot launch. A live loader is
# always demoted into boot_backup.efi, so the stale case is a boot root with a
# config but no loaders: what an interrupted first install leaves behind.
setup c14 no 120 256
printf 'version 1\ngeneration 3\ntimeout 5\ndefault android-backup\nmode 1\ndevinfo-repair asneeded\n\nentry android-backup\n  title Android (previous)\n  image boot_backup.efi\n  mode 1\n  role backup\n' > "$D/canoe.cfg"
OUT="$TMP/c14/out"; ERR="$TMP/c14/err"; SHADOW=""
run_install "$ST" "$D" || fail "14: install failed: $(cat "$ERR")"
grep -q 'CANOE-MARK: entry-removed id=android-backup' "$OUT" ||
  fail '14: stale backup entry was not removed'
grep -q '^entry android-backup$' "$D/canoe.cfg" && fail '14: unlaunchable row survived'
grep -q '^entry android-a$' "$D/canoe.cfg" || fail '14: active entry missing'
pass 'a config naming a missing backup loader loses that entry'

# ----------------------------------------------------------------- case 15 --
# A changed signer is an unannounced firmware change unless the caller
# explicitly identifies the vbmeta as supplied by the operator.
setup c15 yes 120 256
dd if=/dev/zero bs=1 count=32 2>/dev/null | tr '\0' N |
  dd of="$ST/boot.efi.gm2p" bs=1 seek=56 conv=notrunc 2>/dev/null
OUT="$TMP/c15/out"; ERR="$TMP/c15/err"; SHADOW=""
live=$(sha "$D/boot.efi"); back=$(sha "$D/boot_backup.efi")
cfg=$(sha "$D/canoe.cfg"); tool=$(sha "$D/tools/BLTools.efi")
if run_install "$ST" "$D"; then fail '15: accepted a changed signer'; fi
grep -q 'CANOE-MARK: signer-changed source=partition' "$OUT" ||
  fail '15: signer change was not reported as partition-sourced'
grep -q 'expected when moving to or from a custom ROM' "$ERR" ||
  fail '15: signer refusal omitted the custom-ROM explanation'
grep -q "no tool here can prove which key is the OEM's" "$ERR" ||
  fail '15: signer refusal omitted the authenticity limitation'
[ "$(sha "$D/boot.efi")" = "$live" ] || fail '15: live loader changed'
[ "$(sha "$D/boot_backup.efi")" = "$back" ] || fail '15: backup changed'
[ "$(sha "$D/canoe.cfg")" = "$cfg" ] || fail '15: config changed'
[ "$(sha "$D/tools/BLTools.efi")" = "$tool" ] || fail '15: tools changed'
[ "$(temps "$D")" = 0 ] || fail '15: refusal left transaction files'
pass 'a changed signer is refused before touching the boot root'

# ----------------------------------------------------------------- case 16 --
setup c16 yes 120 256
dd if=/dev/zero bs=1 count=32 2>/dev/null | tr '\0' N |
  dd of="$ST/boot.efi.gm2p" bs=1 seek=56 conv=notrunc 2>/dev/null
OUT="$TMP/c16/out"; ERR="$TMP/c16/err"; SHADOW=""
new=$(sha "$ST/boot.efi")
CANOE_ALLOW_NEW_SIGNER=1 CANOE_SIGNER_SOURCE=supplied run_install "$ST" "$D" ||
  fail "16: supplied signer install failed: $(cat "$ERR")"
[ "$(sha "$D/boot.efi")" = "$new" ] || fail '16: new loader was not installed'
grep -q 'CANOE-MARK: signer-changed source=supplied' "$OUT" ||
  fail '16: supplied signer source was not reported'
grep -q '^entry android-a$' "$D/canoe.cfg" || fail '16: active entry missing'
pass 'an explicitly supplied changed signer is accepted'

echo 'all canoe device-install fixtures passed'
