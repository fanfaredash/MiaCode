<#
.SYNOPSIS
    Survey real chart background videos (bg.mp4 / pv.mp4 / &video= targets) and
    report which FFmpeg decoders + demuxers they actually need, cross-checked
    against trim-allowlist.psd1.

.DESCRIPTION
    This is the data-driven half of the FFmpeg trim toolchain (see README.md).
    Trimming by guesswork risks 误删 — cutting a decoder/demuxer some user's PV
    needs, which then silently fails to play. This script removes the guesswork:
    it probes a corpus of real chart videos and tells you exactly which codecs
    and containers appear, and which of those are MISSING from the allowlist.

    It NEVER edits the build; it only reports. You review the gaps and add them
    to trim-allowlist.psd1 (Decoders / Demuxers) before building.

.PARAMETER ChartRoots
    One or more directories to scan recursively for chart background videos.

.PARAMETER FfprobePath
    Path to ffprobe.exe. If omitted, searches PATH, the FFmpeg dev SDK
    (third_party/ffmpeg/windows/dev/bin), then falls back to parsing
    `ffmpeg -i` output via -FfmpegPath.

.PARAMETER FfmpegPath
    Path to ffmpeg.exe used as a probe fallback when ffprobe isn't available.
    Defaults to the repo's third_party/ffmpeg/windows/ffmpeg.exe.

.PARAMETER AllowlistPath
    Path to trim-allowlist.psd1 to cross-check observed codecs against.
    Defaults to the sibling file.

.EXAMPLE
    ./survey-chart-codecs.ps1 -ChartRoots '<chart-root-1>', '<chart-root-2>'
#>
param(
    # Folders to scan recursively. With NONE given, scans this script's own
    # folder — so a tester can drop this + ffmpeg.exe in their charts folder and
    # run it (the survey-chart-codecs.bat launcher passes the folder explicitly).
    [string[]]$ChartRoots = @(),
    [string]$FfprobePath = '',
    [string]$FfmpegPath = '',
    [string]$AllowlistPath = '',
    [string]$ReportPath = '',
    # By default only chart-background videos (bg.* / pv.*, the naming convention)
    # are surveyed — export outputs and unrelated videos in the tree are skipped.
    # Pass -IncludeAllVideos to survey every video file instead. Note: a chart
    # whose &video= points to an arbitrarily-named file OUTSIDE bg/pv is only
    # caught with -IncludeAllVideos (this survey matches by filename, not maidata).
    [switch]$IncludeAllVideos
)

$ErrorActionPreference = 'Stop'
$scriptDir = $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $scriptDir))

if ($null -eq $ChartRoots -or $ChartRoots.Count -eq 0) { $ChartRoots = @($scriptDir) }
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $scriptDir 'ffmpeg-codec-survey.txt'
}
if ([string]::IsNullOrWhiteSpace($AllowlistPath)) {
    $AllowlistPath = Join-Path $scriptDir 'trim-allowlist.psd1'
}
if ([string]::IsNullOrWhiteSpace($FfmpegPath)) {
    $FfmpegPath = Join-Path $repoRoot 'third_party\ffmpeg\windows\ffmpeg.exe'
}

# ---- resolve a prober (prefer ffprobe, fall back to ffmpeg -i) -------------
# Search order puts an ffmpeg/ffprobe sitting next to this script first, so a
# tester only has to drop ffmpeg.exe alongside it.
function Resolve-Prober {
    foreach ($p in @((Join-Path $scriptDir 'ffprobe.exe'), $FfprobePath)) {
        if (![string]::IsNullOrWhiteSpace($p) -and (Test-Path $p)) { return @{ Tool = 'ffprobe'; Path = $p } }
    }
    $onPath = Get-Command ffprobe -ErrorAction SilentlyContinue
    if ($onPath) { return @{ Tool = 'ffprobe'; Path = $onPath.Source } }
    $devProbe = Join-Path $repoRoot 'third_party\ffmpeg\windows\dev\bin\ffprobe.exe'
    if (Test-Path $devProbe) { return @{ Tool = 'ffprobe'; Path = $devProbe } }
    foreach ($f in @((Join-Path $scriptDir 'ffmpeg.exe'), $FfmpegPath)) {
        if (![string]::IsNullOrWhiteSpace($f) -and (Test-Path $f)) { return @{ Tool = 'ffmpeg'; Path = $f } }
    }
    $onPathFf = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($onPathFf) { return @{ Tool = 'ffmpeg'; Path = $onPathFf.Source } }
    throw "ffmpeg.exe / ffprobe.exe not found. Put ffmpeg.exe next to this script (or pass -FfmpegPath)."
}

# Map an ffprobe format_name (a comma list like 'mov,mp4,m4a,...') to the
# FFmpeg demuxer component name used by --enable-demuxer.
function Resolve-Demuxer([string]$formatName) {
    $tokens = $formatName -split ','
    foreach ($t in $tokens) {
        switch -Regex ($t.Trim()) {
            '^(mov|mp4|m4a|3gp|3g2|mj2)$' { return 'mov' }
            '^(matroska|webm)$'           { return 'matroska' }
            '^avi$'                       { return 'avi' }
            '^flv$'                       { return 'flv' }
            '^mpegts$'                    { return 'mpegts' }
            '^(mpeg|mpegvideo|vob|ps)$'   { return 'mpegps' }
            '^asf$'                       { return 'asf' }
            '^(mp3|mp2|mpa)$'             { return 'mp3' }
            '^wav$'                       { return 'wav' }
            '^ogg$'                       { return 'ogg' }
            '^flac$'                      { return 'flac' }
            '^(image2|png_pipe|gif)$'     { return 'image2' }
        }
    }
    return ($tokens[0].Trim())  # unknown — report verbatim so the gap is visible
}

function Probe-File($prober, [string]$path) {
    if ($prober.Tool -eq 'ffprobe') {
        $json = & $prober.Path -v error -show_entries 'stream=codec_name,codec_type' `
            -show_entries 'format=format_name' -of json -- $path 2>$null | Out-String
        if ([string]::IsNullOrWhiteSpace($json)) { return $null }
        try { $obj = $json | ConvertFrom-Json } catch { return $null }
        $vid = ($obj.streams | Where-Object { $_.codec_type -eq 'video' } | Select-Object -First 1).codec_name
        $aud = ($obj.streams | Where-Object { $_.codec_type -eq 'audio' } | Select-Object -First 1).codec_name
        return @{ Video = $vid; Audio = $aud; Demuxer = (Resolve-Demuxer $obj.format.format_name) }
    } else {
        # Fallback: parse `ffmpeg -i` stderr (best-effort).
        $err = & $prober.Path -hide_banner -i $path 2>&1 | Out-String
        $vid = $null; $aud = $null
        if ($err -match 'Video:\s*([a-z0-9_]+)') { $vid = $Matches[1] }
        if ($err -match 'Audio:\s*([a-z0-9_]+)') { $aud = $Matches[1] }
        $dem = 'unknown'
        if ($err -match "Input #0,\s*([a-z0-9,_]+),") { $dem = Resolve-Demuxer $Matches[1] }
        return @{ Video = $vid; Audio = $aud; Demuxer = $dem }
    }
}

# ---- gather corpus ---------------------------------------------------------
$exts = @('*.mp4','*.mov','*.m4v','*.mkv','*.webm','*.avi','*.flv','*.ts','*.mpg','*.mpeg','*.gif','*.wmv')
$files = New-Object System.Collections.Generic.List[string]
foreach ($root in $ChartRoots) {
    if (!(Test-Path $root)) { Write-Warning "Chart root not found: $root"; continue }
    foreach ($ext in $exts) {
        Get-ChildItem -Path $root -Recurse -File -Filter $ext -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notmatch '_bak\.' -and ($IncludeAllVideos -or $_.BaseName -match '^(bg|pv)$') } |
            ForEach-Object { $files.Add($_.FullName) }
    }
}
if ($files.Count -eq 0) { throw "No video files found under: $($ChartRoots -join ', ')" }

$prober = Resolve-Prober
Write-Host ("Probing {0} video files with {1} ({2}) ..." -f $files.Count, $prober.Tool, $prober.Path)

$videoCodecs = @{}; $audioCodecs = @{}; $demuxers = @{}; $failures = 0
$perFile = New-Object System.Collections.Generic.List[string]
foreach ($f in $files) {
    $r = Probe-File $prober $f
    if ($null -eq $r -or -not $r.Video) {
        $failures++
        $perFile.Add(("{0} | UNREADABLE (ffmpeg could not open)" -f $f))
        continue
    }
    $v = $r.Video
    $a = if ($r.Audio) { $r.Audio } else { 'none' }
    $d = if ($r.Demuxer) { $r.Demuxer } else { 'unknown' }
    $videoCodecs[$v] = 1 + ($videoCodecs[$v])
    if ($r.Audio) { $audioCodecs[$a] = 1 + ($audioCodecs[$a]) }
    $demuxers[$d] = 1 + ($demuxers[$d])
    $perFile.Add(("{0} | video={1} audio={2} container={3}" -f $f, $v, $a, $d))
}

# ---- build report ----------------------------------------------------------
$report = New-Object System.Collections.Generic.List[string]
$report.Add("MiaCode chart codec survey")
$report.Add("Date: " + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
$report.Add("Scan root: " + ($ChartRoots -join '; '))
$report.Add("Prober: $($prober.Tool)  $($prober.Path)")
$report.Add("Files probed: $($files.Count)  (unreadable: $failures)")
$report.Add("")
$report.Add("== Per file ==")
$perFile | ForEach-Object { $report.Add($_) }

function Add-Tally($title, $counts) {
    $report.Add($title + ":")
    if ($counts.Count -eq 0) { $report.Add("  (none)"); return }
    $counts.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { $report.Add(("  {0} x{1}" -f $_.Key, $_.Value)) }
}
$report.Add("")
$report.Add("== Summary ==")
Add-Tally "Video codecs" $videoCodecs
Add-Tally "Audio codecs" $audioCodecs
Add-Tally "Containers"   $demuxers

# ---- optional allowlist gap check (dev convenience) ------------------------
if (Test-Path $AllowlistPath) {
    $allow = Import-PowerShellDataFile -Path $AllowlistPath
    $allowDecoders = @($allow.Decoders); $allowDemuxers = @($allow.Demuxers)
    $missingDec = @(); foreach ($c in ($videoCodecs.Keys + $audioCodecs.Keys)) { if ($c -and ($allowDecoders -notcontains $c)) { $missingDec += $c } }
    $missingDem = @(); foreach ($d in $demuxers.Keys)                          { if ($d -and ($allowDemuxers -notcontains $d)) { $missingDem += $d } }
    $missingDec = $missingDec | Sort-Object -Unique
    $missingDem = $missingDem | Sort-Object -Unique
    $report.Add("")
    $report.Add("== Allowlist gap report ==")
    if ($missingDec.Count -eq 0 -and $missingDem.Count -eq 0) {
        $report.Add("OK - every observed codec + container is already covered by trim-allowlist.psd1.")
    } else {
        if ($missingDec.Count -gt 0) { $report.Add("MISSING decoders (add to trim-allowlist.psd1 Decoders): " + ($missingDec -join ', ')) }
        if ($missingDem.Count -gt 0) { $report.Add("MISSING demuxers (add to trim-allowlist.psd1 Demuxers): " + ($missingDem -join ', ')) }
    }
}

# ---- write report + echo to console ----------------------------------------
Set-Content -Path $ReportPath -Value $report -Encoding UTF8
Write-Host ""
$report | ForEach-Object { Write-Host $_ }
Write-Host ""
if ($failures -gt 0) {
    Write-Host "Note: $failures file(s) were unreadable (ffmpeg could not open them). The codec list above (from the rest) is still what we need."
}
Write-Host "Report written: $ReportPath"
Write-Host "Please send that file back. Thanks!"
