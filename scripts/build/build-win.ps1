<#
.SYNOPSIS
    Provisions the Windows toolchain and produces a runnable package.

.DESCRIPTION
    Runs the whole Windows chain in order:
      1. FFmpeg export binary        (scripts/ffmpeg/ensure-windows-ffmpeg.ps1)
      2. FFmpeg preview dev SDK      (scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1)
      3. Qt                          (existing install preferred, repository download last)
      4. CMake configure + build     (MiaCode + MiaCodeLauncher)
      5. Package                     (scripts/build/package-win.ps1)

    Qt version, toolchain definitions and the package contents contract live in
    scripts/build/windows-toolchain.psd1.

.PARAMETER Toolchain
    msvc or msvc-arm64. Selects the target architecture, Qt
    architecture directory and the C++ runtime that ends up in the package.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain msvc -BuildJobs 8

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain msvc-arm64 -BuildDir build-msvc-arm64
#>
param(
    [ValidateSet("msvc", "msvc-arm64")]
    [string]$Toolchain = "msvc",
    [string]$QtRoot = "",
    [string]$QtVersion = "",
    [string[]]$QtModules = @(),
    [string]$QtOutputDir = "",
    [string]$BuildDir = "build",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [int]$BuildJobs = [Environment]::ProcessorCount,
    # Skip the decode-only preview FFmpeg SDK build for architectures that
    # default to it (FFmpeg.TrimByArch in windows-toolchain.psd1). The trim
    # toolchain builds the x64 av*.dll set from FFmpeg source, which takes the
    # packaged size from ~150 MB to ~20 MB; arm64 has no trim path and always
    # uses the full BtbN SDK.
    [switch]$SkipTrim
)

$ErrorActionPreference = "Stop"

$toolchainData = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "windows-toolchain.psd1")
$toolchainSpec = $toolchainData.Toolchains.$Toolchain
if ($null -eq $toolchainSpec) {
    throw "Unknown toolchain '$Toolchain' (expected one of: $($toolchainData.Toolchains.Keys -join ', '))"
}
if ([string]::IsNullOrWhiteSpace($QtVersion)) { $QtVersion = $toolchainData.Qt.Version }
if ($QtModules.Count -eq 0) { $QtModules = $toolchainData.Qt.Modules }
$targetArch = $toolchainSpec.Arch
if ([string]::IsNullOrWhiteSpace($targetArch)) {
    throw "Toolchain '$Toolchain' has no Arch entry in windows-toolchain.psd1."
}

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

function Test-QtRoot {
    # A usable Qt root has the CMake package and the deployment tool.
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path $Path)) {
        return $false
    }
    return (Test-Path (Join-Path $Path "lib\cmake\Qt6")) -and (Test-Path (Join-Path $Path "bin\windeployqt.exe"))
}

function Expand-QtCandidate {
    param(
        [string]$Candidate,
        [string]$Version,
        [string]$ArchDir
    )

    if ($Candidate -match '^\$env:([A-Za-z0-9_]+)$') {
        return [Environment]::GetEnvironmentVariable($Matches[1])
    }
    return $Candidate.Replace("{version}", $Version).Replace("{archdir}", $ArchDir)
}

function Resolve-QtRootFromConfigDir {
    # Qt6_DIR points at <root>\lib\cmake\Qt6; walk up to the root.
    param([string]$ConfigDir)

    if ([string]::IsNullOrWhiteSpace($ConfigDir)) {
        return ""
    }
    $path = $ConfigDir
    for ($depth = 0; $depth -lt 6 -and ![string]::IsNullOrWhiteSpace($path); $depth++) {
        if ((Split-Path $path -Leaf) -eq "lib") {
            return (Split-Path $path -Parent)
        }
        $path = Split-Path $path -Parent
    }
    return $ConfigDir
}

function Enter-MsvcEnvironment {
    param([string]$Architecture)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path $vswhere)) {
        throw "vswhere.exe not found."
    }
    $vsInstall = (& $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($vsInstall)) {
        throw "Visual Studio installation not found."
    }
    Import-Module (Join-Path $vsInstall "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
    Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation `
        -DevCmdArguments "-arch=$Architecture -host_arch=$Architecture"
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($QtOutputDir)) {
    $QtOutputDir = Join-Path $repoRoot ".qt"
} else {
    $QtOutputDir = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $QtOutputDir
}
$BuildDir = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $BuildDir

$buildDevTools = if ($Config -eq "Debug") { "ON" } else { "OFF" }

# arm64 keeps its pinned static export binary. The x64 trim build produces a
# shared ffmpeg.exe beside the preview DLLs.
if ($targetArch -eq "arm64") {
    & (Join-Path $repoRoot "scripts\ffmpeg\ensure-windows-ffmpeg.ps1") -RepoRoot $repoRoot -Arch $targetArch
    if ($LASTEXITCODE -ne 0) {
        throw "Windows ffmpeg preparation failed."
    }
}

# 2. Preview FFmpeg dev SDK. A restored media cache is consumed as-is.
$trimPreviewSdk = ($toolchainData.FFmpeg.TrimByArch.$targetArch) -and !$SkipTrim
if ($trimPreviewSdk) {
    $devSdkDir = Join-Path $repoRoot $toolchainData.FFmpeg.DevDirByArch.$targetArch
    $exportFfmpeg = Join-Path (Split-Path $devSdkDir -Parent) "ffmpeg.exe"
    $previewSdkReady = (Test-Path (Join-Path $devSdkDir "include\libavcodec\avcodec.h")) -and
        (Test-Path $exportFfmpeg) -and ((Get-Item $exportFfmpeg).Length -lt 48MB)
    foreach ($runtimeDll in $toolchainData.FFmpeg.RuntimeDllsByArch.$targetArch) {
        $previewSdkReady = $previewSdkReady -and (Test-Path (Join-Path $devSdkDir "bin\$runtimeDll"))
    }
    if ($previewSdkReady) {
        Write-Host "FFmpeg: using cached preview SDK at $devSdkDir"
    } else {
        Write-Host "FFmpeg: building the decode-only preview SDK into $devSdkDir"
        & (Join-Path $repoRoot "scripts\ffmpeg\trim\build-trimmed-ffmpeg.ps1") `
            -OutputDir $devSdkDir `
            -Jobs $BuildJobs
        if ($LASTEXITCODE -ne 0) {
            throw "Windows FFmpeg trim build failed."
        }
    }
} else {
    & (Join-Path $repoRoot "scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1") -RepoRoot $repoRoot -Arch $targetArch
    if ($LASTEXITCODE -ne 0) {
        throw "Windows FFmpeg dev SDK preparation failed."
    }
}

# 3. Qt: an existing install wins over downloading a new one.
if (![string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $QtRoot
    if (!(Test-QtRoot $QtRoot)) {
        throw "Qt root '$QtRoot' does not contain lib\cmake\Qt6 and bin\windeployqt.exe."
    }
    Write-Host "Qt: using -QtRoot $QtRoot"
} else {
    foreach ($candidate in $toolchainData.Qt.RootCandidates) {
        $expanded = Expand-QtCandidate -Candidate $candidate -Version $QtVersion -ArchDir $toolchainSpec.ArchDir
        if ([string]::IsNullOrWhiteSpace($expanded)) {
            continue
        }
        $resolved = Resolve-QtRootFromConfigDir -ConfigDir $expanded
        $resolved = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $resolved
        if (Test-QtRoot $resolved) {
            $QtRoot = $resolved
            Write-Host "Qt: using existing install $QtRoot (candidate '$candidate')"
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    # Last resort: provision from the Qt download repository. provision-qt.ps1
    # probes both upstream layouts (flat pre-6.11 folders and the per-arch
    # folders 6.11 uses), which is why the aqtinstall route is no longer used.
    Write-Host "Qt: no local install found, provisioning Qt $QtVersion ($($toolchainSpec.AqtArch)) into $QtOutputDir"
    & (Join-Path $PSScriptRoot "provision-qt.ps1") `
        -Version $QtVersion `
        -AqtArch $toolchainSpec.AqtArch `
        -ArchDir $toolchainSpec.ArchDir `
        -HostPlatform $toolchainSpec.QtHostPlatform `
        -Modules $QtModules `
        -OutputDir $QtOutputDir
    if ($LASTEXITCODE -ne 0) {
        throw "Qt provisioning failed."
    }
    $QtRoot = Join-Path (Join-Path $QtOutputDir $QtVersion) $toolchainSpec.ArchDir
    if (!(Test-QtRoot $QtRoot)) {
        throw "Qt root not found under $QtOutputDir for Qt $QtVersion ($($toolchainSpec.ArchDir))."
    }
}

# 4. Configure + build.
$env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH"
Enter-MsvcEnvironment -Architecture $targetArch

$generator = $toolchainSpec.Generator
Write-Host "Configure: generator '$generator', Qt '$QtRoot', config '$Config'"

$configureArgs = @("-S", $repoRoot, "-B", $BuildDir, "-G", $generator,
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DMIACODE_BUILD_DEV_TOOLS=$buildDevTools")
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

$buildTargets = @("MiaCode", "MiaCodeLauncher")
if ($buildDevTools -eq "ON") {
    $buildTargets += @("simai_native_dump", "soundtouch_probe")
}
cmake --build $BuildDir --config $Config --target @buildTargets --parallel $BuildJobs
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

# 5. Package.
& (Join-Path $repoRoot "scripts\build\package-win.ps1") `
    -BuildDir $BuildDir `
    -Config $Config `
    -QtRoot $QtRoot `
    -Arch $targetArch `
    -BuildJobs $BuildJobs `
    -IncludeDevTools:($buildDevTools -eq "ON")
if ($LASTEXITCODE -ne 0) {
    throw "Windows packaging failed."
}
