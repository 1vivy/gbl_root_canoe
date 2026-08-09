#!/system/bin/sh
set -e

SCRIPTDIR=$(dirname "$0")
cd "$SCRIPTDIR"

./bin/extractfv -o ./ ./images/abl.img || { echo "ERROR: extractfv failed"; exit 1; }
if [ ! -f ./LinuxLoader.efi ]; then
  echo "ERROR: extractfv produced no LinuxLoader.efi"
  exit 1
fi

mv ./LinuxLoader.efi ./ABL_original.efi
rm -f ./ABL.efi
if ! ./bin/patch_abl ./ABL_original.efi ./ABL.efi > ./patch_log.txt 2>&1; then
  cat ./patch_log.txt
  echo "ERROR: patch_abl failed"
  exit 1
fi
cat ./patch_log.txt
if [ ! -f ./ABL.efi ]; then
  echo "ERROR: patch_abl produced no ABL.efi"
  exit 1
fi

if grep -q "Warning: Failed to patch ABL GBL" ./patch_log.txt; then
  gbl_ok=no
  echo ""
  echo "WARNING: No GBL exploit found in this ABL (Failed to patch ABL GBL)."
  echo "ABL.efi is still produced and valid, but the abl partition must be downgraded"
  echo "to an older ABL with the GBL vulnerability before booting."
else
  gbl_ok=yes
fi

echo ""
echo "========================================"
echo "Patched. Output: ABL.efi (fake BL)"
echo "Note: toolkit is manual-install only; superfb does not provide"
echo "automated installation for toolkit users."
echo ""
echo "Manual install flow:"
echo "1. Create folder /mnt/vendor/persist/efisp (e.g. via MT Manager)"
echo "2. Copy ABL.efi into it"
echo "   (optional: also copy ABL_original.efi as the no-fake-BL entry)"
echo "3. Create boot entry file BOOTENTRIES with:"
echo "   ANDROID:ABL.efi"
echo "   (optional) ANDROID_NOFAKEBL:ABL_original.efi"
echo "4. sync"
if [ "$gbl_ok" = "no" ]; then
  echo "5. Downgrade the abl partition to an older ABL with the GBL vulnerability"
  echo "   (ABL.efi and the abl partition do not need to match versions)"
  echo "6. Flash BDS.efi to the efisp partition:"
  echo "   dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M"
else
  echo "5. Flash BDS.efi to the efisp partition:"
  echo "   dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M"
fi
echo "========================================"
