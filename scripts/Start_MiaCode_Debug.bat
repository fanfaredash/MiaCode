@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM Beta34+ default: embedded QSG-only legacy pipeline. Adding --debug
REM activates the full runtime/audio/startup-timing log channels under
REM .\logs\ . Pass extra CLI flags as %1..%9 (e.g. a chart path).
echo MiaCode debug mode (default embedded QSG legacy pipeline)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Debug logs:
echo   %MIACODE_LOG_DIR%\miacode_runtime_debug.log
echo   %MIACODE_LOG_DIR%\miacode_audio_debug.log
echo   %MIACODE_LOG_DIR%\miacode_video_export.log
echo   %MIACODE_LOG_DIR%\miacode_startup_timing.log
echo   %MIACODE_LOG_DIR%\miacode_fatal.log

exit /b %EXIT_CODE%
