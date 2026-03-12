param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$SkipConfigure
)

$ErrorActionPreference = "Stop"

if (-not $SkipConfigure) {
    cmake --preset vs2022-qt6 -DMIACODE_BUILD_DEV_TOOLS=ON | Out-Host
}

$buildPreset = if ($Configuration -eq "Debug") { "debug" } else { "release" }
cmake --build --preset $buildPreset --target simai_parser_spec | Out-Host

$exePath = Join-Path -Path "build" -ChildPath "$Configuration/simai_parser_spec.exe"
if (-not (Test-Path $exePath)) {
    throw "simai_parser_spec not found: $exePath"
}

& $exePath
