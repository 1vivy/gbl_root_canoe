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

# Keep packaged EFI tools in the transaction when the archive contains them.
if [ -d "$SCRIPTDIR/efisp/tools" ]; then
  mkdir -p "$STAGE/tools"
  cp -r "$SCRIPTDIR/efisp/tools/." "$STAGE/tools/"
fi

# The canonical boot manager owns extraction, patching, sidecar derivation and
# validation.  Keep its output visible so failures retain the tool diagnostic.
BOOTMGR="$SCRIPTDIR/bin/canoe-bootmgr"
[ -x "$BOOTMGR" ] || die "bundled canoe-bootmgr is missing or not executable"
if ! build_output=$("$BOOTMGR" --json build \
    --abl "$ABL_SOURCE" --vbmeta "$VBMETA_SOURCE" --staged "$STAGE" \
    --tools "$SCRIPTDIR/bin" --keep-unpatched "$ABL_ORIGINAL" \
    --patch-log "$PATCH_LOG" 2>&1); then
  printf '%s\n' "$build_output"
  [ -f "$PATCH_LOG" ] && cat "$PATCH_LOG"
  case "$build_output" in
    *"build step extractfv"*) die "extractfv failed" ;;
    *"build step patch_abl"*) die "patch_abl failed" ;;
    *"build step mode2_profile derive"*) die "mode2_profile derive failed" ;;
    *"build step mode2_profile validate"*) die "mode2_profile validate failed" ;;
    *"build step abl_tzmap derive"*) die "abl_tzmap derive failed" ;;
    *"build step abl_tzmap validate"*) die "abl_tzmap validate failed" ;;
    *"build step abl_tzmap verify"*) die "abl_tzmap verify failed" ;;
    *) die "canoe-bootmgr build failed" ;;
  esac
fi
printf '%s\n' "$build_output"
case "$build_output" in
  *'"gbl_patched":false'*)
    echo "WARNING: No GBL exploit found in this ABL (Failed to patch ABL GBL)."
    echo "The abl partition must be downgraded to an older vulnerable ABL before booting."
    ;;
esac

# The boot manager's local-dir backend is the package format here: Android
# already mounted persist before invoking this temporary-root builder.
if [ "$VBMETA_KIND" = supplied ]; then
  "$BOOTMGR" --boot-root "$BOOT_ROOT" install --staged "$STAGE" \
    --slot "${SLOT#_}" --mode "$MODE" --allow-new-signer ||
    die "canoe-bootmgr install failed"
else
  "$BOOTMGR" --boot-root "$BOOT_ROOT" install --staged "$STAGE" \
    --slot "${SLOT#_}" --mode "$MODE" ||
    die "canoe-bootmgr install failed"
fi

cat <<'EOF'
Temporary-root files were installed in the persist boot root.
The operator owns the dd of the vulnerable ABL to the abl partition and of BDS.efi to efisp.
This wrapper performs no partition writes; nothing else was written.
EOF
