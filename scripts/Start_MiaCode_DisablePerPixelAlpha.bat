@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"
if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM ============================================================
REM Investigation #1: DComp per-pixel-alpha + QRhi D3D11 texture
REM ============================================================
REM Hypothesis: with per-pixel alpha ON (default), QML VideoOutput renders
REM bg.mp4 directly via QRhi/D3D11 into the WS_EX_NOREDIRECTIONBITMAP popup.
REM Qt 6.8 QMediaPlayer::setPlaybackRate(non-1.0) on BufferedMedia +
REM 1920x1080 NV12 source resets the decoder/converter pipeline and
REM triggers QVideoSink surface-format / texture invalidation while the
REM QSG render thread is mid-frame sampling that texture. The D3D11
REM device-removed result goes through DXGI's RaiseFailFastException
REM path → process dies with no VEH catch (matches logs_22/23/24).
REM
REM Setting MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA=0 falls back to legacy
REM LWA composition mode: video frame is converted to QImage on the GUI
REM thread (CPU detour), DComp paints it via StageBackgroundSource. No
REM QRhi texture sharing → no D3D11 invalidation race during rate change.
REM
REM Test procedure: launch with this .bat, load LAPSE chart with bg.mp4,
REM hit 0.5x or 1.5x. If process survives the rate switch (it has
REM survived in logs_22/23/24 layouts where per-pixel-alpha was on), the
REM hypothesis is CONFIRMED and the fix is on the QML / DComp surface
REM side, not on the QMediaPlayer side.
REM
REM Other relevant Phase 4d behaviors:
REM   - DComp exclusive mode auto-disables (it's gated on per-pixel alpha)
REM   - PreviewStageMediaHost.cpp:1735 starts converting frames to QImage
REM   - The HUD stutter counter may go up vs per-pixel-alpha mode
REM     (expected, not a regression — just CPU detour)
set "MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA=0"

echo MiaCode debug mode (DComp per-pixel-alpha DISABLED)
echo   App       : %APP_DIR%\MiaCode.exe
echo   Logs      : %MIACODE_LOG_DIR%
echo   PerPixelA : 0  (legacy LWA + CPU bg detour)
echo.
echo Test: load chart with bg.mp4, click 0.5x or 1.5x. If it does NOT
echo crash, the per-pixel-alpha + QRhi D3D11 path is confirmed as the
echo deterministic crash trigger.
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Debug logs:
echo   %MIACODE_LOG_DIR%\miacode_runtime_debug.log
echo   %MIACODE_LOG_DIR%\miacode_audio_debug.log
echo   %MIACODE_LOG_DIR%\miacode_startup_timing.log
echo   %MIACODE_LOG_DIR%\miacode_startup_beacon_*.txt

exit /b %EXIT_CODE%
