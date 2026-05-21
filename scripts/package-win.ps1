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

function Compress-ArchiveWithRetry {
    param(
        [string]$SourcePath,
        [string]$DestinationPath,
        [int]$MaxAttempts = 5,
        [int]$InitialDelaySeconds = 2
    )

    $lastError = $null
    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        if (Test-Path -LiteralPath $DestinationPath) {
            Remove-Item -LiteralPath $DestinationPath -Force -ErrorAction SilentlyContinue
        }

        try {
            Compress-Archive -Path $SourcePath -DestinationPath $DestinationPath -CompressionLevel Optimal -ErrorAction Stop
            return
        } catch {
            $lastError = $_.Exception
            if ($attempt -ge $MaxAttempts) {
                break
            }

            $delaySeconds = [Math]::Min(30, $InitialDelaySeconds * $attempt)
            Write-Warning "Compress-Archive failed (attempt $attempt/$MaxAttempts): $($lastError.Message). Retrying in $delaySeconds seconds..."
            Start-Sleep -Seconds $delaySeconds
        }
    }

    throw "Compress-Archive failed after $MaxAttempts attempts: $($lastError.Message)"
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
    # Beta20+ unifies version display: package filename and About-dialog
    # version are always identical (CMakeLists used to keep them split via
    # MIACODE_DISPLAY_PRERELEASE; they drifted in practice and beta20
    # collapsed them).
    return [PSCustomObject]@{
        PackageVersion = $version
        DisplayVersion = $version
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
    Write-Host "Precheck: building MiaCodeLauncher ($Config) ..."
    & cmake --build $BuildDir --target MiaCodeLauncher --config $Config --parallel $BuildJobs | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build MiaCodeLauncher failed with exit code $LASTEXITCODE"
    }
}

function Invoke-MiaCodeConfigure {
    param(
        [string]$RepoRoot,
        [string]$BuildDir
    )

    Write-Host "Precheck: reconfiguring CMake in $BuildDir ..."
    # CMake prints qsb shader-compile diagnostics to stderr even on
    # success; PS 5.1's `$ErrorActionPreference = Stop` (set at the top
    # of this script) treats those as terminating errors and aborts the
    # configure with NativeCommandError. Relax around the call and gate
    # success on the exit code instead. Mirrors the windeployqt block.
    $prevErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & cmake -S $RepoRoot -B $BuildDir | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "cmake configure failed with exit code $LASTEXITCODE"
        }
    } finally {
        $ErrorActionPreference = $prevErrorActionPreference
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
    $versionMetadataChanged = $false
    if (!(Test-Path $exePath)) {
        $needsBuildReasons.Add("MiaCode executable is missing")
    }

    $generatedVersionInfo = Read-VersionInfoFromGeneratedHeader -HeaderPath $generatedHeaderPath
    if ($null -eq $generatedVersionInfo) {
        $needsBuildReasons.Add("generated AppVersion.h is missing or unreadable")
    } else {
        if ($generatedVersionInfo.PackageVersion -ne $ExpectedVersionInfo.PackageVersion) {
            $versionMetadataChanged = $true
        }
        if ($generatedVersionInfo.DisplayVersion -ne $ExpectedVersionInfo.DisplayVersion) {
            $versionMetadataChanged = $true
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

    if ($versionMetadataChanged -or $needsBuildReasons.Count -gt 0) {
        Write-Host "Precheck: refreshing package build."
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

# All runtime DLLs and Qt plugin/QML subtrees live in app/ so the dist
# root only exposes the user-facing entry points (launcher exe + .bat
# files + assets/ + logs/).
$appDir = Join-Path $DistDir "app"
New-Item -ItemType Directory -Path $appDir | Out-Null

# Real MiaCode.exe goes inside app/; the wrapper at root is named
# MiaCode.exe and forwards execution to it.
Copy-Item $exePath (Join-Path $appDir "MiaCode.exe") -Force

$buildOutputDir = Join-Path $BuildDir $Config
$launcherExe = Join-Path $buildOutputDir "MiaCodeLauncher.exe"
if (!(Test-Path $launcherExe)) {
    throw "MiaCodeLauncher.exe not found after build: $launcherExe"
}
Copy-Item $launcherExe (Join-Path $DistDir "MiaCode.exe") -Force

$debugLauncherSrc = Join-Path $repoRoot "scripts\\Start_MiaCode_Debug.bat"
if (!(Test-Path $debugLauncherSrc)) {
    throw "Missing required launcher script: $debugLauncherSrc"
}
Copy-Item $debugLauncherSrc (Join-Path $DistDir "Start_MiaCode_Debug.bat") -Force

# beta55 diagnostic launchers — paired with the playback-rate crash
# investigation. Default Start_MiaCode_Debug.bat reproduces the crash;
# these two .bat files toggle env vars that isolate the root cause:
#   * Start_MiaCode_DisablePerPixelAlpha.bat → MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA=0
#     Tests hypothesis #1 (QRhi D3D11 texture invalidation during
#     Qt setPlaybackRate). If launching with this .bat survives the
#     rate switch, hypothesis confirmed.
#   * Start_MiaCode_FFmpegBackend.bat → QT_MEDIA_BACKEND=ffmpeg
#     Tests whether the Windows Media Foundation backend specifically
#     is involved; logs_24 already showed FFmpeg also crashes, so this
#     is mostly a control / fallback option.
# Both .bat files live in scripts/ (kept in source control) AND in the
# zip root (alongside Start_MiaCode_Debug.bat) so users can double-click
# them. When the investigation closes they can be removed from both
# sides — for now they ship with every package.
foreach ($diagLauncher in @("Start_MiaCode_DisablePerPixelAlpha.bat", "Start_MiaCode_FFmpegBackend.bat")) {
    $src = Join-Path $repoRoot "scripts\\$diagLauncher"
    if (!(Test-Path $src)) {
        throw "Missing required diagnostic launcher script: $src"
    }
    Copy-Item $src (Join-Path $DistDir $diagLauncher) -Force
}

# beta51: Start_MiaCode_Debug.bat is the only "normal" launcher shipped now.
# The legacy QML launcher and the beta45 D3D11/Plugin/AsyncLog triage
# launchers (Start_MiaCode_Legacy_QML / SkipDiagD3D11 / DiagBypass /
# SkipBoth / QtPluginDiag) used to land here too. They were diagnostic
# probes for regressions long since closed; keeping them around mostly
# confused testers about which .bat to double-click. The source files
# in scripts/ are kept (cheap, useful for one-off reproducions) but
# they no longer flow into the distributed zip.

New-Item -ItemType Directory -Path (Join-Path $DistDir "logs") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "logs\\worker-hwnd") -Force | Out-Null

if ($Config -eq "Debug") {
    $deployMode = "--debug"
} else {
    $deployMode = "--release"
}

# windeployqt routinely warns to stderr (e.g. "Cannot find any version of
# the dxcompiler.dll and dxil.dll" — we handle those ourselves below from
# the build output dir). PS 5.1 treats native-command stderr as a
# terminating error under `$ErrorActionPreference = Stop`, which would
# abort the package right after windeployqt with no real failure. Drop
# to Continue around the call and verify success via $LASTEXITCODE.
$prevErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    & $deployTool `
        --dir $appDir `
        $deployMode `
        --compiler-runtime `
        --no-translations `
        --qmldir (Join-Path $repoRoot "src") `
        (Join-Path $appDir "MiaCode.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
} finally {
    $ErrorActionPreference = $prevErrorActionPreference
}

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
    # QuickShell needs QtQuick.Controls (toolbar / sidebar / dialog
    # components throughout src/app/quick_shell/qml/). MiaCode.exe
    # imports Qt6QuickControls2 directly (verified via dumpbin).
    "Qt6QuickControls2",
    "Qt6Svg"
)
Copy-QtRuntimeDllSet -QtBinDir $qtBinDir -DistDir $appDir -BaseNames $requiredQtRuntimeDllBaseNames -Config $Config
foreach ($deprecatedBaseName in @("Qt6OpenGLWidgets", "Qt6Concurrent")) {
    $deprecatedDll = Get-QtRuntimeDllName -BaseName $deprecatedBaseName -Config $Config
    Remove-PackagedDllIfPresent -DistDir $appDir -DllName $deprecatedDll
}
Remove-PackagedDllIfPresent -DistDir $appDir -DllName "opengl32sw.dll"

foreach ($runtimeDll in @("dxcompiler.dll", "dxil.dll")) {
    $srcDll = Join-Path $buildOutputDir $runtimeDll
    if (Test-Path $srcDll) {
        Copy-Item $srcDll (Join-Path $appDir $runtimeDll) -Force
    }
}

# Beta49: app-local VC++ runtime to bypass user-system MSVCP140/VCRUNTIME140
# version mismatch. Confirmed root cause for the Win10-22H2 AMD Renoir
# silent-crash report — fault at MSVCP140.dll+0x12EB0 in 4 independent code
# paths, all from the first std::mutex lock of the process. Loader pulls
# the system32 copy if no app-local copy exists; bundling our build-machine
# version next to MiaCode.exe makes the loader find app/ first (default
# DLL search order: directory of the EXE → System32 → ...).
$vcRuntimeSrc = $null
# Probe candidate Visual Studio 2022 redist layouts. We can't rely on
# $env:VCToolsRedistDir because package-win.ps1 is normally invoked from
# a plain PowerShell session, not VsDevCmd. Walk the well-known install
# roots and pick the highest-versioned Microsoft.VC14X.CRT folder under
# x64. Tested against BuildTools, Community, Professional, Enterprise.
$vsCandidateRoots = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC'
)
foreach ($root in $vsCandidateRoots) {
    if (!(Test-Path $root)) { continue }
    $crtDirs = Get-ChildItem -Path (Join-Path $root '*\x64\Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue
    if ($crtDirs) {
        # Prefer numeric version dirs (14.x.y) over symlink-style (v143).
        # Sort by parent's parent name descending so the highest version wins.
        $vcRuntimeSrc = ($crtDirs | Sort-Object { $_.Parent.Parent.Name } -Descending | Select-Object -First 1).FullName
        break
    }
}

if (![string]::IsNullOrWhiteSpace($vcRuntimeSrc) -and (Test-Path $vcRuntimeSrc)) {
    Write-Host "Bundling VC++ runtime from: $vcRuntimeSrc"
    $vcRuntimeDlls = @(
        'vcruntime140.dll',
        'vcruntime140_1.dll',
        'msvcp140.dll',
        'msvcp140_1.dll',
        'msvcp140_2.dll',
        'msvcp140_codecvt_ids.dll',
        'concrt140.dll'
    )
    foreach ($dll in $vcRuntimeDlls) {
        $src = Join-Path $vcRuntimeSrc $dll
        if (Test-Path $src) {
            Copy-Item $src (Join-Path $appDir $dll) -Force
            Write-Host "  + $dll"
        }
    }
} else {
    Write-Warning "VC++ runtime redist directory not found; app-local CRT bundling skipped."
    Write-Warning "Expected: `$env:VCToolsRedistDir/x64/Microsoft.VC143.CRT/"
}

$bassRuntimeDir = Join-Path $repoRoot "third_party\\bass\\bin\\win64"
$requiredBassRuntimeDlls = @(
    "bass.dll",
    "bassmix.dll",
    "bass_fx.dll",
    "bass_aac.dll",
    "bassopus.dll"
)
foreach ($runtimeDll in $requiredBassRuntimeDlls) {
    $srcDll = Join-Path $bassRuntimeDir $runtimeDll
    if (!(Test-Path $srcDll)) {
        throw "Missing required BASS runtime DLL: $srcDll"
    }
    Copy-Item $srcDll (Join-Path $appDir $runtimeDll) -Force
}

# (libmpv removed in beta20 — chart-preview video backgrounds use
# Qt6Multimedia's QMediaPlayer + QVideoSink stack via PreviewStageMediaHost,
# not the planned MpvVideoSource that never landed. libmpv-2.dll was 113 MB
# of dead weight just to log a startup probe version line.)

if ($IncludeDevTools) {
    foreach ($toolName in @("simai_native_dump.exe")) {
        $toolPath = Join-Path $buildOutputDir $toolName
        if (Test-Path $toolPath) {
            Copy-Item $toolPath (Join-Path $appDir $toolName) -Force
        }
    }
}

$assetsSrc = Join-Path $repoRoot "assets"
if (Test-Path $assetsSrc) {
    $requiredSfxDir = Join-Path $assetsSrc "SFX"
    $requiredSfxFiles = @(
        "answer.wav",
        "break_tap.wav",
        "slide.wav",
        "break.wav",
        "break_slide.wav",
        "slide_break_start.wav",
        "slide_break_slide.wav",
        "tap_ex.wav",
        "tap_perfect.wav",
        "touch_hanabi.wav",
        "touch.wav",
        "touch_Hold_riser.wav",
        "clock.wav"
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
    # ffmpeg lives under app/ alongside the real exe so that
    # resolveFfmpegExecutable() finds it via the appDir/ffmpeg/
    # candidate (the real exe's applicationDirPath = app/).
    $ffmpegDstDir = Join-Path $appDir "ffmpeg"
    New-Item -ItemType Directory -Path $ffmpegDstDir -Force | Out-Null
    Copy-Item $ffmpegSrc (Join-Path $ffmpegDstDir "ffmpeg.exe") -Force
} else {
    Write-Host "Run .\scripts\ensure-windows-ffmpeg.ps1 to download the pinned Windows ffmpeg binary."
    throw "Missing required ffmpeg binary: $ffmpegSrc"
}

$requiredPackagePaths = @(
    # Root: user-facing entry points + content + log dirs only.
    "MiaCode.exe",
    "Start_MiaCode_Debug.bat",
    # beta55 diagnostic launchers (see comment block at the copy site).
    "Start_MiaCode_DisablePerPixelAlpha.bat",
    "Start_MiaCode_FFmpegBackend.bat",
    "logs",
    "logs\\worker-hwnd",
    "assets\\SFX",
    # app/: the real exe and its DLL/plugin/QML retinue.
    "app\\MiaCode.exe",
    "app\\ffmpeg\\ffmpeg.exe",
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Core" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Gui" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Widgets" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Multimedia" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Network" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6OpenGL" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Quick" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Qml" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6QmlMeta" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6QmlModels" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6QmlWorkerScript" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6QuickControls2" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Svg" -Config $Config)),
    "app\\D3Dcompiler_47.dll",
    "app\\dxcompiler.dll",
    "app\\dxil.dll",
    "app\\bass.dll",
    "app\\bassmix.dll",
    "app\\bass_fx.dll",
    "app\\bass_aac.dll",
    "app\\bassopus.dll",
    "app\\platforms\\qwindows.dll",
    "app\\qml\\QtQuick\\qtquick2plugin.dll",
    "app\\qml\\QtQuick\\Controls\\qtquickcontrols2plugin.dll",
    "app\\qml\\QtQuick\\Layouts\\qquicklayoutsplugin.dll",
    "app\\qml\\QtQml\\Models\\modelsplugin.dll",
    "app\\qml\\QtQml\\WorkerScript\\workerscriptplugin.dll"
)
$unexpectedPackagePaths = @(
    # docs/ + RELEASE_README.txt intentionally removed.
    "docs",
    "RELEASE_README.txt",
    "Start_MiaCode_Debug_CompareDump.bat",
    "Start_MiaCode_Debug_View.bat",
    "Start_MiaCode_Debug_Widget.bat",
    "Start_MiaCode_QuickShell_Debug.bat",
    # beta51: Start_MiaCode_Debug.bat is the only launcher shipped. The
    # legacy QML and beta45 triage launchers are no longer copied into
    # DistDir; assert they don't accidentally come back via a stray
    # build step or leftover output dir.
    "Start_MiaCode_Legacy_QML.bat",
    "Start_MiaCode_SkipDiagD3D11.bat",
    "Start_MiaCode_DiagBypass.bat",
    "Start_MiaCode_SkipBoth.bat",
    "Start_MiaCode_QtPluginDiag.bat",
    "logs\\quick-shell-beta",
    # No DLLs at root anymore — everything moved to app/.
    (Get-QtRuntimeDllName -BaseName "Qt6Core" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Gui" -Config $Config),
    (Get-QtRuntimeDllName -BaseName "Qt6Widgets" -Config $Config),
    "bass.dll",
    "bassmix.dll",
    "D3Dcompiler_47.dll",
    "dxcompiler.dll",
    "ffmpeg\\ffmpeg.exe",
    "libmpv-2.dll",
    "soundtouch_probe.exe",
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6OpenGLWidgets" -Config $Config)),
    (Join-Path "app" (Get-QtRuntimeDllName -BaseName "Qt6Concurrent" -Config $Config)),
    "app\\opengl32sw.dll"
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
    Compress-ArchiveWithRetry -SourcePath $zipFolderName -DestinationPath $zipPath
} finally {
    Pop-Location
}

Write-Host "Packaged to $DistDir"
Write-Host "Zip created: $zipPath"
