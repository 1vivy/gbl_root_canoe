REM change to the directory of this script
@echo off
chcp 65001 >nul
cd /d "%~dp0"
if exist efisp\boot.efi del /q efisp\boot.efi
if exist efisp\boot.efi.gm2p del /q efisp\boot.efi.gm2p
if exist efisp\boot.efi.tzmap del /q efisp\boot.efi.tzmap
if exist extracted\LinuxLoader.efi del /q extracted\LinuxLoader.efi
if exist ABL_original.efi del /q ABL_original.efi
if exist patch_log.txt del /q patch_log.txt
if not exist images\vbmeta.img (
  echo ERROR: matching images\vbmeta.img is required
  exit /b 1
)

bin\extractfv images/abl.img
if errorlevel 1 (
  echo ERROR: extractfv failed
  exit /b 1
)
if not exist extracted\LinuxLoader.efi (
  echo ERROR: extractfv produced no LinuxLoader.efi
  exit /b 1
)
move /Y extracted\LinuxLoader.efi ABL_original.efi >nul
if errorlevel 1 (
  echo ERROR: failed to move extracted LinuxLoader.efi
  exit /b 1
)
bin\patch_abl ABL_original.efi efisp\boot.efi > patch_log.txt 2>&1
if errorlevel 1 (
  type patch_log.txt
  echo ERROR: patch_abl failed
  exit /b 1
)
type patch_log.txt
if not exist efisp\boot.efi (
  echo ERROR: patch_abl produced no efisp/boot.efi
  exit /b 1
)
for %%A in (efisp\boot.efi) do if "%%~zA"=="0" (
  echo ERROR: patch_abl produced an empty efisp\boot.efi
  exit /b 1
)

bin\mode2_profile.exe derive --vbmeta images\vbmeta.img --out efisp\boot.efi.gm2p
if errorlevel 1 (
  del /q efisp\boot.efi efisp\boot.efi.gm2p efisp\boot.efi.tzmap 2>nul
  echo ERROR: mode2_profile derive failed
  exit /b 1
)
bin\mode2_profile.exe validate --input efisp\boot.efi.gm2p
if errorlevel 1 (
  del /q efisp\boot.efi efisp\boot.efi.gm2p efisp\boot.efi.tzmap 2>nul
  echo ERROR: mode2_profile validate failed
  exit /b 1
)
for %%A in (efisp\boot.efi.gm2p) do if not "%%~zA"=="120" (
  del /q efisp\boot.efi efisp\boot.efi.gm2p efisp\boot.efi.tzmap 2>nul
  echo ERROR: mode2_profile output is not exactly 120 bytes
  exit /b 1
)
rem --allow-incomplete: an ABL with no recorded RE evidence still gets a sidecar
rem carrying the soundly derived identifier flags.
bin\abl_tzmap.exe derive ABL_original.efi -o efisp\boot.efi.tzmap --allow-incomplete
if errorlevel 1 (
  del /q efisp\boot.efi efisp\boot.efi.gm2p efisp\boot.efi.tzmap 2>nul
  echo ERROR: abl_tzmap derive failed
  exit /b 1
)
bin\abl_tzmap.exe validate efisp\boot.efi.tzmap
if errorlevel 1 (
  del /q efisp\boot.efi efisp\boot.efi.gm2p efisp\boot.efi.tzmap 2>nul
  echo ERROR: abl_tzmap validate failed
  exit /b 1
)
for %%A in (efisp\boot.efi.tzmap) do if not "%%~zA"=="256" (
  del /q efisp\boot.efi efisp\boot.efi.gm2p efisp\boot.efi.tzmap 2>nul
  echo ERROR: abl_tzmap output is not exactly 256 bytes
  exit /b 1
)



set GBL_OK=yes
findstr /C:"Warning: Failed to patch ABL GBL" patch_log.txt >nul && (
  set GBL_OK=no
  echo.
  echo WARNING: No GBL exploit found in this ABL ^(Failed to patch ABL GBL^).
  echo efisp/boot.efi is still produced and valid, but the abl partition must be
  echo downgraded to an older ABL with the GBL vulnerability before booting.
  echo 警告：此 ABL 中未找到 GBL 漏洞（Failed to patch ABL GBL）。
  echo efisp/boot.efi 仍已生成且有效，但开机前必须将 abl 分区降级为带 GBL 漏洞的旧版 ABL。
)

echo.
echo ========================================
echo Patched. Outputs:
echo   efisp/boot.efi     - patched ABL loader (use with the matching GM2P profile)
echo   efisp/boot.efi.gm2p - locked/green KeyMint profile for images/vbmeta.img
echo   efisp\boot.efi.tzmap - ABL-derived TrustZone interface map
echo   efisp/BOOTENTRIES  - boot entry list (includes the tools submenu)
echo   efisp/tools/       - tools submenu (Reboot / BL / ARB tools)
echo   BDS.efi            - superfastboot BDS (flash raw to the efisp partition)
echo   ABL_original.efi   - original unpatched loader (for analysis; do NOT flash)
echo.
echo ---- Install over ADB from a custom recovery (recommended) ----
echo Standalone, no firmware package:
echo      canoe_prep_device.bat    ^&^& rem derive from the device's own abl/vbmeta
echo      canoe_stage.bat          ^&^& rem install the persist tree, then the BDS
echo Alongside a Super Flasher / RegionalHybrid package:
echo      canoe_prep.bat --pkg ^<dir^> --recovery ^<custom^>.img --abl ^<vulnerable^>.img --in-place
echo      rem run the package's own flasher, then:
echo      canoe_stage.bat
echo See README.canoe.md for the full contract and guarantees.
echo.
echo Or place the files by hand:
echo.
echo ---- Manual install flow (English) ----
echo 1. Copy the efisp/ folder to the persist boot root:
echo      cp -r efisp/. /mnt/vendor/persist/efisp/
echo    (create /mnt/vendor/persist/efisp first if needed, e.g. via MT Manager)
echo 2. sync
if "%GBL_OK%"=="no" (
  echo 3. Downgrade the abl partition to an older ABL with the GBL vulnerability
  echo    ^(efisp/boot.efi and the abl partition do not need to match versions^)
  echo 4. Flash BDS.efi to the efisp partition:
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
) else (
  echo 3. Flash BDS.efi to the efisp partition:
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
)
echo.
echo ---- 手动安装步骤 (中文) ----
echo 1. 将 efisp/ 文件夹复制到 persist 启动根目录：
echo      cp -r efisp/. /mnt/vendor/persist/efisp/
echo    （如不存在请先创建 /mnt/vendor/persist/efisp，例如用 MT 管理器）
echo 2. sync
if "%GBL_OK%"=="no" (
  echo 3. 将 abl 分区降级为带 GBL 漏洞的旧版 ABL
  echo    （efisp/boot.efi 与 abl 分区版本不必一致）
  echo 4. 将 BDS.efi 刷入 efisp 分区：
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
) else (
  echo 3. 将 BDS.efi 刷入 efisp 分区：
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
)
echo ========================================
exit /b 0
