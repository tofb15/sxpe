@echo off
setlocal
cd /d "%~dp0"
"sxpe_mcp.exe" %*
exit /b %ERRORLEVEL%
