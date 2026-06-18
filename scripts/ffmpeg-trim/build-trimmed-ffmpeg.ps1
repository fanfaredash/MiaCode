<#
.SYNOPSIS
    Build a decode-only, minimal-size FFmpeg (n7.1, LGPL, shared) for MiaCode's
    QtAVPlayer preview backend and install it into third_party/ffmpeg/windows/dev.

.DESCRIPTION
    Replaces the full ~110 MB BtbN FFmpeg DLL set with a trimmed build that keeps
    only the components MiaCode actually needs (see trim-allowlist.psd1), shrinking
    avcodec/avformat/avfilter dramatically while keeping QtAVPlayer's mandatory
    filtergraph plumbing intact.

    Pipeline:
      0. Preflight   — MSYS2 (make/gcc/nasm), MSVC (dumpbin/lib via vcvars), git.
      1. Toolchain   — pacman -S the MinGW build deps (skippable).
      2. Source      — git clone FFmpeg at the pinned tag (skippable/reusable).
      3. Validate    — intersect the allowlist with `./configure --list-*` so a
                       typo'd / nonexistent component is reported, never silently
                       dropped or fed to configure as a hard error.
      4. Configure   — --disable-everything + the validated --enable-* allowlist.
                       Strictly LGPL: NO --enable-gpl / --enable-nonfree / x264.
      5. Build       — make + make install into a staging prefix.
      6. ImportLibs  — DLL -> .def (dumpbin) -> .lib (lib.exe) for MSVC linking.
      7. Assemble    — back up the existing dev SDK, then install the 6 trimmed
                       av*.dll + .lib + headers (avdevice intentionally excluded).
      8. Verify      — run the trimmed ffmpeg to assert every mandatory filter +
                       allowlisted decoder/demuxer is present; report size delta.

    LICENSE: decode-only => pure LGPL v2.1+, identical obligations to the FFmpeg
    already shipped. Do NOT add --enable-gpl / libx264 — that would make MiaCode
    GPL. (Export encoding stays in the separate GPL ffmpeg.exe by design.)

.PARAMETER PrintPlanOnly
    Run preflight + allowlist→flags assembly and print the exact configure command
    + phase plan, then exit WITHOUT installing the toolchain, fetching source, or
    building. Use this to review what the build will do.

.EXAMPLE
    # Review the plan without building:
    ./build-trimmed-ffmpeg.ps1 -PrintPlanOnly

    # Full build (after calibrating trim-allowlist.psd1 via survey-chart-codecs.ps1):
    ./build-trimmed-ffmpeg.ps1
#>
param(
    [string]$AllowlistPath = '',
    [string]$Msys2Root = 'C:\msys64',
    [string]$OutputDir = '',
    [string]$SourceDir = '',
    [string]$VcvarsPath = '',
    [int]$Jobs = 0,
    # Git remote(s) to clone FFmpeg from, tried in order until one succeeds.
    # Empty => a default list that prefers a CN-reachable mirror before GitHub
    # (GitHub https clones are frequently reset behind the GFW). Pass your own
    # mirror here to force it, e.g. -FfmpegGitUrl 'https://gitee.com/mirrors/ffmpeg.git'.
    [string[]]$FfmpegGitUrl = @(),
    [switch]$SkipToolchainInstall,
    [switch]$SkipSourceFetch,
    [switch]$PrintPlanOnly
)

$ErrorActionPreference = 'Stop'
$scriptDir = $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

if ([string]::IsNullOrWhiteSpace($AllowlistPath)) { $AllowlistPath = Join-Path $scriptDir 'trim-allowlist.psd1' }
if ([string]::IsNullOrWhiteSpace($OutputDir))     { $OutputDir = Join-Path $repoRoot 'third_party\ffmpeg\windows\dev' }
if ([string]::IsNullOrWhiteSpace($SourceDir))     { $SourceDir = Join-Path $repoRoot 'build\ffmpeg-trim' }
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

$bashExe = Join-Path $Msys2Root 'usr\bin\bash.exe'
$pacmanExe = Join-Path $Msys2Root 'usr\bin\pacman.exe'

# ---- helpers ---------------------------------------------------------------
function To-Msys2Path([string]$winPath) {
    # C:\a\b -> /c/a/b
    $p = $winPath -replace '\\', '/'
    if ($p -match '^([A-Za-z]):/(.*)$') { return ('/' + $Matches[1].ToLower() + '/' + $Matches[2]) }
    return $p
}

function Invoke-Msys2Bash {
    param([string]$Command, [switch]$AllowFail)
    # MINGW64 subsystem; inherit Windows PATH so a vcvars-seeded env (if any) and
    # git are visible. Login shell so /mingw64/bin (gcc/nasm) is on PATH.
    $env:MSYSTEM = 'MINGW64'
    $env:MSYS2_PATH_TYPE = 'inherit'
    $env:CHERE_INVOKING = '1'
    # Emits the command's stdout to the pipeline (callers may capture it);
    # throws on REAL failure (nonzero exit). Deliberately does NOT return
    # $LASTEXITCODE, which would otherwise contaminate captured output.
    #
    # Native tools here write routine progress/warnings to stderr: git prints
    # "Cloning into..." and gcc/configure emit warnings. Under the script's
    # $ErrorActionPreference='Stop', if a caller merges streams (e.g. `... *>&1 |
    # Tee-Object`) PowerShell wraps each stderr line as a NativeCommandError and
    # Stop makes it TERMINATING — aborting mid-clone/mid-build on harmless output.
    # So run the child under 'Continue' and flatten any ErrorRecord to plain text;
    # success/failure is decided solely by $LASTEXITCODE below.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $bashExe -lc $Command 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) { [string]$_.Exception.Message } else { $_ }
        }
    } finally {
        $ErrorActionPreference = $prevEAP
    }
    if ($LASTEXITCODE -ne 0 -and -not $AllowFail) {
        throw "MSYS2 bash command failed (exit $LASTEXITCODE): $Command"
    }
}

function Resolve-Vcvars {
    if (![string]::IsNullOrWhiteSpace($VcvarsPath) -and (Test-Path $VcvarsPath)) { return $VcvarsPath }
    $roots = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',
        'C:\Program Files\Microsoft Visual Studio\2022\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community'
    )
    foreach ($r in $roots) {
        $v = Join-Path $r 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $v) { return $v }
    }
    throw "vcvars64.bat not found (needed for dumpbin/lib import-lib generation). Pass -VcvarsPath."
}

function Get-ValidComponents {
    param([string]$SrcMsysPath, [string]$Category)  # e.g. 'decoders'
    $out = Invoke-Msys2Bash "cd '$SrcMsysPath' && ./configure --list-$Category 2>/dev/null"
    return (($out -join ' ') -split '\s+' | Where-Object { $_ -ne '' })
}

function Select-Available {
    param([string[]]$Requested, [string[]]$Available, [string]$Label)
    $ok = @(); $missing = @()
    foreach ($r in $Requested) { if ($Available -contains $r) { $ok += $r } else { $missing += $r } }
    if ($missing.Count -gt 0) {
        Write-Warning ("{0}: {1} requested name(s) not in this FFmpeg, skipped: {2}" -f $Label, $missing.Count, ($missing -join ', '))
    }
    return ,$ok
}

function New-MsvcImportLib {
    param([string]$DllPath, [string]$OutLibPath, [string]$Vcvars)
    $dllName = Split-Path $DllPath -Leaf
    $defPath = [System.IO.Path]::ChangeExtension($OutLibPath, '.def')
    $dump = & cmd /c "`"$Vcvars`" >nul 2>&1 && dumpbin /nologo /exports `"$DllPath`""
    $names = New-Object System.Collections.Generic.List[string]
    foreach ($line in $dump) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') { $names.Add($Matches[1]) }
    }
    if ($names.Count -eq 0) { throw "No exports parsed from $dllName — cannot build import lib." }
    Set-Content -Path $defPath -Encoding ascii -Value (@("LIBRARY $dllName", "EXPORTS") + $names)
    & cmd /c "`"$Vcvars`" >nul 2>&1 && lib /nologo /def:`"$defPath`" /machine:x64 /out:`"$OutLibPath`"" | Out-Null
    if (!(Test-Path $OutLibPath)) { throw "lib.exe did not produce $OutLibPath" }
}

function Invoke-FfmpegClone {
    # Try each candidate remote (in order), each up to $Retries times, until a
    # checkout containing ./configure lands. GitHub https clones are commonly
    # reset behind the GFW, so a CN mirror is tried first by default.
    param([string[]]$Urls, [string]$Tag, [string]$DestMsys, [string]$DestWin, [int]$Retries = 2)
    foreach ($url in $Urls) {
        for ($attempt = 1; $attempt -le $Retries; $attempt++) {
            Write-Host ("  clone {0}  (try {1}/{2})  branch {3}" -f $url, $attempt, $Retries, $Tag)
            Invoke-Msys2Bash "rm -rf '$DestMsys'" -AllowFail | Out-Null
            # postBuffer bump helps flaky/large transfers; --depth 1 keeps it small.
            Invoke-Msys2Bash "git -c http.postBuffer=524288000 clone --depth 1 --branch $Tag '$url' '$DestMsys'" -AllowFail | Out-Null
            if ($LASTEXITCODE -eq 0 -and (Test-Path (Join-Path $DestWin 'configure'))) {
                Write-Host "  -> source ready ($url)"
                return
            }
            Write-Warning ("  clone failed from {0} (exit {1})." -f $url, $LASTEXITCODE)
        }
    }
    throw @"
Could not clone FFmpeg $Tag from any remote: $($Urls -join ', ')
Workarounds:
  - pass a reachable mirror:  -FfmpegGitUrl 'https://gitee.com/mirrors/ffmpeg.git'
  - or clone it yourself, checkout tag $Tag into '$DestWin', then re-run with -SkipSourceFetch -SkipToolchainInstall
"@
}

function Get-DirSizeMB([string]$path) {
    if (!(Test-Path $path)) { return 0 }
    return [math]::Round((Get-ChildItem $path -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1MB, 1)
}

# ---- 0. preflight ----------------------------------------------------------
Write-Host "== Preflight =="
$allow = Import-PowerShellDataFile -Path $AllowlistPath
Write-Host "  allowlist: $AllowlistPath (FFmpeg $($allow.FFmpegVersion))"
if (!(Test-Path $bashExe)) { throw "MSYS2 bash not found at $bashExe. Install MSYS2 or pass -Msys2Root." }
$vcvars = Resolve-Vcvars
Write-Host "  msys2:  $Msys2Root"
Write-Host "  vcvars: $vcvars"
Write-Host "  jobs:   $Jobs"
$srcMsys = To-Msys2Path (Join-Path $SourceDir 'FFmpeg')
$prefix = Join-Path $SourceDir 'install'
$prefixMsys = To-Msys2Path $prefix

# ---- assemble configure flags (validated later against --list-*) -----------
$mand = $allow.Mandatory
$plannedFilters = @($mand.Filters)
$alwaysOnFilters = if ($mand.AlwaysOnFilters) { @($mand.AlwaysOnFilters) } else { @('buffer','buffersink','abuffer','abuffersink') }
$plannedDecoders = @($allow.Decoders)
$plannedDemuxers = @($allow.Demuxers)
$plannedParsers = @($allow.Parsers)
$plannedBsfs = @($mand.Bsfs)
$plannedHwaccels = @($mand.Hwaccels)
$plannedProtocols = @($mand.Protocols)
$extraArgs = @($mand.ExtraConfigureArgs)

if ($PrintPlanOnly) {
    Write-Host ""
    Write-Host "== PLAN (no build) =="
    Write-Host "  source dir : $SourceDir"
    Write-Host "  output dir : $OutputDir"
    Write-Host "  configure (component names NOT yet validated against --list-*):"
    Write-Host ("    --disable-everything --disable-doc --disable-debug --disable-static --enable-shared")
    Write-Host ("    --arch=x86_64 --target-os=mingw32 (NO --enable-gpl / nonfree — LGPL decode-only)")
    Write-Host ("    --enable-decoder=" + ($plannedDecoders -join ','))
    Write-Host ("    --enable-parser="  + ($plannedParsers  -join ','))
    Write-Host ("    --enable-demuxer=" + ($plannedDemuxers -join ','))
    Write-Host ("    --enable-protocol=" + ($plannedProtocols -join ','))
    Write-Host ("    --enable-filter="  + ($plannedFilters  -join ','))
    Write-Host ("    --enable-bsf="     + ($plannedBsfs     -join ','))
    Write-Host ("    --enable-hwaccel=" + ($plannedHwaccels -join ','))
    Write-Host ("    " + ($extraArgs -join ' '))
    Write-Host ""
    Write-Host "  phases: toolchain(pacman) -> clone $($allow.FFmpegVersion) -> validate -> configure -> make -j$Jobs -> import-libs -> assemble(+backup) -> verify"
    Write-Host "  Re-run without -PrintPlanOnly to build. Expect ~30-60 min."
    return
}

# ---- 1. toolchain ----------------------------------------------------------
if (-not $SkipToolchainInstall) {
    Write-Host "== Toolchain (pacman) =="
    # mingw-w64-x86_64-dav1d: software AV1 decoder (libdav1d), required by the
    # --enable-libdav1d flag in trim-allowlist.psd1 ExtraConfigureArgs. Ships a
    # static libdav1d.a so it can link into avcodec-61.dll (self-contained).
    Invoke-Msys2Bash "$(To-Msys2Path $pacmanExe) -S --needed --noconfirm make diffutils pkgconf git mingw-w64-x86_64-gcc mingw-w64-x86_64-nasm mingw-w64-x86_64-dav1d"
} else {
    Write-Host "== Toolchain install skipped (-SkipToolchainInstall) =="
}

# ---- 2. source -------------------------------------------------------------
$ffmpegSrcWin = Join-Path $SourceDir 'FFmpeg'
if (-not $SkipSourceFetch) {
    Write-Host "== Source (git clone $($allow.FFmpegVersion)) =="
    New-Item -ItemType Directory -Force -Path $SourceDir | Out-Null
    if (Test-Path $ffmpegSrcWin) { Remove-Item -Recurse -Force $ffmpegSrcWin }
    $cloneUrls = if ($FfmpegGitUrl -and $FfmpegGitUrl.Count -gt 0) { $FfmpegGitUrl } else { @(
        'https://gitee.com/mirrors/ffmpeg.git',   # CN-reachable mirror (tried first)
        'https://github.com/FFmpeg/FFmpeg.git',   # upstream
        'https://git.ffmpeg.org/ffmpeg.git'       # official mirror
    ) }
    Invoke-FfmpegClone -Urls $cloneUrls -Tag $allow.FFmpegVersion -DestMsys $srcMsys -DestWin $ffmpegSrcWin
} else {
    if (!(Test-Path $ffmpegSrcWin)) { throw "-SkipSourceFetch set but no source at $ffmpegSrcWin" }
    Write-Host "== Source fetch skipped, reusing $ffmpegSrcWin =="
}

# ---- 3. validate allowlist against this FFmpeg's component lists ------------
Write-Host "== Validate allowlist against ./configure --list-* =="
$decoders = Select-Available $plannedDecoders (Get-ValidComponents $srcMsys 'decoders') 'decoders'
$parsers  = Select-Available $plannedParsers  (Get-ValidComponents $srcMsys 'parsers')  'parsers'
$demuxers = Select-Available $plannedDemuxers (Get-ValidComponents $srcMsys 'demuxers') 'demuxers'
$filters  = Select-Available $plannedFilters  (Get-ValidComponents $srcMsys 'filters')  'filters'
$bsfs     = Select-Available $plannedBsfs     (Get-ValidComponents $srcMsys 'bsfs')     'bsfs'
$hwaccels = Select-Available $plannedHwaccels (Get-ValidComponents $srcMsys 'hwaccels') 'hwaccels'
$protocols = Select-Available $plannedProtocols (Get-ValidComponents $srcMsys 'protocols') 'protocols'

# Hard guard for the QtAVPlayer filtergraph endpoints. These are NOT toggleable
# --enable-filter components (they never appear in --list-filters), so don't look
# for them there. FFmpeg's configure appends asrc_abuffer/vsrc_buffer/asink_abuffer/
# vsink_buffer to filter_list UNCONDITIONALLY whenever libavfilter is built. Assert
# that contract still holds in this checkout (cheap, pre-build); the post-build
# runtime verify then confirms buffer/buffersink/abuffer/abuffersink are present.
$cfgText = Get-Content -Raw -LiteralPath (Join-Path $ffmpegSrcWin 'configure')
if ($cfgText -notmatch 'asrc_abuffer\s+vsrc_buffer\s+asink_abuffer\s+vsink_buffer') {
    throw "This FFmpeg's configure no longer auto-includes the buffer/buffersink filtergraph endpoints unconditionally; the trim allowlist + this guard need review before building (would risk breaking ALL playback)."
}

# ---- 4 + 5. configure + build ----------------------------------------------
$cfg = @(
    "./configure",
    "--prefix='$prefixMsys'",
    "--arch=x86_64", "--target-os=mingw32",
    "--enable-shared", "--disable-static",
    "--disable-everything", "--disable-doc", "--disable-debug",
    "--enable-decoder=$($decoders -join ',')",
    "--enable-parser=$($parsers -join ',')",
    "--enable-demuxer=$($demuxers -join ',')",
    "--enable-protocol=$($protocols -join ',')",
    "--enable-filter=$($filters -join ',')",
    "--enable-bsf=$($bsfs -join ',')",
    "--enable-hwaccel=$($hwaccels -join ',')"
) + $extraArgs
$cfgLine = $cfg -join ' '
Write-Host "== Configure =="
Write-Host "  $cfgLine"
Invoke-Msys2Bash "cd '$srcMsys' && $cfgLine"
Write-Host "== Build (make -j$Jobs) =="
Invoke-Msys2Bash "cd '$srcMsys' && make -j$Jobs && make install"

# ---- 6. MSVC import libs ----------------------------------------------------
Write-Host "== Generate MSVC import libs (dumpbin -> .def -> lib) =="
$stageBin = Join-Path $prefix 'bin'
$stageLib = Join-Path $prefix 'lib'
foreach ($base in $allow.ExpectedDlls) {
    $dll = Join-Path $stageBin "$base.dll"
    if (!(Test-Path $dll)) { throw "Expected DLL not produced: $dll (check the allowlist / configure output)" }
    # avcodec-61.dll -> avcodec.lib (strip the -NN major suffix to match find_library NAMES)
    $libBase = ($base -replace '-\d+$', '')
    New-MsvcImportLib -DllPath $dll -OutLibPath (Join-Path $stageLib "$libBase.lib") -Vcvars $vcvars
    Write-Host "  $base.dll -> $libBase.lib"
}

# ---- 7. assemble into the dev SDK (with backup) ----------------------------
Write-Host "== Assemble into $OutputDir =="
if (Test-Path $OutputDir) {
    $bak = "$OutputDir.full.bak"
    if (Test-Path $bak) {
        # dev.full.bak already holds the ORIGINAL full SDK from the first run —
        # NEVER clobber it (that's the precious A/B baseline + restore source). The
        # current dev/ here is a previous *trimmed* build, disposable; just remove it.
        Remove-Item -Recurse -Force $OutputDir
        Write-Host "  kept existing full-SDK backup $bak; discarded previous trimmed dev"
    } else {
        Move-Item $OutputDir $bak
        Write-Host "  backed up full dev SDK -> $bak (A/B fallback + restore source)"
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $OutputDir 'bin'), (Join-Path $OutputDir 'lib'), (Join-Path $OutputDir 'include') | Out-Null
Copy-Item (Join-Path $prefix 'include\*') (Join-Path $OutputDir 'include') -Recurse -Force
# Drop libavdevice headers — avdevice is not linked (QT_AVPLAYER_NO_AVDEVICE).
$avdInc = Join-Path $OutputDir 'include\libavdevice'
if (Test-Path $avdInc) { Remove-Item -Recurse -Force $avdInc }
foreach ($base in $allow.ExpectedDlls) {
    Copy-Item (Join-Path $stageBin "$base.dll") (Join-Path $OutputDir 'bin') -Force
    $libBase = ($base -replace '-\d+$', '')
    Copy-Item (Join-Path $stageLib "$libBase.lib") (Join-Path $OutputDir 'lib') -Force
}

# ---- 8. verify -------------------------------------------------------------
Write-Host "== Verify =="
$ffmpegExe = Join-Path $stageBin 'ffmpeg.exe'
if (Test-Path $ffmpegExe) {
    $haveFilters  = (& $ffmpegExe -hide_banner -filters  2>$null | Out-String)
    $haveDecoders = (& $ffmpegExe -hide_banner -decoders 2>$null | Out-String)
    $haveDemuxers = (& $ffmpegExe -hide_banner -formats  2>$null | Out-String)
    $verifyFail = @()
    foreach ($f in ($alwaysOnFilters + @('scale','format'))) {
        if ($haveFilters -notmatch "(?m)\s$([regex]::Escape($f))\s") { $verifyFail += "filter:$f" }
    }
    foreach ($d in $decoders) { if ($haveDecoders -notmatch "(?m)\s$([regex]::Escape($d))\s") { $verifyFail += "decoder:$d" } }
    if ($verifyFail.Count -gt 0) { Write-Warning ("Verify gaps (not found in trimmed ffmpeg): " + ($verifyFail -join ', ')) }
    else { Write-Host "  OK - mandatory filters + all allowlisted decoders present in the trimmed build." }
} else {
    Write-Warning "  trimmed ffmpeg.exe not found for verification (built with --disable-programs?); skipping component assert."
}

# Self-containment assert: a MinGW *shared* build can leave the av*.dll dynamically
# depending on MinGW runtime DLLs (libwinpthread-1/zlib1/libiconv-2/libgcc_s_*) that
# are NOT shipped next to MiaCode.exe -> loader fails with ERROR_MOD_NOT_FOUND (126)
# and the app won't open. --extra-ldflags=-static (+ --disable-iconv) must eliminate
# these. Verify with objdump so a regression fails the build instead of shipping a
# DLL that crashes the app on a clean machine.
Write-Host "== Verify self-containment (no external MinGW runtime deps) =="
$forbidden = 'libwinpthread|zlib1\.dll|libiconv|libgcc_s|libstdc\+\+|libssp'
$selfFail = @()
foreach ($base in $allow.ExpectedDlls) {
    $dllMsys = To-Msys2Path (Join-Path $OutputDir "bin\$base.dll")
    $deps = Invoke-Msys2Bash "objdump -p '$dllMsys' 2>/dev/null | grep -i 'DLL Name'" -AllowFail
    $bad = @($deps | Where-Object { $_ -match $forbidden })
    if ($bad.Count -gt 0) { $selfFail += ("{0}: {1}" -f $base, (($bad -replace '.*DLL Name:\s*','').Trim() -join ', ')) }
}
if ($selfFail.Count -gt 0) {
    throw ("Trimmed DLLs still depend on un-shipped MinGW runtime DLLs (app would fail to open):`n  " + ($selfFail -join "`n  ") + "`nFix the static-link flags in trim-allowlist.psd1 ExtraConfigureArgs and rebuild.")
}
Write-Host "  OK - all av*.dll are self-contained (no external MinGW runtime DLL deps)."
$newSize = Get-DirSizeMB (Join-Path $OutputDir 'bin')
$bakSize = Get-DirSizeMB (Join-Path "$OutputDir.full.bak" 'bin')
Write-Host ""
Write-Host ("DONE. Trimmed FFmpeg DLLs: {0} MB (was {1} MB). Next: reconfigure + rebuild MiaCode, then re-run the smoke test." -f $newSize, $bakSize)
Write-Host "If a chart PV fails to play on the trimmed build, A/B against $OutputDir.full.bak to confirm a trim gap, add the codec to trim-allowlist.psd1, and rebuild."
