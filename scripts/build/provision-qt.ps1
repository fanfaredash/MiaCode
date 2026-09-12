<#
.SYNOPSIS
    Provisions Qt for Windows straight from the Qt download repository.

.DESCRIPTION
    Replaces the `aqt install-qt` step, which cannot handle Qt >= 6.11: upstream
    moved the desktop repository from one flat folder per version
    (<base>/qt6_6103/qt6_6103/Updates.xml) to one folder per architecture
    (<base>/qt6_6111/qt6_6111_msvc2022_64/Updates.xml). This script probes both
    layouts, reads the package metadata, downloads the archives of the base
    package plus the requested modules and extracts them into
    <OutputDir>/<version>/<archDir>.

.PARAMETER Version
    Qt version, e.g. 6.11.1.

.PARAMETER AqtArch
    Repository architecture name, e.g. win64_mingw or win64_msvc2022_64.

.PARAMETER ArchDir
    Target directory name under <OutputDir>/<version>/, e.g. mingw_64.

.PARAMETER HostPlatform
    Qt download repository host tree: windows_x86 for x64 hosts (including
    cross-compiled arm64 packages), windows_arm64 for native arm64 hosts.

.PARAMETER Modules
    Add-on modules to install alongside the base package, e.g. qtmultimedia.

.PARAMETER PlanOnly
    Resolve and print the plan (folders, packages, archive count) without
    downloading archives.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\build\provision-qt.ps1 -Version 6.11.1 -AqtArch win64_mingw -ArchDir mingw_64 -Modules qtmultimedia,qtshadertools
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$AqtArch,
    [Parameter(Mandatory = $true)][string]$ArchDir,
    [string]$HostPlatform = "windows_x86",
    [string[]]$Modules = @(),
    [string]$OutputDir = "",
    [string]$BaseUrl = "https://download.qt.io",
    [switch]$PlanOnly,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# Accept both -Modules a,b and -Modules a b (the -File invocation path always
# delivers a single comma-joined string).
$Modules = @($Modules | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot ".qt"
}

function Test-QtRootValid {
    param([string]$Path)
    return (Test-Path (Join-Path $Path "lib\cmake\Qt6")) -and (Test-Path (Join-Path $Path "bin\windeployqt.exe"))
}

function Get-Url {
    param([string]$Url, [string]$OutFile = "")

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -ne $curl) {
        if ([string]::IsNullOrWhiteSpace($OutFile)) {
            return (& $curl.Source -s -L --fail --retry 3 --retry-delay 2 $Url 2>$null) -join "`n"
        }
        & $curl.Source -s -L --fail --retry 3 --retry-delay 2 -o $OutFile $Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
        return ""
    }
    if ([string]::IsNullOrWhiteSpace($OutFile)) {
        return (Invoke-WebRequest -Uri $Url -UseBasicParsing).Content
    }
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
    return ""
}

function Test-UrlExists {
    param([string]$Url)

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -ne $curl) {
        # Range request keeps the probe to a byte; 206 (range honoured) and 200
        # (range ignored) both mean the path exists.
        $status = (& $curl.Source -s -o NUL -r 0-0 -w "%{http_code}" --max-time 30 $Url 2>$null)
        return ($status -eq "200" -or $status -eq "206")
    }
    try {
        $null = Invoke-WebRequest -Uri $Url -Method Head -UseBasicParsing
        return $true
    } catch {
        return $false
    }
}

$targetRoot = Join-Path (Join-Path $OutputDir $Version) $ArchDir
if ((Test-QtRootValid $targetRoot) -and !$Force) {
    Write-Host "Qt $Version ($ArchDir) already present at $targetRoot"
    exit 0
}

$versionNoDots = $Version.Replace(".", "")
$majorVersion = ($Version -split "\.")[0]
# The repository folder strips the host prefix from the aqt arch name:
# win64_mingw -> mingw, win64_msvc2022_64 -> msvc2022_64.
$repoArchSuffix = $AqtArch -replace '^win(64|32)_', ''
$desktopBase = "$BaseUrl/online/qtsdkrepository/$HostPlatform/desktop"
# Qt < 6.11 keeps every architecture beside a flat version folder; Qt >= 6.11
# uses one folder per architecture. Probe both and take whichever resolves.
$layoutCandidates = @(
    @{ Folder = "qt$majorVersion`_$versionNoDots/qt$majorVersion`_$versionNoDots" },
    @{ Folder = "qt$majorVersion`_$versionNoDots/qt$majorVersion`_${versionNoDots}_$repoArchSuffix" }
)
$layout = $null
foreach ($candidate in $layoutCandidates) {
    $updatesUrl = "$desktopBase/$($candidate.Folder)/Updates.xml"
    if (Test-UrlExists $updatesUrl) {
        $layout = $candidate
        $layout.UpdatesUrl = $updatesUrl
        break
    }
}
if ($null -eq $layout) {
    throw "No Qt repository metadata found for $Version / $AqtArch under $desktopBase. Check the version, the architecture and network access."
}
Write-Host "Qt repository layout: $($layout.Folder)"

$metadata = [xml](Get-Url -Url $layout.UpdatesUrl)

function Get-PackageArchives {
    param([xml]$Metadata, [string]$PackageName)

    foreach ($package in $Metadata.SelectNodes("//PackageUpdate")) {
        $name = $package.SelectSingleNode("Name").InnerText
        if ($name -ne $PackageName) { continue }
        $archives = @()
        $node = $package.SelectSingleNode("DownloadableArchives")
        if ($null -ne $node -and $node.InnerText) {
            $archives = @($node.InnerText -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        }
        return $archives
    }
    return @()
}

# Package names: qt.qt6.<versionNoDots>.<aqtArch> for the base, and
# qt.qt6.<versionNoDots>.addons.<module>.<aqtArch> for add-ons.
$packageNames = @("qt.qt6.$versionNoDots.$AqtArch")
foreach ($module in $Modules) {
    $packageNames += "qt.qt6.$versionNoDots.addons.$module.$AqtArch"
}

function Resolve-ArchiveEntry {
    # Every Qt 6.8+ repository keeps each package's files in a folder named after
    # the package and prefixes each file with a version-build stamp, so the
    # metadata name is a suffix of the real file name and has to be resolved by
    # listing the folder.
    param([string]$PackageFolderUrl, [string]$ArchiveName)

    $listing = Get-Url -Url $PackageFolderUrl
    foreach ($match in [regex]::Matches($listing, 'href="([^"]+)"')) {
        $href = $match.Groups[1].Value
        if ($href -notlike "*$ArchiveName" -or $href -like "*.sha1" -or $href -like "*.mirrorlist") {
            continue
        }
        return @{ Name = $href; Url = "$PackageFolderUrl/$href"; Sha1Url = "$PackageFolderUrl/$href.sha1" }
    }
    throw "Archive '$ArchiveName' is not listed in $PackageFolderUrl"
}

$downloadPlan = @()
foreach ($packageName in $packageNames) {
    $archives = Get-PackageArchives -Metadata $metadata -PackageName $packageName
    if ($archives.Count -eq 0) {
        throw "Package '$packageName' not found in $($layout.UpdatesUrl) (or it has no downloadable archives)."
    }
    $packageFolderUrl = "$desktopBase/$($layout.Folder)/$packageName"
    foreach ($archive in $archives) {
        $entry = Resolve-ArchiveEntry -PackageFolderUrl $packageFolderUrl -ArchiveName $archive
        $entry.Package = $packageName
        $downloadPlan += $entry
    }
}

$totalBytes = 0
Write-Host "Packages: $($packageNames -join ', ')"
Write-Host "Archives: $($downloadPlan.Count)"
foreach ($entry in $downloadPlan) { Write-Host "  $($entry.Name)" }

if ($PlanOnly) {
    Write-Host "Plan only: no archives downloaded."
    exit 0
}

New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("miacode-qt-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$extractor = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($null -eq $extractor) { $extractor = Get-Command 7za.exe -ErrorAction SilentlyContinue }

try {
    foreach ($entry in $downloadPlan) {
        $localArchive = Join-Path $tempDir $entry.Name
        Write-Host "Downloading $($entry.Name)"
        Get-Url -Url $entry.Url -OutFile $localArchive | Out-Null
        $sha1Text = Get-Url -Url $entry.Sha1Url
        if (![string]::IsNullOrWhiteSpace($sha1Text)) {
            $expectedSha1 = ($sha1Text -split "\s+")[0].Trim().ToUpperInvariant()
            $actualSha1 = (Get-FileHash -LiteralPath $localArchive -Algorithm SHA1).Hash
            if ($expectedSha1 -ne $actualSha1) {
                throw "SHA1 mismatch for $($entry.Name): expected $expectedSha1, got $actualSha1"
            }
        } else {
            Write-Host "  (no .sha1 published, skipping hash check)"
        }
        $totalBytes += (Get-Item -LiteralPath $localArchive).Length
        if ($null -ne $extractor) {
            & $extractor.Source x "-y" "-o$targetRoot" $localArchive | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "7z extraction failed for $($entry.Name)" }
        } else {
            $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
            if ($null -eq $pythonCommand) { $pythonCommand = Get-Command py.exe -ErrorAction SilentlyContinue }
            if ($null -eq $pythonCommand) { throw "7z.exe not found and Python is unavailable for the py7zr fallback." }
            & $pythonCommand.Source -m py7zr x $localArchive "-o$targetRoot" | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "py7zr extraction failed for $($entry.Name)" }
        }
        Remove-Item -LiteralPath $localArchive -Force
    }
} finally {
    if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }
}

if (!(Test-QtRootValid $targetRoot)) {
    throw "Qt extraction finished but $targetRoot does not contain lib\cmake\Qt6 and bin\windeployqt.exe."
}
$sizeMb = [math]::Round((Get-ChildItem -LiteralPath $targetRoot -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "Qt $Version ($ArchDir) installed at $targetRoot ($sizeMb MB, $([math]::Round($totalBytes / 1MB, 1)) MB downloaded)"
