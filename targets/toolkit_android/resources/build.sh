#!/system/bin/sh
set -e

SCRIPTDIR=$(dirname "$0")
cd "$SCRIPTDIR"
./bin/extractfv -o ./ ./images/abl.img
mv ./LinuxLoader.efi ./ABL_original.efi
./bin/patch_abl ./ABL_original.efi ./ABL.efi > ./patch_log.txt
cat ./patch_log.txt
echo "Patching completed. Output: ABL.efi    Please flash it to efisp"