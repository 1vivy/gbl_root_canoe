#!/bin/sh
#
# canoe_device_install.sh - the persist install transaction, run ON THE DEVICE.
#
# This is the single source of truth for the install transaction. The host-side
# drivers (canoe_stage.sh on Linux, canoe_stage.bat on Windows) only stage files
# and invoke this script, so there is exactly one implementation of the rollback
# logic rather than one per host platform.
#
#   sh canoe_device_install.sh <staging_dir> <efisp_dir> [<efisp_dev> <backup>]
#
#   staging_dir  holds boot.efi, boot.efi.gm2p, boot.efi.tzmap, BOOTENTRIES,
#                optionally tools/, and optionally BDS.efi. It should live on the
#                same filesystem as efisp_dir so the commit is a rename.
#   efisp_dir    the boot root, e.g. /persist/efisp or /mnt/vendor/persist/efisp
#   efisp_dev    block device for the efisp partition; omit to skip the BDS write
#   backup       where to write the pre-write copy of efisp_dev
#
# Every absolute device path arrives as an argument. Nothing is hardcoded, which
# is what lets the transaction be exercised directly on a host against ordinary
# directories and a regular file standing in for the block device.
#
# Transaction:
#   1. validate the staged set (sizes are exact contract values)
#   2. snapshot everything the commit will overwrite: the live triplet, the
#      existing backup generation, BOOTENTRIES and tools/
#   3. commit: live -> boot_backup.*, staged -> live
#   4. install BOOTENTRIES and tools, sync
#   5. optionally back up efisp, write the BDS, verify it byte-for-byte
#
# Any failure from step 3 onward restores every snapshotted item, so the boot root
# is never left with one generation's loader beside another's menu tree. A failed
# BDS write or verification also restores the partition from the backup taken in
# step 5. The persist tree is complete and synced before the BDS is written, so an
# interrupted run never leaves a live BDS pointing at half-installed sidecars.
#
# The preferred-mode record is never touched: only the leading len(BDS.efi) bytes
# of the partition are written, and the record lives 3072 bytes before its end.
# The abl partition is never touched.
set -eu

STAGING=${1:-}
D=${2:-}
EFISP_DEV=${3:-}
BACKUP=${4:-}

say()  { printf 'canoe-device: %s\n' "$1"; }
mark() { printf 'CANOE-MARK: %s\n' "$1"; }
die()  { printf 'canoe-device: error: %s\n' "$1" >&2; exit 1; }

[ -n "$STAGING" ] && [ -n "$D" ] || \
  die "usage: canoe_device_install.sh <staging_dir> <efisp_dir> [<efisp_dev> <backup>]"
[ -d "$STAGING" ] || die "staging directory not found: $STAGING"
if [ -n "$EFISP_DEV" ] && [ -z "$BACKUP" ]; then
  die "a backup path is required when an efisp device is given"
fi

size_of() { wc -c < "$1" | tr -d ' \n\r'; }

# ------------------------------------------------------ 1. validate staged ---
[ -s "$STAGING/boot.efi" ]       || die "staged boot.efi is missing or empty"
[ -s "$STAGING/boot.efi.gm2p" ]  || die "staged boot.efi.gm2p is missing or empty"
[ -s "$STAGING/boot.efi.tzmap" ] || die "staged boot.efi.tzmap is missing or empty"
[ -f "$STAGING/BOOTENTRIES" ]    || die "staged BOOTENTRIES is missing"
gm2p=$(size_of "$STAGING/boot.efi.gm2p")
tzmap=$(size_of "$STAGING/boot.efi.tzmap")
[ "$gm2p" = 120 ]  || die "boot.efi.gm2p must be exactly 120 bytes, got $gm2p"
[ "$tzmap" = 256 ] || die "boot.efi.tzmap must be exactly 256 bytes, got $tzmap"
if [ -n "$EFISP_DEV" ]; then
  [ -s "$STAGING/BDS.efi" ] || die "staged BDS.efi is missing or empty"
  [ -e "$EFISP_DEV" ]       || die "efisp device not found: $EFISP_DEV"
fi
mark "staged-set-validated gm2p=$gm2p tzmap=$tzmap"

mkdir -p "$D" "$D/tools" || die "could not create $D"

# ------------------------------------------- 2. snapshot what commit touches --
# The menu tree is part of the transaction: a rollback that restored only the
# triplet would leave the old loader beside the new BOOTENTRIES and tools.
rm -rf "$D"/.canoe.live.* "$D"/.canoe.oldbak.* "$D/.canoe.oldmenu" || :
mkdir -p "$D/.canoe.oldmenu" || die "could not create the snapshot directory"

[ -s "$D/boot.efi" ]              && cp -f "$D/boot.efi" "$D/.canoe.live.efi"
[ -s "$D/boot.efi.gm2p" ]         && cp -f "$D/boot.efi.gm2p" "$D/.canoe.live.gm2p"
[ -s "$D/boot.efi.tzmap" ]        && cp -f "$D/boot.efi.tzmap" "$D/.canoe.live.tzmap"
[ -s "$D/boot_backup.efi" ]       && cp -f "$D/boot_backup.efi" "$D/.canoe.oldbak.efi"
[ -s "$D/boot_backup.efi.gm2p" ]  && cp -f "$D/boot_backup.efi.gm2p" "$D/.canoe.oldbak.gm2p"
[ -s "$D/boot_backup.efi.tzmap" ] && cp -f "$D/boot_backup.efi.tzmap" "$D/.canoe.oldbak.tzmap"
[ -f "$D/BOOTENTRIES" ]           && cp -f "$D/BOOTENTRIES" "$D/.canoe.oldmenu/BOOTENTRIES"
if [ -d "$D/tools" ]; then
  mkdir -p "$D/.canoe.oldmenu/tools"
  for t in "$D"/tools/*; do
    [ -e "$t" ] || continue
    cp -f "$t" "$D/.canoe.oldmenu/tools/" || die "could not snapshot $(basename "$t")"
  done
fi
sync || :

if [ -s "$D/.canoe.live.efi" ]; then
  mark "previous-generation-saved"
else
  mark "first-install"
fi

COMMITTED=no

restore_pair() {
  [ "$COMMITTED" = no ] && return 0
  say "restoring the previous generation"
  if [ -s "$D/.canoe.live.efi" ]; then
    cp -f "$D/.canoe.live.efi" "$D/boot.efi" || :
    if [ -s "$D/.canoe.live.gm2p" ]; then
      cp -f "$D/.canoe.live.gm2p" "$D/boot.efi.gm2p" || :
    else
      rm -f "$D/boot.efi.gm2p"
    fi
    if [ -s "$D/.canoe.live.tzmap" ]; then
      cp -f "$D/.canoe.live.tzmap" "$D/boot.efi.tzmap" || :
    else
      rm -f "$D/boot.efi.tzmap"
    fi
  else
    # There was no live generation: a failed first install must not leave a
    # partial boot.efi behind.
    rm -f "$D/boot.efi" "$D/boot.efi.gm2p" "$D/boot.efi.tzmap"
  fi

  if [ -s "$D/.canoe.oldbak.efi" ]; then
    cp -f "$D/.canoe.oldbak.efi" "$D/boot_backup.efi" || :
  else
    rm -f "$D/boot_backup.efi"
  fi
  if [ -s "$D/.canoe.oldbak.gm2p" ]; then
    cp -f "$D/.canoe.oldbak.gm2p" "$D/boot_backup.efi.gm2p" || :
  else
    rm -f "$D/boot_backup.efi.gm2p"
  fi
  if [ -s "$D/.canoe.oldbak.tzmap" ]; then
    cp -f "$D/.canoe.oldbak.tzmap" "$D/boot_backup.efi.tzmap" || :
  else
    rm -f "$D/boot_backup.efi.tzmap"
  fi

  # Menu tree, snapshotted in step 2. Absent snapshot means it did not exist.
  if [ -f "$D/.canoe.oldmenu/BOOTENTRIES" ]; then
    cp -f "$D/.canoe.oldmenu/BOOTENTRIES" "$D/BOOTENTRIES" || :
  else
    rm -f "$D/BOOTENTRIES"
  fi
  rm -rf "$D/tools" || :
  mkdir -p "$D/tools" || :
  if [ -d "$D/.canoe.oldmenu/tools" ]; then
    for t in "$D"/.canoe.oldmenu/tools/*; do
      [ -e "$t" ] || continue
      cp -f "$t" "$D/tools/" || :
    done
  fi

  sync || :
  mark "pair-restored"
}

restore_efisp() {
  [ -n "$EFISP_DEV" ] && [ -s "$BACKUP" ] || return 0
  say "restoring $EFISP_DEV from $BACKUP"
  if dd if="$BACKUP" of="$EFISP_DEV" bs=4M conv=fsync 2>/dev/null; then
    sync || :
    mark "efisp-restored"
  else
    say "efisp restore FAILED; reflash it from $BACKUP before rebooting"
    mark "efisp-restore-failed"
  fi
}

cleanup() {
  rm -rf "$D"/.canoe.live.* "$D"/.canoe.oldbak.* "$D/.canoe.oldmenu" 2>/dev/null || :
}

fail() {
  restore_efisp
  restore_pair
  cleanup
  die "$1"
}

# ------------------------------------------------------------- 3. commit -----
COMMITTED=yes
if [ -s "$D/.canoe.live.efi" ]; then
  cp -f "$D/.canoe.live.efi" "$D/boot_backup.efi" || fail "could not write boot_backup.efi"
  # A backup without its matching sidecar is worse than no sidecar at all.
  if [ -s "$D/.canoe.live.gm2p" ]; then
    cp -f "$D/.canoe.live.gm2p" "$D/boot_backup.efi.gm2p" || fail "could not write boot_backup.efi.gm2p"
  else
    rm -f "$D/boot_backup.efi.gm2p"
  fi
  if [ -s "$D/.canoe.live.tzmap" ]; then
    cp -f "$D/.canoe.live.tzmap" "$D/boot_backup.efi.tzmap" || fail "could not write boot_backup.efi.tzmap"
  else
    rm -f "$D/boot_backup.efi.tzmap"
  fi
fi
mv -f "$STAGING/boot.efi"       "$D/boot.efi"       || fail "could not install boot.efi"
mv -f "$STAGING/boot.efi.gm2p"  "$D/boot.efi.gm2p"  || fail "could not install boot.efi.gm2p"
mv -f "$STAGING/boot.efi.tzmap" "$D/boot.efi.tzmap" || fail "could not install boot.efi.tzmap"
mark "committed"

# ----------------------------------------------- 4. boot menu tree + sync ----
cp -f "$STAGING/BOOTENTRIES" "$D/BOOTENTRIES" || fail "could not install BOOTENTRIES"
if [ -d "$STAGING/tools" ]; then
  for t in "$STAGING"/tools/*; do
    [ -e "$t" ] || continue
    cp -f "$t" "$D/tools/" || fail "could not install $(basename "$t")"
  done
fi
sync || fail "sync of the persist tree failed"
mark "tree-synced"

# ---------------------------------------------------------------- 5. BDS -----
if [ -n "$EFISP_DEV" ]; then
  part_bytes=$(blockdev --getsize64 "$EFISP_DEV" 2>/dev/null || size_of "$EFISP_DEV")
  case "$part_bytes" in
    ''|*[!0-9]*) fail "could not read the size of $EFISP_DEV" ;;
  esac
  bds_bytes=$(size_of "$STAGING/BDS.efi")
  [ "$bds_bytes" -le "$part_bytes" ] || fail "BDS.efi does not fit in $EFISP_DEV"
  if [ "$part_bytes" -lt 1048576 ]; then
    say "WARNING: $EFISP_DEV is under 1 MiB; the BDS mode store needs that much slack and will fall back to Mode 1"
  fi
  mark "efisp-size part=$part_bytes bds=$bds_bytes"

  dd if="$EFISP_DEV" of="$BACKUP" bs=4M 2>/dev/null || fail "could not back up $EFISP_DEV"
  [ "$(size_of "$BACKUP")" = "$part_bytes" ] || fail "backup of $EFISP_DEV is the wrong size"
  mark "efisp-backed-up"

  blockdev --setrw "$EFISP_DEV" 2>/dev/null || :
  if ! dd if="$STAGING/BDS.efi" of="$EFISP_DEV" bs=4M conv=fsync 2>/dev/null; then
    fail "write to $EFISP_DEV failed"
  fi
  sync || :

  # Compare the full written length, not a magic prefix.
  if ! dd if="$EFISP_DEV" of="$STAGING/readback.img" bs="$bds_bytes" count=1 2>/dev/null; then
    fail "could not read back $EFISP_DEV"
  fi
  if ! cmp "$STAGING/BDS.efi" "$STAGING/readback.img" >/dev/null 2>&1; then
    fail "verification of $EFISP_DEV failed"
  fi
  rm -f "$STAGING/readback.img"
  mark "efisp-verified bytes=$bds_bytes"
fi

cleanup
mark "done"
say "install complete"
