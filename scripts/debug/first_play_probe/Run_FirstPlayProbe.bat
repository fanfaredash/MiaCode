@echo off
setlocal

REM Double-click entry for the MiaCode first-play probe (see README_ZH.md).
REM Runs the 64-bit Windows PowerShell 5.1 that ships with Windows; no compiler
REM or extra install is needed. Extra arguments are passed through, e.g.
REM   Run_FirstPlayProbe.bat -AppDir "D:\MiaCode" -ChartDir "D:\charts\song"

set "HERE=%~dp0"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"

"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%HERE%FirstPlayProbe.ps1" %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
pause
exit /b %EXIT_CODE%
