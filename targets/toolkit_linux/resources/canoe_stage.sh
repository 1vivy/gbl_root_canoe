#!/usr/bin/env bash
#
# canoe_stage.sh - install the prepared canoe boot chain over ADB.
#
# Works with either preparation front-end and needs nothing from a firmware
# package: its only prerequisite is a custom recovery with ADB enabled, because
# persist is writable there and no root on the running system is required.
#
#   canoe_prep.sh          package-coupled: derive from a firmware package,
#                          graft a custom recovery, substitute images in place
#   canoe_prep_device.sh   standalone: derive from the device's own partitions
#
# This script is a thin driver. It validates the local artifacts, pushes them to
# a staging directory inside the boot root, and hands the actual transaction to
# canoe_device_install.sh running on the device. The transaction - snapshot,
# commit, rollback, BDS backup and byte-for-byte verification - lives there and
# only there, so the Windows driver (canoe_stage.bat) shares one implementation
# rather than reimplementing the rollback logic.
#
# Staging inside the boot root is deliberate: it puts the staged files on the same
# filesystem as their destination, so the commit is a rename rather than a copy.
#
# This script never touches the abl partition. Making that partition carry the
# GBL vulnerability is the operator's own fastboot step; see README.canoe.md.
#
# --mode N additionally sets the BDS preferred-mode record on efisp after a
# successful install: the toolkit's Android-arm64 mode2_profile is pushed and
# its mode-write (aligned read-modify-write of the record block, verified by
# reread) runs on the device. This folds the old standalone set_mode1.sh helper
# into the one-shot install.
set -euo pipefail

CANOE_PROG=canoe_stage
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
cd "$SCRIPTDIR"
# shellcheck source=canoe_lib.sh
. ./canoe_lib.sh

SERIAL=""
PERSIST=""
SKIP_BDS=no
MODE=""
WORKDIR="$SCRIPTDIR/work"

usage() {
  cat <<'EOF'
Usage: canoe_stage.sh [options]

  -s, --serial SERIAL   adb device serial
      --persist PATH    persist mount point (default: autodetect /persist,
                        then /mnt/vendor/persist)
      --skip-bds        install the persist tree only; do not write efisp
      --mode 0|1|2      after a successful install, set the preferred boot mode
                        on efisp (0=honest unlocked, 1=ABL fake locked,
                        2=KM/SPSS profile); needs bin/mode2_profile-arm64
      --work DIR        local backup directory (default: ./work)
  -h, --help            this text

Expects, in the toolkit directory:
  efisp/boot.efi, efisp/boot.efi.gm2p, efisp/boot.efi.tzmap,
  efisp/BOOTENTRIES, efisp/tools/, BDS.efi, canoe_device_install.sh
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -s|--serial) SERIAL=${2:?--serial needs a value}; shift 2 ;;
    --persist)   PERSIST=${2:?--persist needs a path}; shift 2 ;;
    --work)      WORKDIR=${2:?--work needs a directory}; shift 2 ;;
    --skip-bds)  SKIP_BDS=yes; shift ;;
    --mode)      MODE=${2:?--mode needs 0, 1 or 2}; shift 2 ;;
    -h|--help)   usage; exit 0 ;;
    *)           usage >&2; die "unknown argument: $1" ;;
  esac
done

case "$MODE" in ''|0|1|2) ;; *) die "--mode must be 0, 1 or 2 (got '$MODE')" ;; esac

# ------------------------------------------------------------ local inputs ---
for f in efisp/boot.efi efisp/boot.efi.gm2p efisp/boot.efi.tzmap efisp/BOOTENTRIES; do
  [ -s "$f" ] || die "missing or empty: $f (run canoe_prep.sh or canoe_prep_device.sh first)"
done
[ -f canoe_device_install.sh ] || die "missing canoe_device_install.sh"
gm2p_size=$(wc -c < efisp/boot.efi.gm2p)
tzmap_size=$(wc -c < efisp/boot.efi.tzmap)
[ "$gm2p_size" = 120 ] || die "boot.efi.gm2p must be exactly 120 bytes, got $gm2p_size"
[ "$tzmap_size" = 256 ] || die "boot.efi.tzmap must be exactly 256 bytes, got $tzmap_size"
if [ "$SKIP_BDS" = no ]; then
  [ -s BDS.efi ] || die "missing BDS.efi (use --skip-bds to install the tree only)"
fi
mkdir -p "$WORKDIR"

step "Connecting"
dev_init "$SERIAL"

step "Locating the persist mount"
PERSIST=$(find_persist "$PERSIST")
sh_dev "touch $PERSIST/.canoe.rwtest && rm -f $PERSIST/.canoe.rwtest" \
  || die "$PERSIST is not writable"
printf '    persist: %s (writable)\n' "$PERSIST"

D="$PERSIST/efisp"
STAGE="$D/.canoe.stage"

# ---------------------------------------------------------------- staging ----
# Nothing live is touched here: the device-side script owns every write into the
# boot root, and it is not invoked until the whole staged set has landed.
step "Staging into $STAGE"
sh_dev "rm -rf $STAGE && mkdir -p $STAGE/tools" || die "could not create $STAGE"

stage_file() {
  push_dev "$1" "$STAGE/$2"
  printf '    %s\n' "$2"
}
stage_file efisp/boot.efi       boot.efi
stage_file efisp/boot.efi.gm2p  boot.efi.gm2p
stage_file efisp/boot.efi.tzmap boot.efi.tzmap
stage_file efisp/BOOTENTRIES    BOOTENTRIES
if [ -d efisp/tools ]; then
  for t in efisp/tools/*; do
    [ -e "$t" ] || continue
    stage_file "$t" "tools/$(basename "$t")"
  done
fi
[ "$SKIP_BDS" = no ] && stage_file BDS.efi BDS.efi
push_dev canoe_device_install.sh "$STAGE/canoe_device_install.sh"

# A failed transfer must not reach the transaction at all.
[ "$(dev_size "$STAGE/boot.efi")" = "$(wc -c < efisp/boot.efi)" ] \
  || die "boot.efi did not land at its full length"
[ "$(dev_size "$STAGE/boot.efi.gm2p")" = 120 ] || die "gm2p did not land as 120 bytes"
[ "$(dev_size "$STAGE/boot.efi.tzmap")" = 256 ] || die "tzmap did not land as 256 bytes"
if [ "$SKIP_BDS" = no ]; then
  [ "$(dev_size "$STAGE/BDS.efi")" = "$(wc -c < BDS.efi)" ] \
    || die "BDS.efi did not land at its full length"
fi
printf '    staged set validated on device\n'

# ------------------------------------------------------------ transaction ----
# efisp geometry is fixed, so read it before the transaction: an early failure
# beats a late one, and the --mode step below reuses the values afterwards.
DEV_BACKUP="$STAGE/efisp-backup.img"
if [ "$SKIP_BDS" = no ] || [ -n "$MODE" ]; then
  EFISP_DEV=$(resolve_part efisp "") || die "efisp partition not found"
  PART_BYTES=$(sh_dev "blockdev --getsize64 $EFISP_DEV" | tr -d '\r\n ')
  case "$PART_BYTES" in
    ''|*[!0-9]*) die "could not read the size of $EFISP_DEV" ;;
  esac
  printf '    efisp device: %s (%s bytes)\n' "$EFISP_DEV" "$PART_BYTES"
fi
if [ "$SKIP_BDS" = yes ]; then
  step "Running the device-side install (tree only)"
  install_args="$STAGE $D"
else
  step "Running the device-side install"
  install_args="$STAGE $D $EFISP_DEV $DEV_BACKUP"
fi

rc=0
sh_dev "sh $STAGE/canoe_device_install.sh $install_args" || rc=$?

if [ "$SKIP_BDS" = no ]; then
  # Keep the pre-write partition image on the host whether or not the write
  # succeeded: on failure it is the recovery artifact, on success the rollback one.
  if sh_dev "[ -s $DEV_BACKUP ]"; then
    pull_dev "$DEV_BACKUP" "$WORKDIR/efisp-backup.img" || \
      warn "could not retrieve the efisp backup from the device"
    [ -s "$WORKDIR/efisp-backup.img" ] && \
      printf '    efisp backup saved to %s\n' "$WORKDIR/efisp-backup.img"
  fi
fi

sh_dev "rm -rf $STAGE" >/dev/null 2>&1 || true

if [ "$rc" -ne 0 ]; then
  die "the device-side install failed and rolled back (exit $rc)"
fi

# ---------------------------------------------------------- preferred mode ---
# Runs after the transaction, not inside it: mode-write is an aligned
# read-modify-write of the record block with its own reread verification, so a
# failure here never endangers the just-installed boot chain.
if [ -n "$MODE" ]; then
  step "Setting the preferred boot mode to $MODE"
  ARM64_BIN=bin/mode2_profile-arm64
  [ -x "$ARM64_BIN" ] || die "--mode needs $ARM64_BIN in the toolkit (built by 'make target_toolkit_android')"
  [ "$PART_BYTES" -ge 1048576 ] || die "efisp is under 1 MiB ($PART_BYTES); no room for the mode record"
  # The store requires partition_bytes % block_size == 0. UFS is 4096; fall
  # back to 512 rather than guessing past a geometry the tool would reject.
  BLOCK=4096
  if [ $((PART_BYTES % BLOCK)) -ne 0 ]; then
    BLOCK=512
    [ $((PART_BYTES % BLOCK)) -eq 0 ] || die "efisp size $PART_BYTES is not a multiple of 4096 or 512"
  fi
  printf '    %s: %s bytes, block size %s\n' "$EFISP_DEV" "$PART_BYTES" "$BLOCK"

  REMOTE=/tmp/canoe-mode2_profile
  push_dev "$ARM64_BIN" "$REMOTE"
  sh_dev "chmod 755 $REMOTE" || die "could not chmod $REMOTE"
  sh_dev "$REMOTE mode-write --device $EFISP_DEV --partition-bytes $PART_BYTES --block-size $BLOCK --mode $MODE" \
    || { sh_dev "rm -f $REMOTE" >/dev/null 2>&1 || true; die "mode-write failed"; }
  AFTER=$(sh_dev "$REMOTE mode-read --device $EFISP_DEV --partition-bytes $PART_BYTES --block-size $BLOCK") \
    || { sh_dev "rm -f $REMOTE" >/dev/null 2>&1 || true; die "mode-read failed after the write"; }
  sh_dev "rm -f $REMOTE" >/dev/null 2>&1 || true
  case "$AFTER" in
    *"MODE=$MODE|"*"MODE_DEFAULTED=0"*) printf '    record reread: %s\n' "$AFTER" ;;
    *) die "mode record reread does not show mode $MODE non-defaulted: $AFTER" ;;
  esac
fi

cat <<EOF

========================================
canoe_stage: done.

Installed under $D:
  boot.efi, boot.efi.gm2p, boot.efi.tzmap, BOOTENTRIES, tools/
  boot_backup.efi (previous generation, selectable from the BDS menu)

EOF
if [ -n "$MODE" ]; then
  printf 'Preferred boot mode set to %s.\n' "$MODE"
else
  printf 'The preferred-mode record was left untouched.\n'
fi
cat <<EOF
Reboot to use the new boot chain.
========================================
EOF
