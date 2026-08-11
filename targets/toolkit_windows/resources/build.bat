REM change to the directory of this script
@echo off
cd /d %~dp0

bin\extractfv images/abl.img
if not exist extracted\LinuxLoader.efi (
  echo ERROR: extractfv produced no LinuxLoader.efi
  exit /b 1
)
move /Y extracted\LinuxLoader.efi ABL_original.efi >nul
if exist ABL.efi del ABL.efi
bin\patch_abl ABL_original.efi ABL.efi > patch_log.txt 2>&1
if errorlevel 1 (
  type patch_log.txt
  echo ERROR: patch_abl failed
  exit /b 1
)
type patch_log.txt
if not exist ABL.efi (
  echo ERROR: patch_abl produced no ABL.efi
  exit /b 1
)

set GBL_OK=yes
findstr /C:"Warning: Failed to patch ABL GBL" patch_log.txt >nul && (
  set GBL_OK=no
  echo.
  echo WARNING: No GBL exploit found in this ABL (Failed to patch ABL GBL).
  echo ABL.efi is still produced and valid, but the abl partition must be downgraded
  echo to an older ABL with the GBL vulnerability before booting.
)

echo.
echo ========================================
echo Patched. Output: ABL.efi (fake BL)
echo Note: toolkit is manual-install only; superfb does not provide
echo automated installation for toolkit users.
echo.
echo Manual install flow:
echo 1. Create folder /mnt/vendor/persist/efisp (e.g. via MT Manager)
echo 2. Copy ABL.efi into it
echo 3. Create boot entry file BOOTENTRIES with:
echo    ANDROID:ABL.efi
echo 4. sync
if "%GBL_OK%"=="no" (
  echo 5. Downgrade the abl partition to an older ABL with the GBL vulnerability
  echo    (ABL.efi and the abl partition do not need to match versions)
  echo 6. Flash BDS.efi to the efisp partition:
  echo    dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
) else (
  echo 5. Flash BDS.efi to the efisp partition:
  echo    dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
)
echo ========================================
