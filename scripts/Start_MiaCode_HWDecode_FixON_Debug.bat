@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"
if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM ===========================================================
REM  HW-decode GREEN fix A/B test -- THIS = FIX ON.
REM  Forces D3D11VA hardware decode + enables the completion-wait
REM  fix + bounded frame dump, all under --debug logging.
REM  Compare with Start_MiaCode_HWDecode_FixOFF_Debug.bat.
REM ===========================================================
set "MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=0"
set "MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT=1"
set "MIACODE_PREVIEW_DUMP_HWFRAMES=15"

echo MiaCode HW-decode A/B  --  FIX ON (completion_wait=1)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Play a HARDWARE-decoded video chart, then seek a few times.
echo   Expect: NO green; log hint=interior_clean; completion_waits rising.
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Debug log: %MIACODE_LOG_DIR%\miacode_runtime_debug.log
echo   look for: preview/hwframe  and  preview/hwdecode_summary
exit /b %EXIT_CODE%
