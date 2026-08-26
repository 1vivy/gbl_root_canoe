@echo off
setlocal
set "ROOT=%~dp0"
if exist "%ROOT%python\python.exe" (
  "%ROOT%python\python.exe" "%ROOT%canoe" %*
) else (
  py -3 "%ROOT%canoe" %*
)
exit /b %errorlevel%
