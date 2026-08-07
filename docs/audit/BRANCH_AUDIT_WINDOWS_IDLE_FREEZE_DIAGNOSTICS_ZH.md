# 分支代码审计报告 — `codex/windows-idle-freeze-diagnostics`

- 审计基线：`dev`（merge-base `677a9625`）→ 分支 HEAD `f82cfa64`
- 规模：42 commits，98 files，+8143 / −335
- 审计范围：① 日志遗漏 ② 孤儿代码 ③ 冗余实现与清除方案 ④ 线程管理
- 代码位置：worktree `/Users/caoyusen/.codex/worktrees/MiaCode/windows-idle-freeze-diagnostics`

> **勘误（2026-08-07，修复实施后）**：本报告有两条结论经复核为**错误，已撤回**。
> - **R-5（MMCSS 说明失实）撤回**。`src/preview/quick_scene/PreviewQuickSceneRoot.cpp:798` **确实**在默认路径上把 QSG 渲染线程注册进 MMCSS（task class `Games`，仅 `MIACODE_DISABLE_MMCSS=1` 可关）。原判断源于一次被 `head -20` 截断的 grep。分支原有的注释与 `DEBUG_INDEX.md` 描述**是准确的**，`PreviewAudioHealthSpec.cpp` 也已断言 `app_mmcss_task_class=Games`。未做任何修改。
> - **O-1 / R-2（`TimelineView` 为死代码）撤回**。`src/app/cli_video_export.cpp:346` 的 `MainWindow window;` 使用默认实参 `quickShellBootstrapMode = false`，因此 `--export-video` CLI 导出路径**会**构造 `TimelineView`，并通过 `attachReferenceView()` 与 `TimelineQuickStateBridge` 双向绑定。原判断遗漏了这个 `MainWindow` 构造点。3468 行代码是活的，未删除。
>
> 其余结论经实施与编译/测试验证后成立。

## 处理状态总表（截至 2026-08-07）

第 1–3 类已实施，共 33 个提交（`f82cfa64..HEAD`，169 files，+1452 / −13398）。构建绿；`ctest` 46/49，3 个失败（`oplog_self_test`、`plain_code_editor_spec`、`preview_firework_lifecycle_spec`）为分支既有，已逐项核对这三个目标的 `SOURCES` 未被本轮改动触及。

**2026-08-07 追加一轮**：第 4 类的 T-1、T-2 已实施，另修复两处 watchdog 观测缺口（下表 W-1 / W-2）。触发这轮的是 8-07 15:51 的用户 capture（构建 `f515ec4a`）：其中两次音频设备切换各自伴随一段 GUI 线程停顿（2.075 s、4.683 s），期间 `pause_second`（视觉秒）与 `authoritative_second`（音频秒）分别岔开 1.916 s 与 4.023 s——**跳转幅度即停顿时长**；而同一份日志里 `ui/hang_watchdog` 一条报告都没有，`sym_ready=0` 使得即便报告了也拿不到符号。ctest 仍为 46/49，同样三个既有失败，且这三个目标的 `SOURCES` 均未被本轮触及（已核对）。

| 条目 | 状态 | 提交 |
|---|---|---|
| L-1 调度器 disarm 无日志 | 已修复 | `4826eb24` |
| L-2 调度器自我禁用无日志 | 已修复 | `9b6ba7fb` |
| L-3 mixer sync 丢弃分支静默 | 已修复 | `1ab03a41` |
| L-13 `bass_status` 不报实际布防组 + 失效注释 | 已修复 | `94db28bc` |
| L-7 原生 COM 注册无日志 | 已修复 | `7168745d` |
| L-8 设备变更被忽略分支静默 | 已修复 | `b6cf8eba` |
| L-9 `unregisterWindow()` 不记录 | 已修复 | `16afb366` |
| O-3 `isRelevantMessage()` 无生产调用 | 已改为前置快筛 | `c4d474b5` |
| R-4 `appendDurableEnvironmentEvent` 二次 flush 冗余 | 已删除 | `9b173e6a` |
| R-6 两条设备变更检测路径不同步 | 已修复（保留原生路径即时性） | `38ce484f` |
| L-5 hang 报告不说明为何无栈 | 已修复（5 种原因） | `2024362e` |
| L-6 `SymInitialize` 结果不上报 | 已修复 | `16f98b54` |
| O-2 `hasStackWalkTargetThread()` 孤儿 | 已删除 | `c50902d9` |
| L-4 纹理容量冲刷无日志 | 已修复 | `13a626e3` |
| L-10 焦点去重静默丢弃 | 已修复（`deduped=N`） | `b3a5bd8d` |
| L-11 `setBackgroundScaleMode()` 无日志 | 已修复 | `f397b6ed` |
| L-12 暂停秒单写入口有漏口 | 已修复 + 文档改为规则+grep | `f721dee8` |
| R-7 `DEBUG_INDEX.md` 自相矛盾 | 已修复并重新点算 | `794bead6` |
| O-4 `DebugImageCompare.h` | 已删除 | `8e64c8f7` |
| O-5 `PreviewQuickSceneInvalidationPolicy.h` | 已删除 | `3402668e` |
| O-6 `previewForceSoftwareVideoDecodeEnabled()` | 已删除（env 经三态访问器仍有效） | `a507efd3` |
| O-7 `previewVisualSmoothingEnabled()` | 已删除并退役 flag | `e14d22d7`、`acedd35a` |
| O-8 `hasRuntimeDebugArg()` | 已删除 | `bee15f21` |
| O-8 `debugCategoryEnabled()` | **未删** — 带明确保留说明 | — |
| O-9 `MIACODE_SKIP_DIAG_D3D11` 不可达 | 已退役 | `dbb0b6aa` |
| O-10 `videoExportBackgroundScaleModeToken()` | 已删除 | `de3751d1` |
| R-3 `&first` / `shifted*` 去重 | 已修复（纯去重，零行为变化） | `5dcf6cc1` |
| R-1 DComp/D3D11 栈清除 | 已删除（71 files / 11404 行 / 6 flags） | `8e53d5ae`…`364a936d` |
| R-5 MMCSS 说明失实 | **已撤回** — 结论错误，见勘误 | — |
| O-1 / R-2 `TimelineView` 死代码 | **已撤回** — 结论错误，见勘误 | — |
| T-1 `logPlaybackStatus` 持锁写日志 | 已修复（锁内只快照，出锁再格式化与写盘） | 本轮 |
| T-2 `schedulerMutex_` × BASS 内部锁 ABBA | 已修复（`BASS_ChannelRemoveSync` 移出临界区） | 本轮 |
| T-3 `playbackSession_` 跨线程读写 | **部分修复** — 见下方复核，触摸长按那条已修，其余重新评级 | 本轮 |
| T-4 通知客户端引用计数非原子 + UAF | 已修复（原子计数 + `detachOwner()` 屏障） | 本轮 |
| T-5 GUI 线程被改公寓模型 | 已修复（改 `COINIT_APARTMENTTHREADED`） | 本轮 |
| T-7 启动期 `SymInitialize` 与 loader 争锁 | 已修复（推迟到首个心跳，监控不中断） | 本轮 |
| T-6 资源计量在 GUI 线程上反复建毁 DXGI 工厂 | 已修复（首采样改投递 + 工厂缓存 `IsCurrent()` 校验） | 本轮 |
| T-8 挂起报告链路被它要报告的挂起阻塞 | 已修复（`logMutex` 改 2 s `tryLock` + 独立兜底文件） | 本轮 |
| T-9 线程池职责划分 | 未实施 —— 与本次冻结无关（切谱面延迟） | — |
| T-10 栈捕获超时泄漏 | 未实施 —— 原报告本身认同当前权衡 | — |
| W-1 亚阈值 GUI 卡顿完全无日志 | 已修复（新增 `action=gui_thread_stall`） | 本轮 |
| W-2 `SymInitialize` 单点失败导致全程无符号 | 已修复（`fInvadeProcess=FALSE` 回退 + `SymRefreshModuleList`） | 本轮 |

实施过程中发现的、报告原文未写到的补充事实：

- **L-12 的数字错得比报告说的更多**：`MainWindowShared.h` 注释写 "eighteen assignments across six section files"、`DEBUG_INDEX.md` 写 "twenty"、commit `60149edb` 的说明写 "twenty…across six"，而实际是 **20 处跨 8 个文件**。三处互相矛盾且都不准。现已改为「规则 + 可执行 grep」，不再依赖会腐烂的计数。
- **R-3 实际是 5 份不是 6 份**：`MainWindow.PreviewTimelineFlow.cpp` 的 `shiftedTimelineSecond` / `shiftedBeatMarkers` / `shiftedNoteMarkers` 只互相调用，无外部调用者，属死代码，已直接删除。
- **`MainWindow.ExportSnapshot.cpp` 的 `parsedDocumentFirstSeconds` 这个不同名字是有作用的**：改名成 `parsedFirstSeconds` 会因类作用域优先于 using 声明而静默绑定到读**实时控件**的 `MainWindow::parsedFirstSeconds`，而导出必须读已提交的 document。已保留独立名字并在注释中记录该陷阱。
- **`d3d11` 链接库不可删**：报告 R-1 第 5 步把它列入清除清单是错的。仍有 17 个文件使用（`PreviewSharedD3D11Device`、`PreviewQuickD3D11ExportSession`、`QuickShellPreviewCompositeSurface`、`gpu_device_provider`、stage-media host 等），删除会断 Windows 链接。实际只移除了 `dcomp` 与 `d3dcompiler`。
- **`--quick-shell-beta` 现已完全无作用**：其唯一效果是 `qputenv("MIACODE_PREVIEW_USE_DCOMP", "1")`，随 DComp 一并移除。参数仍被静默接受（`startupOpenTargetFromArguments()` 跳过 `-` 开头的 token），不会被误认为谱面路径。
- **`docs/ops/DEBUG_INDEX.md` 的 flag 计数**：91 live / 8 retired → **85 live / 14 retired**。仓库自带漂移守卫 `debug_flag_index_spec` 是真正的强制点，其正则扫描源码文本且不区分注释——退役 flag 的字面名不可留在注释里。
- **skill 参考文档有三份镜像**（`.claude/`、`.codex/`、`.agents/`），三份都会漂移，flag / 结构变更需同时更新。
- **`PreviewStageMediaHost::currentBackgroundImage()` 现已零调用**（唯一消费者 `StageBackgroundSource` 随 DComp 删除）。这意味着 `noteVideoFrameArrived()` 中每帧的 `QVideoFrame::toImage()` 成为 GUI 线程上的纯无效开销。删除是实质性能收益，但会改变当前行为，未在本轮夹带。
- **导出路径 `&first=inf` 会产出全 NaN 的 marker**，预览路径不会。按「纯去重、零行为变化」的决定原样保留，但这看起来是缺陷而非设计。

**总体判断**：诊断能力的建设方向和落点都对（idle 心跳、GUI 线程自栈回溯、DXGI per-adapter VRAM、窗口遮挡、BASS underrun），策略层拆成纯函数 + spec 的做法也符合仓库惯例。真正的问题集中在两处：**(a) 新增的观测点自身在关键分支上"静默"**，导致一份 capture 仍然无法区分"没发生"和"发生了但没记"；**(b) 新增的 SFX mixer 调度器把 BASS 音频回调线程和 GUI 线程用同一把 `schedulerMutex_` 绑在了一起，而 GUI 侧在持锁期间写日志** —— 这恰好是本分支想要诊断的那类卡顿的成因。

---

## 一、日志遗漏

判定标准：用户可见操作 / 外部事件 → 其后续影响没有留下可检索的行；或分支被静默吞掉，使 capture 无法区分"未发生"与"发生了但没记录"。

### L-1（高）SFX 调度器只记 `anchor`，不记 `disarm`，16 个撤防点全部静默

- 位置：`src/audio/BassPreviewAudioBackend_EventDrain.cpp:disarmSfxScheduler()`
- `anchorSfxScheduler()` 会写 `bass_sfx_scheduler action=anchor …`，但 `disarmSfxScheduler()` **一行都不写**。全仓 16 处调用（`suspendPlaybackTransport` / `anchorTransportToSecond` / `repositionPausedTransportToSecond` / `applyLevels` / `configureTimeline` / `clearTimeline` / `resetAssets` / `startBackgroundTrack` / `seekBackgroundTrack` / `pauseBackgroundTrack` / `stopPlaybackSession` / `applyPlaybackRateAtChartSecond` / `resetCursor` …）。
- 后果：`disarm` 后若因 `masterRunning == false` 走不到重新 `anchor`，实时 SFX 通道就永久退化为 GUI `drainEvents` 回退路径，而日志里只表现为"`action=anchor` 突然不再出现"。调查者无法判断是"没触发"还是"撤防后没重新布防"。
- 建议：`disarmSfxScheduler()` 增加 `bass_sfx_scheduler action=disarm reason=<caller> had_sync=%1 group_idx=%2`，`reason` 由调用方传入（与本分支 `writePreviewPauseSecond(reason)` 同一手法）。

### L-2（高）调度器自我禁用无日志：`armNextGroupSyncLocked()` 失败即静默降级

- 位置：`src/audio/BassPreviewAudioBackend_EventDrain.cpp` — `BASS_ChannelSetSync` 返回 0 时执行 `noteBassErr("sfx_scheduler/set_sync"); sfxSchedulerActive_ = false; return;`
- `noteBassErr` 只在 `BASS_ErrorGetCode() != 0` 时才写 `bass_err`；而 `sfxSchedulerActive_ = false` 这一步——把整个实时 SFX 路径切回 GUI 回退——**没有任何显式行**。
- 后果：这是一次运行模式切换（mixer-driven → wall-clock-driven），音画同步特性随之改变，却在日志里不可见。
- 建议：无条件补 `bass_sfx_scheduler action=deactivated reason=set_sync_failed bass_err=%1`。

### L-3（高）Mixer sync 回调丢弃分支静默：一个音符没响不会留痕

- 位置：`handleMixerGroupSync()` 开头 `if (shuttingDown || !sfxSchedulerActive_ || handle == 0 || handle != scheduledGroupSync_) return;`
- 该 `return` 表示"一个已经排程的音符组被丢弃"，是直接的可听后果，却没有日志。
- 建议：在非 `shuttingDown` 的丢弃分支写 `bass_sfx_mixer_drop reason=stale_handle|inactive handle=%1 expected=%2`（本身极罕见，成本可忽略）。

### L-4（高）Timeline 纹理缓存"容量兜底冲刷"完全无日志

- 位置：`src/timeline/quick/TimelineQuickItem.cpp:1550-1562` + `TimelineQuickTextureCache::capacityFlushRequired()`
- `capacityFlushRequired()` 为真时会 `invalidateAll()` 并强制重建整棵 node tree —— 一次用户可见的掉帧/卡顿。该路径**零日志**。
- 分支自己的注释写着"expected never to fire"，恰恰说明记录它几乎免费，而一旦触发就是唯一能解释那次卡顿的证据。
- 建议：紧邻 `invalidateAll()` 补一行 `timeline/texture_cache action=capacity_flush tex=%1 tex_bytes=%2 pixmaps=%3 limits=%4/%5/%6`。

### L-5（中高）`ui/hang_watchdog` 报告不说明"为什么这次没有栈"

- 位置：`src/common/UiHangWatchdog.cpp:watchdogLoop()`
- `policy::shouldCaptureStack()` 返回 false 有三种原因（会话被永久禁用 / 30 s 间隔未到 / 16 次配额用尽），**三种都不写日志**。`action=gui_thread_stale` 行里也没有对应字段。
- 后果：一个长冻结只有前几条 stale 行带栈，之后的裸 stale 行看起来像"栈捕获坏了"。
- 建议：把跳过原因并入 `gui_thread_stale` 行：`stack=captured|skipped_interval|skipped_budget|skipped_disabled captures_so_far=%1`。

### L-6（中高）`SymInitialize` 结果从不上报，`symbol=(nosym)` 无法归因

- 位置：`src/common/ThreadStackCapture.cpp:ensureSymbolHandler()` — `g_symbolHandlerReady` 只在进程内使用，从未进日志；`prepareStackWalkSymbols()` 也无返回值。
- 后果：栈帧里的 `symbol=(nosym)` 既可能是"用户机器没有 PDB"（正常），也可能是"SymInitialize 失败"（异常），日志无法区分——而 `DEBUG_INDEX.md` 明确要求读者按前者解读。
- 建议：`prepareStackWalkSymbols()` 输出状态，watchdog 安装行补 `sym_handler_ready=%1 sym_init_err=%2`。

### L-7（中高）`PreviewAudioDeviceWatcher` 的原生 COM 注册结果完全不记

- 位置：`src/audio/PreviewAudioDeviceWatcher.cpp` 构造函数
- `CoInitializeEx` / `CoCreateInstance(MMDeviceEnumerator)` / `RegisterEndpointNotificationCallback` 三个失败点全部静默 `return`。
- 对比：同分支的 `WindowsIdleEventMonitor::registerWindow()` 就规范地写了 `action=registration … session_registered=%1 session_error=%2 …`。同一分支两套标准。
- 后果：capture 里看不到 `action=native_default_output_changed` 时，无法区分"用户没换设备"和"原生监听根本没装上"（例如 GUI 线程已是 STA，或音频服务未就绪）。
- 建议：构造函数结束时写 `preview/audio_device action=native_registration com_hr=0x%1 enumerator_hr=0x%2 registered=%3`。

### L-8（中）设备变更被忽略的分支静默

- 位置：`MainWindow.PreviewPlaybackState.cpp:pausePreviewForAudioDeviceChange()` 的首个 `return`（`shouldPausePreview()` 为 false，即预览未播放）。
- 该早退是设计意图（一次物理热插拔 Qt 会发多次通知），但没有计数或行，`device_change_pause_begin` 的序号因此存在不可解释的跳号（`previewAudioDeviceChangeSequence_` 只在真正暂停时自增）。
- 建议：写一行 `device_change_ignored change=%1 playing=0`，或至少让序号在忽略时也自增并在行内标注。

### L-9（中）`WindowsIdleEventMonitor::unregisterWindow()` 不记录

- 位置：`src/app/WindowsIdleEventDiagnostics.cpp`
- 注册写日志、注销不写。`beginAcceptedRootWindowShutdown()` 与析构都会调用它，之后所有 `windows/environment_event` 静默消失。
- 建议：对称补 `action=unregistration hwnd=0x%1`。

### L-10（中）`quick_shell/focus` 去重丢弃无计数，与同分支的 ValidationFlow 节流标准不一致

- 位置：`QuickShellBootstrap.cpp:logFocusEvent()` — `if (signature == lastFocusFilterSignature_) return;`
- 同一分支的 `appendExtraSelectionsPerfLog()` 节流会把 `suppressed=N suppressed_max_ms=… suppressed_total_ms=…` 附到下一行；焦点去重则是**纯丢弃**。
- 后果：如果"焦点抖动风暴"本身就是冻结的症状（这在 QuickShell + 原生 surface 混排下并非臆测），本次改动恰好把它抹平了。
- 建议：沿用 ValidationFlow 的写法，在下一条真实 transition 行上带 `deduped=N`。

### L-11（中）`setBackgroundScaleMode()` 及其新副作用无日志

- 位置：`src/preview/runtime/PreviewStageMediaHost_Media.cpp:setBackgroundScaleMode()`
- 这是用户在渲染设置里可见的选项。本分支给它加了新的副作用 `refreshInnerVideoSinkForScaleMode()`（进入模式 3 时把保留帧推进 inner sink，离开时清空以释放 decode-pool surface）。整条链路零日志。
- 后果：模式 3 下"切进去第一帧不对/切出去后显存没降"这类反馈无法从日志复现。
- 建议：`preview/stage_media action=scale_mode from=%1 to=%2 inner_sink_active=%3 primed=%4`。

### L-12（中）`writePreviewPauseSecond` 的"全覆盖"承诺存在一个漏口

- 位置：`src/tools/latency/LatencySandboxController.cpp:270`
  ```cpp
  owner_->state_.qtPreviewPauseSecond_ = clamped;   // 直写，绕过 writePreviewPauseSecond
  ```
- 与之矛盾的文档：`MainWindowShared.h` 注释与 `docs/ops/DEBUG_INDEX.md` 都写"**Every one** of the twenty assignments to `qtPreviewPauseSecond_` … is routed through `shared::writePreviewPauseSecond`, so a backward move **cannot happen without a row**"。
- 实测：全仓 20 处走了封装，**第 21 处（延迟检测沙盒）没走**。`applyPlayheadToScene()` 在热改 BPM/offset/细分时会重新锚定播放头，完全可能向后移动。
- 后果：这是本分支为定位"暂停后时间轴倒退"专门建立的不变量，而漏口正好在一个会热改播放头的功能上。
- 建议：改为 `miacode::mainwindow::shared::writePreviewPauseSecond(owner_->state_.qtPreviewPauseSecond_, clamped, owner_->state_.qtPreviewPlaying_, "latency_sandbox_apply_playhead")`，并同步修正两处文档措辞。

### L-13（低）`bass_status next_group_idx` 不再指向真正布防的组

- 位置：`BassPreviewAudioBackend_PlaybackClock.cpp:322` 附近，注释仍写 `// G1 Commit 7: scheduledGroupIndex_ deleted with the BASS_SYNC_POS scheduler.`
- 本分支把 `scheduledGroupIndex_` 加回来了，但 `bass_status` 仍报 `playbackSession_.eventGroupIndex`。当 mixer 已为"pending BGM"布防（此时 `scheduledGroupIndex_ == -1`）时，两者含义不同。
- 建议：删除失效注释，行内补 `armed_group_idx=%1 armed_action=%2`。

### L-14（低）扩展事件被跳过时不可见

- 位置：`EmbeddedExtensionRuntime::dispatchEvent()` 新增的 `if (!hasEventSubscriber(kind)) return;`
- 无订阅者时事件被静默丢弃，扩展作者报"我的订阅不触发"时无法区分"事件没发"与"发了但没匹配上"。附带行为变化：`nextEventSequence_` 不再为被跳过的事件自增（`sequence` 语义从"发布序号"变成"投递序号"）。
- 建议：devtools 快照里加一个 per-kind 的 `skippedNoSubscriber` 计数（不要每帧写日志）。

---

## 二、孤儿代码（0 调用，且无"刻意保留"说明）

### O-1（高）`TimelineView` 整套 QWidget 时间轴渲染器 —— 3468 行，运行期永不实例化

- 文件：`src/timeline/TimelineView.h` / `TimelineView.cpp` / `TimelineView.Core.cpp` / `TimelineView.Paint.cpp` / `TimelineView.Interaction.cpp`（合计 3468 行）
- 证据链：
  1. `src/app/main.cpp:704` 是**唯一**的 GUI 启动路径：`QuickShellBootstrap quickShellBootstrap(appIcon);`
  2. `QuickShellBootstrap.cpp:237`：`backend_ = std::make_unique<MainWindow>(true);` —— `quickShellBootstrapMode` 恒为 `true`
  3. `MainWindow.FrameBootstrap.cpp:94`：`timelineWidgetlessQuickRoute_ = quickShellBootstrapMode_;` —— 恒为 `true`
  4. `MainWindow.FrameBootstrap.cpp:1614`：`if (!timelineWidgetlessQuickRoute_) { timelineView_ = new TimelineView(bottomTabs_); … }` —— **唯一构造点，恒不进入**
- 连带：`timelineView_` 共 50 处引用（`ExtensionHostRequests.cpp` 17 处、`FrameBootstrap.cpp` 14 处、`WindowInteraction.cpp` 5 处、`WindowShell.cpp` 4 处、`PreviewTimelineFlow.cpp` 4 处、`TimelineLayoutUI.cpp` 3 处、`BottomTabsHost.cpp` 1 处），全部是 `!= nullptr` 死分支；另有 14 个 `#include "TimelineView.h"`（因 `MainWindow.h:30` 直接包含它，实际拖慢整个 MainWindow 编译）。
- 注：`TimelineQuickStateBridge::attachReferenceView(timelineView_)` 也只在该死分支里调用 —— 值得单独确认 bridge 里的 "reference view" 相关字段是否也随之成为死码。

### O-2（中）`miacode::diag::hasStackWalkTargetThread()` —— 生产端 0 调用

- 位置：`src/common/ThreadStackCapture.h` + `.cpp:315`（Win）/ `.cpp:452`（非 Win）
- 唯一调用者是 `src/tools/debug_index/ThreadStackCaptureSpec.cpp`。既非 API 契约的一部分（`captureRegisteredThreadStack()` 内部自行判空并返回 `skipReason=not_registered`），也没有"保留待用"的注释。
- 处置：删除；或在 watchdog 安装行里实际用它（这样同时修掉 L-6 的一半）。

### O-3（中）`windows_idle_diagnostics::isRelevantMessage()` —— 生产端 0 调用

- 位置：`src/app/WindowsIdleEventDiagnostics.h` / `.cpp`
- `observeNativeMessage()` 并不调用它，而是直接调 `eventPayload()` 并靠"返回空串"过滤。唯一调用者是 spec。
- 处置：要么让 `observeNativeMessage()` 先用它做前置快筛（每条 Windows 消息都会走这条路，快筛有实际价值），要么删掉。**保持现状是最差选项**——spec 在测一个生产代码不使用的谓词。

### O-4（中）`src/common/DebugImageCompare.h` —— 266 行，全仓无人 include

- 仅出现在 `CMakeLists.txt:251` 的源文件列表里。无 `.cpp`，无任何 `#include`。
- 处置：直接删除（含 CMake 条目）。

### O-5（中）`src/preview/quick_scene/PreviewQuickSceneInvalidationPolicy.h` —— 全仓无人 include

- 定义了 `PreviewQuickSceneDirtyFlag` 位标志与 `kPreviewQuickSceneAllDirty`，仅出现在 `CMakeLists.txt:515`。
- 处置：删除，或说明它是哪一次未完成重构的遗留并补上使用点。

### O-6（中）`debug_options::previewForceSoftwareVideoDecodeEnabled()` —— 0 调用，但 flag 被文档为"active"

- 位置：`src/common/DebugOptions.h:263`
- 已被三态的 `previewVideoDecodePreference()` 取代（后者仍读同一个 `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO`，所以 **env 本身仍有效**，只是这个布尔访问器是死的）。
- 处置：删函数。

### O-7（中）`debug_options::previewVisualSmoothingEnabled()` —— 生产端 0 调用，但 `DEBUG_INDEX.md:210` 声称有效

- 位置：`src/common/DebugOptions.h:407`；唯一调用者 `DebugOptionsSpec.cpp:161/163`。
- 这是比 O-6 更严重的一类：**文档承诺 `MIACODE_PREVIEW_VISUAL_SMOOTHING=0` 能关闭 scene-playhead 平滑，实际设了没有任何效果**，而 spec 还在为这个空壳做断言，制造"有覆盖"的假象。
- 处置：要么恢复调用点，要么同时删除函数、spec 断言与 DEBUG_INDEX 条目。

### O-8（低）`debug_options::debugCategoryEnabled(const char*)`、`hasRuntimeDebugArg(QStringList)` —— 均 0 调用

- 位置：`DebugOptions.h:171` / `:737`。前者已被缓存 atomic 的各 `*CategoryState()` 取代。

### O-9（低）`MIACODE_SKIP_DIAG_D3D11` 事实上已不可达

- 位置：`src/app/startup_diagnostics_win32.cpp`
- 新逻辑：`if (enableD3D11 == 0) skip; else if (skipD3D11 > 0) skip_via_env; else run;`。只有同时设置 `MIACODE_ENABLE_DIAG_D3D11` **和** `MIACODE_SKIP_DIAG_D3D11` 才会命中旧分支——即"显式打开又显式关掉"。
- 注释称其为 "compatibility override for support runs"，但两个变量都得由支持人员现设，兼容性论据不成立。
- 处置：删掉 `MIACODE_SKIP_DIAG_D3D11` 分支与 DEBUG_INDEX 条目，只留 `MIACODE_ENABLE_DIAG_D3D11`。

---

## 三、冗余实现与清除方案

### R-1（最高优先）DComp / D3D11 渲染栈 —— `src/render` + `src/sources`，11,404 行非默认路径

**现状**

| 目录 | 行数 | 入口 |
|---|---|---|
| `src/render/`（含 `backend_d3d11/`） | 8,844 | `QuickShellBootstrap.cpp:411` 的 `previewUseDCompEnabled()`；`TimelineQuickItem.cpp:845` 的 `previewTimelineUseDCompEnabled()` |
| `src/sources/`（chart + timeline source 套件） | 2,560 | 只被 `src/render/backend_d3d11/*` 包含 |

依赖是闭合的：`src/sources/*` 只被 `src/render/*` 使用，`src/render/*` 只被两个门控点使用，两个门控默认 `false`，仅 `--quick-shell-beta` 会 `qputenv("MIACODE_PREVIEW_USE_DCOMP", "1")` 打开。

**关键变化**：`--quick-shell-beta` 的原始语义（"切换到新 QML 外壳"）已经消失——QuickShell 现在是**唯一**外壳（`main.cpp:704`）。该 flag 今天**只剩** DComp A/B 一个作用。也就是说：一个 11,404 行的子系统，其存在理由只是一个诊断开关。这与仓库既定决策（`.claude/skills/miacode-dev-guide`：QSG 是主路径，DComp 默认关闭并"正在解耦"）和用户记忆（"非默认路径视同不存在"）一致。

**清除方案（按依赖顺序，每步可独立编译验证）**

1. **切断 timeline 侧**：删 `TimelineQuickItem` 的 `dcompView_` 成员、`#include "render/backend_d3d11/TimelineRenderView.h"`、`:845` 与 `:1480` 两个门控块；删 `render/backend_d3d11/TimelineRenderView.{h,cpp}` 与 `src/sources/timeline/*`。删 `previewTimelineUseDCompEnabled()`。
   → 此步之后 `src/timeline` 对 `src/render` 零依赖。
2. **切断 preview 侧**：删 `QuickShellBootstrap` 的 `previewDCompSurface_`、`:411` 门控与 `:576-665` 的挂载/回调块；删 `PreviewDCompSurface`、`PreviewDCompCore`、`PreviewDCompSpritePipeline`、`PreviewDCompTextureCache`、`PreviewDCompRenderer`、`PreviewDCompFrameStateSnapshot`、`src/sources/chart/*`、`render/{source,compositor,snapshot_builder}.h`。
   → `PreviewPopupHwndTracker` 需单独判定：如果只服务 DComp popup 就一并删。
3. **清 HUD 的 DComp 分叉**：`PreviewQuickHudLayer::paint()` 里的 `dcompFallbackActive_` / `previewDCompExclusiveEnabled()` 两个 skip 分支（本分支刚给它们包了 lazy detail lambda，删除后这部分包装也一并省掉）。
4. **清 flag**：`DebugOptions.h` 中 `previewUseDCompEnabled` / `previewDCompExclusiveEnabled` / `previewDCompTopLevelHwndEnabled` / `previewDCompPerPixelAlphaEnabled` / `previewTimelineUseDCompEnabled` / `previewDCompQuiesceQsgEnabled` 及其 6 个 env 条目；`main.cpp:636-641` 的 `qputenv`；`DEBUG_INDEX.md` 的 DComp 小节。
5. **CMake**：删 `if (WIN32)` 块里的 `src/render/*` 源；随后 `d3d11` / `dcomp` / `d3dcompiler` 三个链接库可移除（`dxgi` **必须保留** —— `ProcessDiagnostics.cpp` 的 VRAM gauge 与 `TimelineQuickItem` 的 leak gauge 都用它）。
6. **回收 MMCSS**：见 R-5 —— 删除 DComp 渲染线程后，全仓将**没有任何** MMCSS 注册点，这件事必须在同一次改动里处理，否则 `bass_audio_health` 的两个字段会永久说谎。

预估净删除 ≈ 11.4k 行 + 6 个 env flag + 3 个链接库。

### R-2（高）`TimelineView` widget 渲染器 —— 3,468 行第二套时间轴实现

见 O-1。这是与 `TimelineQuickItem`（1,975 行 QSG）功能重叠的第二套时间轴渲染实现，且已确证运行期不可达。

**清除方案**

1. 删 `src/timeline/TimelineView*.{h,cpp}` 5 个文件与对应 CMake 条目。
2. 删 `MainWindow.FrameBootstrap.cpp:1614-1650` 整个 `if (!timelineWidgetlessQuickRoute_)` 块。
3. 把 50 处 `timelineView_` 引用连同其 `!= nullptr` 分支一起删；删 `MainWindowMemberStorage.inc` 的成员与引用别名。
4. 删 14 个 `#include "TimelineView.h"`，特别是 `MainWindow.h:30` 的直接包含（会显著改善 MainWindow 编译时间）。
5. `timelineWidgetlessQuickRoute_` 与 `quickShellBootstrapMode_` 至此都成为恒真常量，一并折叠删除（`BottomTabsHost.cpp` 有 4 处依赖）。
6. 复核 `TimelineQuickStateBridge::attachReferenceView()` 是否还有其他调用者，无则一并删。

**顺序建议**：R-2 先于 R-1。R-2 无平台条件编译，改动面清晰，可作为 R-1 的演练。

### R-3（高）`&first` 偏移解析 —— 4 份实现，其中 2 份与另外 2 份**语义已经分叉**

| 位置 | 签名 | `ok` 出参 | 备注 |
|---|---|---|---|
| `src/tools/muri/MuriDump.cpp:54` | `parsedFirstSeconds(raw, ok=nullptr)` | 有 | |
| `src/tools/video_export/VideoExportSnapshot.cpp:148` | `parsedFirstSeconds(raw)` | **无** | |
| `src/app/mainwindow/sections/export/MainWindow.ExportSnapshot.cpp:110` | `parsedDocumentFirstSeconds(raw, ok=nullptr)` | 有 | 与 MuriDump 版逐字节相同 |
| `MainWindow.PreviewTimelineFlow.cpp:528` | `TimelineSection::parsedRawFirstSeconds(ok)` | 有 | 额外读 `ui_.firstEdit_` 实时文本 |

外加 `MainWindow.PreviewTimelineFlow.cpp:546` 的 `parsedFirstSeconds()` 是 `parsedRawFirstSeconds()` 的**纯转发别名**（`{ return parsedRawFirstSeconds(ok); }`），以及 `MainWindow.TimelinePlayback.cpp:65/70` 又是这两者的一层转发 —— 三层同名/近名转发。

**更严重的是配套的 `shiftedTimelineSecond` / `shiftedNoteMarkers`：6 份实现，且已分叉**

```
有 qIsFinite 守卫：  MuriDump.cpp:391 / MuriSpec.cpp:27 / PreviewTimelineFlow.cpp:201 / TimelineSlowRefresh.cpp:9
无 qIsFinite 守卫：  VideoExportSnapshot.cpp:156 / MainWindow.ExportSnapshot.cpp:121   ← 两条都是导出路径
```

`shiftedNoteMarkers` 的循环体在 6 份里逐字节相同（second / endSecond / slideTraceSecond / availableSecond / slideSegmentShootSeconds 五个字段），差异只在它调用的 `shiftedTimelineSecond` 有没有守卫。**后果是具体的**：一个非有限的 `&first`（或某个 marker 的非有限秒数）在预览与 timeline 路径下被原样透传，在**两条导出路径**下变成 NaN。这正是仓库 `cross-chain-linkage.md` 明令要当作 sync pair 维护的 parser ↔ timeline ↔ preview ↔ export 一致性。

**清除方案**

1. 新建 `src/core/chart/ChartOffset.h`（`src/core` 无 GPU/Qt-Widgets 依赖，符合分层规则），提供：
   ```cpp
   namespace miacode::chart_offset {
   double parseFirstSeconds(const QString& rawValue, bool* ok = nullptr);   // 采用「有 ok 出参 + 空串合法为 0」的语义
   double shiftSecond(double second, double offsetSeconds);                 // 采用「带 qIsFinite 守卫」的语义
   QVector<TimelineNoteMarker> shiftNoteMarkers(const QVector<TimelineNoteMarker>&, double);
   QVector<TimelineBeatMarker> shiftBeatMarkers(const QVector<TimelineBeatMarker>&, double);
   }
   ```
2. 6 处 `shifted*` + 4 处 `parsed*First*` 全部改为转调。**先在新 spec 里锁死"非有限输入"的行为**，再替换——因为这次统一会改变两条导出路径的现有行为，必须是有意识的决定而不是顺手统一。
3. `MainWindow::TimelineSection::parsedRawFirstSeconds()` 保留为薄壳（它有 `ui_.firstEdit_` 实时读取这个额外职责），但改为 `chart_offset::parseFirstSeconds(rawValue, ok)`；删掉 `parsedFirstSeconds()` 这个纯别名以及 `MainWindow` 上的第二层转发。
4. 新增 `src/tools/core/ChartOffsetSpec.cpp`，覆盖：空串 / 合法负数 / 非法文本 / `inf` / `nan` / 前后空白。

### R-4（中）`appendDurableEnvironmentEvent` 里的二次 flush 是纯冗余，且其失败分支不可达

- 位置：`src/app/WindowsIdleEventDiagnostics.cpp`
  ```cpp
  miacode::debug_log::appendLine(..., /*force=*/true, Level::Fatal);
  if (!miacode::debug_log::flushAsyncLogWriter(1000)) { … "action=log_flush_timeout" … }
  ```
- `DebugLog.cpp:1026` 已经写明：`level == Level::Fatal` 时先 `AsyncLogWriter::instance().flush(1000)`，再同步 open/write/flush/close。也就是说 `appendLine` 返回时队列必然已空，紧接着的 `flushAsyncLogWriter(1000)` 恒返回 true，`action=log_flush_timeout` 是**死代码**。
- 处置：删掉 flush 调用与失败分支；如果确实想保留"耐久性证据"，改为在 `appendLine` 的返回值上判断。

### R-5（中）MMCSS：唯一注册点在默认关闭的 DComp 路径上，两个新日志字段因此永久说谎

- 全仓 MMCSS 注册点只有一处：`src/render/PreviewDCompRenderer.cpp:255` `registerCurrentThread("Games")`。
- 但本分支在 `src/audio/PreviewAudioHealth.h:133-140` 与 `BassPreviewAudioBackend_PlaybackClock.cpp:268` 写道：*"the only MMCSS registration in the default code path is on the QSG render thread (`preview/quick_scene/PreviewQuickSceneRoot.cpp`)"* —— **该文件里没有任何 MMCSS 代码**。`DEBUG_INDEX.md` 也复述了同一说法。
- 实际后果：默认运行下 `miacode::mmcss::lastRegistrationStatus().everRegistered` 恒为 false，`bass_audio_health` 的 `app_mmcss_task_class` 恒为 `(none)`。该字段被引入的目的是"把猜测变成证据"，结果它证明的是一件与注释所述相反的事，而读者会按注释解读。
- 处置（二选一，但必须做）：
  - **A（推荐）**：在 QSG 渲染线程真正注册 MMCSS（`PreviewQuickSceneRoot` 的 `sceneGraphInitialized` / `beforeRendering` 首帧），使注释成真；这也直接回应本分支追查的 CPU 争用假设。
  - **B**：把注释与 DEBUG_INDEX 改成"默认路径下无任何线程注册 MMCSS"，并把字段名改为 `app_mmcss_task_class=(none, no registration site on default path)` 之类不会被误读的形式。
  - 无论哪种，`Mmcss.h:20` 的 *"typically Qt's QSG render thread"* 也是失实描述，需同步修正。

### R-6（低）`PreviewAudioDeviceWatcher` 有两条互不同步的变更检测路径

- Qt 路径 `handleAudioOutputsChanged()`：比较快照 → 更新 `snapshot_` → `emit`。
- 原生路径 `handleNativeDefaultOutputChanged()`：**不比较、不更新 `snapshot_`**，直接 `emit Change::DefaultOutputChanged`。
- 后果：一次默认设备切换会先由原生路径发一次（快），随后 Qt 路径再发一次（因为 `snapshot_` 没被原生路径更新，比较必然为真）。第二次因为预览已暂停而被 `shouldPausePreview()` 吞掉，行为上无害，但：① 两条路径绕过了同一个策略对象，`PreviewAudioDeviceChangePolicySpec` 只覆盖了其中一条；② 日志里出现一对语义重复的行。
- 处置：让原生回调也走 `handleAudioOutputsChanged()`（即只当作"触发重新比较"的信号），策略层保持单一。

### R-7（低）`DEBUG_INDEX.md` 自相矛盾

- 文件头：*"Reconciled against the code on 2026-08-04 (83 live `MIACODE_*` environment flags …). **The idle-freeze diagnostics add no environment flag.**"*
- 同文件下方却新增了 4 个：`MIACODE_UI_HANG_ACTIVE_PHASE_MS`、`MIACODE_UI_HANG_IDLE_HEARTBEAT_MS`、`MIACODE_ENABLE_DIAG_D3D11`、`MIACODE_ENABLE_DIAG_MODULE_LIST`。计数 83 未更新。
- 处置：改为 87（或删掉 O-6/O-7/O-9 后重新点算），删掉那句断言。

---

## 四、线程管理

### T-1（最高）`schedulerMutex_` 把 BASS 混音线程和 GUI 线程绑死，且 GUI 侧在持锁期间写日志

> **已修复（2026-08-07）**：`logPlaybackStatus` 改为锁内只把 20 个字段拷进局部变量，出锁后再做 26 处 `.arg()` 与 `appendAudioDebugLog`。`disarmSfxScheduler` 的那一半此前已修。规则本身写进了 `BassPreviewAudioBackend.h` 的 `schedulerMutex_` 声明处：**持锁期间禁止 I/O、日志、以及任何回调进 BASS**，并列出三个遵守该规则的站点。

**耦合结构**

```
GUI 线程                                  BASS mixer/update 线程
─────────────────────────────────         ──────────────────────────────
logPlaybackStatus()                       handleMixerGroupSync()
  lock(schedulerMutex_)                     lock(schedulerMutex_)
  ... 24 个 .arg() 构造 payload ...           triggerGroup() → playKindInternal()
  appendAudioDebugLog()  ← 写文件            armNextGroupSyncLocked()
  unlock                                    unlock
```

- 位置：`BassPreviewAudioBackend_PlaybackClock.cpp:303`（`QMutexLocker schedulerLocker(&schedulerMutex_);` 之后直到函数结束都在锁内，含整条 `appendAudioDebugLog`）
- 同类问题：`disarmSfxScheduler()` 在锁内调用 `noteBassErr("sfx_scheduler/remove_sync")` —— 同样是持锁写日志。
- 频率：`logPlaybackStatus` 约 1 Hz；开启 `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG` 时降到 250 ms 一次。
- 后果：GUI 线程一次慢磁盘写就直接推迟音频回调里的下一组 SFX 触发 → 音符迟到甚至 underrun。**这正是本分支新增的 `bass_audio_stall` 探针要抓的现象，而探针本身参与制造它。**
- 讽刺点：`handleMixerGroupSync()` 里明确写着 *"holding an audio-callback lock across a log write is exactly the kind of stall the buffer-health probe exists to catch"*，并为此把日志挪到锁外——但 GUI 侧的两个入口没有遵守同一条规则。

**建议**
1. `logPlaybackStatus`：在锁内只做**快照拷贝**（一个 POD struct），`unlock` 后再格式化与写日志。
2. `disarmSfxScheduler`：把 `noteBassErr` 移出 `QMutexLocker` 作用域（错误码先存局部变量）。
3. 在 `BassPreviewAudioBackend.h` 的 `schedulerMutex_` 声明处写死规则：**持有此锁期间禁止任何 I/O、日志与内存分配**，并在两侧各留一条断言性注释。

### T-2（高）`schedulerMutex_` 与 BASS 内部锁的顺序倒置风险

> **已修复（2026-08-07）**：按下面建议的形状实施——锁内取走 `scheduledGroupSync_` 句柄并把调度器状态清空，出锁后再调 `BASS_ChannelRemoveSync`。安全性依据已在代码注释里写明并复核过：`handleMixerGroupSync` 开头就有 `if (!sfxSchedulerActive_) dropReason = "inactive"` 的早退分支，而该标志在解锁前已置 false，所以解锁到移除之间触发的回调走自己的早退路径，不会操作正在拆除的调度器。

- GUI 线程：`disarmSfxScheduler()` 持有 `schedulerMutex_` → 调用 `BASS_ChannelRemoveSync()`（取 BASS 内部同步锁）。
- 音频线程：BASS 持有内部锁 → 调用 `onMixerGroupSync` → `handleMixerGroupSync()` 尝试取 `schedulerMutex_`。
- 这是标准的 ABBA 顺序。`BASS_ChannelRemoveSync` 在同步正在被处理时会等待其完成，此时该同步正阻塞在 `schedulerMutex_` 上 —— 而锁的持有者就是调用 `RemoveSync` 的那个线程。
- 触发面很宽：`disarmSfxScheduler()` 有 16 个调用点，覆盖暂停、seek、切谱面、改音量、改倍速等**全部常规交互**。
- 表现形式将是"GUI 线程无响应"—— 与本分支正在追查的冻结**不可区分**。
- 建议：把 `BASS_ChannelRemoveSync` 移出临界区：
  ```cpp
  quint32 syncToRemove = 0;
  { QMutexLocker l(&schedulerMutex_); syncToRemove = std::exchange(scheduledGroupSync_, 0u);
    scheduledGroupIndex_ = -1; scheduledMixerAction_ = None; sfxSchedulerActive_ = false; }
  if (syncToRemove && masterMixer_) { BASS_ChannelRemoveSync(masterMixer_, syncToRemove); }
  ```
  由于 `sfxSchedulerActive_` 已在锁内置 false，即使回调此刻在跑也会走它自己的早退分支，安全性不变。

### T-3（高）音频回调线程改写 `playbackSession_`，而 GUI 线程多处无锁读写同一结构

> **实施时复核（2026-08-07）：原条目高估了前三个例子，漏掉了真正严重的那个。** 逐点核对结果：
>
> - **`resetCursor()` —— 不是竞态。** 它在写 `eventGroupIndex` 之前先读 `sfxSchedulerActive_`（持锁）并在其为真时 `disarmSfxScheduler()`；撤防后 `sfxSchedulerActive_` 为 false，`handleMixerGroupSync` 走 `dropReason="inactive"` 早退，不会再碰这些字段。而 `rearmScheduler` 为 false 的那条路径，恰恰意味着调度器本来就没布防，回调同样早退。两条路都安全。
> - **`logPreparedEventWindow()` —— 不值得加锁，加了反而有害。** 它是 `const` 的纯诊断读，结果立即被 `qBound` 夹住，最坏是索引偏一格。更重要的是它在**循环里调 `appendAudioDebugLog`**——给它加 `schedulerMutex_` 等于新造一个 T-1。
> - **`applyLevels()` —— 保留为待办**，量级同上，未复核出实际危害。
> - **触摸长按那条是真的，而且比原文写的更严重。** `reconcileTouchholdVoice()` 由 `triggerGroup()` 调用，而 `triggerGroup()` 在 `handleMixerGroupSync` 的**临界区内**执行——也就是说它过去**在 BASS 混音回调线程上、持 `schedulerMutex_` 写日志文件**，一次触发写两行。这正是 T-1 的缺陷本身，出现在最不该出现的线程上，而 T-1 条目没有把它算进去。已修：新增 `TouchholdTransition` 出参，锁内只记录发生了什么，出锁后由 `handleMixerGroupSync` 调 `logTouchholdTransition()` 写出。GUI 路径（`restoreTouchholdVoices`）不持锁，仍直接写。
> - **仍未处理**：`touchholdSample_` / `touchholdOwnerSpanIndex_` 本身的跨线程访问——音频线程持锁操作，GUI 线程的 `pauseTouchholdVoices()` / `restoreTouchholdVoices()` 不持锁操作同一对象。给这一对加锁需要先审计约 15 个调用点是否已持锁（`QMutex` 非递归，且 `restoreTouchholdVoices` 内部还会调 `pauseTouchholdVoices`），本机无法跑音频冒烟测试，未夹带。
> - **原条目建议的"析出 `SfxSchedulerState`"未实施**：这六个字段共 134 处引用，加触摸长按 23 处，是一次覆盖音频热路径 ~150 个调用点的重构，而本仓库没有任何音频行为的自动化测试可以兜底。应作为独立改动、在有 Windows 实机验证的前提下做。

- `handleMixerGroupSync()`（音频线程，持 `schedulerMutex_`）写：`eventGroupIndex`、`lastTriggeredGroupIndex`、`lastTriggeredGroupSecond`、`triggeredGroupCount`、`backgroundTrackPendingStart`、`backgroundTrackRunning`。
- GUI 线程**不持锁**读写同一批字段的例子：
  - `resetCursor()` 直接写 `playbackSession_.eventGroupIndex`（撤防在前，但 `rearmScheduler` 为 false 时不撤防）；
  - `applyLevels()` 在 `disarmSfxScheduler()` **之前**读 `playbackSession_.lastAuthoritativeSecond` 作为实参；
  - `logPreparedEventWindow()` 无锁读 `eventGroupIndex`；
  - `triggerGroup()` → `reconcileTouchholdVoice()` 在音频线程改写 `touchholdOwnerSpanIndex_` 并操作 `touchholdSample_`，而 GUI 线程的 `restoreTouchholdVoices()` / `pauseTouchholdVoices()` 无锁操作同一对象。
- 本分支已经为 `backgroundTrackPendingStart` / `backgroundTrackOffsetSeconds` / `backgroundTrackRunning` 补了三处 `QMutexLocker`（`applyPausedPreviewState`、`pausePreview…`、`isBackgroundTrackRunning`），说明作者意识到了这个问题，但**覆盖是逐点的、不完整的**。
- 建议：不要继续逐字段补锁。把被跨线程共享的那一小组状态从 `playbackSession_` 里**析出**为独立的 `SfxSchedulerState`，规定它只能在 `schedulerMutex_` 下访问；`playbackSession_` 其余部分明确标注为 GUI-thread-only。这样"哪些字段需要锁"就成了类型层面的事实，而不是每个调用点的自觉。

### T-4（高）`PreviewAudioNativeEndpointNotificationClient` 的引用计数非原子 + 析构存在 UAF 窗口

- 位置：`src/audio/PreviewAudioDeviceWatcher.cpp`
  ```cpp
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --references_; if (r == 0) delete this; return r; }
  ...
  ULONG references_ = 1;   // 普通 ULONG，非原子
  ```
  这是一个**明确为跨线程使用而设计**的 COM 对象（Windows 音频服务在 MTA 线程上回调），引用计数必须用 `InterlockedIncrement/Decrement` 或 `std::atomic<ULONG>`。
- 析构顺序：
  ```cpp
  enumerator->UnregisterEndpointNotificationCallback(client);
  client->Release();          // → delete this
  ```
  `Unregister` 不保证正在执行中的 `OnDefaultDeviceChanged` 已经返回。该回调持有裸 `owner_`（即正在析构的 `PreviewAudioDeviceWatcher`）并对其调用 `QMetaObject::invokeMethod(this, …)`。窗口很窄但真实存在，且退出路径上出现的崩溃最难复现。
- 建议：`references_` 改原子；`owner_` 改为在 `Unregister` 后先原子置空（客户端里加一个 `setOwner(nullptr)`），再 `Release`。

### T-5（中高）GUI 线程被 `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` 尝试改公寓模型

- 位置：`PreviewAudioDeviceWatcher` 构造函数（在 GUI 线程执行）。
- 实践中 Qt 已在 Windows 上对 GUI 线程做过 OLE 初始化（拖放需要 STA），所以这里通常返回 `RPC_E_CHANGED_MODE`，代码也正确处理了。但这是在**依赖一个未被断言的时序**：一旦构造顺序变化使得 Qt 尚未 OLE 初始化，GUI 线程就会被置入 MTA，破坏拖放与 Shell 对话框，且没有任何日志。
- 建议：GUI 线程不要调用 `CoInitializeEx`。要么用 `COINIT_APARTMENTTHREADED`，要么完全不初始化（依赖 Qt 已做的初始化）并只在失败时降级；无论如何把 `comResult` 记进日志（与 L-7 合并处理）。

### T-6（中高）`installPeriodicProcessResourceGauge` 在 GUI 线程上每 30 s 建/毁一次 DXGI 工厂，首次调用还在启动关键路径上

- 位置：`src/common/ProcessDiagnostics.cpp` — `emitSample()` 内调用 `sampleAdapterVideoMemory()`，后者每次 `CreateDXGIFactory1()` → `EnumAdapters1` 循环 → `factory->Release()`。
- 两个问题：
  1. **启动路径**：`main.cpp:433` 在 `QApplication` 构造后立刻 `installPeriodicProcessResourceGauge(&app)`，函数体里 `emitSample()` 是**同步先跑一次**再 `timer->start()`。也就是说在 Qt RHI 初始化之前、GUI 线程上创建一次 DXGI 工厂。本分支在 `startup_diagnostics_win32.cpp` 里刚刚以"device creation loads vendor graphics drivers before Qt initializes"为由把 D3D11 探针改成默认关闭 —— 同一份改动的另一半又引入了同类风险。
  2. **稳态**：GUI 线程每 30 s 与两个 QSG 渲染线程争用同一批 DXGI 对象。
- 建议：① 首次采样改为 `QTimer::singleShot(0, …)` 或延后到首帧之后；② 缓存 `IDXGIFactory1` / `IDXGIAdapter3` 指针，不要每次重建；③ 若仍有可观测抖动，把采样搬到一个专用低优先级线程。

### T-7（中高）Watchdog 线程在进程启动最脆弱的时刻调用 `SymInitialize(TRUE)`

- 位置：`UiHangWatchdog.cpp:watchdogLoop()` 首行 `miacode::diag::prepareStackWalkSymbols();`；线程在 `installGuiHeartbeat()`（`main.cpp:432`，QApplication 构造后、Qt 插件加载前）启动。
- `SymInitialize(process, nullptr, TRUE)` 会枚举全部已加载模块并取 **loader lock**，此时 GUI 线程正在加载 Qt 平台插件 / 图形后端 —— 两者争用同一把锁。
- 缓解因素：设置了 `SYMOPT_DEFERRED_LOADS`，符号本身不会立刻加载。但模块枚举与 loader lock 仍在。
- 建议：把 `prepareStackWalkSymbols()` 推迟到第一次收到 GUI 心跳之后（此时事件循环已就绪、插件加载完毕），或改用 `fInvadeProcess=FALSE` + 首次捕获前再 `SymRefreshModuleList()`。函数注释目前的论证（"deferring it to first use is a deadlock risk"）只考虑了"挂起期间取锁"，没有考虑"启动期与 loader 争用"，两者需要一起权衡。

### T-8（中）挂起报告链路可能被它要报告的挂起本身阻塞

- `appendGuiThreadStackReport()` 在 watchdog 线程上最多写 1 + 64 = 65 条 `Level::Fatal` 行；每条都会 `AsyncLogWriter::flush(1000)` + 取 `logMutex()` + open/write/flush/close（`DebugLog.cpp:1026-1038`）。
- 若 GUI 线程正是卡在一次同步 Fatal 写里（磁盘挂起、网络盘断连），它持有 `logMutex()`，watchdog 会在**第一条**报告行上无限期阻塞，冻结报告永远写不出来。
- 建议：为 watchdog 的报告路径提供一条绕过 `logMutex()` 的独立写出口（类似 `oplog` 的 startup beacon 已有的直写机制），或对 `logMutex()` 使用带超时的 `tryLock` 并在超时时降级到 beacon 文件。

### T-9（中）三个线程池的职责划分与实际负载不匹配

现状（`MainWindow.FrameBootstrap.cpp:122-138`，均 `setExpiryTimeout(-1)`，线程常驻）：

| 池 | maxThreadCount | 实际投喂者 |
|---|---|---|
| `previewWarmupPool_` | 2 | `DocumentFileFlow.cpp:351`（开谱面预热）+ `PreviewTimelineFlow.cpp:1454`（`WaveformCacheService` 波形构建） |
| `timelineSlowRefreshPool_` | 1 | `PreviewTimelineFlow.cpp:1020` |
| `timelineAnalysisPool_` | 1 | `TimelineAnalysisFlow.cpp:133` |
| `QThreadPool::globalInstance()` | 默认（≈核数） | `PreviewSceneAssetRepository.cpp:68`（精灵资源加载）+ 上面三处的 nullptr 兜底 |

问题：
1. **波形构建与预览预热共用一个 2 线程池**。整首歌的波形解码可达数秒；两个波形任务就能占满该池，让紧随其后的切谱面预热排队 —— 表现为"切谱面后预览迟迟不出画"。二者的延迟要求完全不同（预热要求低延迟、波形可以慢）。
2. `PreviewSceneAssetRepository` 用全局池，既没有命名（诊断时在线程名里看不出是谁），也不受任何配额约束。
3. 四处 `pool != nullptr ? pool : QThreadPool::globalInstance()` 的兜底在实际运行中不可达（三个池都在构造函数里无条件创建），属于防御性死分支。

建议：
- 给 `WaveformCacheService` 独立的 `waveformBuildPool_`（`maxThreadCount(1)`、命名 `WaveformBuildPool`），`previewWarmupPool_` 专职预热。
- `PreviewSceneAssetRepository` 改用一个命名的专用池。
- 删掉四处 `globalInstance()` 兜底，改为断言。

### T-10（低）栈捕获超时后泄漏一个线程 + 一个内核句柄，且状态不可恢复

- 位置：`ThreadStackCapture.cpp` — 超时时 `worker.detach()`、`g_stackWalkAbandoned = true`、`releaseStackWalkTargetThread()` 里刻意不 `CloseHandle`。
- 这是有意识的权衡，注释也写清楚了（"One leaked thread and one leaked handle in an already-failing process is an acceptable price"），**判断本身认同**。
- 但两点可以更好：① `g_stackWalkAbandoned` 一旦置位就再也无法恢复，即使 dbghelp 后来恢复正常；② 被遗弃的 worker 若解除阻塞，会继续写 `job`（shared_ptr 保活，安全）但不会有任何日志说明它最终结束了。
- 建议：至少让被遗弃的 worker 在完成时写一行 `action=abandoned_worker_completed suspended_us=%1`，这条信息能直接告诉支持人员"dbghelp 只是慢，不是死锁"。

---

## 四·补、Watchdog 自身的两个观测缺口（2026-08-07 capture 暴露）

这两条不在原报告里。它们是在核对"为什么 8-07 的 capture 里有两次多秒 GUI 停顿、却一条 `ui/hang_watchdog` 报告都没有"时定位到的，性质与第一类（日志遗漏）相同：**探针在关键分支上静默**。

### W-1（最高）2–5 s 的 GUI 卡顿落在阈值死区里，完全无日志

- `policy::classify()` 只有两个触发条件：`heartbeatArmed && heartbeatAge >= idleHeartbeatHangMs`（默认 **5000 ms**），或 `phaseActive && activeMs >= activePhaseHangMs`（默认 **2000 ms**）。
- 后者要求有人**显式标记过 phase**。常规播放不标记任何 phase，于是唯一可用的条件就是 5 s。
- 实测：8-07 capture 里两次停顿分别为 **2075 ms** 和 **4683 ms**，两次都卡在 2 s 与 5 s 之间，`ui/hang_watchdog` 全程只有 `action=installed` 和 `action=stack_symbols` 两行。同一时间窗口里渲染线程自己的 `update_paint_node_stats` 已经掉到 10.5 fps / 15.8 fps，主线程 1 Hz 的 `bass_status` 心跳漏拍——**除了 watchdog，每个通道都看见了**。
- 修复：新增 `action=gui_thread_stall`，`edge=began` 在心跳陈旧满 1000 ms 时立即写出（保证进程被杀也留痕），`edge=ended` 带 `stall_ms=`。时长取两次 GUI 心跳时间戳之差，因此是 GUI 线程真实失服务时长，而非 500 ms 轮询窗口。Info 级、不带栈，不改动既有 hang 路径的阈值/节奏/栈预算。判定函数 `classifyHeartbeatStall` 为纯函数并在 `ui_hang_watchdog_policy_spec` 里锁死，spec 里显式钉了 2075 / 4683 这两个真实值。
- 阈值取常量 1000 ms 而非新增 env flag：这是"日志本来就应该有的下限"，不是需要按次调节的旋钮。

### W-2（高）`SymInitialize` 单点失败让整个会话拿不到任何符号

- `SymInitialize(GetCurrentProcess(), nullptr, TRUE)` 的 `fInvadeProcess=TRUE` 会把进程内所有模块当作一个整体枚举，**整体失败**——一个不配合的模块就让全进程无符号。
- 实测：capture 里 `action=stack_symbols sym_attempted=1 sym_ready=0 sym_err=3221225476`。`0xC0000004` 是 `STATUS_INFO_LENGTH_MISMATCH`，一个 NTSTATUS，来自内部查询遗留在 last-error 槽里的值，并不是这个 API 文档化的 Win32 错误码。后果：即使 W-1 修好后 hang 报告真的触发了，栈也全是 `symbol=(nosym)`，等于没有。
- 修复：失败后先 `SymCleanup`，再以 `fInvadeProcess=FALSE` 重试并调 `SymRefreshModuleList`。日志新增 `sym_invaded=` / `sym_invade_err=`：`sym_invaded=0 sym_ready=1` 是"走了回退且可用"，与 `sym_ready=0`（彻底没有符号）是两件事。`SYMOPT_DEFERRED_LOADS` 本就按模块懒加载，所以即使 refresh 失败，句柄仍能解析它够得着的部分。
- **未在本机验证**：这段代码在 `#ifdef Q_OS_WIN` 内，macOS 上不参与编译。需要一次 Windows 构建确认。

---

## 五、建议的处理顺序

下表为原始排期。第 1–3 类已全部实施（见文首状态总表），此处仅保留**尚未处理**的条目，按剩余优先级重排。

| 优先级 | 条目 | 理由 |
|---|---|---|
| P0 | T-2（`schedulerMutex_` 与 BASS 内部锁 ABBA）、T-1（持锁写日志） | 会制造与被调查冻结不可区分的症状；R-5 撤回后这是仅有的两个 P0 |
| P1 | T-3（`playbackSession_` 跨线程竞态）、T-4（COM 引用计数非原子 + 析构 UAF） | 真实数据竞争 / UAF |
| P2 | T-6 / T-7（启动路径上的诊断副作用：GUI 线程建 DXGI 工厂、watchdog 线程 `SymInitialize` 争 loader lock） | 与本分支自己的「诊断不得拖累正常启动」原则冲突 |
| P3 | T-9（波形构建与预览预热共用 2 线程池）、T-5（GUI 线程 `CoInitializeEx` MTA） | 可感知的交互延迟 / 潜在公寓模型破坏 |
| P4 | T-8（挂起报告可被它要报告的挂起阻塞）、T-10（栈捕获超时后不可恢复）、L-14（扩展事件跳过不可见） | 清扫 |
| 附带 | `currentBackgroundImage()` 死链导致的每帧 `toImage()`；导出路径 `&first=inf` → NaN | 均为独立行为变更，需单独决策（见文首补充事实） |
