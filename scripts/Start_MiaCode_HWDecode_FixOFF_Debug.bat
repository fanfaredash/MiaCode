@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"
if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM ===========================================================
REM  HW-decode GREEN fix A/B test -- THIS = FIX OFF (baseline).
REM  Forces D3D11VA hardware decode + DISABLES the completion-wait
REM  fix + enables the bounded frame dump, all under --debug.
REM  This reproduces the bug; compare with the FixON launcher.
REM ===========================================================
set "MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=0"
set "MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT=0"
set "MIACODE_PREVIEW_DUMP_HWFRAMES=15"

echo MiaCode HW-decode A/B  --  FIX OFF (completion_wait=0, baseline)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Play a HARDWARE-decoded video chart, then seek a few times.
echo   Expect: GREEN returns; log hint=green_fill_interior after seek.
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Debug log: %MIACODE_LOG_DIR%\miacode_runtime_debug.log
echo   look for: preview/hwframe  and  preview/hwdecode_summary
exit /b %EXIT_CODE%
