@echo off
rem ===========================================================================
rem  MiaCode chart codec survey  (test-user edition)
rem
rem  Double-click this file. It scans THIS folder and all sub-folders for chart
rem  background videos (bg.* / pv.*) and writes  ffmpeg-codec-survey.txt  next
rem  to it -- please send that .txt file back.
rem
rem  This launcher drives the Unicode-safe PowerShell survey, so Chinese /
rem  Japanese / emoji / bracketed folder names are handled correctly.
rem
rem  PUT THESE THREE FILES IN THE SAME FOLDER:
rem    - survey-chart-codecs.bat   (this file)
rem    - survey-chart-codecs.ps1   (ships alongside it)
rem    - ffmpeg.exe                (copy from your MiaCode folder: app\ffmpeg\ffmpeg.exe)
rem
rem  OPTIONAL: drag a charts folder onto this .bat to scan that folder instead.
rem ===========================================================================
setlocal
set "HERE=%~dp0"
set "SCAN=%~1"
if "%SCAN%"=="" set "SCAN=%HERE%."

if not exist "%HERE%survey-chart-codecs.ps1" (
  echo [!] survey-chart-codecs.ps1 is missing -- keep it next to this .bat.
  echo.
  pause
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%survey-chart-codecs.ps1" -ChartRoots "%SCAN%"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo [!] Survey failed ^(exit %RC%^). If it says ffmpeg not found, copy
  echo     ffmpeg.exe ^(from MiaCode app\ffmpeg\ffmpeg.exe^) next to this .bat.
)
echo.
pause
exit /b %RC%
