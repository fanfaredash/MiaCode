param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$QtRoot = "D:\\Qt\\6.8.3\\msvc2022_64",
    [string]$BuildDir = "build",
    [string]$DistDir = "dist\\windows-x64"
)

$ErrorActionPreference = "Stop"

$exePath = Join-Path $BuildDir "$Config\\maicode.exe"
if (!(Test-Path $exePath)) {
    throw "Executable not found: $exePath. Build first."
}

$deployTool = Join-Path $QtRoot "bin\\windeployqt.exe"
if (!(Test-Path $deployTool)) {
    throw "windeployqt not found: $deployTool"
}

if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir | Out-Null

Copy-Item $exePath $DistDir

& $deployTool `
    --dir $DistDir `
    --release `
    --compiler-runtime `
    --no-translations `
    (Join-Path $DistDir "maicode.exe")

Write-Host "Packaged to $DistDir"
Write-Host "Note: optional legacy preview fallback still depends on the external MaiMuriDX scripts."
