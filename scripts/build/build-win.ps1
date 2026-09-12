<#
.SYNOPSIS
    Provisions the Windows toolchain and produces a runnable package.

.DESCRIPTION
    Runs the whole Windows chain in order:
      1. Python packaging deps (aqtinstall + py7zr)
      2. FFmpeg export binary        (scripts/ffmpeg/ensure-windows-ffmpeg.ps1)
      3. FFmpeg preview dev SDK      (scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1)
      4. Qt                          (existing install preferred, aqt download last)
      5. CMake configure + build     (MiaCode + MiaCodeLauncher)
      6. Package                     (scripts/build/package-win.ps1)

    Qt version, toolchain definitions and the package contents contract live in
    scripts/build/windows-toolchain.psd1.

.PARAMETER Toolchain
    mingw or msvc. Selects the generator, the compiler, the Qt architecture
    directory and the C++ runtime that ends up in the package.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain mingw

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain msvc -BuildJobs 8
#>
param(
    [ValidateSet("mingw", "msvc")]
    [string]$Toolchain = "msvc",
    [string]$QtRoot = "",
    [string]$QtVersion = "",
    [string[]]$QtModules = @(),
    [string]$PythonExe = "",
    [string]$QtOutputDir = "",
    [string]$BuildDir = "build",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [ValidateRange(1, 64)]
    [int]$BuildJobs = 4
)

$ErrorActionPreference = "Stop"

$toolchainData = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "windows-toolchain.psd1")
$toolchainSpec = $toolchainData.Toolchains.$Toolchain
if ($null -eq $toolchainSpec) {
    throw "Unknown toolchain '$Toolchain' (expected one of: $($toolchainData.Toolchains.Keys -join ', '))"
}
if ([string]::IsNullOrWhiteSpace($QtVersion)) { $QtVersion = $toolchainData.Qt.Version }
if ($QtModules.Count -eq 0) { $QtModules = $toolchainData.Qt.Modules }

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

function Resolve-PythonExe {
    param([string]$Preferred)

    if (![string]::IsNullOrWhiteSpace($Preferred)) {
        return $Preferred
    }

    foreach ($candidate in @("python", "py")) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            if ($candidate -eq "py") {
                return "py -3"
            }
            return $candidate
        }
    }

    throw "Python executable not found."
}

function Invoke-Python {
    param(
        [string]$PythonCommand,
        [string[]]$Arguments
    )

    if ($PythonCommand -eq "py -3") {
        & py -3 @Arguments
    } else {
        & $PythonCommand @Arguments
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed: $PythonCommand $($Arguments -join ' ')"
    }
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

function Resolve-MsvcGenerator {
    # Ask vswhere which Visual Studio versions exist, then take the matching
    # generator name straight from cmake itself. No hard-coded year/edition.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path $vswhere)) {
        return ""
    }
    $installVersions = & $vswhere -all -products * -property installationVersion 2>$null
    $majors = @($installVersions | ForEach-Object { ($_ -split "\.")[0] } | Where-Object { $_ } | Sort-Object -Unique -Descending)
    $generatorList = (cmake --help) -join "`n"
    foreach ($major in $majors) {
        $match = [regex]::Match($generatorList, "Visual Studio $major \d{4}")
        if ($match.Success) {
            return $match.Value
        }
    }
    return ""
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($QtOutputDir)) {
    $QtOutputDir = Join-Path $repoRoot ".qt"
} else {
    $QtOutputDir = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $QtOutputDir
}
$BuildDir = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $BuildDir

$pythonCommand = Resolve-PythonExe -Preferred $PythonExe
$buildDevTools = if ($Config -eq "Debug") { "ON" } else { "OFF" }

# 1. Python deps: py7zr backs the archive extraction fallback in
#    provision-qt.ps1 and package-win.ps1 (7z.exe is preferred when present).
Invoke-Python -PythonCommand $pythonCommand -Arguments @("-m", "pip", "install", "--user", "py7zr==1.0.*")

# 2. Export ffmpeg binary + 3. preview FFmpeg dev SDK (or its trimmed replacement).
& (Join-Path $repoRoot "scripts\ffmpeg\ensure-windows-ffmpeg.ps1") -RepoRoot $repoRoot
if ($LASTEXITCODE -ne 0) {
    throw "Windows ffmpeg preparation failed."
}
& (Join-Path $repoRoot "scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1") -RepoRoot $repoRoot
if ($LASTEXITCODE -ne 0) {
    throw "Windows FFmpeg dev SDK preparation failed."
}

# 4. Qt: an existing install wins over downloading a new one.
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

# 5. Configure + build.
$compilerBinDir = ""
if (![string]::IsNullOrWhiteSpace($toolchainSpec.CompilerRoot)) {
    $compilerBinDir = Join-Path $toolchainSpec.CompilerRoot "bin"
    $env:PATH = "$compilerBinDir;$env:PATH"
}
$env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH"

$generator = $toolchainSpec.Generator
if ([string]::IsNullOrWhiteSpace($generator)) {
    $generator = Resolve-MsvcGenerator
    if ([string]::IsNullOrWhiteSpace($generator)) {
        throw "No Visual Studio installation with C++ build tools found (vswhere returned nothing matching a cmake generator). Pass -QtRoot and install the tools, or use -Toolchain mingw."
    }
}
Write-Host "Configure: generator '$generator', Qt '$QtRoot', config '$Config'"

$configureArgs = @("-S", $repoRoot, "-B", $BuildDir, "-G", $generator,
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DMIACODE_BUILD_DEV_TOOLS=$buildDevTools")
if ($Toolchain -eq "mingw") {
    # Single-config generator: the configuration is a configure-time decision.
    $configureArgs += "-DCMAKE_BUILD_TYPE=$Config"
    if (![string]::IsNullOrWhiteSpace($toolchainSpec.CCompiler)) {
        $configureArgs += "-DCMAKE_C_COMPILER=$(Join-Path $toolchainSpec.CompilerRoot $toolchainSpec.CCompiler)"
    }
    if (![string]::IsNullOrWhiteSpace($toolchainSpec.CxxCompiler)) {
        $configureArgs += "-DCMAKE_CXX_COMPILER=$(Join-Path $toolchainSpec.CompilerRoot $toolchainSpec.CxxCompiler)"
    }
}
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

# 6. Package.
& (Join-Path $repoRoot "scripts\build\package-win.ps1") `
    -BuildDir $BuildDir `
    -Config $Config `
    -QtRoot $QtRoot `
    -BuildJobs $BuildJobs `
    -IncludeDevTools:($buildDevTools -eq "ON")
if ($LASTEXITCODE -ne 0) {
    throw "Windows packaging failed."
}
