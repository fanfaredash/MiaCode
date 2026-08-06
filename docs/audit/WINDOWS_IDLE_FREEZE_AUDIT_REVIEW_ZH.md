# Windows 空闲卡死审计复核 + 复现验证方案

- 复核日期：2026-08-04
- 复核对象：`docs/audit/WINDOWS_IDLE_FREEZE_POST_V1_0_0_AUDIT_ZH.md`
- 复核基准：当前 `dev` 分支 `677a9625`（`1.1.0-beta.7`）
- 症状原文：**打开一段时间，放着它不管，也会卡死**

---

## 0. 复核结论摘要

原审计的**代码事实描述基本准确**（F-01、X-01 我逐条验证过，成立）。问题不在读代码，在**选错了代码范围**和**没有按症状筛选嫌疑对象**。

三个结论：

1. **范围错误（最严重）。** 原审计把用户报告的 `1.1.0-beta2` 映射到 `1.0.1-beta.2`（`30322d31`，2026-06-27）。但 `1.1.0` 线从 2026-07-07 才开始，且 CHANGELOG 明确写明 `1.1.0-beta` 合并了 `1.0.1-beta.3/4/5-test`——两者是**前后相继的两条线**，`1.0.1-beta.2` 还要再往前一条。据此审计只看了 74 提交 / 99 文件 / ~8.9k 增行，**排除掉了 257 提交 / 385 文件 / ~44.6k 增行**，是它实际审查量的 5 倍。X-01 正是被这个错误前提排除的。

**代价是具体的：** 被排除的区间里就有 `41cf08a8`（2026-07-04，`Enable high-performance GPU defaults`）——把高性能 GPU 绑定翻成默认开启，首个用户可见版本 `1.1.0-beta`（07-07）。它只在双显卡机上生效，而代码注释至今仍写着该绑定「在双显卡机器上验证之前是 opt-in」。**症状形状、硬件条件、版本窗口三者同时收敛，且全部落在审计视野之外。** 详见 O-1。

2. **F-01/F-02/F-03 都不匹配「空闲」这个时间特征。** F-01 在打开时跑、有界、会跑完；F-02 审计自己承认静置时不自循环（我已证实确实不会）；F-03 需要播放过。原审计是按「最近改过 + 看着危险」排序，而不是按「没人碰它的时候，什么还在跑 / 什么状态还在变」排序。**空闲期行为体清单从未被列举。**

3. **漏掉了仓库里已有的挂起检测设施，以及它的盲区。** `src/common/UiHangWatchdog.cpp` 存在，但**它检测不到这个 bug**（见 O-3）。这既是审计遗漏，也是让此问题可诊断的最高性价比改动。

**我没有证明根因。** 本文做的是：修正范围、下调两项结论、补出按症状排序的候选集，并给出把它闭环所需的埋点与复现方案。

---

## 1. 原审计中经验证成立的部分

| 项 | 验证结果 |
|---|---|
| F-01 BASS 波形链路 | **成立**。`WaveformCache.cpp:299` `decodeMonoSamplesWithBass()`；`:309` 独立 mutex；`:319` `file.readAll()` 全文件驻留；`:325` `BASS_StreamCreateFile`；`:350` 整轨 `QVector<float>` 预留。设备自管：`:148` `BASS_GetDevice()` / `:153` `BASS_Init(0,…,NOSPEAKER)` / `:160` `BASS_Free()`。与前台预览 BASS 生命周期确实无统一串行化。 |
| F-01 是否重复解码 | **否**，需澄清。`:404` BASS 成功即 `return`（`:409`），miniaudio 只是回退，不存在两份整轨 PCM 同时驻留。 |
| X-01 悬垂迭代器 | **成立且比审计描述的更严重**，见 O-4。 |
| F-02 「未能证明静置自循环」 | **审计的谨慎是对的**。我已确认 `syncVideoOutputBinding()`（`QuickShellPreviewSurface.qml:60-101`）幂等：宿主/输出未变即早退，且**不修改自身几何**，所以 `:138` 的 0 ms `geometryLogTimer` 无法自喂。**F-02 应下调**，它需要外部几何振荡源。 |

---

## 2. 审计的关键问题

### P-1 版本锚点错误 → 审查范围错误

证据链：

- 版本时间线（由 `CMakeLists.txt` 历史逐提交还原）：`1.0.1-beta.2` = `30322d31`（06-27）；`1.0.1-beta.5-test` 到 07-07；**`1.1.0-beta` 自 07-07 起**，随后 `beta.3`(07-09) → `beta.4` → … → `beta.7`。
- `CHANGELOG.md` 的 `1.1.0-beta` 条目原文说明其合并了 `1.0.1-beta.3-test`、`1.0.1-beta.4-test`、`1.0.1-beta.5-test`。
- About 显示串 = `MIACODE_DISPLAY_VERSION_STRING` = `MIACODE_VERSION_STRING`，两者在整个区间内恒等。所以 `30322d31` 的用户看到的是 `1.0.1-beta.2`，**不可能显示成 `1.1.0-beta2`**。

审计在脚注里承认了歧义（这点值得肯定），但随后**用歧义最远的那个解释**（差一整条版本线、早 10 天）支撑全文，并据此排除 X-01。正确处理是：要么先取证锁定版本，要么对 1.1.0 线并行分析。

> `1.1.0-beta.2` 在 CMake 里确实不存在（`beta` 之后直接跳 `beta.3`）。所以用户串对不上任何提交，**这本身就是必须先向用户取证的信号**，而不是可以自行挑一个替代值的理由。

### P-2 未按症状筛选

「打开→静置→卡死」要求嫌疑对象满足以下之一：**空闲期仍在跑**、**被环境事件触发**、或**随时间单调累积**。F-01（打开时一次性、有界、会结束）、F-03（需播放）都不满足；F-02 需要外部驱动。原审计没有做这一步筛选。

### P-3 日志取证方案不可靠

审计建议「开日志看卡死前最后一段活动」，但：`DebugLog.cpp:554` 队列上限 `kMaxQueueSize=4096` 且 `:475-481` **丢最旧**；文件 4 MB 轮转、仅保留 3 段。长时间静置 + 任一啰嗦通道，卡死前的窗口很可能已被轮转/丢弃掉。只有 `Level::Fatal` 走**同步持久化刷盘**。所以取证必须：`MIACODE_LOG_DIR` 指到宽裕路径、关注 `dropped=` 标记、并让关键埋点用 `Fatal` 级别。

---

## 3. 审计遗漏的发现

按与「空闲卡死」的匹配度排序。

### O-1（P0，首要候选）高性能 GPU 绑定默认开启 —— 且版本窗口精确收敛

`MIACODE_GPU_BIND_HIGH_PERFORMANCE` **默认 ON**，`bindHighPerformanceQuickGraphicsDevice()` 把根 Quick 窗口通过 `QQuickGraphicsDevice::fromAdapter` 绑到解析出的高性能 DXGI 适配器。

#### 版本收敛（这是把它排在第一的理由）

| 提交 | 日期 | 作用 |
|---|---|---|
| `c87ae83f` | 2026-07-04 | 引入该 flag，**默认关**（`envFlagEnabled`，opt-in） |
| `41cf08a8` | 2026-07-04 | `Enable high-performance GPU defaults` —— 改为 `envOptionalFlagValue` + `return true`，**默认开** |

翻转时 CMake 版本为 `1.0.1-beta.5-test`（内部测试号）。按 CHANGELOG 的合并规则 `-test` 版不单列，故**首个带此默认值的用户可见版本是 `1.1.0-beta`（2026-07-07）**；CHANGELOG 的 `1.1.0-beta` 条目亦明写「默认启用高性能 GPU 偏好」。

**这正落在用户报告的 `1.1.0-beta2` 那条线上，也正落在原审计排除掉的 257 个提交之内。**

#### 代码中可确认的事实

1. **作者自述该默认值未经验证。** [`gpu_device_provider.cpp:109`](../../src/app/gpu_device_provider.cpp#L109) 的注释至今仍写着 `fromAdapter binding is opt-in until it is validated on a dual-GPU machine`，而 `41cf08a8` 已把默认翻成开。**按注释自己陈述的前提条件，默认开启的条件并未满足**，注释与代码处于矛盾状态。
2. **仅在双显卡机器上生效。** [`:150-160`](../../src/app/gpu_device_provider.cpp#L150) 有 no-op 短路：高性能适配器 LUID == DXGI 默认适配器即跳过。单显卡机器完全不受影响。**唯一会生效的硬件，恰好就是注释所说「尚未验证」的那一类。**
3. **生效时一个进程内跑着两块 GPU。** [`:94-107`](../../src/app/gpu_device_provider.cpp#L94) 预览合成表面（`preferVideoShareDevice=true`）**必须留在 Qt 默认适配器**（核显），因为 D3D11VA 桥是同适配器限定；而根窗口（[`QuickShellBootstrap.cpp:377`](../../src/app/quick_shell/QuickShellBootstrap.cpp#L377)）被绑到独显。两个 Quick 窗口、两个 RHI device、两块适配器。[`:110-112`](../../src/app/gpu_device_provider.cpp#L110) 的注释本身即警告根窗口也嵌入解码视频，`carries the same risk`。

#### 推断部分（假设，未证）

混合显卡笔记本会对独显做电源管理，长时间无提交即驻留/降功耗；显示器休眠与驱动 TDR 复位同样会使 D3D device 失效。设备丢失后需重建，而本仓库**没有任何应用层响应**（即 O-2）。渲染线程若滞留在失效 device 上，GUI 线程会阻塞于 QSG sync，表现为整窗无响应且 CPU ≈ 0。两块适配器意味着两份彼此独立的丢失风险。

> **已排除的机制：** keyed-mutex `AcquireSync(INFINITE)` 冻结路径**已修复**——`third_party/QtAVPlayer/.../qavhwdevice_d3d11.cpp:711` 现为 `kPreviewAcquireSyncTimeoutMs = 180` 的有界等待，超时丢帧。不要再把它当作本问题的机制。

#### 零成本分诊

`startup/gpu_provider` 日志行的 `action=bound|skip` 直接回答「这台机器是否受影响」：

- `action=skip … reason=high_perf_equals_default_adapter` → 单显卡，**当场排除 O-1**
- `action=bound source=from_adapter adapter="…" luid=… default_luid=…` → 双显卡且已绑定，O-1 在场

向反馈用户索取这一行的成本几乎为零，应放进 Phase 0。A/B 开关：`MIACODE_GPU_BIND_HIGH_PERFORMANCE=0`。

### O-2（P0/P1）完全没有处理任何 Windows 环境/会话事件

全仓 `src/` 搜不到 `WM_POWERBROADCAST`、`WM_DISPLAYCHANGE`、`WM_WTSSESSION_CHANGE`、`WM_DEVICECHANGE` 的任何处理（唯一的 `QAbstractNativeEventFilter` 是 `QuickShellBootstrap.cpp:134` 的关闭过滤器）。也没有 `SetThreadExecutionState` 之类的电源意图声明。

后果需要分成两类，不能混为一谈。

#### 确定的后果：诊断盲

因为没有任何代码观测电源 / 显示 / 会话转换，日志里**不存在**「T 时刻显示器休眠」「T 时刻会话锁定」「T 时刻适配器复位」这类标记。于是即便拿到 dump，也**无法把卡死与触发它的环境事件对应起来**。

与 O-3（看门狗对空闲期完全失明）叠加，结论是：**该故障目前光靠日志无法定位。** 这也正是原审计「开日志看卡死前最后一段活动」那套取证方案会落空的根本原因——不是日志开得不够，是**根本没有任何东西记录环境转换**。

#### 可能的后果：无恢复路径（未证）

GPU 设备丢失后没有任何应用层重建逻辑。Qt 的 RHI 能否自行恢复取决于具体版本与场景；若恢复不了，应用既无兜底也无一行日志，表现即静默冻死。此条须由 Phase 3 的线程栈确认，不应先当作结论。

#### 旁证：仓库其实知道会话转换有敌意，但没落到主路径

[`OwnerHwndTracker.cpp:66`](../../src/render/backend_d3d11/OwnerHwndTracker.cpp#L66) 的注释专门写了 `RDP detach silently drops the hook on some Windows builds`，并为此加了 1 Hz 看门狗重注册。但该类**只在 `CMakeLists.txt:758-759` 被编译，全仓无任何调用点**——是随出进程预览 worker 一起废弃的死代码。这个认识存在过，却从未落到当前主路径上。

### O-3（P0，可观测性）挂起看门狗检测不到空闲挂起

`src/common/UiHangWatchdog.cpp`：250 ms GUI 心跳 + 500 ms 看门狗线程，超过 2 s 未结束的 phase 会上报。

**但 `watchdogLoop()` 在 `:99-101` 有 `if (!phase.active) { continue; }`。** 它只在 `MIACODE_HANG_PHASE` 作用域内才可能触发。全仓 21 处埋点全部位于导出流程、时间轴布局、窗口/表面操作——**全是用户主动操作**。空闲时没有任何 phase 处于 active，因此空闲卡死会产出**零条**看门狗记录。

`heartbeatAge` 在 `:97` 已经算出来了，却只作为**报告里的一个字段**，从未用作触发条件。

这是本次调查的**首要修复项**：不修它，任何复现都拿不到证据。

### O-4（P1）事件总线：写穿悬垂迭代器（UAF）+ 可无限自喂

即审计的 X-01，但被错误前提排除，且描述偏轻。精确形态（`src/extensions/EmbeddedExtensionRuntime.cpp`）：

- `:1653` `callbackIt` 是指向 `QVector<EventCallback> eventCallbacks_` 的迭代器；
- `:1662` **调用进 JavaScript**；
- `:1663-1681` **继续通过 `callbackIt` 写入**（`lastDurationNs`、`++delivered`、`suspended = true`）。

回调里可达的 JS 能触发 `miacode.events.on(...)` → `registerEventCallback` → `:1751` `eventCallbacks_.append()`（**扩容重分配**），或 `dispose()` → `unregisterEventCallback` → `:1757` `erase(remove_if(...))`（**元素前移**）。两者都会让 `callbackIt` 失效，而代码随后**对它执行写操作** → 堆破坏。这类损坏通常在很久之后才以挂起或崩溃浮现，与「跑一段时间后出事」高度吻合。

另外 `:1684-1685`：只要本轮 flush 又产生了待发事件，就以 **0 ms** 重新武装定时器，**没有整批时间预算**。唯一护栏是单订阅「连续 5 次超过 16 ms」——对「每次都很快但永不结束」的自喂环完全无效。GUI 线程被钉在 100%，表现与卡死无异。

诚实的限定：真正空闲时，发射源（`ExtensionManager.cpp:1790-1819` 的 `document.onDidChangeText` / `workspace.onDidSaveDocument` / `timeline.onDidSeek` / `preview.onDidChangeState`）都是用户驱动的。**所以这是确凿的 bug，但还不是已证的空闲触发路径**，且需要装有扩展。

### O-5（P2）随包发布的 pet-overlay 扩展会拉起常驻 `powershell.exe`

`dist/…/extensions/miacode-pet-overlay/` 在 `onStartupFinished` 激活；若其状态文件 `autoStart` 为真，`main.js:24` 通过 `miacode.experimental.invoke("process.spawn")` 启动 `powershell.exe -File pet-window.ps1`（权限含 `process.manage`）。

两个问题：

1. 需要核实宿主是否读取/排空子进程 stdio。未排空的管道或只进不出的缓冲是典型的「越放越糟」故障。
2. **它不在 git 里。** 仓库只跟踪 `resources/extensions/bundled/miacode-mine-skin-toggle/`。随包发给用户、却无法 bisect 的组件，本身就是复现与追责的障碍，应当先补进仓库或明确从发布物剔除。

### O-6（P2）脏文档时，自动保存每 2 分钟在 GUI 线程上干活

`MainWindowShared.h:55-57`：`kAutosaveIntervalMs = 2*60*1000`、`kAutosaveHistoryMaxVersions = 30`、`kAutosaveLatestIdleMs = 2*1000`。定时器仅在文档变脏时启动（`DocumentEditorState.cpp:478-484`），`runAutosaveCheck(true)` 在 GUI 线程执行。

历史被裁剪到 30 份，所以**有界**，不是泄漏。但它是「用户放着不管」时**唯一确定按时触发**的重活。价值在于它给出一个可证伪的时间特征：**卡死是否落在约 2 分钟的整数边界上？**

### O-7（P2，可观测性）泄漏计量表看不见空闲期泄漏

`preview/resource_gauge` 与 `timeline/leak_gauge` 都是**每次用户暂停时采一次**，没有周期采样。（dev-guide 记录 beta7 曾出现 30–44 MB/s 播放时长的泄漏。）空闲时没人暂停，于是一个样本都不会产生。

结果：**当前没有任何周期性内存/句柄采样**，光看日志无法区分「渐进泄漏→换页/OOM 卡死」「瞬时死锁」「GUI 线程空转」。这是第二个必须补的埋点。

### O-8（次要，但补完了 F-02 的未决问题）确实存在不依赖几何变化的自重启定时器

`QuickShellMain.qml:908` 的 `fullscreenControlsHideTimer`，在 `fullscreenHoveringRevealZone || fullscreenHoveringControls` 时对自己 `restart()`。若应用停在**全屏预览且光标停在控件上**，这是永久的 1.2 s 自循环。开销极低，**不是卡死原因**，但它回答了 F-02 遗留的问题，并说明「全屏下放置」是一个需要单独测试的空闲态。

另外，F-02 式振荡若真存在，最可能的源头是 `previewPaneWidth` ↔ `controller.setPreviewPaneWidthRatio` 的 QML↔C++ 往返（`QuickShellMain.qml:532` 写、`:520` 读）。我**未能**静态证明它收敛或发散，仅标注为需要实测的点。

---

## 4. 修正后的候选排序

| 排序 | 候选 | 症状匹配 | 版本窗口 | 现成对照手段 |
|---|---|---|---|---|
| 1 | **O-1 高性能 GPU 绑定（双显卡）** | 高 | **精确收敛：默认开始于 `1.1.0-beta`（07-07），正是用户报告的线** | `MIACODE_GPU_BIND_HIGH_PERFORMANCE=0`；`startup/gpu_provider` 一行即可分诊 |
| 2 | O-2 显示器休眠 / 会话锁 / TDR 无处理 | 高 | 一直存在 | 复现矩阵 B/C |
| 3 | O-4 事件总线 UAF + 0 ms 自喂 | 中（需装扩展） | 禁用全部扩展 |
| 4 | O-6 自动保存 2 分钟节拍 | 中（需脏文档） | 先保存成干净文档 |
| 5 | F-01 波形 BASS 全量解码 | 低（打开时、有界、会结束） | 用已有磁盘波形缓存的谱面 |
| 6 | O-5 pet-overlay 常驻 PowerShell | 低（需开 autoStart） | 删除该扩展目录 |
| 7 | F-02 QuickShell 几何链 | 低（已证不自喂） | — |
| 8 | F-03 BGM tempo | 低（需播放） | 复现矩阵 A 不播放 |

---

## 5. 复现与验证方案

### Phase 0 — 锁定版本（阻塞项，成本极低）

在动任何代码之前，向反馈用户取回：

1. About 对话框里的**完整版本串**；
2. 安装包**文件名**（`MiaCode-v<version>-win64.zip`）；
3. 日志里的 `startup/process_identity` 行（回答「到底跑的哪个 exe」）；
4. **日志里的 `startup/gpu_provider` 行** —— 见下方分诊，成本几乎为零；
5. 是否使用 `--quick-shell-beta`，是否设过任何 `MIACODE_*` 环境变量；
6. Windows 版本号、GPU 型号（**是否双显卡**）、显示器休眠/锁屏设置；
7. 是否启用了任何扩展（尤其 pet-overlay 的 autoStart）。

**O-1 即时分诊**（第 4 项）：

- `action=skip … reason=high_perf_equals_default_adapter` → 单显卡，**当场排除 O-1**，直接跳到 O-2 / O-4 / O-6
- `action=bound source=from_adapter adapter="…" luid=…` → 双显卡且已绑定，**O-1 在场，Phase 4 优先跑它的 A/B**
- `action=skip … reason=bind_disabled_by_env` → 用户自己关过，排除

这一行能在收到反馈的当天就把候选集砍掉一半，应当先于任何复现执行。

把版本串映射回提交后再继续。**在此之前，任何「beta2 = 某提交」的断言都不成立。**

### Phase 1 — 先把空闲挂起变成可观测（两处小改，都 `--debug` 门控）

不做这一步，后面每一次复现都只会得到「卡了，但没日志」。

**1-A：给看门狗加空闲触发。** 在 `watchdogLoop()` 中，除现有的 phase 判定外，增加一条：`heartbeatAge` 超过阈值（建议 5 s）即上报，**不要求 `phase.active`**。`heartbeatAge` 在 `:97` 已算好，目前被丢弃。上报走 `Level::Fatal`，以命中同步持久刷盘路径，确保记录能在卡死中存活。

这一改动把「卡死且无日志」变成「T 时刻卡死，心跳已停 X 毫秒」。

**1-B：加周期性资源采样。** 用慢定时器（建议 30 s）周期发射 `miacode::diag::processResourceGaugePayload()`，而不是只在暂停时采。复用 `src/common/ProcessDiagnostics.h` 既有 API，落到 Runtime 通道。这样渐进泄漏会呈现为一条可读的曲线。

两处都复用现有设施，不新增环境变量（符合仓库对 `MIACODE_*` 数量的约束）。

### Phase 2 — 复现矩阵（务必让机器真正空闲）

关键点：多数人「测空闲」时屏幕常亮、还会动鼠标，**这恰好规避了最可能的触发源**。

统一前置：Release 构建、加 `--debug`、`MIACODE_LOG_DIR` 指向宽裕磁盘路径。

**硬件要求：必须在双显卡笔记本上跑。** O-1 的绑定在单显卡机上是 no-op（`reason=high_perf_equals_default_adapter`），在这类机器上无论放置多久都不可能复现 O-1。若手头只有单显卡机，先做 Phase 0 分诊确认用户那台是否双显卡——如果是，而复现机不是，那么复现失败**不能**用来排除 O-1。

| 组 | 条件 | 时长 | 针对 |
|---|---|---|---|
| A | 打开谱面，不播放，禁用显示器休眠 | 2 h | 基线 |
| B | **同 A，但显示器休眠设 5 分钟，允许休眠/唤醒** | 2 h | **O-1 / O-2（最高价值）** |
| C | 同 A，`Win+L` 锁屏，30 分钟后返回 | 30 min+ | O-2 |
| D | 有未保存修改，放置 | 2 h | O-6（看是否落在 2 分钟边界） |
| E | 全屏预览，光标停在控件上 | 2 h | O-8 |
| F | 播放→暂停→放置 | 2 h | F-03 |

先跑 B。A 组的作用是提供对照，不是先跑完 A 再跑 B。

### Phase 3 — 卡死当场分类（决定性步骤，别急着杀进程）

冻住的那一刻，先分类再取 dump。三类故障需要完全不同的后续证据：

```powershell
Get-Process MiaCode | Select-Object CPU, PrivateMemorySize64, HandleCount, Threads
```

- **单核 ~100%** → **GUI 线程空转**（→ O-4 自喂环，或某个 QML 定时器环）
- **CPU ≈ 0** → **死锁 / 阻塞等待**（→ O-1 / O-2 渲染线程卡在已驻留的适配器上，或某个互斥量）
- **Private Bytes 高且仍在涨、缺页很多** → **内存/换页**（→ F-01 类）

然后取**完整** dump（内存类必须有堆，minidump 不够）：

```powershell
procdump -ma (Get-Process MiaCode).Id C:\miacode-freeze.dmp
```

WinDbg 里 `~*k` 打所有线程栈。判别性栈帧：

| 栈特征 | 指向 |
|---|---|
| GUI 线程在 `QSGRenderThread` / sync / `WaitForSingleObject`，渲染线程在 `Present` / DXGI | O-1 / O-2 |
| 任一线程在 `EmbeddedExtensionRuntime::flushPendingEvents` 或 `QJSEngine` | O-4 |
| `BASS_ChannelGetData` / `QFile::readAll` / `QVector` 扩容 | F-01 |
| GUI 线程在 `runAutosaveCheck` | O-6 |

同时收 `.miacode/logs/` 全量，特别关注 `dropped=` 标记（说明证据窗口可能已丢）。

### Phase 4 — 用现有开关做 A/B（都不需要重新编译）

每组都必须跑**相同的空闲时长**，且优先在 B 条件（允许显示器休眠）下跑，否则对照无意义。

| 开关 | 验证 |
|---|---|
| `MIACODE_GPU_BIND_HIGH_PERFORMANCE=0` | **O-1，直接对照。** 仅在 Phase 0 分诊显示 `action=bound` 的机器上有意义；单显卡机上该开关本就是 no-op，跑了也说明不了任何问题 |
| `MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP=1` | 渲染移到 GUI 线程；若卡死消失或形态改变 → 渲染线程相关（O-1/O-2） |
| 禁用全部扩展 | O-4 / O-5 |
| 保存文档使其干净 | O-6 |
| 确认未加 `--quick-shell-beta`、`MIACODE_PREVIEW_USE_DCOMP` 未设 | 排除 DComp 分支（默认已关） |
| 换用已有磁盘波形缓存的谱面 | F-01 |

### Phase 5 — 定位后的回归护栏

根因确定后，至少补上：空闲 30 分钟的 GUI 心跳存活检查；显示器休眠/唤醒往返；会话锁定/解锁往返；双显卡机型上的适配器绑定 A/B。若命中 O-4，另需一个「事件回调内注册/注销订阅」的单测——当前 `src/tools/extensions/ExtensionRuntimeSpec.cpp` 没有覆盖重入路径。

---

## 6. 复核边界

本文同样是静态调研 + 版本历史考证，**未包含 Windows 实机复现、dump 解析或构建验证**。

请严格区分本文里的三类陈述：

- **代码/历史中直接确认的事实** —— O-1 的版本翻转时点与首个用户可见版本、`gpu_device_provider.cpp:109` 注释与默认值的矛盾、单显卡 no-op 短路、根窗口与预览表面分处两块适配器；O-3 看门狗的 `phase.active` 门控；O-4 的悬垂迭代器与 0 ms 无预算重武装；O-7 计量表只在暂停时采样；O-2 的诊断盲。
- **有强收敛证据但未证的因果** —— O-1 从「独显电源管理 / 设备丢失」到「整窗冻死」的那一段机制。版本、硬件条件、症状形状三者收敛使它成为首要候选，但**收敛不等于证明**。
- **已排除的机制** —— keyed-mutex `AcquireSync(INFINITE)` 冻结路径已修复为 180 ms 有界等待，不要再据此推理。

根因仍须由 Phase 3 的线程栈与 Phase 4 的 A/B 闭环确认。在 `startup/gpu_provider` 显示 `action=bound` 之前，O-1 对具体某台机器甚至不成立。
