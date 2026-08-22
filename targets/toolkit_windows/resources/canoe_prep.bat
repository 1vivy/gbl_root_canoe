@echo off
setlocal EnableExtensions DisableDelayedExpansion
chcp 65001 >nul
cd /d "%~dp0"
if errorlevel 1 (
  echo canoe_prep: error: failed to change to the toolkit directory 1>&2
  exit /b 1
)

set "PKG="
set "CUSTOM_RECOVERY="
set "VULN_ABL="
set "IN_PLACE=no"
set "WORKDIR=%CD%\work"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--pkg" (
  if "%~2"=="" (
    echo canoe_prep: error: --pkg needs a directory 1>&2
    exit /b 1
  )
  set "PKG=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--recovery" (
  if "%~2"=="" (
    echo canoe_prep: error: --recovery needs an image 1>&2
    exit /b 1
  )
  set "CUSTOM_RECOVERY=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--abl" (
  if "%~2"=="" (
    echo canoe_prep: error: --abl needs an image 1>&2
    exit /b 1
  )
  set "VULN_ABL=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--work" (
  if "%~2"=="" (
    echo canoe_prep: error: --work needs a directory 1>&2
    exit /b 1
  )
  set "WORKDIR=%~2"
  shift
  shift
  goto parse_args
)
if /I "%~1"=="--in-place" (
  set "IN_PLACE=yes"
  shift
  goto parse_args
)
if /I "%~1"=="-h" goto show_help
if /I "%~1"=="--help" goto show_help
call :usage 1>&2
echo canoe_prep: error: unknown argument: %~1 1>&2
exit /b 1

:show_help
call :usage
exit /b 0

:args_done
if not defined PKG (
  call :usage 1>&2
  echo canoe_prep: error: --pkg is required 1>&2
  exit /b 1
)
if not exist "%PKG%\." (
  echo canoe_prep: error: package directory not found: %PKG% 1>&2
  exit /b 1
)
if not exist "bin\vbmetabackup.exe" (
  echo canoe_prep: error: missing bin\vbmetabackup.exe 1>&2
  exit /b 1
)
if not exist "bin\vbmetaport.exe" (
  echo canoe_prep: error: missing bin\vbmetaport.exe 1>&2
  exit /b 1
)

set "PKG_RECOVERY=%PKG%\recovery.img"
set "PKG_ABL=%PKG%\abl.img"
set "PKG_VBMETA=%PKG%\vbmeta.img"
if not exist "%PKG_ABL%" (
  echo canoe_prep: error: package is missing abl.img: %PKG_ABL% 1>&2
  exit /b 1
)
if not exist "%PKG_VBMETA%" (
  echo canoe_prep: error: package is missing vbmeta.img: %PKG_VBMETA% 1>&2
  exit /b 1
)
if defined CUSTOM_RECOVERY (
  if not exist "%CUSTOM_RECOVERY%" (
    echo canoe_prep: error: custom recovery not found: %CUSTOM_RECOVERY% 1>&2
    exit /b 1
  )
  if not exist "%PKG_RECOVERY%" (
    echo canoe_prep: error: package is missing recovery.img, so its official vbmeta cannot be lifted 1>&2
    exit /b 1
  )
)
if defined VULN_ABL if not exist "%VULN_ABL%" (
  echo canoe_prep: error: vulnerable ABL not found: %VULN_ABL% 1>&2
  exit /b 1
)

if not exist "%WORKDIR%\." mkdir "%WORKDIR%"
if errorlevel 1 (
  echo canoe_prep: error: could not create work directory: %WORKDIR% 1>&2
  exit /b 1
)

set "GRAFTED="
if not defined CUSTOM_RECOVERY goto sidecars

echo.
echo [*] Lifting the official recovery vbmeta out of %PKG_RECOVERY%
if not exist "%WORKDIR%\vbmetas\." mkdir "%WORKDIR%\vbmetas"
if errorlevel 1 (
  echo canoe_prep: error: could not create vbmeta staging directory 1>&2
  exit /b 1
)
if exist "%WORKDIR%\vbmetas\recovery.vbmeta" del /q "%WORKDIR%\vbmetas\recovery.vbmeta"
if errorlevel 1 (
  echo canoe_prep: error: could not remove the previous recovery.vbmeta 1>&2
  exit /b 1
)
bin\vbmetabackup.exe -f "%PKG_RECOVERY%" -n recovery -o "%WORKDIR%\vbmetas"
if errorlevel 1 (
  echo canoe_prep: error: failed to extract the official recovery vbmeta 1>&2
  exit /b 1
)
if not exist "%WORKDIR%\vbmetas\recovery.vbmeta" (
  echo canoe_prep: error: recovery.vbmeta is empty 1>&2
  exit /b 1
)
for %%A in ("%WORKDIR%\vbmetas\recovery.vbmeta") do if "%%~zA"=="0" (
  echo canoe_prep: error: recovery.vbmeta is empty 1>&2
  exit /b 1
)

echo.
echo [*] Grafting it onto %CUSTOM_RECOVERY%
set "GRAFTED=%WORKDIR%\grafted_recovery.img"
if exist "%GRAFTED%" del /q "%GRAFTED%"
if errorlevel 1 (
  echo canoe_prep: error: could not remove the previous grafted recovery 1>&2
  exit /b 1
)
bin\vbmetaport.exe "%WORKDIR%\vbmetas\recovery.vbmeta" "%CUSTOM_RECOVERY%" "%GRAFTED%"
if errorlevel 1 (
  echo canoe_prep: error: vbmetaport failed 1>&2
  exit /b 1
)
if not exist "%GRAFTED%" (
  echo canoe_prep: error: grafted recovery is empty 1>&2
  exit /b 1
)
for %%A in ("%GRAFTED%") do if "%%~zA"=="0" (
  echo canoe_prep: error: grafted recovery is empty 1>&2
  exit /b 1
)
for %%A in ("%GRAFTED%") do set "GRAFT_SIZE=%%~zA"
for %%A in ("%CUSTOM_RECOVERY%") do set "CUSTOM_SIZE=%%~zA"
if not "%GRAFT_SIZE%"=="%CUSTOM_SIZE%" (
  echo canoe_prep: error: grafted recovery changed size from %CUSTOM_SIZE% bytes to %GRAFT_SIZE% bytes 1>&2
  exit /b 1
)
echo     grafted_recovery.img: %GRAFT_SIZE% bytes ^(size preserved^)

:sidecars
echo.
echo [*] Deriving the canoe boot chain from the package's stock abl/vbmeta pair
if not exist "images\." mkdir "images"
if errorlevel 1 (
  echo canoe_prep: error: could not create images directory 1>&2
  exit /b 1
)
copy /Y "%PKG_ABL%" "images\abl.img" >nul
if errorlevel 1 (
  echo canoe_prep: error: could not copy package abl.img into images 1>&2
  exit /b 1
)
copy /Y "%PKG_VBMETA%" "images\vbmeta.img" >nul
if errorlevel 1 (
  echo canoe_prep: error: could not copy package vbmeta.img into images 1>&2
  exit /b 1
)
call build.bat
if errorlevel 1 (
  echo canoe_prep: error: build.bat failed 1>&2
  exit /b 1
)
if not exist "efisp\boot.efi" (
  echo canoe_prep: error: build.bat did not produce efisp\boot.efi 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi") do if "%%~zA"=="0" (
  echo canoe_prep: error: build.bat produced an empty efisp\boot.efi 1>&2
  exit /b 1
)
if not exist "efisp\boot.efi.gm2p" (
  echo canoe_prep: error: build.bat did not produce efisp\boot.efi.gm2p 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi.gm2p") do if not "%%~zA"=="120" (
  echo canoe_prep: error: efisp\boot.efi.gm2p is not exactly 120 bytes 1>&2
  exit /b 1
)
if not exist "efisp\boot.efi.tzmap" (
  echo canoe_prep: error: build.bat did not produce efisp\boot.efi.tzmap 1>&2
  exit /b 1
)
for %%A in ("efisp\boot.efi.tzmap") do if not "%%~zA"=="256" (
  echo canoe_prep: error: efisp\boot.efi.tzmap is not exactly 256 bytes 1>&2
  exit /b 1
)

echo.
if not "%IN_PLACE%"=="yes" goto report

echo [*] Substituting prepared images into %PKG%
if defined GRAFTED (
  call :substitute "%GRAFTED%" "%PKG_RECOVERY%" "grafted recovery"
  if errorlevel 1 exit /b 1
)
if defined VULN_ABL (
  call :substitute "%VULN_ABL%" "%PKG_ABL%" "vulnerable ABL"
  if errorlevel 1 exit /b 1
)
if not defined GRAFTED if not defined VULN_ABL echo     nothing to substitute ^(no --recovery, no --abl^)

goto report

:report
echo.
echo ========================================
echo canoe_prep: done.
echo.
echo Prepared:
if defined GRAFTED echo   %GRAFTED%
echo   efisp/boot.efi          patched ABL loader
echo   efisp/boot.efi.gm2p     KeyMint profile for %PKG_VBMETA%
echo   efisp/boot.efi.tzmap    ABL-derived TrustZone map
echo   BDS.efi                 superfastboot BDS ^(written raw to efisp^)
echo.
echo Next:
if "%IN_PLACE%"=="yes" goto report_in_place
echo   1. Install the prepared images into %PKG% yourself, or rerun with --in-place:
if defined GRAFTED echo       copy /Y "%GRAFTED%" "%PKG_RECOVERY%"
if defined VULN_ABL echo       copy /Y "%VULN_ABL%" "%PKG_ABL%"
echo     then run the package's own flasher.
goto report_common

:report_in_place
echo   1. Run the package's own flasher ^(Super_Flasher.bat / RegionalHybrid^).
echo      It will pick up the substituted images from %PKG% automatically.

:report_common
echo   2. Boot the custom recovery and enable ADB from its UI.
echo   3. Run: canoe_stage.bat
echo ========================================
exit /b 0

:substitute
set "SUB_SRC=%~1"
set "SUB_DST=%~2"
set "SUB_LABEL=%~3"
if not exist "%SUB_DST%.canoe-orig" (
  copy /Y "%SUB_DST%" "%SUB_DST%.canoe-orig" >nul
  if errorlevel 1 (
    echo canoe_prep: error: could not back up %SUB_DST% 1>&2
    exit /b 1
  )
  echo     backed up %SUB_DST% -^> %SUB_DST%.canoe-orig
) else (
  echo     backup already present: %SUB_DST%.canoe-orig
)
copy /Y "%SUB_SRC%" "%SUB_DST%" >nul
if errorlevel 1 (
  echo canoe_prep: error: could not install %SUB_LABEL% into the package 1>&2
  exit /b 1
)
echo     installed %SUB_LABEL% as %SUB_DST%
exit /b 0

:usage
echo Usage: canoe_prep.bat --pkg DIR [options]
echo.
echo   --pkg DIR          firmware image directory ^(e.g. OOS_FILES_HERE^)
echo   --recovery IMG     custom recovery to graft the official vbmeta onto
echo   --abl IMG          vulnerable ABL to flash instead of the package's abl.img
echo   --in-place         substitute prepared images into --pkg, keeping
echo                      ^<name^>.img.canoe-orig backups
echo   --work DIR         staging directory ^(default: .\work^)
echo   -h, --help         this text
echo.
echo Outputs ^(in --work^):
echo   vbmetas\recovery.vbmeta   official recovery vbmeta from the package
echo   grafted_recovery.img      custom recovery carrying that vbmeta ^(with --recovery^)
echo.
echo Outputs in the toolkit root, via build.bat:
echo   efisp\boot.efi, efisp\boot.efi.gm2p, efisp\boot.efi.tzmap
exit /b 0
