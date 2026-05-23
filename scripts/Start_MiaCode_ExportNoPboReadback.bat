@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"

REM Diagnostic launcher: force the synchronous non-PBO readback path for
REM exports. The default PBO path schedules an async glReadPixels into a
REM pixel-pack buffer and then proceeds to clear/draw into the same FBO
REM for the next frame without an explicit fence. On drivers that don't
REM strictly serialise that hazard, the captured bytes for a frame can
REM end up part-of-frame-N + part-of-frame-N+1 — i.e. horizontal-band
REM tearing on individual exported frames. The non-PBO path uses
REM glReadPixels with a CPU pointer, which the driver must block on, so
REM the readback cannot race with the next frame's render.
REM
REM Use this when a user reports per-frame tearing/banding in the
REM exported video that survives the deep-copy diagnostic (i.e. is NOT
REM in the rawvideo pipe pump but upstream of it). Compare an export
REM via this launcher against one via Start_MiaCode_Debug.bat:
REM   * identical output  -> PBO readback is NOT the cause; look at
REM                          per-layer scene-graph or texture upload.
REM   * different output  -> PBO readback race confirmed; the proper
REM                          fix is glFenceSync on the readback path or
REM                          FBO ping-pong.
REM
REM Cost: full GPU->CPU stall per frame (no readback/render overlap).
REM Expect ~20-30%% slower export at 1080p60 — fine for diagnosis,
REM not for production exports.
REM
REM The export log will contain a one-shot line
REM   pbo_readback_disabled_via_env | MIACODE_EXPORT_DISABLE_PBO_READBACK=1
REM at the start of each export, confirming the override took effect.
REM The same log's render_backend line will report pboEnabled=0.
set "MIACODE_EXPORT_DISABLE_PBO_READBACK=1"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

echo MiaCode export diagnostic launcher (non-PBO synchronous readback)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Env : MIACODE_EXPORT_DISABLE_PBO_READBACK=1
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Export log (look for "pbo_readback_disabled_via_env" + "render_backend pboEnabled=0"):
echo   %MIACODE_LOG_DIR%\miacode_video_export.log

exit /b %EXIT_CODE%
