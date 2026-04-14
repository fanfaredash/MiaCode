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

function Resolve-RepoPath {
    param(
        [string]$RepoRoot,
        [string]$PathValue
    )

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $PathValue
    }

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $PathValue))
}

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
        $displayVersion = "$displayVersion-$displayPrerelease"
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
    & cmake --build $BuildDir --target MiaCode --config $Config --parallel $BuildJobs | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-MiaCodeConfigure {
    param(
        [string]$RepoRoot,
        [string]$BuildDir
    )

    Write-Host "Precheck: reconfiguring CMake in $BuildDir ..."
    & cmake -S $RepoRoot -B $BuildDir | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "cmake configure failed with exit code $LASTEXITCODE"
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
        Invoke-MiaCodeConfigure -RepoRoot $RepoRoot -BuildDir $BuildDir
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

function Get-QtRuntimeDllName {
    param(
        [string]$BaseName,
        [string]$Config
    )

    if ($Config -eq "Debug") {
        return "$($BaseName)d.dll"
    }
    return "$BaseName.dll"
}

function Copy-QtRuntimeDllSet {
    param(
        [string]$QtBinDir,
        [string]$DistDir,
        [string[]]$BaseNames,
        [string]$Config
    )

    foreach ($baseName in $BaseNames) {
        $dllName = Get-QtRuntimeDllName -BaseName $baseName -Config $Config
        $srcPath = Join-Path $QtBinDir $dllName
        if (!(Test-Path $srcPath)) {
            throw "Missing required Qt runtime DLL: $srcPath"
        }
        Copy-Item $srcPath (Join-Path $DistDir $dllName) -Force
    }
}

function Remove-PackagedDllIfPresent {
    param(
        [string]$DistDir,
        [string]$DllName
    )

    $dllPath = Join-Path $DistDir $DllName
    if (Test-Path $dllPath) {
        Remove-Item -Force $dllPath
    }
}

function Assert-PackageEntries {
    param(
        [string]$DistDir,
        [string[]]$RequiredRelativePaths,
        [string[]]$UnexpectedRelativePaths
    )

    foreach ($relativePath in $RequiredRelativePaths) {
        $fullPath = Join-Path $DistDir $relativePath
        if (!(Test-Path $fullPath)) {
            throw "Packaged artifact is missing required path: $fullPath"
        }
    }

    foreach ($relativePath in $UnexpectedRelativePaths) {
        $fullPath = Join-Path $DistDir $relativePath
        if (Test-Path $fullPath) {
            throw "Packaged artifact still contains deprecated path: $fullPath"
        }
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $BuildDir
if (![string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $QtRoot
}
$versionInfo = Read-VersionInfoFromCMake -CMakeFilePath (Join-Path $repoRoot "CMakeLists.txt")
$version = $versionInfo.PackageVersion
if ([string]::IsNullOrWhiteSpace($DistDir)) {
    $DistDir = Join-Path (Join-Path $repoRoot "dist") "MiaCode-v$version-win64"
} else {
    $DistDir = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $DistDir
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
$qtBinDir = Split-Path -Parent $deployTool

if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir | Out-Null

Copy-Item $exePath (Join-Path $DistDir "MiaCode.exe") -Force

$debugLauncherSrc = Join-Path $repoRoot "scripts\\Start_MiaCode_Debug.bat"
if (Test-Path $debugLauncherSrc) {
    Copy-Item $debugLauncherSrc (Join-Path $DistDir "Start_MiaCode_Debug.bat") -Force
}
$quickShellDebugLauncherSrc = Join-Path $repoRoot "scripts\\Start_MiaCode_QuickShell_Debug.bat"
if (Test-Path $quickShellDebugLauncherSrc) {
    Copy-Item $quickShellDebugLauncherSrc (Join-Path $DistDir "Start_MiaCode_QuickShell_Debug.bat") -Force
}
New-Item -ItemType Directory -Path (Join-Path $DistDir "logs") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "logs\\quick-shell-beta") -Force | Out-Null

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
    --qmldir (Join-Path $repoRoot "src") `
    (Join-Path $DistDir "MiaCode.exe")

$requiredQtRuntimeDllBaseNames = @(
    "Qt6Core",
    "Qt6Gui",
    "Qt6Widgets",
    "Qt6Multimedia",
    "Qt6Network",
    "Qt6OpenGL",
    "Qt6Quick",
    "Qt6Qml",
    "Qt6QmlMeta",
    "Qt6QmlModels",
    "Qt6QmlWorkerScript",
    "Qt6Svg"
)
Copy-QtRuntimeDllSet -QtBinDir $qtBinDir -DistDir $DistDir -BaseNames $requiredQtRuntimeDllBaseNames -Config $Config
foreach ($deprecatedBaseName in @("Qt6OpenGLWidgets", "Qt6Concurrent")) {
    $deprecatedDll = Get-QtRuntimeDllName -BaseName $deprecatedBaseName -Config $Config
    Remove-PackagedDllIfPresent -DistDir $DistDir -DllName $deprecatedDll
}
Remove-PackagedDllIfPresent -DistDir $DistDir -DllName "opengl32sw.dll"

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
foreach ($docSpec in @(
    @{ Source = Join-Path $repoRoot "README.md"; Destination = Join-Path $docsDir "README.md" },
    @{ Source = Join-Path $repoRoot "README_EN.md"; Destination = Join-Path $docsDir "README_EN.md" },
    @{ Source = Join-Path $repoRoot "docs\\DEBUG_INDEX.md"; Destination = Join-Path $docsDir "DEBUG_INDEX.md" },
    @{ Source = Join-Path $repoRoot "docs\\PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md"; Destination = Join-Path $docsDir "PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md" }
)) {
    if (Test-Path $docSpec.Source) {
        Copy-Item $docSpec.Source $docSpec.Destination -Force
    }
}
$releaseReadme = Join-Path $docsDir "RELEASE_README.txt"
$releaseLines = @(
    "MiaCode release package"
    ""
    "Run:"
    "  MiaCode.exe"
    "  MiaCode.exe --qt-native"
    "  Start_MiaCode_Debug.bat"
    "  Start_MiaCode_QuickShell_Debug.bat"
    ""
    "Debug logs:"
    "  .\\logs\\miacode_runtime_debug.log"
    "  .\\logs\\miacode_audio_debug.log"
    "  .\\logs\\miacode_video_export.log"
    "  .\\logs\\miacode_startup_timing.log"
    "  .\\logs\\miacode_fatal.log"
    "  .\\logs\\quick-shell-beta\\miacode_runtime_debug.log"
    "  .\\logs\\quick-shell-beta\\miacode_audio_debug.log"
    "  .\\logs\\quick-shell-beta\\miacode_video_export.log"
    "  .\\logs\\quick-shell-beta\\miacode_startup_timing.log"
    "  .\\logs\\quick-shell-beta\\miacode_fatal.log"
    ""
    "Included:"
    "  - MiaCode.exe (main app)"
    "  - Start_MiaCode_Debug.bat"
    "  - Start_MiaCode_QuickShell_Debug.bat"
    "  - Qt runtime DLLs, plugin folders, and QML modules"
    "  - ffmpeg/ffmpeg.exe"
    "  - assets/"
    "  - docs/"
    "  - logs/"
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

$requiredPackagePaths = @(
    "MiaCode.exe",
    "Start_MiaCode_Debug.bat",
    "Start_MiaCode_QuickShell_Debug.bat",
    "logs",
    "logs\\quick-shell-beta",
    (Get-QtRuntimeDllName -BaseName "Qt6Core" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Gui" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Widgets" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Multimedia" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Network" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6OpenGL" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Quick" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Qml" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6QmlMeta" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6QmlModels" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6QmlWorkerScript" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Svg" -Config $Config),
    "D3Dcompiler_47.dll",
    "dxcompiler.dll",
    "dxil.dll",
    "platforms\\qwindows.dll",
    "qml\\QtQuick\\qtquick2plugin.dll",
    "qml\\QtQml\\Models\\modelsplugin.dll",
    "qml\\QtQml\\WorkerScript\\workerscriptplugin.dll",
    "docs\\PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md"
)
$unexpectedPackagePaths = @(
    "Start_MiaCode_Debug_CompareDump.bat",
    "Start_MiaCode_Debug_View.bat",
    "Start_MiaCode_Debug_Widget.bat",
    (Get-QtRuntimeDllName -BaseName "Qt6OpenGLWidgets" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Concurrent" -Config $Config),
    "opengl32sw.dll"
)
Assert-PackageEntries -DistDir $DistDir -RequiredRelativePaths $requiredPackagePaths -UnexpectedRelativePaths $unexpectedPackagePaths

$zipPath = "$DistDir.zip"
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
$zipParentDir = Split-Path -Parent $DistDir
$zipFolderName = Split-Path -Leaf $DistDir
Push-Location $zipParentDir
try {
    Compress-Archive -Path $zipFolderName -DestinationPath $zipPath -CompressionLevel Optimal
} finally {
    Pop-Location
}

Write-Host "Packaged to $DistDir"
Write-Host "Zip created: $zipPath"
