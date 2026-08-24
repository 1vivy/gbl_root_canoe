@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0python\python.exe" (
  echo canoe_prep_device: error: python\python.exe is missing from this toolkit 1>&2
  exit /b 1
)
"%~dp0python\python.exe" "%~dp0canoe_prep_device" %*
exit /b %ERRORLEVEL%
