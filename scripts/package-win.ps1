param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$QtRoot = "",
    [string]$BuildDir = "build",
    [string]$DistDir = "",
    [switch]$IncludeDevTools
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
    $version = "$major.$minor.$patch"
    $prerelease = [regex]::Match($content, 'set\(MIACODE_VERSION_PRERELEASE\s+"([^"]*)"').Groups[1].Value
    if (![string]::IsNullOrWhiteSpace($prerelease)) {
        $version = "$version-$prerelease"
    }
    return $version
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$version = Read-VersionFromCMake -CMakeFilePath (Join-Path $repoRoot "CMakeLists.txt")
if ([string]::IsNullOrWhiteSpace($DistDir)) {
    $DistDir = Join-Path "dist" "MiaCode-v$version-win64"
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

if ($IncludeDevTools) {
    foreach ($toolName in @("simai_native_dump.exe", "soundtouch_probe.exe")) {
        $toolPath = Join-Path $buildOutputDir $toolName
        if (Test-Path $toolPath) {
            Copy-Item $toolPath (Join-Path $DistDir $toolName) -Force
        }
    }
}

$assetsSrc = Join-Path $repoRoot "assets"
if (Test-Path $assetsSrc) {
    $requiredSfxDir = Join-Path $assetsSrc "SFX"
    $requiredSfxFiles = @(
        "answer.wav",
        "slide.wav",
        "break.wav",
        "firework.wav",
        "judge_ex.wav",
        "touch.wav",
        "touchHold_riser.wav"
    )
    foreach ($sfxFile in $requiredSfxFiles) {
        $sfxPath = Join-Path $requiredSfxDir $sfxFile
        if (!(Test-Path $sfxPath)) {
            throw "Missing required SFX asset: $sfxPath"
        }
    }
    Copy-Item $assetsSrc (Join-Path $DistDir "assets") -Recurse -Force
}

$ffmpegSrc = Join-Path $repoRoot "third_party\\ffmpeg\\windows\\ffmpeg.exe"
if (Test-Path $ffmpegSrc) {
    $ffmpegSize = (Get-Item $ffmpegSrc).Length
    if ($ffmpegSize -lt 1MB) {
        throw "Invalid ffmpeg binary (too small): $ffmpegSrc ($ffmpegSize bytes)"
    }
    $ffmpegDstDir = Join-Path $DistDir "ffmpeg"
    New-Item -ItemType Directory -Path $ffmpegDstDir -Force | Out-Null
    Copy-Item $ffmpegSrc (Join-Path $ffmpegDstDir "ffmpeg.exe") -Force
    $ffprobeSrc = Join-Path $repoRoot "third_party\\ffmpeg\\windows\\ffprobe.exe"
    if (Test-Path $ffprobeSrc) {
        Copy-Item $ffprobeSrc (Join-Path $ffmpegDstDir "ffprobe.exe") -Force
    }
} else {
    Write-Host "Run .\scripts\ensure-windows-ffmpeg.ps1 to download the pinned Windows ffmpeg binary."
    throw "Missing required ffmpeg binary: $ffmpegSrc"
}

$docsDir = Join-Path $DistDir "docs"
New-Item -ItemType Directory -Path $docsDir -Force | Out-Null
foreach ($docFile in @("README.md", "README_EN.md")) {
    $srcDoc = Join-Path $repoRoot $docFile
    if (Test-Path $srcDoc) {
        Copy-Item $srcDoc (Join-Path $docsDir $docFile) -Force
    }
}
$releaseReadme = Join-Path $docsDir "RELEASE_README.txt"
$releaseLines = @(
    "MiaCode release package"
    ""
    "Run:"
    "  MiaCode.exe"
    ""
    "Included:"
    "  - MiaCode.exe (main app)"
    "  - Qt runtime DLLs and plugin folders"
    "  - ffmpeg/ffmpeg.exe"
    "  - assets/"
    "  - docs/"
)
if ($IncludeDevTools) {
    $releaseLines += @(
        "  - simai_native_dump.exe"
        "  - soundtouch_probe.exe"
    )
} else {
    $releaseLines += @(
        ""
        "Not included on purpose:"
        "  - simai_native_dump.exe"
        "  - soundtouch_probe.exe"
    )
}
$releaseLines | Set-Content -Path $releaseReadme -Encoding UTF8

$zipPath = "$DistDir.zip"
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
Compress-Archive -Path (Join-Path $DistDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Packaged to $DistDir"
Write-Host "Zip created: $zipPath"
