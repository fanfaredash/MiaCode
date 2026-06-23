@echo off
setlocal

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "MIACODE_LOG_DIR=%APP_DIR%\logs"
if not exist "%MIACODE_LOG_DIR%" mkdir "%MIACODE_LOG_DIR%"

REM Diagnose "No Qt platform plugin could be initialized" startup failures.
REM QT_DEBUG_PLUGINS makes Qt dump every plugin probe attempt + LoadLibrary
REM error code to stderr. QT_FORCE_STDERR_LOGGING and the redirect below
REM capture the output to a file, since GUI subsystem apps normally drop
REM stderr on the floor.
set "QT_DEBUG_PLUGINS=1"
set "QT_FORCE_STDERR_LOGGING=1"
set "QT_LOGGING_TO_CONSOLE=1"

REM Invoke the real exe directly (not via `start`) so cmd's stdout/stderr
REM redirection actually reaches the child. Bypass the wrapper.
echo MiaCode Qt plugin diagnostic mode
echo   App : %APP_DIR%\app\MiaCode.exe
echo   Logs: %MIACODE_LOG_DIR%
echo.
echo Capturing Qt plugin load trace. Re-run this .bat multiple times if
echo the regular launcher only opens intermittently; failure traces will
echo accumulate in qt_plugin_diag_stderr.log.
echo.

"%APP_DIR%\app\MiaCode.exe" --debug %* 1>>"%MIACODE_LOG_DIR%\qt_plugin_diag_stdout.log" 2>>"%MIACODE_LOG_DIR%\qt_plugin_diag_stderr.log"
set "EXIT_CODE=%ERRORLEVEL%"

echo Exit code: %EXIT_CODE%
echo.
echo Captured logs:
echo   %MIACODE_LOG_DIR%\qt_plugin_diag_stderr.log
echo   %MIACODE_LOG_DIR%\qt_plugin_diag_stdout.log

exit /b %EXIT_CODE%
