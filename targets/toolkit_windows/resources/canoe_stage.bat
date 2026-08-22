@echo off
rem canoe_stage.bat - install the prepared canoe boot chain over ADB (Windows).
rem
rem The Windows counterpart of canoe_stage.sh. Both are thin drivers: they
rem validate the local artifacts, push them into a staging directory inside the
rem boot root, and hand the actual transaction to canoe_device_install.sh running
rem on the device. Snapshot, commit, rollback, the efisp backup and the
rem byte-for-byte verification all live in that one device-side script, so this
rem file contains no rollback logic to drift out of step with the Linux driver.
rem
rem Prerequisite: a custom recovery with ADB enabled. Persist is writable there,
rem so no root on the running system is needed.
rem
rem This script never touches the abl partition. Making that partition carry the
rem GBL vulnerability is the operator's own `fastboot flash abl` step.
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "SERIAL="
set "PERSIST="
set "SKIP_BDS=no"
set "WORKDIR=%~dp0work"

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="-s"          ( set "SERIAL=%~2" & shift & shift & goto parse )
if /i "%~1"=="--serial"    ( set "SERIAL=%~2" & shift & shift & goto parse )
if /i "%~1"=="--persist"   ( set "PERSIST=%~2" & shift & shift & goto parse )
if /i "%~1"=="--work"      ( set "WORKDIR=%~2" & shift & shift & goto parse )
if /i "%~1"=="--skip-bds"  ( set "SKIP_BDS=yes" & shift & goto parse )
if /i "%~1"=="-h"          goto usage
if /i "%~1"=="--help"      goto usage
echo canoe_stage: error: unknown argument: %~1 1>&2
goto usage_fail

:usage
echo Usage: canoe_stage.bat [options]
echo.
echo   -s, --serial SERIAL   adb device serial
echo       --persist PATH    persist mount point ^(default: autodetect /persist,
echo                         then /mnt/vendor/persist^)
echo       --skip-bds        install the persist tree only; do not write efisp
echo       --work DIR        local backup directory ^(default: .\work^)
echo   -h, --help            this text
echo.
echo Expects, in the toolkit directory:
echo   efisp\boot.efi, efisp\boot.efi.gm2p, efisp\boot.efi.tzmap,
echo   efisp\BOOTENTRIES, efisp\tools\, BDS.efi, canoe_device_install.sh
exit /b 0
:usage_fail
exit /b 1

:parsed

rem ------------------------------------------------------------- adb -------
set "ADB=adb"
if exist "Platform-Tools\adb.exe" set "ADB=Platform-Tools\adb.exe"
set "ADBARGS="
if not "%SERIAL%"=="" set "ADBARGS=-s %SERIAL%"

rem ------------------------------------------------------ local inputs -----
for %%F in ("efisp\boot.efi" "efisp\boot.efi.gm2p" "efisp\boot.efi.tzmap" "efisp\BOOTENTRIES") do (
  if not exist %%F (
    echo canoe_stage: error: missing %%F ^(run canoe_prep.bat or canoe_prep_device.bat first^) 1>&2
    exit /b 1
  )
)
if not exist "canoe_device_install.sh" (
  echo canoe_stage: error: missing canoe_device_install.sh 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi.gm2p") do set "GM2P=%%~zA"
if not "%GM2P%"=="120" (
  echo canoe_stage: error: boot.efi.gm2p must be exactly 120 bytes, got %GM2P% 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi.tzmap") do set "TZMAP=%%~zA"
if not "%TZMAP%"=="256" (
  echo canoe_stage: error: boot.efi.tzmap must be exactly 256 bytes, got %TZMAP% 1>&2
  exit /b 1
)
if /i "%SKIP_BDS%"=="no" (
  if not exist "BDS.efi" (
    echo canoe_stage: error: missing BDS.efi ^(use --skip-bds to install the tree only^) 1>&2
    exit /b 1
  )
)
if not exist "%WORKDIR%" mkdir "%WORKDIR%"

echo.
echo [*] Connecting
rem NOT `adb wait-for-device`: it waits for state=device specifically, which a
rem TWRP-derived custom recovery never reports (it reports `recovery`), so it
rem blocks forever in exactly the environment this script is documented for.
set /a ADB_WAITED=0
:canoe_wait_transport
set "ADB_STATE="
for /f "usebackq delims=" %%S in (`"%ADB%" %ADBARGS% get-state 2^>nul`) do set "ADB_STATE=%%S"
if /i "%ADB_STATE%"=="device"   goto canoe_transport_ready
if /i "%ADB_STATE%"=="recovery" goto canoe_transport_ready
if /i "%ADB_STATE%"=="rescue"   goto canoe_transport_ready
if /i "%ADB_STATE%"=="sideload" goto canoe_transport_ready
if %ADB_WAITED% GEQ 60 goto canoe_transport_timeout
>nul ping -n 2 127.0.0.1
set /a ADB_WAITED+=1
goto canoe_wait_transport
:canoe_transport_timeout
if not defined ADB_STATE set "ADB_STATE=none"
echo canoe_stage: error: no usable adb transport after 60s ^(state: %ADB_STATE%^) 1>&2
exit /b 1
:canoe_transport_ready
echo     adb transport: %ADB_STATE%
"%ADB%" %ADBARGS% shell true >nul 2>&1
if errorlevel 1 (
  echo canoe_stage: error: no adb shell ^(enable ADB in recovery^) 1>&2
  exit /b 1
)

echo.
echo [*] Locating the persist mount
if "%PERSIST%"=="" (
  for %%P in (/persist /mnt/vendor/persist) do (
    if "!PERSIST!"=="" (
      "%ADB%" %ADBARGS% shell "grep -q ' %%P ' /proc/mounts" >nul 2>&1
      if not errorlevel 1 set "PERSIST=%%P"
    )
  )
)
if "%PERSIST%"=="" (
  echo     not mounted; attempting to mount /persist
  "%ADB%" %ADBARGS% shell "mkdir -p /persist && mount -t ext4 /dev/block/by-name/persist /persist" >nul 2>&1
  if errorlevel 1 (
    echo canoe_stage: error: could not mount persist; pass --persist PATH 1>&2
    exit /b 1
  )
  set "PERSIST=/persist"
)
"%ADB%" %ADBARGS% shell "grep -q ' %PERSIST% ' /proc/mounts"
if errorlevel 1 (
  echo canoe_stage: error: persist is not mounted at %PERSIST% 1>&2
  exit /b 1
)
"%ADB%" %ADBARGS% shell "touch %PERSIST%/.canoe.rwtest && rm -f %PERSIST%/.canoe.rwtest"
if errorlevel 1 (
  echo canoe_stage: error: %PERSIST% is not writable 1>&2
  exit /b 1
)
echo     persist: %PERSIST% ^(writable^)

set "D=%PERSIST%/efisp"
set "STAGE=%D%/.canoe.stage"

rem --------------------------------------------------------- staging -------
rem Nothing live is touched here: the device-side script owns every write into
rem the boot root, and it is not invoked until the whole staged set has landed.
echo.
echo [*] Staging into %STAGE%
"%ADB%" %ADBARGS% shell "rm -rf %STAGE% && mkdir -p %STAGE%/tools"
if errorlevel 1 (
  echo canoe_stage: error: could not create %STAGE% 1>&2
  exit /b 1
)

call :push "efisp\boot.efi"       "boot.efi"       || exit /b 1
call :push "efisp\boot.efi.gm2p"  "boot.efi.gm2p"  || exit /b 1
call :push "efisp\boot.efi.tzmap" "boot.efi.tzmap" || exit /b 1
call :push "efisp\BOOTENTRIES"    "BOOTENTRIES"    || exit /b 1
if exist "efisp\tools" (
  for %%T in ("efisp\tools\*") do (
    call :push "%%~fT" "tools/%%~nxT" || exit /b 1
  )
)
if /i "%SKIP_BDS%"=="no" call :push "BDS.efi" "BDS.efi" || exit /b 1
call :push "canoe_device_install.sh" "canoe_device_install.sh" || exit /b 1

rem A failed transfer must not reach the transaction at all.
call :devsize "%STAGE%/boot.efi.gm2p"
if not "%DEVSIZE%"=="120" (
  echo canoe_stage: error: gm2p did not land as 120 bytes ^(got %DEVSIZE%^) 1>&2
  goto stage_fail
)
call :devsize "%STAGE%/boot.efi.tzmap"
if not "%DEVSIZE%"=="256" (
  echo canoe_stage: error: tzmap did not land as 256 bytes ^(got %DEVSIZE%^) 1>&2
  goto stage_fail
)
call :devsize "%STAGE%/boot.efi"
for %%A in ("efisp\boot.efi") do set "LOCALSIZE=%%~zA"
if not "%DEVSIZE%"=="%LOCALSIZE%" (
  echo canoe_stage: error: boot.efi did not land at its full length 1>&2
  goto stage_fail
)
if /i "%SKIP_BDS%"=="no" (
  call :devsize "%STAGE%/BDS.efi"
  for %%A in ("BDS.efi") do set "LOCALSIZE=%%~zA"
  if not "!DEVSIZE!"=="!LOCALSIZE!" (
    echo canoe_stage: error: BDS.efi did not land at its full length 1>&2
    goto stage_fail
  )
)
echo     staged set validated on device

rem ----------------------------------------------------- transaction -------
set "DEVBACKUP=%STAGE%/efisp-backup.img"
if /i "%SKIP_BDS%"=="yes" (
  echo.
  echo [*] Running the device-side install ^(tree only^)
  set "INSTALLARGS=%STAGE% %D%"
) else (
  echo.
  echo [*] Running the device-side install
  "%ADB%" %ADBARGS% shell "[ -e /dev/block/by-name/efisp ]"
  if errorlevel 1 (
    echo canoe_stage: error: efisp partition not found 1>&2
    goto stage_fail
  )
  echo     efisp device: /dev/block/by-name/efisp
  set "INSTALLARGS=%STAGE% %D% /dev/block/by-name/efisp %DEVBACKUP%"
)

set "INSTALLRC=0"
"%ADB%" %ADBARGS% shell "sh %STAGE%/canoe_device_install.sh !INSTALLARGS!"
if errorlevel 1 set "INSTALLRC=1"

if /i "%SKIP_BDS%"=="no" (
  rem Keep the pre-write partition image on the host whether or not the write
  rem succeeded: on failure it is the recovery artifact, on success the rollback one.
  "%ADB%" %ADBARGS% shell "[ -s %DEVBACKUP% ]" >nul 2>&1
  if not errorlevel 1 (
    "%ADB%" %ADBARGS% pull "%DEVBACKUP%" "%WORKDIR%\efisp-backup.img" >nul
    if errorlevel 1 (
      echo     WARNING: could not retrieve the efisp backup from the device 1>&2
    ) else (
      echo     efisp backup saved to %WORKDIR%\efisp-backup.img
    )
  )
)

"%ADB%" %ADBARGS% shell "rm -rf %STAGE%" >nul 2>&1

if not "%INSTALLRC%"=="0" (
  echo canoe_stage: error: the device-side install failed and rolled back 1>&2
  exit /b 1
)

echo.
echo ========================================
echo canoe_stage: done.
echo.
echo Installed under %D%:
echo   boot.efi, boot.efi.gm2p, boot.efi.tzmap, BOOTENTRIES, tools/
echo   boot_backup.efi ^(previous generation, selectable from the BDS menu^)
echo.
echo The preferred-mode record was left untouched.
echo Reboot to use the new boot chain.
echo ========================================
exit /b 0

:stage_fail
"%ADB%" %ADBARGS% shell "rm -rf %STAGE%" >nul 2>&1
exit /b 1

:push
"%ADB%" %ADBARGS% push "%~1" "%STAGE%/%~2" >nul
if errorlevel 1 (
  echo canoe_stage: error: adb push failed: %~1 1>&2
  exit /b 1
)
echo     %~2
exit /b 0

:devsize
for /f "delims=" %%S in ('"%ADB%" %ADBARGS% shell "wc -c ^< %~1"') do set "DEVSIZE=%%S"
set "DEVSIZE=%DEVSIZE: =%"
exit /b 0
