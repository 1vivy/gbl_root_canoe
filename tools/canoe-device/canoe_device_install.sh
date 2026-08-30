#!/bin/sh
#
# canoe_device_install.sh - the persist install transaction, run ON THE DEVICE.
#
# This is the single source of truth for the install transaction. The host-side
# driver (`canoe install`, one Python implementation shared by the Linux and the
# Windows toolkit) and the on-device module both only stage files and invoke
# this script. It stays shell because it runs on the device, where there is no
# interpreter to ship.
#
#   sh canoe_device_install.sh <staging_dir> <efisp_dir> [<efisp_dev> <backup>]
#
#   staging_dir  holds boot.efi, boot.efi.gm2p and boot.efi.tzmap,
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
#   2. snapshot everything the commit will overwrite: the live triplet,
#      existing backup generation, canoe.cfg, tools/ and .canoe.gen
#   3. set aside foreign boot-root entries, then commit live -> boot_backup.*,
#      staged -> live
#   4. write canoe.cfg and install tools, write the informational .canoe.gen
#      stamp, sync
#   5. optionally back up efisp, write the BDS, verify it byte-for-byte
#
# The efisp partition contains only BDS.efi. Menu state lives in the
# atomically-written canoe.cfg file; the abl partition is never touched.
set -eu

STAGING=${1:-}
D=${2:-}
EFISP_DEV=${3:-}
BACKUP=${4:-}
STAGING_NAME=${STAGING##*/}
ACTIVE_SLOT=${5:-${CANOE_ACTIVE_SLOT:-}}
if [ -z "$ACTIVE_SLOT" ] && command -v getprop >/dev/null 2>&1; then
  ACTIVE_SLOT=$(getprop ro.boot.slot_suffix 2>/dev/null || :)
fi
case "$ACTIVE_SLOT" in _a|_b) ;; *) printf 'canoe-device: error: active slot unknown; pass it explicitly (arg 5 or CANOE_ACTIVE_SLOT)\n' >&2; exit 1 ;; esac
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
sha256_file() {
  sha256sum "$1" 2>/dev/null | cut -d ' ' -f1
}

# canoe.cfg has exactly one writer in this tree. This script owns the POLICY -
# which entries exist and which one is the default - and canoe_boot_entry.sh
# owns the FILE: one upsert per entry, so an entry the operator added by hand
# survives an install instead of being erased by a full regeneration.
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BOOT_ENTRY=${CANOE_BOOT_ENTRY:-$SELF_DIR/canoe_boot_entry.sh}
MODE=${CANOE_MODE:-}
case "$MODE" in
  0|1|2|'') ;;
  *) die "CANOE_MODE must be 0, 1 or 2, got $MODE" ;;
esac

entry_present() {
  [ -f "$D/canoe.cfg" ] && grep -q "^entry $1\$" "$D/canoe.cfg"
}

write_boot_entries() {
  # The active slot always boots boot.efi. The previous generation, when one
  # exists, is exposed through boot_backup.efi.
  entry_active=android-a
  entry_active_title='Android (slot A)'
  if [ "$ACTIVE_SLOT" = "_b" ]; then
    entry_active=android-b
    entry_active_title='Android (slot B)'
  fi
  # Read before the first upsert: a boot root with no canoe.cfg is a first
  # install, and only a first install may set the file-global fallback.
  entry_config_existed=no
  [ -e "$D/canoe.cfg" ] && entry_config_existed=yes

  set -- set "$D" --id "$entry_active" --title "$entry_active_title" \
    --image boot.efi --role active --default
  if [ -n "$MODE" ]; then
    set -- "$@" --mode "$MODE"
    [ "$entry_config_existed" = no ] && set -- "$@" --global-mode "$MODE"
  fi
  sh "$BOOT_ENTRY" "$@" || return 1

  if [ -s "$D/boot_backup.efi" ]; then
    sh "$BOOT_ENTRY" set "$D" --id android-backup --title 'Android (previous)' \
      --image boot_backup.efi --role backup || return 1
  elif entry_present android-backup; then
    sh "$BOOT_ENTRY" remove "$D" --id android-backup || return 1
  fi
}
# ------------------------------------------------------ 1. validate staged ---
[ -s "$STAGING/boot.efi" ]       || die "staged boot.efi is missing or empty"
[ -s "$STAGING/boot.efi.gm2p" ]  || die "staged boot.efi.gm2p is missing or empty"
[ -s "$STAGING/boot.efi.tzmap" ] || die "staged boot.efi.tzmap is missing or empty"
gm2p=$(size_of "$STAGING/boot.efi.gm2p")
tzmap=$(size_of "$STAGING/boot.efi.tzmap")
[ "$gm2p" = 120 ]  || die "boot.efi.gm2p must be exactly 120 bytes, got $gm2p"
[ "$tzmap" = 256 ] || die "boot.efi.tzmap must be exactly 256 bytes, got $tzmap"
if [ -n "$EFISP_DEV" ]; then
  [ -s "$STAGING/BDS.efi" ] || die "staged BDS.efi is missing or empty"
  [ -e "$EFISP_DEV" ]       || die "efisp device not found: $EFISP_DEV"
fi
mark "staged-set-validated gm2p=$gm2p tzmap=$tzmap"
signer_gate() {
  [ -e "$D/boot.efi.gm2p" ] || [ -L "$D/boot.efi.gm2p" ] || return 0

  signer_tmp=$(mktemp -d "${TMPDIR:-/tmp}/canoe-signer.XXXXXX") ||
    die "could not create temporary signer files"
  if ! dd if="$D/boot.efi.gm2p" of="$signer_tmp/live" \
    bs=1 skip=56 count=32 2>/dev/null; then
    rm -rf "$signer_tmp"
    die "could not read the installed vbmeta signer"
  fi
  if ! dd if="$STAGING/boot.efi.gm2p" of="$signer_tmp/staged" \
    bs=1 skip=56 count=32 2>/dev/null; then
    rm -rf "$signer_tmp"
    die "could not read the staged vbmeta signer"
  fi
  signer_rc=0
  cmp "$signer_tmp/live" "$signer_tmp/staged" >/dev/null 2>&1 || signer_rc=$?
  rm -rf "$signer_tmp"
  [ "$signer_rc" = 0 ] && return 0
  [ "$signer_rc" = 1 ] || die "could not compare the installed and staged vbmeta signers"

  # Two independent facts, and conflating them made one of the two channels
  # wrong: CANOE_SIGNER_SOURCE says where the vbmeta came from, which is all the
  # mark reports, and CANOE_ALLOW_NEW_SIGNER says whether a change may proceed.
  # The host refuses a change it was not told about; the module never refuses,
  # because a Custom ROM legitimately changes the signer and stranding the
  # operator mid-install is worse than a Mode 2 profile it then declines to use.
  mark "signer-changed source=${CANOE_SIGNER_SOURCE:-partition}"
  [ "${CANOE_ALLOW_NEW_SIGNER:-}" = 1 ] ||
    die "vbmeta signer changed. This is expected when moving to or from a custom ROM, and no tool here can prove which key is the OEM's."
}

signer_gate

mkdir -p "$D" "$D/tools" || die "could not create $D"

# ------------------------------------------- 2. snapshot what commit touches --
# The boot-root menu state is part of the transaction: a rollback that restored
# only the triplet would leave the old loader beside the new canoe.cfg and tools.
snapshot_file() {
  snapshot_source=$1
  snapshot_target=$2
  if [ -e "$snapshot_source" ] || [ -L "$snapshot_source" ]; then
    cp -f "$snapshot_source" "$snapshot_target" ||
      die "could not snapshot $(basename "$snapshot_source")"
  fi
}

rm -rf "$D"/.canoe.live.* "$D"/.canoe.oldbak.* \
  "$D"/.canoe.oldslot_a.* "$D"/.canoe.oldslot_b.* "$D/.canoe.oldmenu" \
  "$D/.canoe.oldgen" "$D/.canoe.oldcfg" "$D/.canoe.oldcfg.absent" \
  "$D/.canoe.foreign.moves" "$D/.canoe.gen.tmp" "$D"/.canoe.cfg.tmp.* \
  "$D"/.canoe.cfg.gen.* || :
mkdir -p "$D/.canoe.oldmenu" || die "could not create the snapshot directory"

snapshot_file "$D/boot.efi" "$D/.canoe.live.efi"
snapshot_file "$D/boot.efi.gm2p" "$D/.canoe.live.gm2p"
snapshot_file "$D/boot.efi.tzmap" "$D/.canoe.live.tzmap"
snapshot_file "$D/boot_backup.efi" "$D/.canoe.oldbak.efi"
snapshot_file "$D/boot_backup.efi.gm2p" "$D/.canoe.oldbak.gm2p"
snapshot_file "$D/boot_backup.efi.tzmap" "$D/.canoe.oldbak.tzmap"
snapshot_file "$D/boot_a.efi" "$D/.canoe.oldslot_a.efi"
snapshot_file "$D/boot_a.efi.gm2p" "$D/.canoe.oldslot_a.gm2p"
snapshot_file "$D/boot_a.efi.tzmap" "$D/.canoe.oldslot_a.tzmap"
snapshot_file "$D/boot_b.efi" "$D/.canoe.oldslot_b.efi"
snapshot_file "$D/boot_b.efi.gm2p" "$D/.canoe.oldslot_b.gm2p"
snapshot_file "$D/boot_b.efi.tzmap" "$D/.canoe.oldslot_b.tzmap"
if [ -e "$D/canoe.cfg" ]; then
  snapshot_file "$D/canoe.cfg" "$D/.canoe.oldcfg"
else
  : > "$D/.canoe.oldcfg.absent" || die "could not snapshot absent canoe.cfg"
fi
if [ -e "$D/.canoe.gen" ]; then
  snapshot_file "$D/.canoe.gen" "$D/.canoe.oldgen"
fi

if [ -d "$D/tools" ]; then
  mkdir -p "$D/.canoe.oldmenu/tools"
  for t in "$D"/tools/*; do
    [ -e "$t" ] || [ -L "$t" ] || continue
    cp -f "$t" "$D/.canoe.oldmenu/tools/" || die "could not snapshot $(basename "$t")"
  done
fi
sync || :

if [ -s "$D/.canoe.live.efi" ]; then
  mark "previous-generation-saved"
else
  mark "first-install"
fi

COMMITTED=yes

FOREIGN_EXISTED=no
[ -d "$D/.canoe.foreign" ] && FOREIGN_EXISTED=yes

migrate_passthrough() {
  passthrough_slot=a
  while [ "$passthrough_slot" = a ] || [ "$passthrough_slot" = b ]; do
    passthrough_loader="$D/boot_$passthrough_slot.efi"
    if [ -e "$passthrough_loader" ] || [ -L "$passthrough_loader" ]; then
      passthrough_id=android-$passthrough_slot
      if entry_present "$passthrough_id"; then
        sh "$BOOT_ENTRY" remove "$D" --id "$passthrough_id" ||
          fail "could not remove passthrough entry $passthrough_id"
      fi
      rm -f "$passthrough_loader" "$passthrough_loader.gm2p" \
        "$passthrough_loader.tzmap" ||
        fail "could not remove passthrough loader $passthrough_slot"
      mark "passthrough-row-migrated id=$passthrough_id"
    fi
    [ "$passthrough_slot" = a ] && passthrough_slot=b || passthrough_slot=
  done
}

set_aside_foreign() {
  foreign_entry=
  for foreign_entry in "$D"/* "$D"/.[!.]* "$D"/..?*; do
    [ -e "$foreign_entry" ] || [ -L "$foreign_entry" ] || continue
    foreign_name=${foreign_entry##*/}
    case "$foreign_name" in
      # The boot root's own members.
      boot.efi|boot.efi.gm2p|boot.efi.tzmap) continue ;;
      boot_backup.efi|boot_backup.efi.gm2p|boot_backup.efi.tzmap) continue ;;
      canoe.cfg|tools|"$STAGING_NAME") continue ;;
      .canoe.gen|.canoe.foreign) continue ;;
      .canoe.live.*|.canoe.oldbak.*|.canoe.oldslot_a.*|.canoe.oldslot_b.*|.canoe.oldmenu) continue ;;
      .canoe.oldgen|.canoe.oldcfg|.canoe.oldcfg.absent) continue ;;
      .canoe.foreign.moves|.canoe.gen.tmp) continue ;;
      .canoe.cfg.tmp.*|.canoe.cfg.gen.*) continue ;;
    esac
    if [ ! -d "$D/.canoe.foreign" ]; then
      mkdir -p "$D/.canoe.foreign" || fail "could not create .canoe.foreign"
    fi
    foreign_target="$D/.canoe.foreign/$foreign_name"
    foreign_suffix=1
    while [ -e "$foreign_target" ] || [ -L "$foreign_target" ]; do
      foreign_target="$D/.canoe.foreign/$foreign_name.$foreign_suffix"
      foreign_suffix=$((foreign_suffix + 1))
    done
    foreign_target_name=${foreign_target##*/}
    printf '%s\t%s\n' "$foreign_name" "$foreign_target_name" >> "$D/.canoe.foreign.moves" ||
      fail "could not record foreign entry $foreign_name"
    mv -f "$foreign_entry" "$foreign_target" ||
      fail "could not set aside foreign entry $foreign_name"
    say "set aside foreign entry $foreign_name in .canoe.foreign/$foreign_target_name"
  done
}

restore_foreign() {
  [ -f "$D/.canoe.foreign.moves" ] || return 0
  old_ifs=$IFS
  IFS='	'
  while read -r foreign_name foreign_target_name; do
    [ -n "$foreign_name" ] || continue
    mv -f "$D/.canoe.foreign/$foreign_target_name" "$D/$foreign_name" || :
  done < "$D/.canoe.foreign.moves"
  IFS=$old_ifs
  if [ "$FOREIGN_EXISTED" = no ]; then
    rmdir "$D/.canoe.foreign" 2>/dev/null || :
  fi
}

restore_snapshot_file() {
  restore_source=$1
  restore_target=$2
  if [ -e "$restore_source" ] || [ -L "$restore_source" ]; then
    cp -f "$restore_source" "$restore_target" || :
  else
    rm -f "$restore_target"
  fi
}

restore_pair() {
  [ "$COMMITTED" = no ] && return 0
  say "restoring the previous generation"
  if [ -e "$D/.canoe.live.efi" ] || [ -L "$D/.canoe.live.efi" ]; then
    restore_snapshot_file "$D/.canoe.live.efi" "$D/boot.efi"
    restore_snapshot_file "$D/.canoe.live.gm2p" "$D/boot.efi.gm2p"
    restore_snapshot_file "$D/.canoe.live.tzmap" "$D/boot.efi.tzmap"
  else
    rm -f "$D/boot.efi" "$D/boot.efi.gm2p" "$D/boot.efi.tzmap"
  fi

  for restore_slot in a b; do
    for restore_suffix in '' .gm2p .tzmap; do
      restore_snapshot_file \
        "$D/.canoe.oldslot_$restore_slot.efi$restore_suffix" \
        "$D/boot_$restore_slot.efi$restore_suffix"
    done
  done

  for restore_suffix in '' .gm2p .tzmap; do
    restore_snapshot_file "$D/.canoe.oldbak.efi$restore_suffix" \
      "$D/boot_backup.efi$restore_suffix"
  done

  rm -rf "$D/tools" || :
  mkdir -p "$D/tools" || :
  if [ -d "$D/.canoe.oldmenu/tools" ]; then
    for t in "$D"/.canoe.oldmenu/tools/*; do
      [ -e "$t" ] || [ -L "$t" ] || continue
      cp -f "$t" "$D/tools/" || :
    done
  fi

  if [ -e "$D/.canoe.oldgen" ]; then
    cp -f "$D/.canoe.oldgen" "$D/.canoe.gen" || :
  else
    rm -f "$D/.canoe.gen"
  fi
  if [ -e "$D/.canoe.oldcfg" ]; then
    cp -f "$D/.canoe.oldcfg" "$D/canoe.cfg" || :
  elif [ -e "$D/.canoe.oldcfg.absent" ]; then
    rm -f "$D/canoe.cfg"
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
  rm -rf "$D"/.canoe.live.* "$D"/.canoe.oldbak.* \
    "$D"/.canoe.oldslot_a.* "$D"/.canoe.oldslot_b.* "$D/.canoe.oldmenu" \
    "$D/.canoe.oldgen" "$D/.canoe.oldcfg" "$D/.canoe.oldcfg.absent" \
    "$D/.canoe.foreign.moves" "$D/.canoe.gen.tmp" "$D"/.canoe.cfg.gen.* \
    2>/dev/null || :
}

fail() {
  restore_efisp
  restore_foreign
  restore_pair
  cleanup
  die "$1"
}

# ------------------------------------------------------------- 3. commit -----
migrate_passthrough
set_aside_foreign
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
if [ -d "$STAGING/tools" ]; then
  for t in "$STAGING"/tools/*; do
    [ -e "$t" ] || continue

    cp -f "$t" "$D/tools/" || fail "could not install $(basename "$t")"
  done
fi
write_boot_entries || fail "could not install canoe.cfg"
sync || fail "sync of the persist tree failed"
mark "tree-synced"
write_generation() {
  boot_digest=$(sha256_file "$D/boot.efi")
  [ -n "$boot_digest" ] || fail "could not hash installed boot.efi"
  gm2p_digest=$(sha256_file "$D/boot.efi.gm2p")
  [ -n "$gm2p_digest" ] || fail "could not hash installed boot.efi.gm2p"
  tzmap_digest=$(sha256_file "$D/boot.efi.tzmap")
  [ -n "$tzmap_digest" ] || fail "could not hash installed boot.efi.tzmap"
  bds_field=-
  if [ -n "$EFISP_DEV" ]; then
    bds_field=$(sha256_file "$STAGING/BDS.efi")
    [ -n "$bds_field" ] || fail "could not hash installed BDS.efi"
  fi
  printf 'CANOEG1|%s|%s|%s|%s\n' "$bds_field" "$boot_digest" "$gm2p_digest" "$tzmap_digest" > "$D/.canoe.gen.tmp" ||
    fail "could not write .canoe.gen"
  mv -f "$D/.canoe.gen.tmp" "$D/.canoe.gen" || fail "could not install .canoe.gen"
  mark "generation-stamped"
}

write_generation

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
