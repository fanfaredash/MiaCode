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

$oldLogDir = [Environment]::GetEnvironmentVariable("MIACODE_LOG_DIR", "Process")
$oldGpuBinding = [Environment]::GetEnvironmentVariable(
    "MIACODE_GPU_BIND_HIGH_PERFORMANCE", "Process")
$process = $null
try {
    [Environment]::SetEnvironmentVariable("MIACODE_LOG_DIR", $runDirectory, "Process")
    [Environment]::SetEnvironmentVariable(
        "MIACODE_GPU_BIND_HIGH_PERFORMANCE", $gpuBinding, "Process")
    $process = Start-Process `
        -FilePath $resolvedExe `
        -ArgumentList $arguments.ToArray() `
        -WorkingDirectory $exeItem.DirectoryName `
        -PassThru
} finally {
    [Environment]::SetEnvironmentVariable("MIACODE_LOG_DIR", $oldLogDir, "Process")
    [Environment]::SetEnvironmentVariable(
        "MIACODE_GPU_BIND_HIGH_PERFORMANCE", $oldGpuBinding, "Process")
}

$metadata = [ordered]@{
    CollectedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    Profile = $Profile
    GpuBinding = $gpuBinding
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
Write-Host "  PID: $($process.Id)"
Write-Host "  Evidence: $runDirectory"
Write-Host "Do not close the frozen process before collecting Get-Process data and a full dump."
