<#
.SYNOPSIS
    Verifies a packaged Windows build: contents contract, FFmpeg trim match,
    dependency completeness and a smoke launch.

.DESCRIPTION
    Runs after package-win.ps1. Fails (exit 1) when the package is missing a
    required entry, contains a forbidden one, ships FFmpeg DLLs that differ from
    the provisioned dev SDK, leaves an import unresolved, or fails to start.

.PARAMETER DistDir
    Package directory, e.g. dist\MiaCode-v2.0.0-alpha-win64.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\build\verify-win-package.ps1 -DistDir .\dist\MiaCode-v2.0.0-alpha-win64
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$DistDir,
    [string]$QtRoot = "",
    [int]$LaunchTimeoutSeconds = 25,
    [switch]$IncludeDevTools,
    [switch]$SkipLaunch
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$toolchainData = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "windows-toolchain.psd1")
$DistDir = (Resolve-Path -LiteralPath $DistDir).Path
$appDir = Join-Path $DistDir "app"
$failures = New-Object System.Collections.Generic.List[string]

function Get-QtRuntimeDllName {
    param([string]$BaseName, [string]$Config = "Release")

    if ($Config -eq "Debug") { return "$($BaseName)d.dll" }
    return "$BaseName.dll"
}

function Expand-PackagePathEntry {
    param([string]$Entry)

    if ($Entry -like "app-qt:*") { return Join-Path "app" (Get-QtRuntimeDllName -BaseName $Entry.Substring(7)) }
    if ($Entry -like "qt:*") { return Get-QtRuntimeDllName -BaseName $Entry.Substring(3) }
    return $Entry
}

# --- 1. contents contract ----------------------------------------------------
Write-Host "== Contents contract =="
foreach ($entry in $toolchainData.Package.RequiredRelativePaths) {
    $relativePath = Expand-PackagePathEntry -Entry $entry
    if (!(Test-Path -LiteralPath (Join-Path $DistDir $relativePath))) {
        $failures.Add("missing required path: $relativePath")
    }
}
foreach ($toolchainName in $toolchainData.Toolchains.Keys) {
    foreach ($runtimeDll in $toolchainData.Toolchains.$toolchainName.RuntimeDlls) {
        $packaged = Join-Path $appDir $runtimeDll
        if (Test-Path -LiteralPath $packaged) {
            Write-Host "  runtime: $runtimeDll ($toolchainName)"
        }
    }
}
if (!(Test-Path -LiteralPath (Join-Path $appDir "libstdc++-6.dll")) -and !(Test-Path -LiteralPath (Join-Path $appDir "msvcp140.dll"))) {
    $failures.Add("no C++ runtime (GCC or MSVC) in app\ — the package would need one installed on the target machine")
}
foreach ($entry in $toolchainData.Package.ForbiddenRelativePaths) {
    # Dev-tool packages ship simai_native_dump.exe, which links Qt6::Widgets.
    if ($IncludeDevTools -and $entry -eq "app-qt:Qt6Widgets") { continue }
    $relativePath = Expand-PackagePathEntry -Entry $entry
    if (Test-Path -LiteralPath (Join-Path $DistDir $relativePath)) {
        $failures.Add("forbidden path present: $relativePath")
    }
}
if ($IncludeDevTools) {
    $qtWidgetsRelative = Expand-PackagePathEntry -Entry "app-qt:Qt6Widgets"
    if (!(Test-Path -LiteralPath (Join-Path $DistDir $qtWidgetsRelative))) {
        $failures.Add("missing required path for a dev-tools package: $qtWidgetsRelative")
    }
}

# --- 2. FFmpeg trim match ----------------------------------------------------
Write-Host "== FFmpeg runtime matches the provisioned dev SDK =="
$ffmpegBinDir = Join-Path $repoRoot (Join-Path $toolchainData.FFmpeg.DevDir "bin")
foreach ($dll in $toolchainData.FFmpeg.RuntimeDlls) {
    $packaged = Join-Path $appDir $dll
    $provisioned = Join-Path $ffmpegBinDir $dll
    if (!(Test-Path -LiteralPath $packaged)) {
        $failures.Add("missing packaged FFmpeg runtime: $dll")
        continue
    }
    if (!(Test-Path -LiteralPath $provisioned)) {
        Write-Host "  skipped $dll (dev SDK not present)"
        continue
    }
    $packagedHash = (Get-FileHash -LiteralPath $packaged -Algorithm SHA256).Hash
    $provisionedHash = (Get-FileHash -LiteralPath $provisioned -Algorithm SHA256).Hash
    if ($packagedHash -ne $provisionedHash) {
        $failures.Add("packaged $dll does not match the dev SDK copy (trimmed/untrimmed mismatch)")
    } else {
        $sizeMb = [math]::Round((Get-Item -LiteralPath $packaged).Length / 1MB, 1)
        Write-Host "  ok $dll ($sizeMb MB)"
    }
}

# --- 3. dependency completeness ---------------------------------------------
Write-Host "== Dependency completeness =="
$objdump = ""
foreach ($toolchainName in @("mingw", "msvc")) {
    $compilerRoot = $toolchainData.Toolchains.$toolchainName.CompilerRoot
    if (![string]::IsNullOrWhiteSpace($compilerRoot)) {
        $candidate = Join-Path $compilerRoot "bin\objdump.exe"
        if (Test-Path -LiteralPath $candidate) { $objdump = $candidate; break }
    }
}
if ([string]::IsNullOrWhiteSpace($objdump)) {
    $onPath = Get-Command objdump.exe -ErrorAction SilentlyContinue
    if ($null -ne $onPath) { $objdump = $onPath.Source }
}
if ([string]::IsNullOrWhiteSpace($objdump)) {
    $failures.Add("objdump not found (needed for the import scan); install the MinGW toolchain or run this on a machine that has it")
} else {
    $systemDlls = @{}
    foreach ($name in $toolchainData.SystemDlls) { $systemDlls[$name.ToLowerInvariant()] = $true }
    $peFiles = Get-ChildItem -LiteralPath $DistDir -Recurse -File |
        Where-Object { $_.Extension -in @(".exe", ".dll") }
    $unresolved = @{}
    foreach ($file in $peFiles) {
        $dump = & $objdump -p $file.FullName 2>$null
        foreach ($line in $dump) {
            if ($line -notmatch "DLL Name:\s*(\S+)") { continue }
            $import = $Matches[1]
            $lower = $import.ToLowerInvariant()
            if ($systemDlls.ContainsKey($lower) -or $lower.StartsWith("api-ms-win") -or $lower.StartsWith("ext-ms-win")) { continue }
            if ((Test-Path -LiteralPath (Join-Path $file.DirectoryName $import)) -or (Test-Path -LiteralPath (Join-Path $appDir $import))) { continue }
            if (!$unresolved.ContainsKey($file.Name)) { $unresolved[$file.Name] = New-Object System.Collections.Generic.List[string] }
            if (!$unresolved[$file.Name].Contains($import)) { $unresolved[$file.Name].Add($import) }
        }
    }
    Write-Host "  PE files scanned: $($peFiles.Count)"
    foreach ($file in $unresolved.Keys) {
        $failures.Add("unresolved imports in ${file}: $($unresolved[$file] -join ', ')")
    }
}

# --- 4. smoke launch ---------------------------------------------------------
if (!$SkipLaunch) {
    Write-Host "== Smoke launch =="
    $logDir = Join-Path $DistDir "logs"
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    Get-ChildItem -LiteralPath $logDir -Filter "*.log" -ErrorAction SilentlyContinue | Remove-Item -Force
    $previousLogDir = $env:MIACODE_LOG_DIR
    $env:MIACODE_LOG_DIR = $logDir
    try {
        $appProcess = Start-Process -FilePath (Join-Path $appDir "MiaCode.exe") -ArgumentList "--debug" -PassThru
        Start-Sleep -Seconds $LaunchTimeoutSeconds
        $appAlive = !$appProcess.HasExited
        if (!$appAlive) {
            $failures.Add("app\MiaCode.exe exited during the smoke window (code $($appProcess.ExitCode))")
        }
        Get-Process -Name MiaCode -ErrorAction SilentlyContinue | Stop-Process -Force
    } finally {
        $env:MIACODE_LOG_DIR = $previousLogDir
    }

    $runtimeLog = Join-Path $logDir "miacode_runtime_debug.log"
    if (!(Test-Path -LiteralPath $runtimeLog)) {
        $failures.Add("runtime log not written: $runtimeLog")
    } else {
        $logText = Get-Content -LiteralPath $runtimeLog -Raw
        # The app has two UI paths: the QML shell logs action=load_begin /
        # start_ok, the quick shell logs its backend readiness. Either one proves
        # the window came up.
        if ($logText -notmatch "action=load_begin|action=start_ok|quick_shell/backend") {
            $failures.Add("UI never reached a ready state (no load_begin / start_ok / quick_shell backend entry)")
        }
        if ($logText -match "load_failed|qml_object_creation_failed") {
            $failures.Add("QML UI failed to load (see $runtimeLog)")
        }
    }

    $launcherProcess = Start-Process -FilePath (Join-Path $DistDir "MiaCode.exe") -PassThru
    Start-Sleep -Seconds $LaunchTimeoutSeconds
    if ($launcherProcess.HasExited) {
        $failures.Add("dist\MiaCode.exe (launcher) exited during the smoke window (code $($launcherProcess.ExitCode))")
    }
    $childProcesses = Get-Process -Name MiaCode -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $launcherProcess.Id }
    if (!$childProcesses) {
        $failures.Add("launcher did not start the app process")
    }
    Get-Process -Name MiaCode -ErrorAction SilentlyContinue | Stop-Process -Force
}

# --- 5. archives ---------------------------------------------------------------
Write-Host "== Archives =="
$archiveFormats = $toolchainData.Package.ArchiveFormats
if (!$archiveFormats -or $archiveFormats.Count -eq 0) { $archiveFormats = @("7z") }
foreach ($archiveFormat in $archiveFormats) {
    $archivePath = "$DistDir.$archiveFormat"
    if (!(Test-Path -LiteralPath $archivePath)) {
        $failures.Add("missing archive: $archivePath")
        continue
    }
    $archiveSizeMb = [math]::Round((Get-Item -LiteralPath $archivePath).Length / 1MB, 1)
    if ((Get-Item -LiteralPath $archivePath).Length -lt 1MB) {
        $failures.Add("archive is suspiciously small: $archivePath")
    }
    Write-Host "  ${archiveFormat}: $archiveSizeMb MB"
}

# --- report ------------------------------------------------------------------
$sizeMb = [math]::Round((Get-ChildItem -LiteralPath $DistDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "Package size: $sizeMb MB"

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "FAILED checks:"
    foreach ($failure in $failures) { Write-Host "  - $failure" }
    exit 1
}
Write-Host "All checks passed."
