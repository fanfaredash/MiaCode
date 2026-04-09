@echo off
setlocal

set MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY=120
set MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY=120
set MIACODE_PREVIEW_DIAG_COMPARE_DUMP_FRAMES=1
set MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES=8

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"
set "MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR=%MIACODE_LOG_DIR%\preview_compare_png"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"
if not exist "%MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR%" mkdir "%MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR%"

echo MiaCode debug mode with compare PNG dumps
echo   App      : %APP_DIR%\MiaCode.exe
echo   Logs     : %MIACODE_LOG_DIR%
echo   Dump PNG : %MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR%
echo   VideoCmp : every %MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY% frames
echo   Present  : every %MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY% frames
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --qt-native --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Debug logs:
echo   %MIACODE_LOG_DIR%\miacode_runtime_debug.log
echo   %MIACODE_LOG_DIR%\miacode_audio_debug.log
echo   %MIACODE_LOG_DIR%\miacode_video_export.log
echo   %MIACODE_LOG_DIR%\miacode_startup_timing.log
echo   %MIACODE_LOG_DIR%\miacode_fatal.log
echo Compare PNG dumps:
echo   %MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR%

exit /b %EXIT_CODE%
