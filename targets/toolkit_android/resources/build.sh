#!/system/bin/sh
set -e

SCRIPTDIR=$(dirname "$0")
cd "$SCRIPTDIR"
rm -f ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap ./LinuxLoader.efi \
  ./ABL_original.efi ./patch_log.txt

if [ ! -f ./images/vbmeta.img ]; then
  echo "ERROR: matching images/vbmeta.img is required"
  exit 1
fi

./bin/extractfv -o ./ ./images/abl.img || { echo "ERROR: extractfv failed"; exit 1; }
if [ ! -f ./LinuxLoader.efi ]; then
  echo "ERROR: extractfv produced no LinuxLoader.efi"
  exit 1
fi

mv ./LinuxLoader.efi ./ABL_original.efi
if ! ./bin/patch_abl ./ABL_original.efi ./efisp/boot.efi > ./patch_log.txt 2>&1; then
  cat ./patch_log.txt
  echo "ERROR: patch_abl failed"
  exit 1
fi
cat ./patch_log.txt
if [ ! -s ./efisp/boot.efi ]; then
  echo "ERROR: patch_abl produced no nonempty efisp/boot.efi"
  exit 1
fi
if ! ./bin/mode2_profile derive --vbmeta ./images/vbmeta.img --out ./efisp/boot.efi.gm2p; then
  rm -f ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap
  echo "ERROR: mode2_profile derive failed"
  exit 1
fi
if ! ./bin/mode2_profile validate --input ./efisp/boot.efi.gm2p; then
  rm -f ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap
  echo "ERROR: mode2_profile validate failed"
  exit 1
fi
profile_size=$(wc -c < ./efisp/boot.efi.gm2p)
if [ "$profile_size" -ne 120 ]; then
  rm -f ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap
  echo "ERROR: mode2_profile output is not exactly 120 bytes"
  exit 1
fi
# --allow-incomplete: an ABL with no recorded RE evidence still gets a sidecar
# carrying the soundly derived identifier flags.
if ! ./bin/abl_tzmap derive ./ABL_original.efi -o ./efisp/boot.efi.tzmap --allow-incomplete; then
  rm -f ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap
  echo "ERROR: abl_tzmap derive failed"
  exit 1
fi
if ! ./bin/abl_tzmap validate ./efisp/boot.efi.tzmap; then
  rm -f ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap
  echo "ERROR: abl_tzmap validate failed"
  exit 1
fi
tzmap_size=$(wc -c < ./efisp/boot.efi.tzmap)
if [ "$tzmap_size" -ne 256 ]; then
  rm -f ./efisp/boot.efi ./efisp/boot.efi.gm2p ./efisp/boot.efi.tzmap
  echo "ERROR: abl_tzmap output is not exactly 256 bytes"
  exit 1
fi



if grep -q "Warning: Failed to patch ABL GBL" ./patch_log.txt; then
  gbl_ok=no
  echo ""
  echo "WARNING: No GBL exploit found in this ABL (Failed to patch ABL GBL)."
  echo "efisp/boot.efi is still produced and valid, but the abl partition must be"
  echo "downgraded to an older ABL with the GBL vulnerability before booting."
  echo "警告：此 ABL 中未找到 GBL 漏洞（Failed to patch ABL GBL）。"
  echo "efisp/boot.efi 仍已生成且有效，但开机前必须将 abl 分区降级为带 GBL 漏洞的旧版 ABL。"
else
  gbl_ok=yes
fi

echo ""
echo "========================================"
echo "Patched. Outputs:"
echo "  efisp/boot.efi     - patched ABL loader (use with the matching GM2P profile)"
echo "  efisp/boot.efi.gm2p - locked/green KeyMint profile for images/vbmeta.img"
echo "  efisp/boot.efi.tzmap - ABL-derived TrustZone interface map"
echo "  efisp/BOOTENTRIES  - boot entry list (includes the tools submenu)"
echo "  efisp/tools/       - tools submenu (Reboot / BL / ARB tools)"
echo "  BDS.efi            - superfastboot BDS (flash raw to the efisp partition)"
echo "  ABL_original.efi   - original unpatched loader (for analysis; do NOT flash)"
echo ""
echo "Note: this toolkit has no automated installer; place the files by hand."
echo "The Linux and Windows toolkits ship canoe prep-device / prep / install"
echo "for an ADB-driven install from a custom recovery."
echo ""
echo "---- Manual install flow (English) ----"
echo "1. Copy the efisp/ folder to the persist boot root:"
echo "     cp -r efisp/. /mnt/vendor/persist/efisp/"
echo "   (create /mnt/vendor/persist/efisp first if needed, e.g. via MT Manager)"
echo "2. sync"
if [ "$gbl_ok" = "no" ]; then
  echo "3. Downgrade the abl partition to an older ABL with the GBL vulnerability"
  echo "   (efisp/boot.efi and the abl partition do not need to match versions)"
  echo "4. Flash BDS.efi to the efisp partition:"
  echo "     dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M"
else
  echo "3. Flash BDS.efi to the efisp partition:"
  echo "     dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M"
fi
echo ""
echo "---- 手动安装步骤 (中文) ----"
echo "1. 将 efisp/ 文件夹复制到 persist 启动根目录："
echo "     cp -r efisp/. /mnt/vendor/persist/efisp/"
echo "   （如不存在请先创建 /mnt/vendor/persist/efisp，例如用 MT 管理器）"
echo "2. sync"
if [ "$gbl_ok" = "no" ]; then
  echo "3. 将 abl 分区降级为带 GBL 漏洞的旧版 ABL"
  echo "   （efisp/boot.efi 与 abl 分区版本不必一致）"
  echo "4. 将 BDS.efi 刷入 efisp 分区："
  echo "     dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M"
else
  echo "3. 将 BDS.efi 刷入 efisp 分区："
  echo "     dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M"
fi
echo "========================================"
