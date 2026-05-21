@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"
if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM Force Qt 6.8 to use the FFmpeg media backend instead of the default WMF.
REM The 0.5x playback-rate crash (logs_22 / logs_23) was localised to the
REM WMF backend's player_->setPlaybackRate() path in PreviewStageMediaHost.
REM Switching backends bypasses that path while keeping BASS rate-change
REM behaviour unchanged. The ffmpegmediaplugin.dll is bundled under
REM app\multimedia\ in this release, so no extra install is needed.
set "QT_MEDIA_BACKEND=ffmpeg"

echo MiaCode debug mode (FFmpeg media backend)
echo   App           : %APP_DIR%\MiaCode.exe
echo   Logs          : %MIACODE_LOG_DIR%
echo   Media backend : %QT_MEDIA_BACKEND%
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Debug logs:
echo   %MIACODE_LOG_DIR%\miacode_runtime_debug.log
echo   %MIACODE_LOG_DIR%\miacode_audio_debug.log
echo   %MIACODE_LOG_DIR%\miacode_startup_timing.log
echo   %MIACODE_LOG_DIR%\miacode_startup_beacon_*.txt
echo.
echo If the 0.5x crash no longer reproduces with this launcher, the WMF
echo backend is the culprit. The audio_debug.log's first media_backend row
echo should show qt_media_backend_env=ffmpeg to confirm the switch took.

exit /b %EXIT_CODE%
