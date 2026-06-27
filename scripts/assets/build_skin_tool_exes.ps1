param(
    [string]$OutputDir = "dist/skin-tools-win64",
    [string]$WorkDir = "build/skin-tool-exe-work"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$outputPath = Join-Path $repoRoot $OutputDir
$workPath = Join-Path $repoRoot $WorkDir
$specPath = Join-Path $workPath "spec"
$pyInstallerDist = Join-Path $workPath "dist"
$pyInstallerBuild = Join-Path $workPath "build"

New-Item -ItemType Directory -Force -Path $outputPath, $specPath, $pyInstallerDist, $pyInstallerBuild | Out-Null

$tools = @(
    @{
        Name = "miacode-outline-canvas-tool"
        Script = Join-Path $repoRoot "scripts\assets\match_outline_canvas_ratio.py"
    },
    @{
        Name = "miacode-skin-mine-tool"
        Script = Join-Path $repoRoot "scripts\gen_skin_mine_sprites.py"
    }
)

foreach ($tool in $tools) {
    if (-not (Test-Path -LiteralPath $tool.Script)) {
        throw "Missing source script: $($tool.Script)"
    }

    python -m PyInstaller `
        --onefile `
        --clean `
        --noconfirm `
        --exclude-module numpy `
        --exclude-module pygame `
        --exclude-module psutil `
        --name $tool.Name `
        --distpath $pyInstallerDist `
        --workpath $pyInstallerBuild `
        --specpath $specPath `
        $tool.Script

    $exe = Join-Path $pyInstallerDist ($tool.Name + ".exe")
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "PyInstaller did not create $exe"
    }
    Copy-Item -LiteralPath $exe -Destination $outputPath -Force
}

Write-Host "Skin tool executables written to $outputPath"
