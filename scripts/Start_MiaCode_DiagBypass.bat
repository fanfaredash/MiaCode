@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"

REM Beta45 triage launcher B: bypasses the AsyncLogWriter singleton entirely
REM (synchronous log writes only). If THIS launcher starts the app but
REM Start_MiaCode_Debug.bat does not, the regression is in that singleton's
REM first-time construction or in flush() interacting with hooked SRWLock /
REM CONDITION_VARIABLE primitives.
set "MIACODE_SKIP_ASYNCLOG_FLUSH=1"

if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

echo MiaCode triage launcher B (skip async log writer)
echo   App : %APP_DIR%\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo   Env : MIACODE_SKIP_ASYNCLOG_FLUSH=1
echo.

start "" /wait "%APP_DIR%\MiaCode.exe" --debug %*
exit /b %ERRORLEVEL%
