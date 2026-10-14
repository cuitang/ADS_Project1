@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_A.ps1 %*
set "ADS_A_EXIT=%ERRORLEVEL%"
if not "%ADS_A_EXIT%"=="0" echo Member A pipeline failed. See the error message above.
exit /b %ADS_A_EXIT%
