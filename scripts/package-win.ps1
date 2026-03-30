param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$QtRoot = "",
    [string]$BuildDir = "build",
    [string]$DistDir = "",
    [switch]$IncludeDevTools,
    [int]$BuildJobs = 8
)

$ErrorActionPreference = "Stop"

function Read-VersionInfoFromCMake {
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
    $baseVersion = "$major.$minor.$patch"
    $version = $baseVersion
    $prerelease = [regex]::Match($content, 'set\(MIACODE_VERSION_PRERELEASE\s+"([^"]*)"').Groups[1].Value
    if (![string]::IsNullOrWhiteSpace($prerelease)) {
        $version = "$version-$prerelease"
    }
    $displayVersion = $baseVersion
    $displayPrerelease = [regex]::Match($content, 'set\(MIACODE_DISPLAY_PRERELEASE\s+"([^"]*)"').Groups[1].Value
    if (![string]::IsNullOrWhiteSpace($displayPrerelease)) {
        $displayVersion = "$displayVersion.$displayPrerelease"
    }

    return [PSCustomObject]@{
        PackageVersion = $version
        DisplayVersion = $displayVersion
    }
}

function Read-VersionInfoFromGeneratedHeader {
    param([string]$HeaderPath)
    if (!(Test-Path $HeaderPath)) {
        return $null
    }

    $content = Get-Content $HeaderPath -Raw
    $packageVersion = [regex]::Match($content, '#define MIACODE_VERSION_STRING "([^"]+)"').Groups[1].Value
    $displayVersion = [regex]::Match($content, '#define MIACODE_DISPLAY_VERSION_STRING "([^"]+)"').Groups[1].Value
    if ([string]::IsNullOrWhiteSpace($packageVersion) -or [string]::IsNullOrWhiteSpace($displayVersion)) {
        return $null
    }

    return [PSCustomObject]@{
        PackageVersion = $packageVersion
        DisplayVersion = $displayVersion
    }
}

function Invoke-MiaCodeBuild {
    param(
        [string]$BuildDir,
        [string]$Config,
        [int]$BuildJobs
    )

    if ($BuildJobs -lt 1) {
        throw "BuildJobs must be >= 1."
    }

    Write-Host "Precheck: building MiaCode ($Config) in $BuildDir with --parallel $BuildJobs ..."
    & cmake --build $BuildDir --target MiaCode --config $Config --parallel $BuildJobs
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build failed with exit code $LASTEXITCODE"
    }
}

function Ensure-PackageBuildReady {
    param(
        [string]$RepoRoot,
        [string]$BuildDir,
        [string]$Config,
        [int]$BuildJobs,
        [object]$ExpectedVersionInfo
    )

    $cmakeFilePath = Join-Path $RepoRoot "CMakeLists.txt"
    $appVersionTemplatePath = Join-Path $RepoRoot "src\\app\\AppVersion.h.in"
    $cachePath = Join-Path $BuildDir "CMakeCache.txt"
    $generatedHeaderPath = Join-Path $BuildDir "generated\\AppVersion.h"
    $exePath = Join-Path $BuildDir "$Config\\MiaCode.exe"

    if (!(Test-Path $cachePath)) {
        throw "Build directory is not configured: $BuildDir. Configure/build it first."
    }

    $needsBuildReasons = New-Object System.Collections.Generic.List[string]
    if (!(Test-Path $exePath)) {
        $needsBuildReasons.Add("MiaCode executable is missing")
    }

    $generatedVersionInfo = Read-VersionInfoFromGeneratedHeader -HeaderPath $generatedHeaderPath
    if ($null -eq $generatedVersionInfo) {
        $needsBuildReasons.Add("generated AppVersion.h is missing or unreadable")
    } else {
        if ($generatedVersionInfo.PackageVersion -ne $ExpectedVersionInfo.PackageVersion) {
            $needsBuildReasons.Add(
                "generated package version '$($generatedVersionInfo.PackageVersion)' != expected '$($ExpectedVersionInfo.PackageVersion)'"
            )
        }
        if ($generatedVersionInfo.DisplayVersion -ne $ExpectedVersionInfo.DisplayVersion) {
            $needsBuildReasons.Add(
                "generated display version '$($generatedVersionInfo.DisplayVersion)' != expected '$($ExpectedVersionInfo.DisplayVersion)'"
            )
        }
    }

    if (Test-Path $exePath) {
        $exeWriteTime = (Get-Item $exePath).LastWriteTimeUtc
        foreach ($inputPath in @($cmakeFilePath, $appVersionTemplatePath, $generatedHeaderPath)) {
            if ((Test-Path $inputPath) -and ((Get-Item $inputPath).LastWriteTimeUtc -gt $exeWriteTime)) {
                $needsBuildReasons.Add("MiaCode.exe is older than $(Split-Path $inputPath -Leaf)")
                break
            }
        }
    }

    if ($needsBuildReasons.Count -gt 0) {
        Write-Host "Precheck: package build is stale."
        foreach ($reason in $needsBuildReasons) {
            Write-Host "  - $reason"
        }
        Invoke-MiaCodeBuild -BuildDir $BuildDir -Config $Config -BuildJobs $BuildJobs
    } else {
        Write-Host "Precheck: package build is up to date."
    }

    if (!(Test-Path $exePath)) {
        throw "Executable not found after precheck build: $exePath"
    }

    $generatedVersionInfo = Read-VersionInfoFromGeneratedHeader -HeaderPath $generatedHeaderPath
    if ($null -eq $generatedVersionInfo) {
        throw "generated AppVersion.h is still missing or unreadable after precheck build: $generatedHeaderPath"
    }
    if ($generatedVersionInfo.PackageVersion -ne $ExpectedVersionInfo.PackageVersion) {
        throw "Post-build package version mismatch: expected '$($ExpectedVersionInfo.PackageVersion)', got '$($generatedVersionInfo.PackageVersion)'"
    }
    if ($generatedVersionInfo.DisplayVersion -ne $ExpectedVersionInfo.DisplayVersion) {
        throw "Post-build display version mismatch: expected '$($ExpectedVersionInfo.DisplayVersion)', got '$($generatedVersionInfo.DisplayVersion)'"
    }

    return $exePath
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$versionInfo = Read-VersionInfoFromCMake -CMakeFilePath (Join-Path $repoRoot "CMakeLists.txt")
$version = $versionInfo.PackageVersion
if ([string]::IsNullOrWhiteSpace($DistDir)) {
    $DistDir = Join-Path "dist" "MiaCode-v$version-win64"
}

$exePath = Ensure-PackageBuildReady `
    -RepoRoot $repoRoot `
    -BuildDir $BuildDir `
    -Config $Config `
    -BuildJobs $BuildJobs `
    -ExpectedVersionInfo $versionInfo

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

if ($Config -eq "Debug") {
    $deployMode = "--debug"
} else {
    $deployMode = "--release"
}

& $deployTool `
    --dir $DistDir `
    $deployMode `
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
