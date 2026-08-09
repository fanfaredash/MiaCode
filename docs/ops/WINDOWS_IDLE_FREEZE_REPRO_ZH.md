# Windows 冻结/卡顿复现与取证指南

本文用于在用户的 Windows 双显卡机器上复现并验证 MiaCode 的冻结与预览卡顿问题。当前改动是**诊断增强**，不是在缺少线程栈与严格 A/B 结果时提前宣称根因已经修复。开发侧没有等价 Windows 硬件环境，因此最终结论必须以用户机证据为准。

## 0. 已确认的历史基线

- 受影响版本确认为短暂发布过的 `1.1.0-beta.2`；不要将它改写或映射为 `1.0.1-beta.2`。
- 设备：11th Gen Intel Core i5-1155G7（4 核 8 线程、15 W 移动端）、16 GB RAM、NVIDIA GeForce MX450 **2 GB** 与 Intel Iris Xe 双显卡、477 GB SSD。
- 用户未启用任何扩展。
- 本轮诊断构建保持高性能 GPU 绑定默认开启；`GpuOff` 只作为受控 A/B 对照。

新诊断构建会在启动日志记录版本、源码 revision、工作树状态与实际 exe 身份；两次 A/B 必须使用同一个 `app\MiaCode.exe`，并用 `phase0.json` 的 SHA-256 复核。

## 0.1 场景已被用户反馈改写：这是**争用**问题，不是**空闲**问题

2026-08-04 的用户反馈把问题 1 的条件改写为：

| | 早期假设 | 用户实际 |
|---|---|---|
| 屏幕 | 黑屏 / 休眠 | **未黑屏、未锁定** |
| 窗口 | 前台放置 | **最小化 / 被浏览器挡住** |
| 机器负载 | 空闲 | **浏览器持续播放视频** |
| 时长 | 2 小时 | **5～10 分钟** |

同一台机器还报告了问题 2：**OBS 推流时**预览播放卡顿，密度阈值降到平时的约 1/10。

两者的公因子是第三方进程持续占用 CPU/GPU。**应用是空闲的，机器不是。** 因此下面的矩阵按“争用优先”排列；显示器休眠与锁屏被降级为回归项。详细候选机制见 `docs/audit/OBS_CONTENTION_PLAYBACK_STUTTER_AUDIT_ZH.md`。

## 1. 准备

需要：

1. 包内真实 GUI 程序 `app\MiaCode.exe`。包根目录的启动器不能替代它做身份或 GPU 设置验证。
2. Windows PowerShell 5.1。
3. Microsoft Sysinternals ProcDump，用于冻结当场取得完整 dump。
4. 一个空间充足的证据目录；每次启动会新建带时间戳的子目录，不覆盖旧证据。

不要设置其他 `MIACODE_*` 变量，不要加 `--quick-shell-beta`，保持所有扩展关闭。两组使用相同谱面、供电方式、显示设置、放置时长与唤醒操作。

**唯一的例外，且每一组都要加：`MIACODE_PREVIEW_FRAME_PACING_DIAG=1`。**

```powershell
$env:MIACODE_PREVIEW_FRAME_PACING_DIAG = "1"
```

这个开关早已存在，但此前从未要求打开。它让运行时日志输出 `render_frame_profile`，把一帧拆成
`paint_ms / sync_ms / pre_render_wait_ms / render_submit_ms / swap_gpu_ms`，并且对**每一个 ≥30 ms 的慢帧自动记录**（`slow=1`），不只是按 `MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS` 采样。第 6 节的判别树完全依赖这些字段。

### 必须记录：OBS 的编码器是 x264 还是 NVENC

这一项目前**未知**，且它决定争用落在哪里：

- **x264（软编）** → 争用在 **CPU**，4 核机器会被吃满，优先看 `paint_ms` / `layer_sum_ms` 与音频欠载；
- **NVENC（硬编）** → 争用在 **MX450 的 2 GB 显存**上，与 MiaCode 根窗口挤在同一块卡，优先看 `idle/vram_gauge` 的 `local_over_budget` 与 `swap_gpu_ms`。

在 OBS 的“设置 → 输出 → 编码器”里确认，并连同分辨率、码率、帧率一起写进本次证据目录。**不要事后猜。**

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

### 首选顺序（按用户实际场景重排）

先做争用场景。第 1～3 组每轮只要 5～15 分钟，单轮成本比旧矩阵低一个数量级，可以多跑几遍取重复性：

| 顺序 | Profile | 条件 | 时长 |
|---|---|---|---|
| 1 | `GpuBound` | 打开谱面；**最小化 / 被浏览器完全挡住，浏览器持续播放视频网站**；不休眠、不锁屏 | 5–15 min |
| 2 | `GpuOff` | 与第 1 组完全相同（`MIACODE_GPU_BIND_HIGH_PERFORMANCE=0` 对照，把根窗口拉回 Iris Xe，取消跨适配器分裂并让根窗口离开 2 GB 卡） | 5–15 min |
| 3 | `GpuBound` | **前台放置**（不最小化、不遮挡）+ 浏览器持续播放视频 | 5–15 min |
| 4 | `GpuBound` / `GpuOff` | 回归项：显示器 5 分钟后关闭、允许休眠唤醒；以及 `Win+L` 后解锁 | 2 h |

第 3 组的作用是把「被遮挡」和「被争用」两个变量分开：

- 只有第 1 组冻结、第 3 组不冻结 → 与**遮挡/最小化**相关（看 `window/visibility`）；
- 第 1、3 组都冻结 → 与**争用**相关，遮挡不是必要条件；
- 第 1 组冻结而第 2 组不冻结 → 支持 GPU 绑定 / 2 GB 显存候选（看 `idle/vram_gauge`）。

问题 2（OBS 卡顿）用同一构建、同一开关另跑一组四格对照：OBS 关/浏览器关（基线）、OBS 开/浏览器关、OBS 关/浏览器开、两者都开。**同一谱面、同一密度、同一时长**，比较各组 `render_frame_profile` 里 `slow=1` 行哪一项膨胀。

完成一组并退出 MiaCode 后，再用另一 Profile 启动：

```powershell
& $reproScript -MiaCodeExe $realExe -Profile GpuOff -LogRoot $evidenceRoot
```

后续才扩展到“未保存修改”“播放后暂停”“全屏预览”等矩阵项。每个对照都必须保持相同的时长与相同的第三方负载，不能用一次短测否定候选原因。

## 3. 启动后先验收诊断链

每组开始后检查本次目录中的 `phase0.json` 和 `miacode_runtime_debug.log`：

- 两组 `Executable.SHA256` 必须相同，`Executable.Path` 必须指向包内 `app\MiaCode.exe`。
- `startup/process_identity` 应含 `version`、`git_revision`、`git_dirty` 与真实 exe 路径。
- `GpuBound` 的 `startup/gpu_provider` 应显示实际 `action=bound`，或明确说明为何跳过；若是 `reason=high_perf_equals_default_adapter`，这台机器上的 GPU 绑定 A/B 没有判别力。
- `GpuOff` 应出现 `reason=bind_disabled_by_env`。
- `[runtime/idle/resource_gauge] action=sample sample=0` 应在启动后出现，随后约每 30 秒一条。
- `[runtime/idle/vram_gauge] action=adapter_scan` 应与上面同一节拍出现，`adapter_count` 在这台机器上应为 **2**，随后每块适配器各一条 `action=sample`，含 `local_budget_mb` / `local_usage_mb` / `local_over_budget`。
- `[runtime/window/visibility] action=installed` 应对 `surface=root_window` 与 `surface=preview_composite` 各出现一次；同时应有一条 `action=graphics_persistence surface=preview_composite persistent_graphics=1`，它说明**最小化不会释放预览的显存**。
- `[runtime/ui/hang_watchdog] action=installed` 应记录 heartbeat 与 hang threshold；当前 watchdog 不执行 GUI 线程栈获取，也不会加载 Windows 符号组件。
- 开启 `MIACODE_PREVIEW_FRAME_PACING_DIAG=1` 后，播放时应出现 `render_frame_profile` 行。
- 播放时音频日志应出现 `bass_audio_health`（约每 5 秒一条）。
- 显示器状态改变应出现 `[runtime/windows/environment_event] action=console_display_state console_display_state=off|on|dimmed`。
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

同时记录冻结发生的本地时间、窗口状态（最小化 / 被遮挡 / 前台）、浏览器与 OBS 当时是否在跑、是否能移动窗口或操作菜单，以及任务管理器中 CPU 与内存是否仍在变化。

初步分类：

- CPU 约为 0：更像死锁或阻塞等待。
- 单核长期接近满载：更像 GUI 线程或事件循环空转。
- Private Bytes 持续上涨：更像渐进内存/换页问题。

日志中若出现 `[runtime/ui/hang_watchdog] action=gui_thread_stale trigger=idle_heartbeat`，其 `heartbeat_age_ms` 给出 GUI 心跳停止时间。若完全没有该行，仍须保留 dump；同时用启动身份和资源采样行确认日志目录及诊断构建是否正确。

**watchdog 不再获取 GUI 线程栈。** 为避免 Windows 用户遇到线程句柄、权限或安全软件拦截导致的错误弹窗，watchdog 只记录 heartbeat、stall episode 和 hang report；不会执行 GUI 线程挂起、符号初始化或栈遍历。发生冻结时，必须使用本指南前面的 ProcDump 流程获取完整 dump。

## 5. 必须回传的证据

- 本次完整时间戳目录，包括 `phase0.json`、`powercfg.txt`、全部日志和 `process_snapshot.txt`；
- **OBS 编码器类型（x264 / NVENC）与输出设置**，以及浏览器当时播放的分辨率；
- 完整 `.dmp`（能传就传；当前 watchdog 不提供 GUI 栈文本替代，因此冻结取证必须使用 ProcDump 或其他 dump 工具）；
- 冻结发生的准确时间、当时的窗口状态（最小化 / 被遮挡 / 前台）和触发步骤；
- 两个 Profile 的相同条件、相同时长结果；
- 日志中的 `dropped=` 计数；非零表示部分证据窗口可能已丢失。

## 6. 判别树：先读 `render_frame_profile`，再下结论

`MIACODE_PREVIEW_FRAME_PACING_DIAG=1` 打开后，每个 ≥30 ms 的慢帧都会记一行 `slow=1`。按哪一项膨胀分支：

| 哪一项大 | 指向 | 交叉验证 |
|---|---|---|
| `render_submit_ms` 大且 `pre_render_wait_ms` ≈ 0 | QSG 渲染线程被整片调度出去（MMCSS 保底在多进程争用下被稀释） | 其他进程是否也注册了 MMCSS |
| `pre_render_wait_ms` 大 | present / DXGI flip-queue 背压 | `GpuOff` 组是否改善 |
| `swap_gpu_ms` 大 | GPU 执行慢 / 显存驱逐 | `idle/vram_gauge` 的 `local_over_budget=1` |
| `paint_ms` / `layer_sum_ms` 大 | CPU 侧密度成本（x264 软编抢核） | `top_layer` 指出具体层 |
| 上面都不大，但用户听到卡顿 | 音频欠载 | 音频日志的 `bass_audio_stall` / `bass_audio_health` 的 `buffered_ms` |

## 7. 如何解释 A/B

- 只有 `GpuBound` 在重复的相同条件下冻结/卡顿，而 `GpuOff` 多次正常：支持 GPU 绑定与 2 GB 显存候选，仍需 `idle/vram_gauge` 或线程栈闭环。
- 两组都冻结：应转向争用、渲染线程或其他共享路径，不能把原因归结为绑定开关。
- 两组都不冻结：只能说明本轮未复现，不能排除低概率问题。
- 资源曲线持续增长：优先分析内存与句柄来源。
- `local_usage_mb` 逼近或超过 `local_budget_mb`（`local_over_budget=1`）：显存驱逐候选成立，应与慢帧时间戳对齐核对。
- `window/visibility` 的 `occluded_for_ms` 给出本次被遮挡/最小化的实际时长，用它核对“5～10 分钟”这个尺度是否吻合。

最终根因判断以冻结当场的 GUI 线程栈、`render_frame_profile` 分支、显存曲线、环境事件时间线和严格 A/B 为准。
