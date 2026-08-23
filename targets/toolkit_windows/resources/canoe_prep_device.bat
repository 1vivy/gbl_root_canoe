@echo off
setlocal EnableExtensions DisableDelayedExpansion
chcp 65001 >nul
cd /d "%~dp0"
if errorlevel 1 (
  echo canoe_prep_device: error: failed to change to the toolkit directory 1>&2
  exit /b 1
)

set "SLOT="
set "SERIAL="
set "KEEP=no"
set "ABL_OVERRIDE="
set "SLOT="
set "SLOT_FORCED=no"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--slot" (
  if "%~2"=="" (
    echo canoe_prep_device: error: --slot needs a value 1>&2
    exit /b 1
  )
  if /I "%~2"=="active" (
    set "SLOT="
    set "SLOT_FORCED=no"
  ) else if /I "%~2"=="inactive" (
    set "SLOT=inactive"
    set "SLOT_FORCED=yes"
  ) else (
    call :normalize_slot "%~2"
    if errorlevel 1 exit /b 1
    set "SLOT_FORCED=yes"
  )
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--abl" (
  if "%~2"=="" (
    echo canoe_prep_device: error: --abl needs an image 1>&2
    exit /b 1
  )
  set "ABL_OVERRIDE=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--vbmeta" (
  if "%~2"=="" (
    echo canoe_prep_device: error: --vbmeta needs an image 1>&2
    exit /b 1
  )
  set "VBMETA_OVERRIDE=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="-s" (
  if "%~2"=="" (
    echo canoe_prep_device: error: --serial needs a value 1>&2
    exit /b 1
  )
  set "SERIAL=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--serial" (
  if "%~2"=="" (
    echo canoe_prep_device: error: --serial needs a value 1>&2
    exit /b 1
  )
  set "SERIAL=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--keep-images" (
  set "KEEP=yes"
  shift
  goto parse_args
)
if /I "%~1"=="-h" goto show_help
if /I "%~1"=="--help" goto show_help
call :usage 1>&2
echo canoe_prep_device: error: unknown argument: %~1 1>&2
exit /b 1

:show_help
call :usage
exit /b 0

:args_done
if defined ABL_OVERRIDE if defined VBMETA_OVERRIDE goto pair_ok
if not defined ABL_OVERRIDE if not defined VBMETA_OVERRIDE goto pair_ok
echo canoe_prep_device: error: --abl and --vbmeta must be given together ^(they are a matched pair^) 1>&2
exit /b 1

:pair_ok
if not exist "images\." mkdir "images"
if errorlevel 1 (
  echo canoe_prep_device: error: could not create images directory 1>&2
  exit /b 1
)
set "SOURCE_DESC="
if defined ABL_OVERRIDE goto supplied_pair

goto pull_device

:supplied_pair
if not exist "%ABL_OVERRIDE%" (
  echo canoe_prep_device: error: ABL image not found: %ABL_OVERRIDE% 1>&2
  exit /b 1
)
if not exist "%VBMETA_OVERRIDE%" (
  echo canoe_prep_device: error: vbmeta image not found: %VBMETA_OVERRIDE% 1>&2
  exit /b 1
)
echo.
echo [*] Using the supplied stock pair
copy /Y "%ABL_OVERRIDE%" "images\abl.img" >nul
if errorlevel 1 (
  echo canoe_prep_device: error: could not copy the supplied ABL image 1>&2
  exit /b 1
)
copy /Y "%VBMETA_OVERRIDE%" "images\vbmeta.img" >nul
if errorlevel 1 (
  echo canoe_prep_device: error: could not copy the supplied vbmeta image 1>&2
  exit /b 1
)
echo     abl:    %ABL_OVERRIDE%
echo     vbmeta: %VBMETA_OVERRIDE%
set "SOURCE_DESC=the supplied stock pair"
goto derive

:pull_device
echo.
echo [*] Connecting
if exist "Platform-Tools\adb.exe" (
  set "ADB=Platform-Tools\adb.exe"
) else (
  where adb >nul 2>&1
  if errorlevel 1 (
    echo canoe_prep_device: error: adb not found ^(put it on PATH or in .\Platform-Tools\^) 1>&2
    exit /b 1
  )
  set "ADB=adb"
)
set "ADB_SERIAL_ARGS="
if defined SERIAL set "ADB_SERIAL_ARGS=-s "%SERIAL%""
rem NOT `adb wait-for-device`: it waits for state=device specifically, which a
rem TWRP-derived custom recovery never reports (it reports `recovery`), so it
rem blocks forever in exactly the environment this script is documented for.
set /a ADB_WAITED=0
:canoe_wait_transport
set "ADB_STATE="
for /f "usebackq delims=" %%S in (`"%ADB%" %ADB_SERIAL_ARGS% get-state 2^>nul`) do set "ADB_STATE=%%S"
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
echo canoe_prep_device: error: no usable adb transport after 60s ^(state: %ADB_STATE%^) 1>&2
exit /b 1
:canoe_transport_ready
echo     adb transport: %ADB_STATE%
"%ADB%" %ADB_SERIAL_ARGS% shell true >nul 2>&1
if errorlevel 1 (
  echo canoe_prep_device: error: no adb shell ^(enable ADB in recovery^) 1>&2
  exit /b 1
)

echo.
echo [*] Resolving the source slot
if /I "%SLOT%"=="inactive" goto resolve_inactive_slot
if defined SLOT goto slot_resolved
call :detect_active_slot
if errorlevel 1 exit /b 1
goto slot_resolved

:resolve_inactive_slot
set "SLOT="
call :detect_active_slot
if errorlevel 1 exit /b 1
if not defined SLOT (
  echo canoe_prep_device: error: --slot inactive needs a detectable active slot; pass --slot _a or _b explicitly 1>&2
  exit /b 1
)
set "ACTIVE_SLOT=%SLOT%"
if /I "%SLOT%"=="_a" (set "SLOT=_b") else (set "SLOT=_a")
echo     active slot: %ACTIVE_SLOT%; sourcing from the inactive slot %SLOT%
echo     ^(the slot an adb sideload has just written^)
goto slot_message_done

:slot_resolved
if not defined SLOT goto no_slot_message
if "%SLOT_FORCED%"=="yes" goto slot_forced
echo     active slot: %SLOT%
goto slot_message_done

:slot_forced
echo     slot forced to %SLOT%
goto slot_message_done

:no_slot_message
echo     no slot suffix reported; using non-A/B partition names

:slot_message_done
if defined SLOT goto set_slotted_paths
set "ABL_DEV=/dev/block/by-name/abl"
set "VBMETA_DEV=/dev/block/by-name/vbmeta"
goto check_partitions

:set_slotted_paths
set "ABL_DEV=/dev/block/by-name/abl%SLOT%"
set "VBMETA_DEV=/dev/block/by-name/vbmeta%SLOT%"

:check_partitions
"%ADB%" %ADB_SERIAL_ARGS% shell "[ -e %ABL_DEV% ]" >nul 2>&1
if errorlevel 1 (
  echo canoe_prep_device: error: abl%SLOT% not found; pass --slot explicitly or supply --abl/--vbmeta 1>&2
  exit /b 1
)
"%ADB%" %ADB_SERIAL_ARGS% shell "[ -e %VBMETA_DEV% ]" >nul 2>&1
if errorlevel 1 (
  echo canoe_prep_device: error: vbmeta%SLOT% not found; pass --slot explicitly or supply --abl/--vbmeta 1>&2
  exit /b 1
)
echo     abl:    %ABL_DEV%
echo     vbmeta: %VBMETA_DEV%

echo.
echo [*] Pulling the abl/vbmeta pair
call :dump_partition "%ABL_DEV%" "images\abl.img" "abl"
if errorlevel 1 exit /b 1
call :dump_partition "%VBMETA_DEV%" "images\vbmeta.img" "vbmeta"
if errorlevel 1 exit /b 1
set "SOURCE_DESC=%ABL_DEV% + %VBMETA_DEV%"

goto derive

:derive
echo.
echo [*] Deriving the boot chain
call build.bat
if errorlevel 1 (
  echo canoe_prep_device: error: build.bat failed 1>&2
  exit /b 1
)
if not exist "efisp\boot.efi" (
  echo canoe_prep_device: error: build.bat did not produce efisp\boot.efi 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi") do if "%%~zA"=="0" (
  echo canoe_prep_device: error: build.bat produced an empty efisp\boot.efi 1>&2
  exit /b 1
)
if not exist "efisp\boot.efi.gm2p" (
  echo canoe_prep_device: error: build.bat did not produce efisp\boot.efi.gm2p 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi.gm2p") do if not "%%~zA"=="120" (
  echo canoe_prep_device: error: efisp\boot.efi.gm2p is not exactly 120 bytes 1>&2
  exit /b 1
)
if not exist "efisp\boot.efi.tzmap" (
  echo canoe_prep_device: error: build.bat did not produce efisp\boot.efi.tzmap 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi.tzmap") do if not "%%~zA"=="256" (
  echo canoe_prep_device: error: efisp\boot.efi.tzmap is not exactly 256 bytes 1>&2
  exit /b 1
)

set "GBL_MISSING=no"
if not exist "patch_log.txt" goto remove_images
findstr /C:"Warning: Failed to patch ABL GBL" "patch_log.txt" >nul 2>&1
if errorlevel 1 goto gbl_no_match
set "GBL_MISSING=yes"
:gbl_no_match
if errorlevel 2 (
  echo canoe_prep_device: error: failed to inspect patch_log.txt 1>&2
  exit /b 1
)

:remove_images
if not "%KEEP%"=="no" goto report
if exist "images\abl.img" del /q "images\abl.img"
if errorlevel 1 (
  echo canoe_prep_device: error: could not remove images\abl.img 1>&2
  exit /b 1
)
if exist "images\vbmeta.img" del /q "images\vbmeta.img"
if errorlevel 1 (
  echo canoe_prep_device: error: could not remove images\vbmeta.img 1>&2
  exit /b 1
)

goto report

:report
echo.
echo ========================================
echo canoe_prep_device: done.
echo.
echo Derived from %SOURCE_DESC%:
echo   efisp/boot.efi          patched ABL loader
echo   efisp/boot.efi.gm2p     KeyMint profile for the matching vbmeta
echo   efisp/boot.efi.tzmap    ABL-derived TrustZone map
echo.
if "%GBL_MISSING%"=="yes" goto report_missing
echo The source ABL carries the GBL vulnerability.
echo.
echo If it was pulled from the device, the abl partition is already vulnerable and no ABL flash is needed:
echo.
echo   canoe_stage.bat
echo.
echo If this ABL is an older downgrade image while the device runs newer firmware,
echo check that --vbmeta came from the SAME build; a mismatched boot.efi/.gm2p pair
echo is the one thing this step cannot detect for you.
goto report_end

:report_missing
echo The source ABL does NOT carry the GBL vulnerability. The sidecars above are
echo still correct - they describe the stock pair - but the abl partition has to hold
echo a vulnerable ABL for the chain to load:
echo.
echo   canoe_stage.bat
echo   fastboot flash abl ^<vulnerable^>.img
goto report_end

:report_end
echo ========================================
exit /b 0

:dump_partition
set "DUMP_DEV=%~1"
set "DUMP_OUT=%~2"
"%ADB%" %ADB_SERIAL_ARGS% shell dd if=%DUMP_DEV% of=/tmp/canoe-dump.img bs=4M >nul 2>&1
if errorlevel 1 (
  echo canoe_prep_device: error: could not read %DUMP_DEV% 1>&2
  exit /b 1
)
set "DUMP_PULL_FAILED=no"
"%ADB%" %ADB_SERIAL_ARGS% pull /tmp/canoe-dump.img "%DUMP_OUT%" >nul 2>&1
if errorlevel 1 set "DUMP_PULL_FAILED=yes"
"%ADB%" %ADB_SERIAL_ARGS% shell rm -f /tmp/canoe-dump.img >nul 2>&1
if errorlevel 1 (
  echo canoe_prep_device: error: could not remove the device dump 1>&2
  exit /b 1
)
if "%DUMP_PULL_FAILED%"=="yes" (
  echo canoe_prep_device: error: adb pull failed: %DUMP_DEV% 1>&2
  exit /b 1
)
if not exist "%DUMP_OUT%" (
  echo canoe_prep_device: error: dump of %DUMP_DEV% is empty 1>&2
  exit /b 1
)
for %%A in ("%DUMP_OUT%") do if "%%~zA"=="0" (
  echo canoe_prep_device: error: dump of %DUMP_DEV% is empty 1>&2
  exit /b 1
)
for %%A in ("%DUMP_OUT%") do echo     %DUMP_OUT%: %%~zA bytes
exit /b 0

:parse_slot_token
set "SLOT_TOKEN=%~1"
for /f "tokens=1,* delims==" %%A in ("%SLOT_TOKEN%") do if /I "%%A"=="androidboot.slot_suffix" set "SLOT=%%B"
exit /b 0
:detect_active_slot
set "SLOT_FILE=%TEMP%\canoe-slot-%RANDOM%-%RANDOM%.txt"
"%ADB%" %ADB_SERIAL_ARGS% shell getprop ro.boot.slot_suffix >"%SLOT_FILE%" 2>nul
if errorlevel 1 (
  echo canoe_prep_device: error: could not read ro.boot.slot_suffix 1>&2
  exit /b 1
)
set "SLOT_PROP="
for /f "usebackq tokens=* delims=" %%A in ("%SLOT_FILE%") do if not defined SLOT_PROP set "SLOT_PROP=%%A"
del /q "%SLOT_FILE%" >nul 2>&1
if errorlevel 1 (
  echo canoe_prep_device: error: could not remove the temporary slot file 1>&2
  exit /b 1
)
if not defined SLOT_PROP goto detect_cmdline_slot
set "SLOT=%SLOT_PROP%"
call :normalize_slot "%SLOT%"
if errorlevel 1 exit /b 1
exit /b 0

:detect_cmdline_slot
set "CMDLINE_FILE=%TEMP%\canoe-cmdline-%RANDOM%-%RANDOM%.txt"
"%ADB%" %ADB_SERIAL_ARGS% shell cat /proc/cmdline >"%CMDLINE_FILE%" 2>nul
if errorlevel 1 (
  echo canoe_prep_device: error: could not read /proc/cmdline 1>&2
  exit /b 1
)
set "CMDLINE="
for /f "usebackq tokens=* delims=" %%A in ("%CMDLINE_FILE%") do if not defined CMDLINE set "CMDLINE=%%A"
del /q "%CMDLINE_FILE%" >nul 2>&1
if errorlevel 1 (
  echo canoe_prep_device: error: could not remove the temporary cmdline file 1>&2
  exit /b 1
)
if defined CMDLINE for %%A in (%CMDLINE%) do call :parse_slot_token "%%A"
if errorlevel 1 exit /b 1
if not defined SLOT exit /b 0
call :normalize_slot "%SLOT%"
if errorlevel 1 exit /b 1
exit /b 0

:normalize_slot
set "SLOT=%~1"
if /I "%~1"=="a" set "SLOT=_a"
if /I "%~1"=="b" set "SLOT=_b"
if /I "%SLOT%"=="_a" exit /b 0
if /I "%SLOT%"=="_b" exit /b 0
echo canoe_prep_device: error: --slot must be _a, _b, active or inactive ^(got '%SLOT%'^) 1>&2
exit /b 1

:usage
echo Usage: canoe_prep_device.bat [options]
echo.
echo   --slot SLOT         source slot: _a, _b, active ^(default^) or inactive.
echo                       "inactive" is the slot that is not booted right now,
echo                       e.g. the one an adb sideload has just written.
echo   --abl IMG           use this ABL image instead of pulling the partition
echo   --vbmeta IMG        use this vbmeta image instead of pulling the partition
echo   -s, --serial SERIAL adb device serial
echo       --keep-images   keep the pulled images in .\images
echo   -h, --help          this text
echo.
echo --abl and --vbmeta must be given together: they are a matched stock pair, and
echo mixing a supplied image with a pulled one is exactly the mismatch this guards
echo against.
echo.
echo Run this from a custom recovery with ADB enabled, then:
echo   canoe_stage.bat                         install the persist tree and the BDS
echo   fastboot flash abl ^<vulnerable^>.img      only if the abl partition is not
echo                                           already a GBL-vulnerable version
exit /b 0
