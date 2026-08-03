# Windows 空闲冻结复现与取证指南

本文用于在用户的 Windows 双显卡机器上复现并验证 MiaCode 空闲冻结问题。当前改动是**诊断增强**，不是在缺少线程 dump 与严格 A/B 结果时提前宣称根因已经修复。开发侧没有等价 Windows 硬件环境，因此最终结论必须以用户机证据为准。

## 0. 已确认的历史基线

- 受影响版本确认为短暂发布过的 `1.1.0-beta.2`；不要将它改写或映射为 `1.0.1-beta.2`。
- 设备：11th Gen Intel Core i5-1155G7、16 GB RAM、NVIDIA GeForce MX450 2 GB 与 Intel Iris Xe 双显卡、477 GB SSD。
- 用户未启用任何扩展。
- 本轮诊断构建保持高性能 GPU 绑定默认开启；`GpuOff` 只作为受控 A/B 对照。

新诊断构建会在启动日志记录版本、源码 revision、工作树状态与实际 exe 身份；两次 A/B 必须使用同一个 `app\MiaCode.exe`，并用 `phase0.json` 的 SHA-256 复核。

## 1. 准备

需要：

1. 包内真实 GUI 程序 `app\MiaCode.exe`。包根目录的启动器不能替代它做身份或 GPU 设置验证。
2. Windows PowerShell 5.1。
3. Microsoft Sysinternals ProcDump，用于冻结当场取得完整 dump。
4. 一个空间充足的证据目录；每次启动会新建带时间戳的子目录，不覆盖旧证据。

不要设置其他 `MIACODE_*` 变量，不要加 `--quick-shell-beta`，保持所有扩展关闭。两组使用相同谱面、供电方式、显示设置、空闲时长与唤醒操作。

## 2. 启动严格 A/B

在 Windows PowerShell 5.1 中运行；路径按实际位置替换：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
$reproScript = "C:\path\to\Start_MiaCode_IdleFreezeRepro.ps1"
$realExe = "C:\path\to\package\app\MiaCode.exe"
$evidenceRoot = "D:\miacode-idle-freeze"

& $reproScript -MiaCodeExe $realExe -Profile GpuBound -LogRoot $evidenceRoot
```

需要固定谱面时添加 `-ChartPath "D:\charts\example.maidata.txt"`。脚本会：

- 显式设置 `MIACODE_GPU_BIND_HIGH_PERFORMANCE=1`（`GpuBound`）或 `0`（`GpuOff`）；
- 以 `--debug` 启动真实 GUI exe；
- 保存 exe SHA-256、文件版本、CPU/GPU/内存/系统信息、已有 `MIACODE_*` 环境变量和 `powercfg` 快照；
- 将本次全部 MiaCode 日志定向到独立证据目录；
- 启动后立即返回并显示 PID，便于冻结时取证。

### 首选顺序

先做最高价值的显示器休眠场景，不要先花两小时跑常亮基线：

| 顺序 | Profile | 条件 | 时长 |
|---|---|---|---|
| 1 | `GpuBound` | 打开谱面、不播放；显示器 5 分钟后关闭，允许休眠与唤醒 | 2 h |
| 2 | `GpuOff` | 与第 1 组完全相同 | 2 h |
| 3 | `GpuBound` / `GpuOff` | `Win+L`，30 分钟后解锁；两组条件一致 | 30 min+ |
| 4 | `GpuBound` / `GpuOff` | 禁止显示器休眠的常亮基线 | 2 h |

完成一组并退出 MiaCode 后，再用另一 Profile 启动：

```powershell
& $reproScript -MiaCodeExe $realExe -Profile GpuOff -LogRoot $evidenceRoot
```

后续才扩展到“未保存修改”“播放后暂停”“全屏预览”等矩阵项。每个对照都必须保持相同的空闲时长，不能用一次短测否定候选原因。

## 3. 启动后先验收诊断链

每组开始后检查本次目录中的 `phase0.json` 和 `miacode_runtime_debug.log`：

- 两组 `Executable.SHA256` 必须相同，`Executable.Path` 必须指向包内 `app\MiaCode.exe`。
- `startup/process_identity` 应含 `version`、`git_revision`、`git_dirty` 与真实 exe 路径。
- `GpuBound` 的 `startup/gpu_provider` 应显示实际 `action=bound`，或明确说明为何跳过；若是 `reason=high_perf_equals_default_adapter`，这台机器上的 GPU 绑定 A/B 没有判别力。
- `GpuOff` 应出现 `reason=bind_disabled_by_env`。
- `[runtime/idle/resource_gauge] action=sample sample=0` 应在启动后出现，随后约每 30 秒一条。
- 显示器状态改变应出现 `[runtime/windows/environment_event] action=console_display_state state=off|on|dimmed`。
- 锁屏/解锁应出现 `action=session_change reason=session_lock|session_unlock`。

环境事件与看门狗记录使用 `FATAL` 级别是为了同步持久化证据，不表示显示器关闭或锁屏本身是致命错误。

## 4. 冻结当场：先分类，再取 dump

不要先关闭或结束卡死进程。使用 `phase0.json` 中精确记录的 PID：

```powershell
$runDir = "D:\miacode-idle-freeze\20260804-120000-000-gpubound"
$metadata = Get-Content (Join-Path $runDir "phase0.json") -Raw | ConvertFrom-Json
$targetPid = $metadata.Launch.ProcessId

Get-Process -Id $targetPid |
    Select-Object Id, CPU, PrivateMemorySize64, HandleCount, Threads |
    Format-List |
    Out-File (Join-Path $runDir "process_snapshot.txt")

procdump.exe -accepteula -ma $targetPid (Join-Path $runDir "miacode-freeze-$targetPid.dmp")
```

同时记录冻结发生的本地时间、显示器/锁屏状态、是否能移动窗口或操作菜单，以及任务管理器中 CPU 与内存是否仍在变化。

初步分类：

- CPU 约为 0：更像死锁或阻塞等待。
- 单核长期接近满载：更像 GUI 线程或事件循环空转。
- Private Bytes 持续上涨：更像渐进内存/换页问题。

日志中若出现 `[runtime/ui/hang_watchdog] action=gui_thread_stale trigger=idle_heartbeat`，其 `heartbeat_age_ms` 给出 GUI 心跳停止时间。若完全没有该行，仍须保留 dump；同时用启动身份和资源采样行确认日志目录及诊断构建是否正确。

## 5. 必须回传的证据

- 本次完整时间戳目录，包括 `phase0.json`、`powercfg.txt`、全部日志和 `process_snapshot.txt`；
- 完整 `.dmp`，不要只交小型 minidump；
- 冻结发生的准确时间和触发步骤；
- 两个 Profile 的相同条件、相同时长结果；
- 日志中的 `dropped=` 计数；非零表示部分证据窗口可能已丢失。

## 6. 如何解释 A/B

- 只有 `GpuBound` 在重复的相同条件下冻结，而 `GpuOff` 多次不冻结：支持 GPU 绑定/电源状态候选，但仍需线程栈闭环。
- 两组都冻结：应转向显示恢复、渲染线程或其他共享路径，不能把原因归结为绑定开关。
- 两组都不冻结：只能说明本轮未复现，不能排除低概率空闲问题。
- 资源曲线持续增长：优先分析内存与句柄来源。

最终根因判断以冻结当场的完整线程栈、环境事件时间线和严格 A/B 为准。
