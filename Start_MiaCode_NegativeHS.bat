@echo off
setlocal

REM Convenience launcher for the negative-HS demo chart. Negative HS (<HS*-N>,
REM Majdata reverse-flow gimmick) is now ON BY DEFAULT, so no env flag is needed.
REM To DISABLE it (restore strict reject of hs<=0) set MIACODE_PREVIEW_REJECT_NEGATIVE_HS=1.

set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"

set "APP_EXE=%REPO_DIR%\build\Release\MiaCode.exe"
set "DEMO_CHART=%REPO_DIR%\samples\negative_hs_demo\maidata.txt"

echo MiaCode -- negative-HS demo (negative HS is ON by default)
echo   App  : %APP_EXE%
echo   Chart: %DEMO_CHART%
echo.
echo To DISABLE negative HS instead: set MIACODE_PREVIEW_REJECT_NEGATIVE_HS=1
echo If the chart does not auto-open, use File - Open and pick the chart above.
echo.

start "" "%APP_EXE%" "%DEMO_CHART%" %*
exit /b 0
