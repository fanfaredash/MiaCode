#requires -Version 5.1
# MiaCode "first Play does nothing until restart" probe for Windows testers.
#
# Stage 1 (automatic): loads the shipped bass.dll and checks, one fresh process per case,
#   whether BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE) still succeeds after another
#   BASS user has initialized device 0. MiaCode performs that call once per process
#   (std::call_once in disableBassDefaultDeviceEntry). Before 4d7c274e the preview engine
#   made it lazily, so a failure there kept preview audio, and with it Play, dead until
#   restart. Fixed builds make it at the top of main(); on those a HIT is a regression.
# Stage 2 (guided): copies a chart to a fresh folder (no .miacode, so the waveform cache
#   misses), launches MiaCode with --debug, has the tester press Play right away, then
#   reads the logs for the engine init result. Repeats as many rounds as the tester wants.
# Everything the tester should send back is zipped to the Desktop.
#
# Double-click Run_FirstPlayProbe.bat, or:
#   powershell -ExecutionPolicy Bypass -File FirstPlayProbe.ps1 [-AppDir <MiaCode folder>] [-ChartDir <chart folder>] [-ProbeOnly]

[CmdletBinding()]
param(
    [string]$AppDir = "",
    [string]$ChartDir = "",
    [switch]$ProbeOnly,
    # Internal: child-process mode for one stage-1 case.
    [string]$ProbeCase = "",
    [string]$BassDll = ""
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$ProbeCases = @("fresh", "init0", "init0free", "init0thread", "enum", "plugin", "setfirst")

$BassInteropSource = @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

[StructLayout(LayoutKind.Sequential)]
public struct MiaBassDeviceInfo {
    public IntPtr name;
    public IntPtr driver;
    public uint flags;
}

public static class MiaBassProbe {
    public const uint BASS_CONFIG_DEV_DEFAULT = 36;
    public const uint BASS_DEVICE_NOSPEAKER = 0x1000;
    public const uint BASS_UNICODE = 0x80000000;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryW(string path);

    [DllImport("bass.dll")] public static extern uint BASS_GetVersion();
    [DllImport("bass.dll")] public static extern int BASS_ErrorGetCode();
    [DllImport("bass.dll")] public static extern int BASS_SetConfig(uint option, uint value);
    [DllImport("bass.dll")] public static extern uint BASS_GetConfig(uint option);
    [DllImport("bass.dll")] public static extern int BASS_Init(int device, uint freq, uint flags, IntPtr win, IntPtr dsguid);
    [DllImport("bass.dll")] public static extern int BASS_Free();
    [DllImport("bass.dll")] public static extern int BASS_SetDevice(uint device);
    [DllImport("bass.dll")] public static extern int BASS_GetDeviceInfo(uint device, out MiaBassDeviceInfo info);
    [DllImport("bass.dll", CharSet = CharSet.Unicode)] public static extern uint BASS_PluginLoad(string file, uint flags);

    public static string TryDisable(string label) {
        int ok = BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, 0);
        int err = ok != 0 ? 0 : BASS_ErrorGetCode();
        return String.Format("RESULT label=\"{0}\" set_ok={1} err={2}", label, ok != 0 ? 1 : 0, err);
    }

    public static string InitNoSound() {
        int ok = BASS_Init(0, 44100, BASS_DEVICE_NOSPEAKER, IntPtr.Zero, IntPtr.Zero);
        return String.Format("INFO BASS_Init(0,NOSPEAKER)={0} err={1}", ok != 0 ? 1 : 0, ok != 0 ? 0 : BASS_ErrorGetCode());
    }

    public static string InitNoSoundOnThread() {
        string result = null;
        Thread worker = new Thread(delegate() { result = InitNoSound() + " thread=worker"; });
        worker.Start();
        worker.Join();
        return result;
    }

    public static int EnumerateDevices() {
        MiaBassDeviceInfo info;
        int count = 0;
        for (uint i = 0; BASS_GetDeviceInfo(i, out info) != 0; ++i) {
            ++count;
        }
        return count;
    }
}
'@

function Invoke-ProbeCase {
    param([string]$Case, [string]$DllPath)

    Add-Type -TypeDefinition $BassInteropSource -Language CSharp
    $handle = [MiaBassProbe]::LoadLibraryW($DllPath)
    if ($handle -eq [IntPtr]::Zero) {
        $win32 = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Output ("LOADFAIL case={0} win32={1} dll={2}" -f $Case, $win32, $DllPath)
        exit 3
    }
    Write-Output ("INFO case={0} bass_version=0x{1:x8} process_arch={2}" -f $Case, [MiaBassProbe]::BASS_GetVersion(), $env:PROCESSOR_ARCHITECTURE)
    switch ($Case) {
        "fresh" {
            Write-Output ([MiaBassProbe]::TryDisable("fresh process"))
        }
        "init0" {
            Write-Output ([MiaBassProbe]::InitNoSound())
            Write-Output ([MiaBassProbe]::TryDisable("after BASS_Init(0), device held"))
        }
        "init0free" {
            Write-Output ([MiaBassProbe]::InitNoSound())
            [void][MiaBassProbe]::BASS_SetDevice(0)
            [void][MiaBassProbe]::BASS_Free()
            Write-Output ([MiaBassProbe]::TryDisable("after BASS_Init(0) + BASS_Free"))
        }
        "init0thread" {
            Write-Output ([MiaBassProbe]::InitNoSoundOnThread())
            Write-Output ([MiaBassProbe]::TryDisable("after BASS_Init(0) on another thread"))
        }
        "enum" {
            Write-Output ("INFO enumerated={0}" -f [MiaBassProbe]::EnumerateDevices())
            Write-Output ([MiaBassProbe]::TryDisable("after BASS_GetDeviceInfo enumeration"))
        }
        "plugin" {
            $pluginPath = Join-Path (Split-Path -Parent $DllPath) "bassopus.dll"
            $plugin = [MiaBassProbe]::BASS_PluginLoad($pluginPath, [MiaBassProbe]::BASS_UNICODE)
            Write-Output ("INFO BASS_PluginLoad={0} err={1}" -f $plugin, $(if ($plugin -ne 0) { 0 } else { [MiaBassProbe]::BASS_ErrorGetCode() }))
            Write-Output ([MiaBassProbe]::TryDisable("after BASS_PluginLoad"))
        }
        "setfirst" {
            Write-Output ([MiaBassProbe]::TryDisable("fresh process (first)"))
            Write-Output ([MiaBassProbe]::InitNoSound())
            Write-Output ("RESULT label=""config after BASS_Init(0)"" dev_default={0}" -f [MiaBassProbe]::BASS_GetConfig([MiaBassProbe]::BASS_CONFIG_DEV_DEFAULT))
        }
        default {
            Write-Output ("UNKNOWN case={0}" -f $Case)
            exit 2
        }
    }
    exit 0
}

if ($ProbeCase -ne "") {
    Invoke-ProbeCase -Case $ProbeCase -DllPath $BassDll
}

# ---------------------------------------------------------------- parent mode helpers

function Write-Step([string]$Text) { Write-Host ""; Write-Host "==== $Text" -ForegroundColor Cyan }
function Write-Good([string]$Text) { Write-Host $Text -ForegroundColor Green }
function Write-Bad([string]$Text) { Write-Host $Text -ForegroundColor Red }
function Write-Warn2([string]$Text) { Write-Host $Text -ForegroundColor Yellow }

$SummaryLines = New-Object System.Collections.Generic.List[string]
function Add-Summary([string]$Text) { $SummaryLines.Add($Text) | Out-Null }

function Read-YesNo([string]$Prompt) {
    while ($true) {
        $answer = (Read-Host "$Prompt [y/n]").Trim().ToLowerInvariant()
        if ($answer -eq "y" -or $answer -eq "yes") { return $true }
        if ($answer -eq "n" -or $answer -eq "no") { return $false }
    }
}

function Select-Folder([string]$Description) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = $Description
        $dialog.ShowNewFolderButton = $false
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            return $dialog.SelectedPath
        }
        return ""
    } catch {
        return (Read-Host "$Description (paste the folder path, or leave empty to skip)").Trim('"', ' ')
    }
}

function Resolve-MiaCodeExe([string]$Folder) {
    if ($Folder -eq "") { return "" }
    foreach ($candidate in @((Join-Path $Folder "MiaCode.exe"), (Join-Path $Folder "app\MiaCode.exe"))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Get-Item -LiteralPath $candidate).FullName }
    }
    return ""
}

function Resolve-BassDll([string]$ExePath) {
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($ExePath -ne "") {
        $exeDir = Split-Path -Parent $ExePath
        $candidates.Add((Join-Path $exeDir "bass.dll"))
        $candidates.Add((Join-Path $exeDir "app\bass.dll"))
    }
    $archFolder = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") { "winarm64" } else { "win64" }
    $candidates.Add((Join-Path $PSScriptRoot "..\..\..\third_party\bass\bin\$archFolder\bass.dll"))
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Get-Item -LiteralPath $candidate).FullName }
    }
    return ""
}

function Test-MiaCodeRunning {
    return @(Get-Process -Name "MiaCode" -ErrorAction SilentlyContinue).Count -gt 0
}

function Wait-MiaCodeClosed {
    while (Test-MiaCodeRunning) {
        Write-Warn2 "MiaCode 仍在运行。请先完全退出 MiaCode（包括托盘/后台进程），然后按回车。"
        [void](Read-Host)
    }
}

function Find-FirstLine([string]$Path, [string]$Needle) {
    if ($Path -eq "" -or !(Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return Select-String -LiteralPath $Path -SimpleMatch -Pattern $Needle -Encoding UTF8 | Select-Object -First 1
}

function Count-Lines([string]$Folder, [string]$Needle) {
    $files = @(Get-ChildItem -LiteralPath $Folder -Filter "*.log" -File -Recurse -ErrorAction SilentlyContinue)
    if ($files.Count -eq 0) { return 0 }
    return @($files | Select-String -SimpleMatch -Pattern $Needle -Encoding UTF8).Count
}

function Get-RoundVerdict([string]$LogDir) {
    $logFiles = @(Get-ChildItem -LiteralPath $LogDir -Filter "*.log" -File -Recurse -ErrorAction SilentlyContinue)
    $audioLog = ""
    $audioLogFile = $logFiles | Where-Object { $_.Name -eq "miacode_audio_debug.log" } | Select-Object -First 1
    if ($audioLogFile -ne $null) { $audioLog = $audioLogFile.FullName }
    $identity = $null
    foreach ($file in $logFiles) {
        $identity = Select-String -LiteralPath $file.FullName -SimpleMatch -Pattern "process_identity" -Encoding UTF8 | Select-Object -First 1
        if ($identity -ne $null) { break }
    }
    $bindFail = Find-FirstLine $audioLog "bass_endpoint_bind_failed reason=disable_default"
    $engineReady = Find-FirstLine $audioLog "bass_engine_ready"
    $waveformBass = Find-FirstLine $audioLog "event=build decoder=bass"
    $waveformDisk = Find-FirstLine $audioLog "event=worker_done source=disk"
    $rejected = (Count-Lines $LogDir "audio_submission_rejected") + (Count-Lines $LogDir "audio_startup_completion_failed")
    $playRequests = Count-Lines $LogDir "play_request"

    $lines = New-Object System.Collections.Generic.List[string]
    if ($logFiles.Count -eq 0) {
        $lines.Add("VERDICT=NO_LOGS  没有生成任何日志。请确认本轮是由脚本启动的 MiaCode。")
        return $lines
    }
    if ($identity -ne $null) {
        $text = $identity.Line.Trim()
        if ($text.Length -gt 400) { $text = $text.Substring(0, 400) + "..." }
        $lines.Add("build: $text")
    }
    $lines.Add(("waveform_bass_decode={0} waveform_from_disk_cache={1} play_request={2} audio_start_rejected_or_failed={3}" -f `
        [int]($waveformBass -ne $null), [int]($waveformDisk -ne $null), $playRequests, $rejected))
    if ($bindFail -ne $null) {
        $err = ""
        if ($bindFail.Line -match "err=(-?\d+)") { $err = $Matches[1] }
        $lines.Add("VERDICT=HIT  预览音频引擎初始化失败：DEV_DEFAULT 无法关闭 (err=$err)。这与假设的根因一致。")
        $lines.Add("  log: " + $bindFail.Line.Trim())
    } elseif ($engineReady -ne $null) {
        $lines.Add("VERDICT=MISS  预览音频引擎本轮正常初始化 (bass_engine_ready)。")
        if ($waveformBass -ne $null) {
            $lines.Add("  注意：本轮波形也走了 BASS 解码，但引擎在它之前完成了初始化，竞态没有触发。")
        }
    } else {
        $lines.Add("VERDICT=UNKNOWN  日志里没有预览音频引擎的初始化记录（可能没有打开谱面、没有按播放，或这个版本不带这些日志）。")
    }
    return $lines
}

# ---------------------------------------------------------------- stage 0: setup

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$desktop = [Environment]::GetFolderPath("Desktop")
$outDir = Join-Path $desktop "miacode_first_play_probe_$stamp"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$transcriptStarted = $false
try {
    Start-Transcript -LiteralPath (Join-Path $outDir "transcript.txt") | Out-Null
    $transcriptStarted = $true
} catch { }

try {
    Write-Host "MiaCode 首次播放失败探针"
    Write-Host "结果目录: $outDir"
    Add-Summary "MiaCode first-play probe  $stamp"
    Add-Summary ("windows={0} process_arch={1} ps={2}" -f [Environment]::OSVersion.VersionString, $env:PROCESSOR_ARCHITECTURE, $PSVersionTable.PSVersion)

    if (![Environment]::Is64BitProcess) {
        throw "请使用 64 位 PowerShell 运行（双击 Run_FirstPlayProbe.bat 即可）。"
    }

    $exePath = ""
    if (!$ProbeOnly) {
        if ($AppDir -eq "") {
            Write-Host "请选择 MiaCode 的安装/解压目录（包含 MiaCode.exe 的文件夹）。取消则只运行第一阶段。选择框可能被挡在这个窗口后面。"
            $AppDir = Select-Folder "选择 MiaCode 所在文件夹（包含 MiaCode.exe）"
        }
        $exePath = Resolve-MiaCodeExe $AppDir
        if ($AppDir -ne "" -and $exePath -eq "") {
            Write-Warn2 "在 $AppDir 下没有找到 MiaCode.exe，第二阶段将跳过。"
        }
    }
    Add-Summary "miacode_exe=$exePath"

    # ------------------------------------------------------------ stage 1: BASS probe

    Write-Step "第一阶段：BASS 配置窗口探针（自动，每个场景一个独立进程）"
    $dll = if ($BassDll -ne "") { $BassDll } else { Resolve-BassDll $exePath }
    if ($dll -eq "") {
        throw "找不到 bass.dll：请选择正确的 MiaCode 目录，或在仓库里运行本脚本。"
    }
    Write-Host "bass.dll: $dll"
    Add-Summary "bass_dll=$dll"
    $probeLog = Join-Path $outDir "stage1_bass_probe.txt"
    $psExe = Join-Path $PSHOME "powershell.exe"
    $results = @{}
    foreach ($case in $ProbeCases) {
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $output = & $psExe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -ProbeCase $case -BassDll $dll 2>&1 | ForEach-Object { "$_" }
        $ErrorActionPreference = $previousPreference
        Add-Content -LiteralPath $probeLog -Value (@("--- case=$case") + $output) -Encoding UTF8
        $results[$case] = $output
        foreach ($line in $output) {
            if ($line -like "RESULT*" -or $line -like "LOADFAIL*") { Write-Host ("  [{0}] {1}" -f $case, $line) }
        }
    }

    function Get-SetOk([string]$Case) {
        foreach ($line in @($results[$Case])) {
            if ($line -match "^RESULT .*set_ok=(\d)") { return [int]$Matches[1] }
        }
        return -1
    }

    $fresh = Get-SetOk "fresh"
    $init0 = Get-SetOk "init0"
    $init0thread = Get-SetOk "init0thread"
    $init0free = Get-SetOk "init0free"
    if ($fresh -ne 1) {
        $stage1 = "STAGE1=INVALID  全新进程里也无法设置 DEV_DEFAULT（或 bass.dll 加载失败），探针结果不可用。"
        Write-Bad $stage1
    } elseif ($init0 -eq 0 -and $init0thread -eq 0) {
        $stage1 = "STAGE1=CONFIRMED  BASS_Init(0) 之后 DEV_DEFAULT 无法再设置（跨线程同样如此，free 之后 set_ok=$init0free）。库层面的竞态机制成立。"
        Write-Good $stage1
    } elseif ($init0 -eq 1 -and $init0thread -eq 1) {
        $stage1 = "STAGE1=REFUTED  BASS_Init(0) 之后 DEV_DEFAULT 仍可设置。假设的根因在 Windows 上不成立，请回传结果。"
        Write-Bad $stage1
    } else {
        $stage1 = "STAGE1=MIXED  init0=$init0 init0thread=$init0thread，结果不一致，请回传结果。"
        Write-Warn2 $stage1
    }
    Add-Summary $stage1

    # ------------------------------------------------------------ stage 2: guided repro

    if ($exePath -ne "") {
        Write-Step "第二阶段：实机复现（需要你操作）"
        if ($ChartDir -eq "") {
            Write-Host "请选择一个谱面文件夹（包含 maidata.txt 和音频）。最好是这台电脑上没打开过的谱面。"
            $ChartDir = Select-Folder "选择谱面文件夹（包含 maidata.txt）"
        }
        if ($ChartDir -eq "" -or !(Test-Path -LiteralPath (Join-Path $ChartDir "maidata.txt") -PathType Leaf)) {
            Write-Warn2 "没有选择包含 maidata.txt 的文件夹，跳过第二阶段。"
            Add-Summary "stage2=skipped (no chart)"
        } else {
            $chartLeaf = Split-Path -Leaf $ChartDir
            $chartCopy = Join-Path $outDir "_chart_copy\$chartLeaf"
            Write-Host "复制谱面到全新目录（不带 .miacode 缓存）: $chartCopy"
            $null = & robocopy $ChartDir $chartCopy /E /XD .miacode /NFL /NDL /NJH /NJS /NP
            if ($LASTEXITCODE -ge 8) { throw "复制谱面失败 (robocopy exit $LASTEXITCODE)" }
            $chartFile = Join-Path $chartCopy "maidata.txt"
            Add-Summary "chart_source=$ChartDir"

            $round = 0
            do {
                $round += 1
                Wait-MiaCodeClosed
                $roundDir = Join-Path $outDir ("round_{0}" -f $round)
                New-Item -ItemType Directory -Path $roundDir -Force | Out-Null
                $cacheDir = Join-Path $chartCopy ".miacode"
                if (Test-Path -LiteralPath $cacheDir) { Remove-Item -LiteralPath $cacheDir -Recurse -Force }

                Write-Step "第 $round 轮"
                try { Set-Clipboard -Value $chartFile } catch { }
                if ($round -eq 1) {
                    Write-Host "1. MiaCode 启动后，打开下面这个谱面（路径已复制到剪贴板，可粘贴进打开对话框，或直接把文件拖进窗口）。"
                    Write-Host "   如果 MiaCode 启动时自动恢复了你上次的谱面，这一轮大概率触发不了，照做即可，从第 2 轮开始才是有效测试。"
                } else {
                    Write-Host "1. MiaCode 应该会自动打开上一轮的测试谱面。如果没有自动打开，就手动打开："
                }
                Write-Host "     $chartFile" -ForegroundColor White
                Write-Host "2. 谱面一加载出来就立刻点播放，不要等。"
                Write-Host "3. 看一眼：按钮有没有反应、画面有没有走。能播放就让它播几秒。"
                Write-Host "4. 完全退出 MiaCode，然后回到这个窗口按回车。"
                $env:MIACODE_LOG_DIR = $roundDir
                Start-Process -FilePath $exePath -ArgumentList "--debug" -WorkingDirectory (Split-Path -Parent $exePath) | Out-Null
                [void](Read-Host "测试完成并退出 MiaCode 后按回车")
                Wait-MiaCodeClosed
                $projectLogs = Join-Path $cacheDir "logs"
                if (Test-Path -LiteralPath $projectLogs) {
                    $null = & robocopy $projectLogs (Join-Path $roundDir "project_logs") /E /NFL /NDL /NJH /NJS /NP
                }

                $failed = Read-YesNo "这一轮点播放是否没反应（按钮不变、画面不走）？"
                $verdict = Get-RoundVerdict $roundDir
                Add-Summary ("round {0}: tester_says_play_failed={1}" -f $round, [int]$failed)
                foreach ($line in $verdict) {
                    Add-Summary ("  " + $line)
                    if ($line -like "VERDICT=HIT*") { Write-Good $line }
                    elseif ($line -like "VERDICT=*") { Write-Warn2 $line }
                    else { Write-Host $line }
                }
                $hit = @($verdict | Where-Object { $_ -like "VERDICT=HIT*" }).Count -gt 0
                if ($failed -and !$hit) {
                    Write-Warn2 "你观察到了失败，但日志里没有 DEV_DEFAULT 失败记录。这一轮很关键，请务必回传。"
                    Add-Summary "  NOTE: failure observed without the DEV_DEFAULT log line"
                }
                if (!$failed -and $hit) {
                    Write-Warn2 "日志显示引擎初始化失败，但你看到播放正常。这一轮也很关键，请务必回传。"
                    Add-Summary "  NOTE: DEV_DEFAULT failure logged but playback looked fine"
                }
                $again = $false
                if ($round -lt 10) {
                    Write-Host "这是竞态问题，不一定每轮都触发；建议至少跑 3 轮，最好跑到出现一次失败。"
                    $again = Read-YesNo "再跑一轮？"
                }
            } while ($again)
            Remove-Item Env:\MIACODE_LOG_DIR -ErrorAction SilentlyContinue
        }
    } else {
        Add-Summary "stage2=skipped (no MiaCode.exe)"
    }
} catch {
    Write-Bad ("出错: " + $_.Exception.Message)
    Add-Summary ("ERROR: " + $_.Exception.Message)
} finally {
    Set-Content -LiteralPath (Join-Path $outDir "summary.txt") -Value $SummaryLines -Encoding UTF8
    if ($transcriptStarted) { try { Stop-Transcript | Out-Null } catch { } }
    $zipPath = "$outDir.zip"
    $toZip = @(Get-ChildItem -LiteralPath $outDir | Where-Object { $_.Name -ne "_chart_copy" } | ForEach-Object { $_.FullName })
    if ($toZip.Count -gt 0) {
        Compress-Archive -LiteralPath $toZip -DestinationPath $zipPath -Force
        Write-Host ""
        Write-Good "完成。请把这个文件发回给开发者："
        Write-Host "  $zipPath" -ForegroundColor White
        Write-Host "（谱面副本不会打包进去，可以随后删除目录 $outDir）"
    }
}
