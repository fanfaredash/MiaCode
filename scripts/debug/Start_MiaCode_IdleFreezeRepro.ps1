#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MiaCodeExe,

    [Parameter(Mandatory = $true)]
    [ValidateSet("GpuBound", "GpuOff")]
    [string]$Profile,

    [string]$LogRoot = (Join-Path (Get-Location) "miacode-idle-freeze-evidence"),

    [string]$ChartPath = ""
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

function Resolve-ExistingFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    return (Get-Item -LiteralPath $Path).FullName
}

function Get-CimSnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ClassName,
        [Parameter(Mandatory = $true)]
        [string[]]$Properties
    )

    try {
        return @(Get-CimInstance -ClassName $ClassName -ErrorAction Stop |
            Select-Object -Property $Properties)
    } catch {
        return @([PSCustomObject]@{
            Error = $_.Exception.Message
            ClassName = $ClassName
        })
    }
}

$resolvedExe = Resolve-ExistingFile -Path $MiaCodeExe -Label "MiaCode executable"
$exeItem = Get-Item -LiteralPath $resolvedExe
if ($exeItem.Name -ne "MiaCode.exe") {
    throw "Expected the real GUI executable named MiaCode.exe, got: $resolvedExe"
}

$resolvedChart = ""
if (![string]::IsNullOrWhiteSpace($ChartPath)) {
    $resolvedChart = Resolve-ExistingFile -Path $ChartPath -Label "Chart file"
}

$resolvedLogRoot = [System.IO.Path]::GetFullPath($LogRoot)
[void](New-Item -ItemType Directory -Path $resolvedLogRoot -Force)
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$runDirectory = Join-Path $resolvedLogRoot ("{0}-{1}" -f $timestamp, $Profile.ToLowerInvariant())
if (Test-Path -LiteralPath $runDirectory) {
    $runDirectory = "$runDirectory-$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
}
[void](New-Item -ItemType Directory -Path $runDirectory)

$gpuBinding = "1"
if ($Profile -eq "GpuOff") {
    $gpuBinding = "0"
}

$miacodeEnvironment = @(
    Get-ChildItem Env:MIACODE_* -ErrorAction SilentlyContinue |
        Sort-Object Name |
        Select-Object Name, Value
)
$fileHash = Get-FileHash -LiteralPath $resolvedExe -Algorithm SHA256
$osInfo = Get-CimSnapshot -ClassName "Win32_OperatingSystem" -Properties @(
    "Caption", "Version", "BuildNumber", "OSArchitecture", "LastBootUpTime"
)
$computerInfo = Get-CimSnapshot -ClassName "Win32_ComputerSystem" -Properties @(
    "Manufacturer", "Model", "SystemType", "TotalPhysicalMemory"
)
$cpuInfo = Get-CimSnapshot -ClassName "Win32_Processor" -Properties @(
    "Name", "Manufacturer", "NumberOfCores", "NumberOfLogicalProcessors", "MaxClockSpeed"
)
$memoryInfo = Get-CimSnapshot -ClassName "Win32_PhysicalMemory" -Properties @(
    "Manufacturer", "PartNumber", "Capacity", "Speed", "ConfiguredClockSpeed"
)
$gpuInfo = Get-CimSnapshot -ClassName "Win32_VideoController" -Properties @(
    "Name", "AdapterCompatibility", "AdapterRAM", "DriverVersion", "DriverDate", "PNPDeviceID"
)

$powerPath = Join-Path $runDirectory "powercfg.txt"
try {
    "# powercfg /getactivescheme" | Out-File -LiteralPath $powerPath -Encoding utf8
    (& powercfg.exe /getactivescheme 2>&1) | Out-File -LiteralPath $powerPath -Encoding utf8 -Append
    "`r`n# powercfg /query" | Out-File -LiteralPath $powerPath -Encoding utf8 -Append
    (& powercfg.exe /query 2>&1) | Out-File -LiteralPath $powerPath -Encoding utf8 -Append
} catch {
    "powercfg collection failed: $($_.Exception.Message)" |
        Out-File -LiteralPath $powerPath -Encoding utf8 -Append
}

$arguments = New-Object System.Collections.Generic.List[string]
$arguments.Add("--debug")
if (![string]::IsNullOrWhiteSpace($resolvedChart)) {
    $escapedChart = $resolvedChart.Replace('"', '\"')
    $arguments.Add("`"$escapedChart`"")
}

# The reported failure is contention, not idleness, so record which third-party
# GPU/CPU consumers were already running when this run started. Without it the
# "OBS on / browser playing video" variable is reconstructed from memory afterwards.
$contendingProcessNames = @(
    "obs64", "obs32", "obs",
    "chrome", "msedge", "firefox", "brave", "opera", "vivaldi",
    "ffmpeg", "nvcontainer", "Video.UI"
)
$contendingProcesses = @(
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $contendingProcessNames -contains $_.ProcessName } |
        Sort-Object ProcessName |
        Select-Object ProcessName, Id, @{ Name = "StartTime"; Expression = {
            try { $_.StartTime.ToUniversalTime().ToString("o") } catch { $null } } }
)

$oldLogDir = [Environment]::GetEnvironmentVariable("MIACODE_LOG_DIR", "Process")
$oldGpuBinding = [Environment]::GetEnvironmentVariable(
    "MIACODE_GPU_BIND_HIGH_PERFORMANCE", "Process")
# Frame-pacing diagnostics are mandatory for this matrix: they are what produce the
# render_frame_profile line (paint_ms / sync_ms / pre_render_wait_ms / render_submit_ms /
# swap_gpu_ms) that the triage tree in the repro doc branches on, and every frame >= 30 ms
# is logged automatically rather than sampled. Setting it here removes the single most
# likely operator mistake: forgetting the flag and having to redo the run.
$oldFramePacingDiag = [Environment]::GetEnvironmentVariable(
    "MIACODE_PREVIEW_FRAME_PACING_DIAG", "Process")
$process = $null
try {
    [Environment]::SetEnvironmentVariable("MIACODE_LOG_DIR", $runDirectory, "Process")
    [Environment]::SetEnvironmentVariable(
        "MIACODE_GPU_BIND_HIGH_PERFORMANCE", $gpuBinding, "Process")
    [Environment]::SetEnvironmentVariable(
        "MIACODE_PREVIEW_FRAME_PACING_DIAG", "1", "Process")
    $process = Start-Process `
        -FilePath $resolvedExe `
        -ArgumentList $arguments.ToArray() `
        -WorkingDirectory $exeItem.DirectoryName `
        -PassThru
} finally {
    [Environment]::SetEnvironmentVariable("MIACODE_LOG_DIR", $oldLogDir, "Process")
    [Environment]::SetEnvironmentVariable(
        "MIACODE_GPU_BIND_HIGH_PERFORMANCE", $oldGpuBinding, "Process")
    [Environment]::SetEnvironmentVariable(
        "MIACODE_PREVIEW_FRAME_PACING_DIAG", $oldFramePacingDiag, "Process")
}

$metadata = [ordered]@{
    CollectedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    Profile = $Profile
    GpuBinding = $gpuBinding
    FramePacingDiagnostics = "1"
    LogDirectory = $runDirectory
    Executable = [ordered]@{
        Path = $resolvedExe
        SHA256 = $fileHash.Hash
        SizeBytes = $exeItem.Length
        LastWriteTimeUtc = $exeItem.LastWriteTimeUtc.ToString("o")
        FileVersion = $exeItem.VersionInfo.FileVersion
        ProductVersion = $exeItem.VersionInfo.ProductVersion
    }
    Launch = [ordered]@{
        ProcessId = if ($null -ne $process) { $process.Id } else { $null }
        Arguments = @($arguments)
        ChartPath = $resolvedChart
    }
    ExistingMiaCodeEnvironment = $miacodeEnvironment
    ContendingProcesses = $contendingProcesses
    OperatingSystem = $osInfo
    ComputerSystem = $computerInfo
    Processor = $cpuInfo
    PhysicalMemory = $memoryInfo
    VideoControllers = $gpuInfo
}
$metadata | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $runDirectory "phase0.json") -Encoding utf8

Write-Host "MiaCode idle-freeze diagnostic run started."
Write-Host "  Profile: $Profile (MIACODE_GPU_BIND_HIGH_PERFORMANCE=$gpuBinding)"
Write-Host "  Frame pacing diagnostics: MIACODE_PREVIEW_FRAME_PACING_DIAG=1"
Write-Host "  PID: $($process.Id)"
Write-Host "  Evidence: $runDirectory"
Write-Host "  Contending processes at launch: $($contendingProcesses.Count)"
Write-Host "Record the OBS encoder (x264 or NVENC) with this run - it decides whether the"
Write-Host "contention lands on the CPU or on the 2 GB MX450, and it is currently unknown."
Write-Host "Do not close the frozen process before collecting Get-Process data and a full dump."
