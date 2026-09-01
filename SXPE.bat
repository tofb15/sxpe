@echo off
setlocal
cd /d "%~dp0build"
if not exist "sxpe_gui.exe" (
  echo SXPE is not built yet. Run build.bat first.
  pause
  exit /b 1
)
start "" "%~dp0build\sxpe_gui.exe" %*
