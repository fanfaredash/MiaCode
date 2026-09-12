# Provisions the FFmpeg *dev SDK* (headers + import libs + runtime DLLs) used to
# build and run the QtAVPlayer preview decode backend on Windows.
#
# This is separate from ensure-windows-ffmpeg.ps1, which fetches the standalone
# `ffmpeg.exe` used by video export. Here we need the shared-build SDK so the
# vendored QtAVPlayer (third_party/QtAVPlayer) can link FFmpeg:
#   third_party/ffmpeg/windows/<win64|winarm64>/dev/
#     include/   libav*/ headers   (compile QtAVPlayer + qavvideoframe bridge)
#     lib/       av*.lib import libs (link MiaCode)
#     bin/       av*.dll runtime    (run + package next to MiaCode.exe)
#
# The whole dev/ tree is gitignored - never committed. CMake discovers it via
# the MIACODE_FFMPEG_DEV_DIR cache variable (built from the target architecture).
#
# Default source per architecture:
#   x64   BtbN FFmpeg-Builds n7.1 LGPL *shared* (the pre-trim baseline; CI then
#         replaces it with the decode-only build from scripts/ffmpeg/trim/)
#   arm64 BtbN FFmpeg-Builds n8.1 LGPL *shared*, pinned to one immutable
#         autobuild tag so the archive hash stays valid. arm64 ships this full
#         SDK — the trim toolchain covers x64 only.
# LGPL (decode-only, no --enable-gpl/--enable-nonfree) matches the existing
# redistribution terms. Major versions must stay avcodec-61/62, avformat-61/62,
# avutil-59/60, swresample-5/6, swscale-8/9, avfilter-10/11 respectively, to
# match scripts/build/windows-toolchain.psd1. (avdevice is dropped -
# capture-device only, see third_party/ffmpeg/README.md -> Size trimming.)
#
# The GPL variant is used only for the standalone export binary
# (ensure-windows-ffmpeg.ps1), which needs libx264.

param(
    [string]$RepoRoot = "",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"

try {
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor
        [Net.SecurityProtocolType]::Tls12 -bor
        [Net.SecurityProtocolType]::Tls13
} catch {
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor
        [Net.SecurityProtocolType]::Tls12
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

$archSuffix = if ($Arch -eq "arm64") { "winarm64" } else { "win64" }
$devDir = Join-Path $RepoRoot "third_party\ffmpeg\windows\$archSuffix\dev"
$includeDir = Join-Path $devDir "include"
$libDir = Join-Path $devDir "lib"
$binDir = Join-Path $devDir "bin"

# Runtime DLLs the build + package require (names are major-version pinned).
# avdevice is intentionally excluded - it's capture-device-only and dropped to
# trim package size (QtAVPlayer is patched not to link/use it).
$requiredDlls = if ($Arch -eq "arm64") {
    @(
        "avcodec-62.dll", "avformat-62.dll", "avutil-60.dll",
        "swresample-6.dll", "swscale-9.dll", "avfilter-11.dll"
    )
} else {
    @(
        "avcodec-61.dll", "avformat-61.dll", "avutil-59.dll",
        "swresample-5.dll", "swscale-8.dll", "avfilter-10.dll"
    )
}
$requiredLibs = @(
    "avcodec.lib", "avformat.lib", "avutil.lib",
    "swresample.lib", "swscale.lib", "avfilter.lib"
)

if ($Arch -eq "arm64") {
    # Pinned immutable BtbN autobuild release. The rolling `latest` tag is
    # replaced daily, so an archive from it cannot be hash-pinned; this one can.
    $ffmpegReleaseTag = "autobuild-2026-09-12-13-12"
    $ffmpegBuildVersion = "n8.1.2-52-g5a03dfa0f6"
    $defaultUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/$ffmpegReleaseTag/ffmpeg-$ffmpegBuildVersion-winarm64-lgpl-shared-8.1.zip"
    $defaultSha256 = "DFB3F394B316F91CC2C399BBAA38A280F81E523E150A20F7DDD477843AA70608"
} else {
    # x64 baseline: the n7.1 rolling asset the repository has always used, and
    # the exact SDK the trim allowlist targets. The trim toolchain replaces this
    # tree before packaging, so the archive is not hash-pinned.
    $defaultUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-shared-7.1.zip"
    $defaultSha256 = ""
}

function Test-DevSdkPresent {
    if (!(Test-Path (Join-Path $includeDir "libavcodec"))) { return $false }
    foreach ($lib in $requiredLibs) {
        if (!(Test-Path (Join-Path $libDir $lib))) { return $false }
    }
    foreach ($dll in $requiredDlls) {
        if (!(Test-Path (Join-Path $binDir $dll))) { return $false }
    }
    return $true
}

if (Test-DevSdkPresent) {
    Write-Host "Using existing FFmpeg dev SDK ($Arch): $devDir"
    return
}

$ffmpegUrl = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_DEV_URL)) {
    $defaultUrl
} else {
    $env:MIACODE_WINDOWS_FFMPEG_DEV_URL
}
$expectedSha256 = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_DEV_SHA256)) {
    $defaultSha256
} else {
    $env:MIACODE_WINDOWS_FFMPEG_DEV_SHA256.ToUpperInvariant()
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("miacode-ffmpeg-dev-" + [System.Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $tmpDir "ffmpeg-dev.zip"
$extractDir = Join-Path $tmpDir "extracted"

try {
    New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

    Write-Host "Downloading FFmpeg dev SDK ($Arch) from $ffmpegUrl"
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -ne $curl) {
        & $curl.Source -L --fail --retry 5 --retry-delay 2 -o $archivePath $ffmpegUrl
        if ($LASTEXITCODE -ne 0) {
            throw "curl.exe failed to download FFmpeg dev SDK with exit code $LASTEXITCODE."
        }
    } else {
        $downloadError = $null
        for ($attempt = 1; $attempt -le 5; $attempt++) {
            try {
                Invoke-WebRequest -Uri $ffmpegUrl -OutFile $archivePath
                $downloadError = $null
                break
            } catch {
                $downloadError = $_.Exception
                if ($attempt -lt 5) {
                    Start-Sleep -Seconds 2
                }
            }
        }
        if ($null -ne $downloadError) {
            throw "Failed to download FFmpeg dev SDK after 5 attempts: $($downloadError.Message)"
        }
    }

    if (![string]::IsNullOrWhiteSpace($expectedSha256)) {
        $actualSha256 = (Get-FileHash -Path $archivePath -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actualSha256 -ne $expectedSha256) {
            throw "FFmpeg dev SDK archive hash mismatch for $Arch. Expected: $expectedSha256 Actual: $actualSha256"
        }
    }

    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractDir -Force

    # BtbN archives extract to a single top-level folder containing include/ lib/ bin/.
    $root = Get-ChildItem -Path $extractDir -Recurse -Directory -Filter "include" |
        Where-Object { Test-Path (Join-Path $_.FullName "libavcodec") } |
        Select-Object -First 1
    if ($null -eq $root) {
        throw "Could not locate include/libavcodec inside the downloaded archive: $archivePath"
    }
    $srcRoot = $root.Parent.FullName

    foreach ($sub in @("include", "lib", "bin")) {
        $src = Join-Path $srcRoot $sub
        if (!(Test-Path $src)) {
            throw "Downloaded FFmpeg dev SDK is missing '$sub/': $src"
        }
    }

    if (Test-Path $devDir) {
        Remove-Item -Recurse -Force $devDir
    }
    New-Item -ItemType Directory -Path $devDir -Force | Out-Null
    Copy-Item (Join-Path $srcRoot "include") $includeDir -Recurse -Force
    Copy-Item (Join-Path $srcRoot "lib") $libDir -Recurse -Force
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    # Copy only the DLLs we link/ship (skip ffmpeg.exe/ffprobe.exe/ffplay.exe -
    # export uses the separately-provisioned ffmpeg.exe).
    foreach ($dll in $requiredDlls) {
        $src = Join-Path (Join-Path $srcRoot "bin") $dll
        if (!(Test-Path $src)) {
            throw "Downloaded FFmpeg dev SDK is missing runtime DLL '$dll' - wrong build/major version? Expected an n7.1 win64 / n8.1 winarm64 lgpl *shared* build."
        }
        Copy-Item $src (Join-Path $binDir $dll) -Force
    }

    if (!(Test-DevSdkPresent)) {
        throw "FFmpeg dev SDK provisioning incomplete after extraction at $devDir"
    }
    Write-Host "Prepared FFmpeg dev SDK ($Arch) at $devDir"
} finally {
    if (Test-Path $tmpDir) {
        Remove-Item -Recurse -Force $tmpDir
    }
}
