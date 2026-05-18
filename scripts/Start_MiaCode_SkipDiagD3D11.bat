@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"

REM Beta45 triage launcher A: skips the D3D11 hardware-device probe in main()'s
REM startup diagnostic. The probe creates a real D3D11 device on the
REM hardware adapter, which loads vendor UMD DLLs (amdxc64.dll / nvwgf2umx.dll
REM etc.) that hook Win32 APIs and never unload. If this .bat makes the app
REM start where Start_MiaCode_Debug.bat does not, the regression is
REM localised to the UMD hook-set conflicting with later MSVC STL primitives.
set "MIACODE_SKIP_DIAG_D3D11=1"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

echo MiaCode triage launcher A (skip D3D11 probe)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Env : MIACODE_SKIP_DIAG_D3D11=1
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
exit /b %ERRORLEVEL%
