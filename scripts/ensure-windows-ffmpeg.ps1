param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

function Get-UpperSha256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Test-ExistingBinary {
    param(
        [string]$Path,
        [string]$ExpectedSha256
    )

    if (!(Test-Path $Path)) {
        return $false
    }

    $info = Get-Item $Path
    if ($info.Length -lt 1MB) {
        Write-Warning "Existing ffmpeg binary is too small: $Path ($($info.Length) bytes)"
        return $false
    }

    $actualSha256 = Get-UpperSha256 -Path $Path
    if ($actualSha256 -ne $ExpectedSha256) {
        Write-Warning "Existing ffmpeg hash mismatch at $Path"
        Write-Warning "Expected: $ExpectedSha256"
        Write-Warning "Actual:   $actualSha256"
        return $false
    }

    return $true
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}

$ffmpegDir = Join-Path $RepoRoot "third_party\ffmpeg\windows"
$ffmpegPath = Join-Path $ffmpegDir "ffmpeg.exe"
$ffprobePath = Join-Path $ffmpegDir "ffprobe.exe"
$ffmpegUrl = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_URL)) {
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-7.1.zip"
} else {
    $env:MIACODE_WINDOWS_FFMPEG_URL
}
$expectedSha256 = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_SHA256)) {
    "DA80A9F19D6D3D58321F4C6C1A7590CE3B98BD7EF59107FEC6556482E188AB9E"
} else {
    $env:MIACODE_WINDOWS_FFMPEG_SHA256.ToUpperInvariant()
}

if (Test-ExistingBinary -Path $ffmpegPath -ExpectedSha256 $expectedSha256) {
    Write-Host "Using existing Windows ffmpeg: $ffmpegPath"
    return
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("miacode-ffmpeg-" + [System.Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $tmpDir "ffmpeg.zip"
$extractDir = Join-Path $tmpDir "extracted"

try {
    New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

    Write-Host "Downloading Windows ffmpeg from $ffmpegUrl"
    Invoke-WebRequest `
        -Uri $ffmpegUrl `
        -OutFile $archivePath `
        -MaximumRetryCount 5 `
        -RetryIntervalSec 2

    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractDir -Force

    $downloadedFfmpeg = Get-ChildItem -Path $extractDir -Recurse -Filter "ffmpeg.exe" | Select-Object -First 1
    if ($null -eq $downloadedFfmpeg) {
        throw "ffmpeg.exe not found inside downloaded archive: $archivePath"
    }

    $actualSha256 = Get-UpperSha256 -Path $downloadedFfmpeg.FullName
    if ($actualSha256 -ne $expectedSha256) {
        throw "Downloaded ffmpeg hash mismatch. Expected: $expectedSha256 Actual: $actualSha256"
    }

    New-Item -ItemType Directory -Path $ffmpegDir -Force | Out-Null
    Copy-Item $downloadedFfmpeg.FullName $ffmpegPath -Force

    $downloadedFfprobe = Get-ChildItem -Path $extractDir -Recurse -Filter "ffprobe.exe" | Select-Object -First 1
    if ($null -ne $downloadedFfprobe) {
        Copy-Item $downloadedFfprobe.FullName $ffprobePath -Force
    }

    $installedInfo = Get-Item $ffmpegPath
    if ($installedInfo.Length -lt 1MB) {
        throw "Installed ffmpeg binary is too small: $ffmpegPath ($($installedInfo.Length) bytes)"
    }

    Write-Host "Prepared Windows ffmpeg at $ffmpegPath"
} finally {
    if (Test-Path $tmpDir) {
        Remove-Item -Recurse -Force $tmpDir
    }
}
