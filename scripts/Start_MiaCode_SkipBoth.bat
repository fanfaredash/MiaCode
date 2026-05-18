@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"

REM Beta45 triage launcher C: skips BOTH the D3D11 probe and the
REM AsyncLogWriter singleton. Most conservative diagnostic mode ? if even
REM this fails, the regression is somewhere outside both candidates.
set "MIACODE_SKIP_DIAG_D3D11=1"
set "MIACODE_SKIP_ASYNCLOG_FLUSH=1"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

echo MiaCode triage launcher C (skip D3D11 probe AND async log writer)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Env : MIACODE_SKIP_DIAG_D3D11=1 MIACODE_SKIP_ASYNCLOG_FLUSH=1
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
exit /b %ERRORLEVEL%
