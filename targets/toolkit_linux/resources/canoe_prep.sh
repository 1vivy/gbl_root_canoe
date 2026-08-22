#!/usr/bin/env bash
#
# canoe_prep.sh - prepare a stock firmware package for a gbl_root_canoe install.
#
# This script does NOT flash anything and does NOT reimplement the packaged
# flasher (Super Flasher / RegionalHybrid). It only produces correct inputs:
#
#   1. lifts the OFFICIAL recovery vbmeta out of the package's own recovery.img
#      (host side, no device, no adb)
#   2. grafts that vbmeta onto a custom recovery image so the flasher can write
#      a custom recovery in place of the stock one
#   3. derives the canoe boot chain (boot.efi + .gm2p + .tzmap) from the
#      package's abl.img/vbmeta.img by calling build.sh
#   4. with --in-place, substitutes the prepared images into the package
#      directory (keeping .canoe-orig backups) so the packaged flasher picks
#      them up unchanged
#
# Afterwards you run the package's own flasher, then canoe_stage.sh.
#
# The sidecars always describe the package's STOCK abl/vbmeta pair, because
# that is the pair boot.efi and boot.efi.gm2p must agree with. --abl only
# changes which ABL image the flasher writes to the abl partition; it does not
# affect sidecar derivation.
set -euo pipefail

CANOE_PROG=canoe_prep
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
cd "$SCRIPTDIR"
# shellcheck source=canoe_lib.sh
. ./canoe_lib.sh

PKG=""
CUSTOM_RECOVERY=""
VULN_ABL=""
IN_PLACE=no
WORKDIR="$SCRIPTDIR/work"

usage() {
  cat <<'EOF'
Usage: canoe_prep.sh --pkg DIR [options]

  --pkg DIR          firmware image directory (e.g. OOS_FILES_HERE)
  --recovery IMG     custom recovery to graft the official vbmeta onto
  --abl IMG          vulnerable ABL to flash instead of the package's abl.img
  --in-place         substitute prepared images into --pkg, keeping
                     <name>.img.canoe-orig backups
  --work DIR         staging directory (default: ./work)
  -h, --help         this text

Outputs (in --work):
  vbmetas/recovery.vbmeta   official recovery vbmeta from the package
  grafted_recovery.img      custom recovery carrying that vbmeta (with --recovery)

Outputs (in the toolkit root, via build.sh):
  efisp/boot.efi, efisp/boot.efi.gm2p, efisp/boot.efi.tzmap
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --pkg)      PKG=${2:?--pkg needs a directory}; shift 2 ;;
    --recovery) CUSTOM_RECOVERY=${2:?--recovery needs an image}; shift 2 ;;
    --abl)      VULN_ABL=${2:?--abl needs an image}; shift 2 ;;
    --work)     WORKDIR=${2:?--work needs a directory}; shift 2 ;;
    --in-place) IN_PLACE=yes; shift ;;
    -h|--help)  usage; exit 0 ;;
    *)          usage >&2; die "unknown argument: $1" ;;
  esac
done

[ -n "$PKG" ] || { usage >&2; die "--pkg is required"; }
[ -d "$PKG" ] || die "package directory not found: $PKG"

VBMETABACKUP=./bin/vbmetabackup
VBMETAPORT=./bin/vbmetaport
[ -x "$VBMETABACKUP" ] || die "missing $VBMETABACKUP"
[ -x "$VBMETAPORT" ]   || die "missing $VBMETAPORT"

PKG_RECOVERY="$PKG/recovery.img"
PKG_ABL="$PKG/abl.img"
PKG_VBMETA="$PKG/vbmeta.img"
[ -f "$PKG_ABL" ]    || die "package is missing abl.img: $PKG_ABL"
[ -f "$PKG_VBMETA" ] || die "package is missing vbmeta.img: $PKG_VBMETA"

if [ -n "$CUSTOM_RECOVERY" ]; then
  [ -f "$CUSTOM_RECOVERY" ] || die "custom recovery not found: $CUSTOM_RECOVERY"
  [ -f "$PKG_RECOVERY" ] || \
    die "package is missing recovery.img, so its official vbmeta cannot be lifted"
fi
if [ -n "$VULN_ABL" ]; then
  [ -f "$VULN_ABL" ] || die "vulnerable ABL not found: $VULN_ABL"
fi

mkdir -p "$WORKDIR"

# ---------------------------------------------------------------- graft ------
GRAFTED=""
if [ -n "$CUSTOM_RECOVERY" ]; then
  step "Lifting the official recovery vbmeta out of $PKG_RECOVERY"
  rm -f "$WORKDIR/vbmetas/recovery.vbmeta"
  mkdir -p "$WORKDIR/vbmetas"
  # -f reads a local image; no device, no adb, no slot detection.
  "$VBMETABACKUP" -f "$PKG_RECOVERY" -n recovery -o "$WORKDIR/vbmetas" \
    || die "failed to extract the official recovery vbmeta"
  [ -s "$WORKDIR/vbmetas/recovery.vbmeta" ] || die "recovery.vbmeta is empty"

  step "Grafting it onto $CUSTOM_RECOVERY"
  GRAFTED="$WORKDIR/grafted_recovery.img"
  rm -f "$GRAFTED"
  "$VBMETAPORT" "$WORKDIR/vbmetas/recovery.vbmeta" "$CUSTOM_RECOVERY" "$GRAFTED" \
    || die "vbmetaport failed"
  [ -s "$GRAFTED" ] || die "grafted recovery is empty"

  graft_size=$(wc -c < "$GRAFTED")
  custom_size=$(wc -c < "$CUSTOM_RECOVERY")
  [ "$graft_size" = "$custom_size" ] || \
    die "grafted recovery changed size ($custom_size -> $graft_size)"
  printf '    grafted_recovery.img: %s bytes (size preserved)\n' "$graft_size"
fi

# ------------------------------------------------------------- sidecars ------
step "Deriving the canoe boot chain from the package's stock abl/vbmeta pair"
mkdir -p ./images
cp -f "$PKG_ABL" ./images/abl.img
cp -f "$PKG_VBMETA" ./images/vbmeta.img
./build.sh

for f in ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap; do
  [ -s "$f" ] || die "build.sh did not produce $f"
done

# ------------------------------------------------------------- in-place ------
substitute() {
  src=$1; dst=$2; label=$3
  if [ ! -f "$dst.canoe-orig" ]; then
    cp -f "$dst" "$dst.canoe-orig" || die "could not back up $dst"
    printf '    backed up %s -> %s.canoe-orig\n' "$dst" "$dst"
  else
    printf '    backup already present: %s.canoe-orig\n' "$dst"
  fi
  cp -f "$src" "$dst" || die "could not install $label into the package"
  printf '    installed %s as %s\n' "$label" "$dst"
}

if [ "$IN_PLACE" = yes ]; then
  step "Substituting prepared images into $PKG"
  [ -n "$GRAFTED" ] && substitute "$GRAFTED" "$PKG_RECOVERY" "grafted recovery"
  [ -n "$VULN_ABL" ] && substitute "$VULN_ABL" "$PKG_ABL" "vulnerable ABL"
  if [ -z "$GRAFTED" ] && [ -z "$VULN_ABL" ]; then
    printf '    nothing to substitute (no --recovery, no --abl)\n'
  fi
fi

# ---------------------------------------------------------------- report -----
cat <<EOF

========================================
canoe_prep: done.

Prepared:
EOF
[ -n "$GRAFTED" ] && echo "  $GRAFTED"
cat <<EOF
  efisp/boot.efi          patched ABL loader
  efisp/boot.efi.gm2p     KeyMint profile for $PKG_VBMETA
  efisp/boot.efi.tzmap    ABL-derived TrustZone map
  BDS.efi                 superfastboot BDS (written raw to efisp)

Next:
EOF
if [ "$IN_PLACE" = yes ]; then
  cat <<EOF
  1. Run the package's own flasher (Super_Flasher.sh / RegionalHybrid).
     It will pick up the substituted images from $PKG automatically.
EOF
else
  cat <<EOF
  1. Install the prepared images into $PKG yourself, or rerun with --in-place:
EOF
  [ -n "$GRAFTED" ] && echo "       cp $GRAFTED $PKG_RECOVERY"
  [ -n "$VULN_ABL" ] && echo "       cp $VULN_ABL $PKG_ABL"
  echo "     then run the package's own flasher."
fi
cat <<EOF
  2. Boot the custom recovery and enable ADB from its UI.
  3. Run: ./canoe_stage.sh
========================================
EOF
