param(
    [string]$PythonExe = "",
    [string]$QtVersion = "6.8.3",
    [string]$QtArch = "win64_msvc2022_64",
    [string[]]$QtModules = @("qtmultimedia"),
    [string]$QtOutputDir = "",
    [string]$BuildDir = "build",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

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

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($QtOutputDir)) {
    $QtOutputDir = Join-Path $repoRoot ".qt"
}

$pythonCommand = Resolve-PythonExe -Preferred $PythonExe

Invoke-Python -PythonCommand $pythonCommand -Arguments @("-m", "pip", "install", "--user", "aqtinstall==3.3.*", "py7zr==1.0.*")
$qtInstallArgs = @(
    "-m", "aqt", "install-qt",
    "windows", "desktop", $QtVersion, $QtArch,
    "--outputdir", $QtOutputDir,
    "--modules"
) + $QtModules
Invoke-Python -PythonCommand $pythonCommand -Arguments $qtInstallArgs

$qtRoot = Join-Path $QtOutputDir $QtVersion "msvc2022_64"
if (!(Test-Path $qtRoot)) {
    $qtRoot = Get-ChildItem -Path (Join-Path $QtOutputDir $QtVersion) -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "msvc*" } |
        Select-Object -First 1 -ExpandProperty FullName
}
if ([string]::IsNullOrWhiteSpace($qtRoot) -or !(Test-Path $qtRoot)) {
    throw "Qt root not found under $QtOutputDir for Qt $QtVersion"
}

$cmakePrefixPath = $qtRoot
$env:PATH = "$qtRoot\bin;$env:PATH"

cmake -S $repoRoot -B $BuildDir -G "Visual Studio 17 2022" -D CMAKE_PREFIX_PATH="$cmakePrefixPath"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

cmake --build $BuildDir --config $Config --target miacode
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

& (Join-Path $repoRoot "scripts\package-win.ps1") -BuildDir $BuildDir -Config $Config -QtRoot $qtRoot
if ($LASTEXITCODE -ne 0) {
    throw "Windows packaging failed."
}
