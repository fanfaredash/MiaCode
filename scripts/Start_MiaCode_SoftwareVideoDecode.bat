@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"

REM Diagnostic launcher: force the preview video to decode in SOFTWARE
REM (QtAVPlayer setInputVideoCodec("software")) instead of D3D11VA hardware
REM decode.
REM
REM Use this when a PV shows a pure-GREEN border/edge in the preview. Cause:
REM a zero-copy D3D11VA hardware frame is a NV12 texture allocated at the
REM H.264 macroblock-aligned coded size (e.g. a 300x300 video is coded as
REM 304x304), and the uninitialized padding columns/rows (chroma U=V=0)
REM sample as pure green when VideoOutput is not cropped to the display
REM rectangle. It reproduces on videos whose width or height is not a
REM multiple of 16; mod-16 videos (e.g. 1080x608) are unaffected.
REM
REM Software frames are CPU buffers cropped to the display size, so no
REM padding is shown. Compare this launcher against Start_MiaCode_Debug.bat
REM on the same chart:
REM   * green border gone  -> confirmed: the D3D11VA non-16-aligned padding
REM                           path; software decode is a safe workaround.
REM   * green border stays  -> not the hardware-frame padding; look elsewhere.
REM
REM Cost: CPU decode instead of GPU. Negligible for small PVs; for large
REM 1080p+ PVs expect higher CPU use. Fine for diagnosis / affected charts.
REM
REM The preview log's "media_backend" line will report force_software=1
REM forced_env=1, confirming the override took effect.
set "MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=1"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

echo MiaCode preview diagnostic launcher (force software video decode)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Env : MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=1
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Preview log (look for "media_backend ... force_software=1 forced_env=1"):
echo   %MIACODE_LOG_DIR%\miacode_preview.log
echo If the green border is gone, software decode confirms the D3D11VA padding cause.

exit /b %EXIT_CODE%
