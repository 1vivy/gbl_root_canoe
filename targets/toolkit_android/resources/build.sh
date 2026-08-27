#!/system/bin/sh
# Derive a temporary-root generation on the active slot and commit it to the
# mounted persist boot root.  No partition is written by this script.
set -eu

SCRIPTDIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BOOT_ROOT=${CANOE_BOOT_ROOT:-/mnt/vendor/persist/efisp}
BY_NAME_DIR=${CANOE_BY_NAME_DIR:-/dev/block/by-name}
STAGE=
ABL_ORIGINAL=
PATCH_LOG=
LINUX_LOADER=

cleanup() {
  [ -n "$LINUX_LOADER" ] && rm -f "$LINUX_LOADER"
  [ -n "$ABL_ORIGINAL" ] && rm -f "$ABL_ORIGINAL"
  [ -n "$PATCH_LOG" ] && rm -f "$PATCH_LOG"
  if [ -n "$STAGE" ]; then
    rm -rf "$STAGE"
  fi
}
trap cleanup EXIT INT TERM HUP

die() {
  echo "ERROR: $*" >&2
  exit 1
}

uid=$(id -u 2>/dev/null) || die "root is required"
[ "$uid" = 0 ] || die "root is required"
command -v getprop >/dev/null 2>&1 || die "getprop is required"
SLOT=$(getprop ro.boot.slot_suffix 2>/dev/null || :)
case "$SLOT" in
  _a|_b) ;;
  *) die "ro.boot.slot_suffix must be _a or _b, got '$SLOT'" ;;
esac

MODE=1
ABL_SOURCE="$BY_NAME_DIR/abl$SLOT"
VBMETA_SOURCE="$BY_NAME_DIR/vbmeta$SLOT"
ABL_KIND=partition
VBMETA_KIND=partition
while [ "$#" -gt 0 ]; do
  case "$1" in
    --mode)
      [ "$#" -ge 2 ] || die "--mode requires 0 or 1"
      MODE=$2
      shift 2
      ;;
    --abl)
      [ "$#" -ge 2 ] || die "--abl requires a non-empty image path"
      ABL_SOURCE=$2
      ABL_KIND=supplied
      shift 2
      ;;
    --vbmeta)
      [ "$#" -ge 2 ] || die "--vbmeta requires a non-empty image path"
      VBMETA_SOURCE=$2
      VBMETA_KIND=supplied
      shift 2
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done
case "$MODE" in
  0|1) ;;
  2) die "Mode 2 belongs to the module/WebUI path; toolkit_android accepts only mode 0 or 1" ;;
  *) die "mode must be 0 or 1" ;;
esac

# Supplied regular files must be non-empty.  Device partitions are block
# devices, whose stat size is not portable, so existence is checked instead.
if [ "$ABL_KIND" = supplied ]; then
  [ -s "$ABL_SOURCE" ] || die "ABL input is missing or empty: $ABL_SOURCE"
else
  [ -e "$ABL_SOURCE" ] || die "active ABL partition is missing: $ABL_SOURCE"
fi
if [ "$VBMETA_KIND" = supplied ]; then
  [ -s "$VBMETA_SOURCE" ] || die "vbmeta input is missing or empty: $VBMETA_SOURCE"
else
  [ -e "$VBMETA_SOURCE" ] || die "active vbmeta partition is missing: $VBMETA_SOURCE"
fi

STAGE="$BOOT_ROOT/canoe-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
ABL_ORIGINAL="$STAGE/ABL_original.efi"
PATCH_LOG="$STAGE/patch_log.txt"
LINUX_LOADER="$STAGE/LinuxLoader.efi"

# Keep the packaged EFI tools in the transaction when the archive contains
# them.  They are copied into the boot root by canoe_device_install.sh.
if [ -d "$SCRIPTDIR/efisp/tools" ]; then
  mkdir -p "$STAGE/tools"
  cp -r "$SCRIPTDIR/efisp/tools/." "$STAGE/tools/"
fi

if ! "$SCRIPTDIR/bin/extractfv" -o "$STAGE" "$ABL_SOURCE"; then
  die "extractfv failed"
fi
[ -f "$LINUX_LOADER" ] || die "extractfv produced no LinuxLoader.efi"
mv "$LINUX_LOADER" "$ABL_ORIGINAL"

if ! "$SCRIPTDIR/bin/patch_abl" "$ABL_ORIGINAL" "$STAGE/boot.efi" > "$PATCH_LOG" 2>&1; then
  cat "$PATCH_LOG"
  die "patch_abl failed"
fi
cat "$PATCH_LOG"
[ -s "$STAGE/boot.efi" ] || die "patch_abl produced no nonempty boot.efi"

if ! "$SCRIPTDIR/bin/mode2_profile" derive --vbmeta "$VBMETA_SOURCE" \
  --out "$STAGE/boot.efi.gm2p"; then
  die "mode2_profile derive failed"
fi
if ! "$SCRIPTDIR/bin/mode2_profile" validate --input "$STAGE/boot.efi.gm2p"; then
  die "mode2_profile validate failed"
fi
profile_size=$(wc -c < "$STAGE/boot.efi.gm2p" | tr -d ' \n\r')
[ "$profile_size" = 120 ] || die "mode2_profile output is not exactly 120 bytes"

# --allow-incomplete keeps temporary-root installs usable with ABLs for which
# no recorded reverse-engineering evidence exists.
if ! "$SCRIPTDIR/bin/abl_tzmap" derive "$ABL_ORIGINAL" \
  -o "$STAGE/boot.efi.tzmap" --allow-incomplete; then
  die "abl_tzmap derive failed"
fi
if ! "$SCRIPTDIR/bin/abl_tzmap" validate "$STAGE/boot.efi.tzmap"; then
  die "abl_tzmap validate failed"
fi
tzmap_size=$(wc -c < "$STAGE/boot.efi.tzmap" | tr -d ' \n\r')
[ "$tzmap_size" = 256 ] || die "abl_tzmap output is not exactly 256 bytes"

if grep -q "Warning: Failed to patch ABL GBL" "$PATCH_LOG"; then
  echo "WARNING: No GBL exploit found in this ABL (Failed to patch ABL GBL)."
  echo "The abl partition must be downgraded to an older vulnerable ABL before booting."
fi

# The staging directory is inside the boot root, so the shared transaction can
# atomically rename its files on the same filesystem.  No efisp block device is
# passed: this is deliberately a tree-only install.
if [ "$VBMETA_KIND" = supplied ]; then
  (cd "$SCRIPTDIR" && \
    CANOE_MODE="$MODE" CANOE_ACTIVE_SLOT="$SLOT" \
    CANOE_ALLOW_NEW_SIGNER=1 CANOE_SIGNER_SOURCE=supplied \
    CANOE_BOOT_ENTRY=./canoe_boot_entry.sh \
    sh ./canoe_device_install.sh "$STAGE" "$BOOT_ROOT") ||
    die "canoe_device_install.sh failed"
else
  (cd "$SCRIPTDIR" && \
    CANOE_MODE="$MODE" CANOE_ACTIVE_SLOT="$SLOT" \
    CANOE_ALLOW_NEW_SIGNER= CANOE_SIGNER_SOURCE= \
    CANOE_BOOT_ENTRY=./canoe_boot_entry.sh \
    sh ./canoe_device_install.sh "$STAGE" "$BOOT_ROOT") ||
    die "canoe_device_install.sh failed"
fi

cat <<'EOF'
Temporary-root files were installed in the persist boot root.
The operator owns the dd of the vulnerable ABL to the abl partition and of BDS.efi to efisp.
This wrapper performs no partition writes; nothing else was written.
EOF
