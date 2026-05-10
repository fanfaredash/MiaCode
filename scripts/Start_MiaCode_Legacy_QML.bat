@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs\worker-hwnd"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM Beta34+ default preview rendering is the embedded QSG-only legacy
REM pipeline. This launcher opts back into the newer out-of-process
REM preview worker plus HWND/DComp timeline topology for A/B testing.
REM
REM Pass --debug as %1 (or any other CLI flags) to enable verbose log
REM output into .\logs\worker-hwnd\.
set "MIACODE_PREVIEW_USE_DCOMP=1"
set "MIACODE_PREVIEW_OUT_OF_PROCESS=1"
set "MIACODE_PREVIEW_WORKER_QSG_RENDER=1"
set "MIACODE_TIMELINE_USE_DCOMP=1"

echo MiaCode worker + HWND timeline pipeline
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Env : MIACODE_PREVIEW_USE_DCOMP=1
echo         MIACODE_PREVIEW_OUT_OF_PROCESS=1
echo         MIACODE_PREVIEW_WORKER_QSG_RENDER=1
echo         MIACODE_TIMELINE_USE_DCOMP=1
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
