param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$QtRoot = "",
    [string]$BuildDir = "build",
    [string]$DistDir = ""
)

$ErrorActionPreference = "Stop"

function Read-VersionFromCMake {
    param([string]$CMakeFilePath)
    if (!(Test-Path $CMakeFilePath)) {
        throw "CMakeLists.txt not found: $CMakeFilePath"
    }
    $content = Get-Content $CMakeFilePath -Raw
    $major = [regex]::Match($content, 'set\(MIACODE_VERSION_MAJOR\s+"([^"]+)"').Groups[1].Value
    $minor = [regex]::Match($content, 'set\(MIACODE_VERSION_MINOR\s+"([^"]+)"').Groups[1].Value
    $patch = [regex]::Match($content, 'set\(MIACODE_VERSION_PATCH\s+"([^"]+)"').Groups[1].Value
    if ([string]::IsNullOrWhiteSpace($major) -or [string]::IsNullOrWhiteSpace($minor) -or [string]::IsNullOrWhiteSpace($patch)) {
        throw "Failed to parse MIACODE_VERSION_* from $CMakeFilePath"
    }
    return "$major.$minor.$patch"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$version = Read-VersionFromCMake -CMakeFilePath (Join-Path $repoRoot "CMakeLists.txt")
if ([string]::IsNullOrWhiteSpace($DistDir)) {
    $DistDir = Join-Path "dist" "MiaCode-v$version-portable-win64"
}

$exePath = Join-Path $BuildDir "$Config\MiaCode.exe"
if (!(Test-Path $exePath)) {
    throw "Executable not found: $exePath. Build first."
}

$deployTool = ""
if (![string]::IsNullOrWhiteSpace($QtRoot)) {
    $deployTool = Join-Path $QtRoot "bin\\windeployqt.exe"
}
if ([string]::IsNullOrWhiteSpace($deployTool) -or !(Test-Path $deployTool)) {
    $deployCmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($null -ne $deployCmd) {
        $deployTool = $deployCmd.Source
    }
}
if ([string]::IsNullOrWhiteSpace($deployTool) -or !(Test-Path $deployTool)) {
    throw "windeployqt not found. Add Qt/bin to PATH or pass -QtRoot <qt-root>."
}

if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir | Out-Null

Copy-Item $exePath (Join-Path $DistDir "MiaCode.exe") -Force

& $deployTool `
    --dir $DistDir `
    --release `
    --compiler-runtime `
    --no-translations `
    (Join-Path $DistDir "MiaCode.exe")

$buildOutputDir = Join-Path $BuildDir $Config
foreach ($runtimeDll in @("dxcompiler.dll", "dxil.dll")) {
    $srcDll = Join-Path $buildOutputDir $runtimeDll
    if (Test-Path $srcDll) {
        Copy-Item $srcDll (Join-Path $DistDir $runtimeDll) -Force
    }
}

$assetsSrc = Join-Path $repoRoot "assets"
if (Test-Path $assetsSrc) {
    Copy-Item $assetsSrc (Join-Path $DistDir "assets") -Recurse -Force
}

$docsDir = Join-Path $DistDir "docs"
New-Item -ItemType Directory -Path $docsDir -Force | Out-Null
foreach ($docFile in @("README.md", "README_EN.md")) {
    $srcDoc = Join-Path $repoRoot $docFile
    if (Test-Path $srcDoc) {
        Copy-Item $srcDoc (Join-Path $docsDir $docFile) -Force
    }
}
$portableReadme = Join-Path $docsDir "PORTABLE_README.txt"
@(
    "MiaCode portable package"
    ""
    "Run:"
    "  MiaCode.exe"
    ""
    "Included:"
    "  - MiaCode.exe (main app)"
    "  - Qt runtime DLLs and plugin folders"
    "  - assets/"
    "  - docs/"
    ""
    "Not included on purpose:"
    "  - simai_native_dump.exe"
    "  - soundtouch_probe.exe"
) | Set-Content -Path $portableReadme -Encoding UTF8

$zipPath = "$DistDir.zip"
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
Compress-Archive -Path (Join-Path $DistDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Packaged to $DistDir"
Write-Host "Zip created: $zipPath"

