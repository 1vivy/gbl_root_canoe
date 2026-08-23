#!/usr/bin/env bash
#
# canoe_prep_device.sh - derive the canoe boot chain from the device itself.
#
# The standalone counterpart to canoe_prep.sh: no firmware package, no flasher,
# no vbmeta graft. Its only prerequisite is a custom recovery with ADB enabled.
# Nothing here overwrites recovery, so there is nothing to graft.
#
# It pulls the abl and vbmeta partitions from the target slot and runs build.sh
# on them, producing efisp/boot.efi plus its matching .gm2p and .tzmap sidecars.
#
# The target slot is the active one by default; --slot inactive selects the
# slot that is not booted right now, which is the slot an adb sideload has
# just written - the custom-ROM install case.
#
# TWO THINGS THIS DOES NOT DO
#
# 1. It does not touch the abl partition. Making that partition carry the GBL
#    vulnerability is a separate, operator-driven step:
#        fastboot flash abl <vulnerable>.img
#    boot.efi and the abl partition do not need to be the same version.
#
# 2. It does not invent a matching pair. boot.efi and boot.efi.gm2p must describe
#    the SAME firmware: boot.efi comes from abl, the profile from vbmeta. Pulling
#    both from the device gives a matching pair only while the abl partition still
#    holds its original ABL. If you have already flashed a downgraded ABL, pass
#    --abl and --vbmeta explicitly with a matching stock pair instead.
#
# So the natural order is: run this first, then canoe_stage.sh, and flash the
# vulnerable ABL last.
set -euo pipefail

CANOE_PROG=canoe_prep_device
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
cd "$SCRIPTDIR"
# shellcheck source=canoe_lib.sh
. ./canoe_lib.sh

SLOT=""
SERIAL=""
KEEP=no
ABL_OVERRIDE=""
VBMETA_OVERRIDE=""

usage() {
  cat <<'EOF'
Usage: canoe_prep_device.sh [options]

  --slot SLOT         source slot: _a, _b, active (default) or inactive.
                      "inactive" is the slot that is not booted right now,
                      e.g. the one an adb sideload has just written.
  --abl IMG           use this ABL image instead of pulling the partition
  --vbmeta IMG        use this vbmeta image instead of pulling the partition
  -s, --serial SERIAL adb device serial
      --keep-images   keep the pulled images in ./images
  -h, --help          this text

--abl and --vbmeta must be given together: they are a matched stock pair, and
mixing a supplied image with a pulled one is exactly the mismatch this guards
against.

Run this from a custom recovery with ADB enabled, then:
  ./canoe_stage.sh                          install the persist tree and the BDS
  fastboot flash abl <vulnerable>.img       only if the abl partition is not
                                            already a GBL-vulnerable version
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --slot)        SLOT=${2:?--slot needs a suffix}; shift 2 ;;
    --abl)         ABL_OVERRIDE=${2:?--abl needs an image}; shift 2 ;;
    --vbmeta)      VBMETA_OVERRIDE=${2:?--vbmeta needs an image}; shift 2 ;;
    -s|--serial)   SERIAL=${2:?--serial needs a value}; shift 2 ;;
    --keep-images) KEEP=yes; shift ;;
    -h|--help)     usage; exit 0 ;;
    *)             usage >&2; die "unknown argument: $1" ;;
  esac
done

# --abl and --vbmeta are a matched pair: accepting one alone would reintroduce
# exactly the boot.efi/.gm2p version mismatch this script warns about.
if { [ -n "$ABL_OVERRIDE" ] && [ -z "$VBMETA_OVERRIDE" ]; } ||
   { [ -z "$ABL_OVERRIDE" ] && [ -n "$VBMETA_OVERRIDE" ]; }; then
  die "--abl and --vbmeta must be given together (they are a matched pair)"
fi

mkdir -p ./images
SOURCE_DESC=""

if [ -n "$ABL_OVERRIDE" ]; then
  # -------------------------------------------------- explicit stock pair ----
  [ -f "$ABL_OVERRIDE" ]    || die "ABL image not found: $ABL_OVERRIDE"
  [ -f "$VBMETA_OVERRIDE" ] || die "vbmeta image not found: $VBMETA_OVERRIDE"
  step "Using the supplied stock pair"
  cp -f "$ABL_OVERRIDE" ./images/abl.img
  cp -f "$VBMETA_OVERRIDE" ./images/vbmeta.img
  printf '    abl:    %s\n' "$ABL_OVERRIDE"
  printf '    vbmeta: %s\n' "$VBMETA_OVERRIDE"
  SOURCE_DESC="the supplied stock pair"
else
  # ------------------------------------------------------- pull from device --
  step "Connecting"
  dev_init "$SERIAL"

  step "Resolving the source slot"
  case "$SLOT" in
    ""|active)
      SLOT=$(detect_slot)
      if [ -n "$SLOT" ]; then
        printf '    active slot: %s\n' "$SLOT"
      else
        printf '    no slot suffix reported; assuming a non-A/B layout\n'
      fi
      ;;
    inactive)
      ACTIVE_SLOT=$(detect_slot)
      [ -n "$ACTIVE_SLOT" ] || die "--slot inactive needs a detectable active slot; pass --slot _a or _b explicitly"
      SLOT=$(other_slot "$ACTIVE_SLOT") \
        || die "could not resolve the inactive slot from '$ACTIVE_SLOT'"
      printf '    active slot: %s; sourcing from the inactive slot %s\n' "$ACTIVE_SLOT" "$SLOT"
      printf '    (the slot an adb sideload has just written)\n'
      ;;
    _a|_b) printf '    slot forced to %s\n' "$SLOT" ;;
    a|b) SLOT="_$SLOT"; printf '    slot forced to %s\n' "$SLOT" ;;
    *) die "--slot must be _a, _b, active or inactive (got '$SLOT')" ;;
  esac

  ABL_DEV=$(resolve_part abl "$SLOT") \
    || die "abl$SLOT not found; pass --slot explicitly or supply --abl/--vbmeta"
  VBMETA_DEV=$(resolve_part vbmeta "$SLOT") \
    || die "vbmeta$SLOT not found; pass --slot explicitly or supply --abl/--vbmeta"
  printf '    abl:    %s\n' "$ABL_DEV"
  printf '    vbmeta: %s\n' "$VBMETA_DEV"

  step "Pulling the abl/vbmeta pair"
  dump_part "$ABL_DEV" ./images/abl.img
  printf '    images/abl.img: %s bytes\n' "$(wc -c < ./images/abl.img)"
  dump_part "$VBMETA_DEV" ./images/vbmeta.img
  printf '    images/vbmeta.img: %s bytes\n' "$(wc -c < ./images/vbmeta.img)"
  SOURCE_DESC="$ABL_DEV + $VBMETA_DEV"
fi

step "Deriving the boot chain"
./build.sh

for f in ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap; do
  [ -s "$f" ] || die "build.sh did not produce $f"
done

gbl=ok
if [ -f ./patch_log.txt ] && grep -q "Warning: Failed to patch ABL GBL" ./patch_log.txt; then
  gbl=missing
fi

[ "$KEEP" = no ] && rm -f ./images/abl.img ./images/vbmeta.img

cat <<EOF

========================================
canoe_prep_device: done.

Derived from $SOURCE_DESC:
  efisp/boot.efi          patched ABL loader
  efisp/boot.efi.gm2p     KeyMint profile for the matching vbmeta
  efisp/boot.efi.tzmap    ABL-derived TrustZone map

EOF
if [ "$gbl" = missing ]; then
  cat <<'EOF'
The source ABL does NOT carry the GBL vulnerability. The sidecars above are
still correct - they describe the stock pair - but the abl partition has to hold
a vulnerable ABL for the chain to load:

  ./canoe_stage.sh
  fastboot flash abl <vulnerable>.img
EOF
else
  cat <<'EOF'
The source ABL carries the GBL vulnerability.

If it was pulled from the device, the abl partition is already vulnerable and no
ABL flash is needed:

  ./canoe_stage.sh

If this ABL is an older downgrade image while the device runs newer firmware,
check that --vbmeta came from the SAME build; a mismatched boot.efi/.gm2p pair
is the one thing this step cannot detect for you.
EOF
fi
echo "========================================"
