# Provisions the standalone `ffmpeg.exe` used by video export.
#
# Source: BtbN FFmpeg-Builds n8.1 *GPL* static build (the GPL variant is the one
# with libx264; the LGPL build disables it, and MiaCode's encoder probe falls
# back to libx264 for software H.264 export). Pinned to one immutable autobuild
# tag so the binary hash stays valid - the rolling `latest` tag is replaced
# daily.
#
# The binary lands in third_party/ffmpeg/windows/<win64|winarm64>/ffmpeg.exe and
# is gitignored. package-win.ps1 ships it as app/ffmpeg/ffmpeg.exe.

param(
    [string]$RepoRoot = "",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"

function Get-UpperSha256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Test-ExistingBinary {
    param(
        [string]$Path,
        [string]$ExpectedSha256,
        [string]$ExpectedVersionPattern
    )

    if (!(Test-Path $Path)) {
        return $false
    }

    $info = Get-Item $Path
    if ($info.Length -lt 1MB) {
        Write-Warning "Existing ffmpeg binary is too small: $Path ($($info.Length) bytes)"
        return $false
    }

    if (![string]::IsNullOrWhiteSpace($ExpectedSha256)) {
        $actualSha256 = Get-UpperSha256 -Path $Path
        if ($actualSha256 -ne $ExpectedSha256) {
            Write-Warning "Existing ffmpeg hash mismatch at $Path"
            Write-Warning "Expected: $ExpectedSha256"
            Write-Warning "Actual:   $actualSha256"
            return $false
        }
    }

    $versionOutput = & $Path -version 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to execute ffmpeg for runtime validation: $Path"
        return $false
    }
    $versionLine = $versionOutput | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($versionLine)) {
        Write-Warning "ffmpeg returned no version output: $Path"
        return $false
    }
    if ($versionLine -notmatch $ExpectedVersionPattern) {
        Write-Warning "Unexpected ffmpeg version output: $versionLine"
        return $false
    }

    return $true
}

function Expand-DownloadedArchive {
    param(
        [string]$ArchivePath,
        [string]$ExtractDir,
        [string]$ArchiveExtension
    )

    $ext = $ArchiveExtension.ToLowerInvariant()
    if ($ext -eq ".zip") {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $ExtractDir -Force
        return
    }
    if ($ext -eq ".7z") {
        $sevenZip = Get-Command 7z -ErrorAction SilentlyContinue
        if ($null -ne $sevenZip) {
            & $sevenZip.Source x "-y" "-o$ExtractDir" $ArchivePath | Out-Null
            if ($LASTEXITCODE -eq 0) {
                return
            }
        }
        $tar = Get-Command tar -ErrorAction SilentlyContinue
        if ($null -ne $tar) {
            & $tar.Source -xf $ArchivePath -C $ExtractDir
            if ($LASTEXITCODE -eq 0) {
                return
            }
        }

        foreach ($python in @("python", "py")) {
            $pythonCmd = Get-Command $python -ErrorAction SilentlyContinue
            if ($null -eq $pythonCmd) {
                continue
            }
            if ($python -eq "py") {
                & $pythonCmd.Source -3 -m py7zr x $ArchivePath "-o$ExtractDir" | Out-Host
            } else {
                & $pythonCmd.Source -m py7zr x $ArchivePath "-o$ExtractDir" | Out-Host
            }
            if ($LASTEXITCODE -eq 0) {
                return
            }
        }

        throw "Unable to extract .7z archive. Install 7z/tar or run python -m pip install --user py7zr and retry."
    }
    throw "Unsupported archive extension: $ArchiveExtension"
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

$archSuffix = if ($Arch -eq "arm64") { "winarm64" } else { "win64" }
$ffmpegDir = Join-Path $RepoRoot "third_party\ffmpeg\windows\$archSuffix"
$ffmpegPath = Join-Path $ffmpegDir "ffmpeg.exe"
$ffprobePath = Join-Path $ffmpegDir "ffprobe.exe"

$ffmpegReleaseTag = "autobuild-2026-09-12-13-12"
$ffmpegBuildVersion = "n8.1.2-52-g5a03dfa0f6"
$ffmpegAssetName = "ffmpeg-$ffmpegBuildVersion-$archSuffix-gpl-8.1.zip"
$defaultUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/$ffmpegReleaseTag/$ffmpegAssetName"
$defaultSha256 = if ($Arch -eq "arm64") {
    "22E1BB241B8747ED5EA5ECE8DE64AFCC8720F4550ED35ED24657D00C2BADBA5E"
} else {
    "7B25E8C22217CCFC608BD609530620CCAD0F8A0F9D1A6BF4CA3B09B46E9E1A66"
}

$ffmpegUrl = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_URL)) {
    $defaultUrl
} else {
    $env:MIACODE_WINDOWS_FFMPEG_URL
}
$expectedSha256 = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_SHA256)) {
    $defaultSha256
} else {
    $env:MIACODE_WINDOWS_FFMPEG_SHA256.ToUpperInvariant()
}
# BtbN keeps the release tag stable but the version suffix inside the binary is
# tied to that build, so match the major/minor only.
$expectedVersionPattern = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_VERSION_PATTERN)) {
    'ffmpeg version n8\.1\.\d+'
} else {
    $env:MIACODE_WINDOWS_FFMPEG_VERSION_PATTERN
}
$archiveExtension = if ([string]::IsNullOrWhiteSpace($env:MIACODE_WINDOWS_FFMPEG_ARCHIVE_EXT)) {
    try {
        $extFromUrl = [System.IO.Path]::GetExtension(([Uri]$ffmpegUrl).AbsolutePath)
        if ([string]::IsNullOrWhiteSpace($extFromUrl)) { ".zip" } else { $extFromUrl }
    } catch {
        ".zip"
    }
} else {
    $env:MIACODE_WINDOWS_FFMPEG_ARCHIVE_EXT
}

if (Test-ExistingBinary -Path $ffmpegPath -ExpectedSha256 $expectedSha256 -ExpectedVersionPattern $expectedVersionPattern) {
    Write-Host "Using existing Windows ffmpeg ($Arch): $ffmpegPath"
    return
}

# A provisioned binary for another architecture cannot pass the version probe on
# this host, so the arch-specific directory is also the cache boundary.
$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("miacode-ffmpeg-" + [System.Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $tmpDir ("ffmpeg" + $archiveExtension)
$extractDir = Join-Path $tmpDir "extracted"

try {
    New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

    Write-Host "Downloading Windows ffmpeg ($Arch) from $ffmpegUrl"
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -ne $curl) {
        & $curl.Source -L --fail --retry 5 --retry-delay 2 -o $archivePath $ffmpegUrl
        if ($LASTEXITCODE -ne 0) {
            throw "curl.exe failed to download Windows ffmpeg with exit code $LASTEXITCODE."
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
            throw "Failed to download Windows ffmpeg after 5 attempts: $($downloadError.Message)"
        }
    }

    Expand-DownloadedArchive -ArchivePath $archivePath -ExtractDir $extractDir -ArchiveExtension $archiveExtension

    $downloadedFfmpeg = Get-ChildItem -Path $extractDir -Recurse -Filter "ffmpeg.exe" | Select-Object -First 1
    if ($null -eq $downloadedFfmpeg) {
        throw "ffmpeg.exe not found inside downloaded archive: $archivePath"
    }

    if (![string]::IsNullOrWhiteSpace($expectedSha256)) {
        $actualSha256 = Get-UpperSha256 -Path $downloadedFfmpeg.FullName
        if ($actualSha256 -ne $expectedSha256) {
            throw "Downloaded ffmpeg hash mismatch. Expected: $expectedSha256 Actual: $actualSha256"
        }
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

    $installedVersionOutput = & $ffmpegPath -version 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to execute downloaded ffmpeg for runtime validation: $ffmpegPath"
    }
    $installedVersionLine = $installedVersionOutput | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($installedVersionLine) -or $installedVersionLine -notmatch $expectedVersionPattern) {
        throw "Unexpected downloaded ffmpeg version output: $installedVersionLine"
    }

    Write-Host "Prepared Windows ffmpeg ($Arch) at $ffmpegPath"
} finally {
    if (Test-Path $tmpDir) {
        Remove-Item -Recurse -Force $tmpDir
    }
}
