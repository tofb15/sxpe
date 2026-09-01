@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  "sxpe.exe" --help
  echo.
  pause
  exit /b 0
)
"sxpe.exe" %*
if errorlevel 1 pause
exit /b %ERRORLEVEL%
