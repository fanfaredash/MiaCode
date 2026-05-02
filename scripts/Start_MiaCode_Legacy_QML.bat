@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs\legacy-qml"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM beta20 default is the DComp pipeline (top-level popup HWND with
REM per-pixel alpha, FRAME_LATENCY_WAITABLE_OBJECT pacing, owner-followed
REM positioning). This launcher opts back into the legacy QSG-only
REM rendering path by setting MIACODE_PREVIEW_USE_DCOMP=0 — useful for
REM old graphics drivers, headless / VM environments, or any case where
REM the DComp path misbehaves.
REM
REM Pass --debug as %1 (or any other CLI flags) to enable verbose log
REM output into .\logs\legacy-qml\.
set "MIACODE_PREVIEW_USE_DCOMP=0"

echo MiaCode legacy QML pipeline (DComp disabled)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Env : MIACODE_PREVIEW_USE_DCOMP=0
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Logs (only populated if --debug was passed):
echo   %MIACODE_LOG_DIR%\miacode_runtime_debug.log
echo   %MIACODE_LOG_DIR%\miacode_audio_debug.log
echo   %MIACODE_LOG_DIR%\miacode_video_export.log
echo   %MIACODE_LOG_DIR%\miacode_startup_timing.log
echo   %MIACODE_LOG_DIR%\miacode_fatal.log

exit /b %EXIT_CODE%
