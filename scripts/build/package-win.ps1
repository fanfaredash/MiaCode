param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",
    [string]$QtRoot = "",
    [string]$BuildDir = "build",
    [string]$DistDir = "",
    [switch]$IncludeDevTools,
    [ValidateRange(1, 64)]
    [int]$BuildJobs = 4
)

$ErrorActionPreference = "Stop"

# Qt version, toolchains and the package contents contract live in one data
# file so the build chain and the packaging chain cannot drift apart.
$toolchainData = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "windows-toolchain.psd1")
$packageChannel = if ([string]::IsNullOrWhiteSpace($env:MIACODE_PACKAGE_CHANNEL)) { "" } else { $env:MIACODE_PACKAGE_CHANNEL }

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
    $buildOutputDir = Resolve-BuildOutputDir -BuildDir $BuildDir -Config $Config
    $exePath = Join-Path $buildOutputDir "MiaCode.exe"
    $launcherExePath = Join-Path $buildOutputDir "MiaCodeLauncher.exe"

    if (!(Test-Path $cachePath)) {
        throw "Build directory is not configured: $BuildDir. Configure/build it first."
    }

    $needsBuildReasons = New-Object System.Collections.Generic.List[string]
    $versionMetadataChanged = $false
    if (!(Test-Path $exePath)) {
        $needsBuildReasons.Add("MiaCode executable is missing")
    }
    if (!(Test-Path $launcherExePath)) {
        $needsBuildReasons.Add("MiaCodeLauncher executable is missing")
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
    if (!(Test-Path $launcherExePath)) {
        throw "Launcher executable not found after precheck build: $launcherExePath"
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

function Resolve-BuildOutputDir {
    param(
        [string]$BuildDir,
        [string]$Config
    )

    # Multi-config generators (Visual Studio, Xcode) write the binaries into a
    # per-configuration subdirectory; single-config generators (Ninja, Make)
    # write them straight into the build directory.
    $configDir = Join-Path $BuildDir $Config
    if (Test-Path (Join-Path $configDir "MiaCode.exe")) {
        return $configDir
    }
    return $BuildDir
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

function Remove-FileWithRetry {
    param(
        [string]$Path,
        [int]$MaxAttempts = 5
    )

    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        if (!(Test-Path -LiteralPath $Path)) {
            return
        }
        try {
            Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
            return
        } catch {
            if ($attempt -ge $MaxAttempts) {
                throw
            }
            # Indexer / antivirus can hold a freshly written archive briefly.
            Start-Sleep -Seconds (2 * $attempt)
        }
    }
}

function Expand-PackagePathEntry {
    param(
        [string]$Entry,
        [string]$Config
    )

    # 'qt:<BaseName>'       -> <BaseName>[d].dll at the package root
    # 'app-qt:<BaseName>'   -> app\<BaseName>[d].dll
    if ($Entry -like "app-qt:*") {
        return Join-Path "app" (Get-QtRuntimeDllName -BaseName $Entry.Substring(7) -Config $Config)
    }
    if ($Entry -like "qt:*") {
        return Get-QtRuntimeDllName -BaseName $Entry.Substring(3) -Config $Config
    }
    return $Entry
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

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildDir = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $BuildDir
if (![string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Resolve-RepoPath -RepoRoot $repoRoot -PathValue $QtRoot
}
$versionInfo = Read-VersionInfoFromCMake -CMakeFilePath (Join-Path $repoRoot "CMakeLists.txt")
$version = $versionInfo.PackageVersion
if ($packageChannel -and $packageChannel -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "MIACODE_PACKAGE_CHANNEL must start with an alphanumeric character and contain only letters, digits, dots, underscores, or hyphens (got: $packageChannel)"
}
$archName = $toolchainData.Package.DistArchName.$Arch
if ([string]::IsNullOrWhiteSpace($archName)) {
    throw "No DistArchName entry for architecture '$Arch' in windows-toolchain.psd1."
}
$distNamePattern = $toolchainData.Package.DistNamePattern
if ([string]::IsNullOrWhiteSpace($distNamePattern)) {
    throw "Package.DistNamePattern is missing from windows-toolchain.psd1."
}
if ([string]::IsNullOrWhiteSpace($DistDir)) {
    $channelSegment = if ($packageChannel) { "-$packageChannel" } else { "" }
    $distName = $distNamePattern.Replace("{version}", $version).Replace("{channel}", $channelSegment).Replace("{arch}", $archName)
    $DistDir = Join-Path (Join-Path $repoRoot "dist") $distName
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

# The compiler that configured the build directory decides which C++ runtime
# the package has to carry. MSVC builds bundle the Visual Studio CRT; MinGW
# builds bundle the GCC runtime. A package must never rely on the target
# machine already having either one installed.
$cmakeCachePath = Join-Path $BuildDir "CMakeCache.txt"
$compilerId = ""
$compilerPath = ""
$cacheBuildType = ""
$cacheIsMultiConfig = $false
if (Test-Path $cmakeCachePath) {
    foreach ($cacheLine in Get-Content $cmakeCachePath) {
        if ($cacheLine -match '^CMAKE_CXX_COMPILER_ID:') { $compilerId = ($cacheLine -split "=", 2)[1].Trim() }
        if ($cacheLine -match '^CMAKE_CXX_COMPILER:') { $compilerPath = ($cacheLine -split "=", 2)[1].Trim() }
        if ($cacheLine -match '^CMAKE_BUILD_TYPE:') { $cacheBuildType = ($cacheLine -split "=", 2)[1].Trim() }
        if ($cacheLine -match '^CMAKE_CONFIGURATION_TYPES:') { $cacheIsMultiConfig = $true }
    }
}
# Single-config generators (Ninja, Make) ignore --config, so a mismatched
# -Config would silently package whatever the cache was built with.
if (!$cacheIsMultiConfig -and ![string]::IsNullOrWhiteSpace($cacheBuildType) -and $cacheBuildType -ne $Config) {
    throw "Build directory '$BuildDir' was configured with CMAKE_BUILD_TYPE=$cacheBuildType, but -Config $Config was requested. Reconfigure the build directory or pass -Config $cacheBuildType."
}
$isMinGw = ($compilerId -eq "GNU") -or ($compilerPath -match "g\+\+")
if ($isMinGw) {
    Write-Host "Compiler: MinGW ($compilerPath)"
} else {
    Write-Host "Compiler: MSVC ($compilerPath)"
}
# The toolchain key decides which C++ runtime the package carries: arm64 always
# comes from the MSVC toolchain, x64 from whichever compiler built the tree.
$toolchainKey = if ($Arch -eq "arm64") { "msvc-arm64" } elseif ($isMinGw) { "mingw" } else { "msvc" }
$toolchainSpec = $toolchainData.Toolchains.$toolchainKey
if ($null -eq $toolchainSpec) {
    throw "Unknown toolchain '$toolchainKey' in windows-toolchain.psd1."
}
$toolchainRuntimeDlls = $toolchainSpec.RuntimeDlls

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

$buildOutputDir = Resolve-BuildOutputDir -BuildDir $BuildDir -Config $Config
$launcherExe = Join-Path $buildOutputDir "MiaCodeLauncher.exe"
if (!(Test-Path $launcherExe)) {
    throw "MiaCodeLauncher.exe not found after build: $launcherExe"
}
Copy-Item $launcherExe (Join-Path $DistDir "MiaCode.exe") -Force

$debugLauncherSrc = Join-Path $repoRoot "scripts\\debug\\Start_MiaCode_Debug.bat"
if (!(Test-Path $debugLauncherSrc)) {
    throw "Missing required launcher script: $debugLauncherSrc"
}
Copy-Item $debugLauncherSrc (Join-Path $DistDir "Start_MiaCode_Debug.bat") -Force

# Start_MiaCode_Debug.bat is the only launcher shipped in the Windows package.
# Focused diagnostic launchers may exist in the repository for support work,
# but they stay out of DistDir and are asserted absent below so testers have
# one obvious entry point.

New-Item -ItemType Directory -Path (Join-Path $DistDir "logs") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "logs\\worker-hwnd") -Force | Out-Null

if ($Config -eq "Debug") {
    $deployMode = "--debug"
} else {
    $deployMode = "--release"
}

# windeployqt routinely warns to stderr (e.g. "Cannot find any version of
# the dxcompiler.dll and dxil.dll" — D3D12 shader compilation is not part of
# this package). PS 5.1 treats native-command stderr as a
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

$requiredQtRuntimeDllBaseNames = $toolchainData.Package.QtRuntimeDllBaseNames
if ($IncludeDevTools) {
    # windeployqt only stages what the product binary imports, and the dev tools
    # are separate executables, so copy Qt6Widgets explicitly for them.
    $requiredQtRuntimeDllBaseNames += "Qt6Widgets"
}
Copy-QtRuntimeDllSet -QtBinDir $qtBinDir -DistDir $appDir -BaseNames $requiredQtRuntimeDllBaseNames -Config $Config
foreach ($deprecatedBaseName in $toolchainData.Package.DeprecatedQtRuntimeDllBaseNames) {
    $deprecatedDll = Get-QtRuntimeDllName -BaseName $deprecatedBaseName -Config $Config
    Remove-PackagedDllIfPresent -DistDir $appDir -DllName $deprecatedDll
}
foreach ($removedAppFile in $toolchainData.Package.RemovedAppFiles) {
    Remove-PackagedDllIfPresent -DistDir $appDir -DllName $removedAppFile
}

# The app pins QQuickStyle::setStyle("Basic") (src/app/main.cpp) and its QML
# imports only QtQuick.Controls / QtQuick.Controls.impl, so every other
# Controls style is dead weight. The exact list lives in the toolchain data
# file together with the required-path contract that asserts they stay out.
$unusedQtStyleBaseNames = $toolchainData.Package.UnusedQtRuntimeDllBaseNames
if ($IncludeDevTools) {
    # The dev tools (simai_native_dump, soundtouch_probe) link Qt6::Widgets, so a
    # package that ships them must keep it.
    $unusedQtStyleBaseNames = @($unusedQtStyleBaseNames | Where-Object { $_ -ne "Qt6Widgets" })
}
foreach ($unusedBaseName in $unusedQtStyleBaseNames) {
    Remove-PackagedDllIfPresent -DistDir $appDir -DllName (Get-QtRuntimeDllName -BaseName $unusedBaseName -Config $Config)
}
$unusedQmlRelativePaths = $toolchainData.Package.UnusedQmlRelativePaths
foreach ($unusedRelativePath in $unusedQmlRelativePaths) {
    $unusedPath = Join-Path $appDir $unusedRelativePath
    if (Test-Path $unusedPath) {
        Remove-Item -Recurse -Force $unusedPath
    }
}

if ($isMinGw) {
    # windeployqt --compiler-runtime already stages these; copy them again so a
    # package can never ship without the GCC runtime the app imports.
    foreach ($mingwRuntimeDll in $toolchainData.Toolchains.mingw.RuntimeDlls) {
        $srcDll = Join-Path $qtBinDir $mingwRuntimeDll
        if (!(Test-Path $srcDll)) {
            throw "Missing required MinGW runtime DLL: $srcDll"
        }
        Copy-Item $srcDll (Join-Path $appDir $mingwRuntimeDll) -Force
        Write-Host "  + $mingwRuntimeDll"
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
# Probe the Visual Studio redist layouts that ship the CRT. Version layout
# and install root both vary by Visual Studio release (2022 uses
# Microsoft.VC143.CRT, 2026 uses Microsoft.VC145.CRT; roots moved from the
# hard-coded 2022 paths to version-agnostic ones), so discovery cannot hard
# code either. We still can't rely on $env:VCToolsRedistDir because
# package-win.ps1 is normally invoked from a plain PowerShell session, not
# VsDevCmd — but when the variable is present it names the exact versioned
# root and is preferred.
$vsRedistRoots = New-Object System.Collections.Generic.List[string]
if (![string]::IsNullOrWhiteSpace($env:VCToolsRedistDir)) {
    $vsRedistRoots.Add($env:VCToolsRedistDir.TrimEnd('\'))
}
$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vsWhere) {
    $installPaths = & $vsWhere -all -products * -property installationPath 2>$null
    foreach ($installPath in @($installPaths)) {
        if (![string]::IsNullOrWhiteSpace($installPath)) {
            $vsRedistRoots.Add((Join-Path $installPath.Trim() 'VC\Redist\MSVC'))
        }
    }
}
foreach ($installRoot in @(
    'C:\BuildTools',
    "$env:ProgramFiles\Microsoft Visual Studio",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
)) {
    if ([string]::IsNullOrWhiteSpace($installRoot) -or !(Test-Path $installRoot)) { continue }
    # Covers every Visual Studio year and edition without enumerating them.
    foreach ($redistRoot in (Get-ChildItem -Path (Join-Path $installRoot '*\*\VC\Redist\MSVC') -Directory -ErrorAction SilentlyContinue)) {
        $vsRedistRoots.Add($redistRoot.FullName)
    }
    $directRoot = Join-Path $installRoot 'VC\Redist\MSVC'
    if (Test-Path $directRoot) { $vsRedistRoots.Add($directRoot) }
}

foreach ($root in ($vsRedistRoots | Select-Object -Unique)) {
    if (!(Test-Path $root)) { continue }
    $redistArch = if ([string]::IsNullOrWhiteSpace($toolchainSpec.MsvcRedistArch)) { "x64" } else { $toolchainSpec.MsvcRedistArch }
    $crtDirs = Get-ChildItem -Path (Join-Path $root "*\$redistArch\Microsoft.VC*.CRT") -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Parent.Parent.Name -match '^\d+\.\d+\.\d+$' }
    if ($crtDirs) {
        # Highest numeric version dir wins, so the bundled CRT matches the
        # newest toolset available on this machine.
        $vcRuntimeSrc = ($crtDirs | Sort-Object { [version]$_.Parent.Parent.Name } -Descending | Select-Object -First 1).FullName
        break
    }
}

if (!$isMinGw -and ![string]::IsNullOrWhiteSpace($vcRuntimeSrc) -and (Test-Path $vcRuntimeSrc)) {
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
} elseif (!$isMinGw) {
    Write-Warning "VC++ runtime redist directory not found; app-local CRT bundling skipped."
    Write-Warning "Expected: <VS install>\VC\Redist\MSVC\<version>\$redistArch\Microsoft.VC14X.CRT/"
    Write-Warning "Set `$env:VCToolsRedistDir to the versioned redist root as a fallback."
}

$bassRuntimeDir = Join-Path $repoRoot $toolchainData.Bass.RuntimeDirByArch.$Arch
$requiredBassRuntimeDlls = $toolchainData.Bass.RuntimeDllsByArch.$Arch
foreach ($runtimeDll in $requiredBassRuntimeDlls) {
    $srcDll = Join-Path $bassRuntimeDir $runtimeDll
    if (!(Test-Path $srcDll)) {
        throw "Missing required BASS runtime DLL: $srcDll"
    }
    Copy-Item $srcDll (Join-Path $appDir $runtimeDll) -Force
}

# FFmpeg shared runtime for the QtAVPlayer preview decode backend.
# PreviewStageMediaHost decodes PV/BG via QtAVPlayer (FFmpeg n8.1 LGPL); these
# av*.dll must sit next to MiaCode.exe. avfilter is NET-NEW vs the older
# package (Qt never shipped it; avdevice is dropped — capture-device only); the
# other five overlap with what windeployqt stages for Qt
# Multimedia's ffmpeg plugin, so we copy our own build AFTER windeployqt
# (-Force overwrite) to keep the runtime matched to the import libs MiaCode
# linked against.
$ffmpegDevBin = Join-Path $repoRoot (Join-Path $toolchainData.FFmpeg.DevDirByArch.$Arch "bin")
$requiredFfmpegRuntimeDlls = $toolchainData.FFmpeg.RuntimeDllsByArch.$Arch
foreach ($runtimeDll in $requiredFfmpegRuntimeDlls) {
    $srcDll = Join-Path $ffmpegDevBin $runtimeDll
    if (!(Test-Path $srcDll)) {
        Write-Host "Run .\scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1 to provision the FFmpeg dev SDK (headers + import libs + runtime DLLs)."
        throw "Missing required FFmpeg runtime DLL: $srcDll"
    }
    Copy-Item $srcDll (Join-Path $appDir $runtimeDll) -Force
}

# (libmpv removed in beta20 — chart-preview video backgrounds decode via
# QtAVPlayer (FFmpeg) on Windows through PreviewStageMediaHost, not the planned
# MpvVideoSource that never landed. libmpv-2.dll was 113 MB of dead weight just
# to log a startup probe version line.)

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
    $requiredSfxFiles = $toolchainData.Package.RequiredSfxFiles
    foreach ($sfxFile in $requiredSfxFiles) {
        $sfxPath = Join-Path $requiredSfxDir $sfxFile
        if (!(Test-Path $sfxPath)) {
            throw "Missing required SFX asset: $sfxPath"
        }
    }
    Copy-Item $assetsSrc (Join-Path $DistDir "assets") -Recurse -Force

    # slide_data.json + the bundled fonts are embedded in MiaCode.exe via qrc
    # (:/data/slide_data.json, :/fonts/*); the loose copies under assets/ are
    # never read at runtime. Drop them so the package does not ship ~17 MB twice.
    foreach ($redundant in $toolchainData.Package.RedundantAssetPaths) {
        $redundantPath = Join-Path $DistDir "assets\$redundant"
        if (Test-Path $redundantPath) {
            Remove-Item $redundantPath -Recurse -Force
        }
    }
}

# Third-party font license (SIL OFL 1.1) for the bundled Resource Han Rounded
# fonts (思源圆体), which are embedded in MiaCode.exe via the intro qrc. OFL
# requires the license text to ship alongside the (redistributed) font.
$licensesSrc = Join-Path $repoRoot "licenses"
if (Test-Path $licensesSrc) {
    Copy-Item $licensesSrc (Join-Path $DistDir "licenses") -Recurse -Force
} else {
    throw "Missing licenses dir: $licensesSrc"
}

$releaseDocs = $toolchainData.Package.ReleaseDocs
foreach ($releaseDoc in $releaseDocs) {
    $releaseDocSrc = Join-Path $repoRoot $releaseDoc
    if (!(Test-Path $releaseDocSrc)) {
        throw "Missing release documentation file: $releaseDocSrc"
    }
    Copy-Item $releaseDocSrc (Join-Path $DistDir $releaseDoc) -Force
}

$ffmpegExportDir = Join-Path $repoRoot $toolchainData.FFmpeg.ExportDirByArch.$Arch
$ffmpegSrc = Join-Path $ffmpegExportDir $toolchainData.FFmpeg.ExportBinary
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
    Write-Host "Run .\scripts\ffmpeg\ensure-windows-ffmpeg.ps1 to download the pinned Windows ffmpeg binary."
    throw "Missing required ffmpeg binary: $ffmpegSrc"
}

# Required/forbidden package contents come from the toolchain data file, so the
# contract is data instead of code: 'qt:' / 'app-qt:' entries pick up the Debug
# 'd' suffix, and the runtime DLLs of the selected toolchain are appended.
$requiredPackagePaths = @($toolchainData.Package.RequiredRelativePaths | ForEach-Object {
    Expand-PackagePathEntry -Entry $_ -Config $Config
})
$archSpecificRequiredPaths = $toolchainData.Package.AdditionalRequiredRelativePathsByArch.$Arch
if ($null -ne $archSpecificRequiredPaths) {
    foreach ($entry in $archSpecificRequiredPaths) {
        $requiredPackagePaths += Expand-PackagePathEntry -Entry $entry -Config $Config
    }
}
foreach ($runtimeDll in $toolchainRuntimeDlls) {
    $requiredPackagePaths += Join-Path "app" $runtimeDll
}
$unexpectedPackagePaths = @($toolchainData.Package.ForbiddenRelativePaths | ForEach-Object {
    Expand-PackagePathEntry -Entry $_ -Config $Config
})
if ($IncludeDevTools) {
    $qtWidgetsDll = Expand-PackagePathEntry -Entry "app-qt:Qt6Widgets" -Config $Config
    $requiredPackagePaths += $qtWidgetsDll
    $unexpectedPackagePaths = @($unexpectedPackagePaths | Where-Object { $_ -ne $qtWidgetsDll })
}

Assert-PackageEntries -DistDir $DistDir -RequiredRelativePaths $requiredPackagePaths -UnexpectedRelativePaths $unexpectedPackagePaths

# Archives are produced from the toolchain data file. 7z (LZMA2 + solid blocks)
# reaches roughly half the size of the deflate zip for this payload.
$archiveFormats = $toolchainData.Package.ArchiveFormats
if (!$archiveFormats -or $archiveFormats.Count -eq 0) {
    $archiveFormats = @("7z")
}
$archiveParentDir = Split-Path -Parent $DistDir
$archiveFolderName = Split-Path -Leaf $DistDir
foreach ($archiveFormat in $archiveFormats) {
    $archivePath = "$DistDir.$archiveFormat"
    if (Test-Path $archivePath) {
        Remove-FileWithRetry -Path $archivePath
    }
    switch ($archiveFormat) {
        "zip" {
            Push-Location $archiveParentDir
            try {
                Compress-ArchiveWithRetry -SourcePath $archiveFolderName -DestinationPath $archivePath
            } finally {
                Pop-Location
            }
        }
        "7z" {
            $sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
            if ($null -eq $sevenZip) {
                $sevenZip = Get-Command 7za.exe -ErrorAction SilentlyContinue
            }
            if ($null -ne $sevenZip) {
                Push-Location $archiveParentDir
                try {
                    $sevenZipOutput = & $sevenZip.Source a -t7z -mx=9 -mmt=on -bso0 -bsp0 $archivePath $archiveFolderName 2>&1
                    if ($LASTEXITCODE -ne 0) {
                        throw "7z failed with exit code $LASTEXITCODE`n$sevenZipOutput"
                    }
                } finally {
                    Pop-Location
                }
            } else {
                # py7zr is installed with the build chain's Python deps and
                # writes the same format when 7z.exe is not on PATH.
                $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
                if ($null -eq $pythonCommand) {
                    $pythonCommand = Get-Command py.exe -ErrorAction SilentlyContinue
                }
                if ($null -eq $pythonCommand) {
                    throw "7z.exe not found and Python is unavailable for the py7zr fallback."
                }
                & $pythonCommand.Source -m py7zr c $archivePath (Join-Path $archiveParentDir $archiveFolderName)
                if ($LASTEXITCODE -ne 0) {
                    throw "py7zr failed with exit code $LASTEXITCODE"
                }
            }
        }
        default {
            throw "Unsupported archive format '$archiveFormat' (expected '7z' or 'zip')"
        }
    }
    $archiveSizeMb = [math]::Round((Get-Item -LiteralPath $archivePath).Length / 1MB, 1)
    Write-Host "Archive created: $archivePath ($archiveSizeMb MB)"
}

Write-Host "Packaged to $DistDir"
