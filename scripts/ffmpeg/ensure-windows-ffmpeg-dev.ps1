# Provisions the FFmpeg *dev SDK* (headers + import libs + runtime DLLs) used to
# build and run the QtAVPlayer preview decode backend on Windows.
#
# This is separate from ensure-windows-ffmpeg.ps1, which fetches the standalone
# `ffmpeg.exe` used by video export. Here we need the shared-build SDK so the
# vendored QtAVPlayer (third_party/QtAVPlayer) can link FFmpeg:
#   third_party/ffmpeg/windows/dev/
#     include/   libav*/ headers   (compile QtAVPlayer + qavvideoframe bridge)
#     lib/       av*.lib import libs (link MiaCode)
#     bin/       av*.dll runtime    (run + package next to MiaCode.exe)
#
# The whole dev/ tree is gitignored — never committed. CMake discovers it via
# the MIACODE_FFMPEG_DEV_DIR cache variable (defaults to this location).
#
# Default source: BtbN FFmpeg-Builds n7.1 LGPL *shared* build. LGPL (decode-only,
# no --enable-gpl/--enable-nonfree) matches the existing redistribution terms.
# Major versions MUST stay avcodec-61 / avformat-61 / avutil-59 / swresample-5 /
# swscale-8 / avfilter-10 to match the packaged runtime + the hard-coded DLL
# names in CMakeLists.txt and scripts/build/package-win.ps1. (avdevice is dropped —
# capture-device only, see third_party/ffmpeg/README.md → Size trimming.)

param(
    [string]$RepoRoot = ""
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

$devDir = Join-Path $RepoRoot "third_party\ffmpeg\windows\dev"
$includeDir = Join-Path $devDir "include"
$libDir = Join-Path $devDir "lib"
$binDir = Join-Path $devDir "bin"

# Runtime DLLs the build + package require (names are major-version pinned).
# avdevice is intentionally excluded — it's capture-device-only and dropped to
# trim package size (QtAVPlayer is patched not to link/use it).
$requiredDlls = @(
    "avcodec-61.dll", "avformat-61.dll", "avutil-59.dll",
    "swresample-5.dll", "swscale-8.dll", "avfilter-10.dll"
)
$requiredLibs = @(
    "avcodec.lib", "avformat.lib", "avutil.lib",
    "swresample.lib", "swscale.lib", "avfilter.lib"
)

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
    Write-Host "Using existing FFmpeg dev SDK: $devDir"
    return
}

$ffmpegUrl = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_DEV_URL)) {
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-shared-7.1.zip"
} else {
    $env:MIACODE_WINDOWS_FFMPEG_DEV_URL
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("miacode-ffmpeg-dev-" + [System.Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $tmpDir "ffmpeg-dev.zip"
$extractDir = Join-Path $tmpDir "extracted"

try {
    New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

    Write-Host "Downloading FFmpeg dev SDK from $ffmpegUrl"
    $downloadError = $null
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        try {
            Invoke-WebRequest -Uri $ffmpegUrl -OutFile $archivePath
            $downloadError = $null
            break
        } catch {
            $downloadError = $_.Exception
            if ($attempt -ge 5) {
                break
            }
            Start-Sleep -Seconds 2
        }
    }
    if ($null -ne $downloadError) {
        $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
        if ($null -eq $curl) {
            throw "Failed to download FFmpeg dev SDK after 5 attempts: $($downloadError.Message)"
        }

        Write-Warning "Invoke-WebRequest failed: $($downloadError.Message). Retrying with curl.exe..."
        & $curl.Source -L --fail --retry 5 --retry-delay 2 -o $archivePath $ffmpegUrl
        if ($LASTEXITCODE -ne 0) {
            throw "curl.exe failed to download FFmpeg dev SDK with exit code $LASTEXITCODE."
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
    # Copy only the DLLs we link/ship (skip ffmpeg.exe/ffprobe.exe/ffplay.exe —
    # export uses the separately-provisioned essentials ffmpeg.exe).
    foreach ($dll in $requiredDlls) {
        $src = Join-Path (Join-Path $srcRoot "bin") $dll
        if (!(Test-Path $src)) {
            throw "Downloaded FFmpeg dev SDK is missing runtime DLL '$dll' — wrong build/major version? Expected an n7.1 win64 lgpl *shared* build."
        }
        Copy-Item $src (Join-Path $binDir $dll) -Force
    }

    if (!(Test-DevSdkPresent)) {
        throw "FFmpeg dev SDK provisioning incomplete after extraction at $devDir"
    }
    Write-Host "Prepared FFmpeg dev SDK at $devDir"
} finally {
    if (Test-Path $tmpDir) {
        Remove-Item -Recurse -Force $tmpDir
    }
}
