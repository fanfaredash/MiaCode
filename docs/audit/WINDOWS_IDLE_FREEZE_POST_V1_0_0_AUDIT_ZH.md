# Windows 空闲卡死调研报告：`0.5.2-beta3` → beta2 故障快照

- 更新日期：2026-08-03
- 基线版本：`0.5.2-beta3`，提交 `7e348e96ddb3bfeff32a322bdb1e9694080a58a2`（2026-06-19）
- 故障快照：提交 `30322d31f1c04bc1905fde0cfdb9cde1629edb89`（2026-06-27，提交说明：`Bump prerelease version to beta.2`）
- 审查范围：`7e348e96..30322d31`，共 74 个祖先提交；`src/` 99 个文件有变更（约 8,936 行新增 / 1,840 行删除）。

> 版本元数据差异：故障反馈称为 `1.1.0-beta2`，但该 beta2 发布提交中的 `CMakeLists.txt` 是 `1.0.1-beta.2`。仓库历史中未找到 `1.1.0-beta2` 对应的 CMake 版本记录。本报告以唯一的 beta2 发布提交 `30322d31` 作为故障代码快照；应以用户安装包的 About、文件名和提交哈希进一步核对命名。

## 结论摘要

首要嫌疑是 `12d2d3eb` 在 Windows 上把波形缓存的解码优先切换到 BASS 后形成的链路：打开谱面即可在共享线程池后台启动，完整读取音频文件并完整展开 PCM；同时它自行探测、初始化和释放 BASS 设备，而前台预览音频也维护同一进程内的 BASS 设备。该提交既放大了基线中“全量 PCM 缓冲”的既有内存风险，又引入了与前台 BASS 生命周期未统一串行化的新路径，最符合“打开后不操作，过一段时间卡住/崩溃”。

第二优先级是 `0fa6b90e` 的 QuickShell 预览面板尺寸—视频输出绑定链。它在默认 QuickShell 界面中增加多处尺寸变化处理、0 ms 定时器和原生视频输出 attach/detach；静态审查未证明它会在窗口完全静止时自循环，但一旦 Windows 窗口尺寸、DPI、可见性或嵌入预览面尺寸反复变化，就会在 GUI 线程反复执行布局和媒体绑定。`--quick-shell-beta` 只会额外开启 DComp，并不意味着该 QML 布局链只在该参数下存在。

`470d0e4e` 的 BGM tempo 默认配置是条件候选：需已创建/播放预览音频或发生速率操作，不能单独解释完全未播放的纯空闲卡死。

此前按 `v1.0.0..HEAD` 审查发现的扩展事件总线问题，首次出现在 beta7 之后，时间上不适用于本 beta2 故障快照，已降为排除项。

## 发现的问题

### F-01：Windows 波形缓存新走 BASS 全量解码，并与前台预览共用未统一管理的 BASS 生命周期

- 状态：**P0 / 高可信候选**。静态代码可以确认资源与调用链；是否为本次崩溃根因仍需 Windows dump 和日志闭环。
- 提交记录：`12d2d3eb9988d1b26cbe77cb2f5efaed35e851fa` — `Add waveform alignment diagnostics`（2026-06-24）。该提交对 `WaveformCache.cpp` 新增 Windows BASS 解码、设备管理和诊断，总计改动 364 行。
- 对应版本：基线 `0.5.2-beta3` 不含该 BASS 波形路径；故障快照 beta2（仓库元数据为 `1.0.1-beta.2`，用户反馈名为 `1.1.0-beta2`）已包含。
- 对应模块：波形缓存、后台任务、Windows BASS 音频解码、预览音频。
- 文件路径：
  - `src/common/WaveformCache.cpp`：`decodeMonoSamplesWithBass()`、`ScopedBassWaveformDevice`、`WaveformCacheService::requestWaveform()`。
  - `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`：`MainWindow::TimelineSection::refreshWaveformCache()`。
  - `src/app/mainwindow/sections/document/MainWindow.DocumentFileFlow.cpp`、`src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp`：文档/音轨流程触发刷新。
  - `src/audio/BassPreviewAudioBackend_EngineInit.cpp`：前台预览的 `BassPreviewAudioBackend::initializeAudioEngine()`。
  - `src/audio/BassPreviewAudioBackend.cpp`：前台预览析构时的 `BASS_Stop()` / `BASS_Free()`。

#### 问题触发链路 / 原因

1. 打开谱面、切换音轨、重新载入文档或导出快照流程会调用 `refreshWaveformCache()`。只要音轨存在且内存/磁盘波形缓存未命中，就调用 `WaveformCacheService::requestWaveform()`。
2. 服务通过 `QThreadPool` 启动后台任务；任务先读磁盘缓存，未命中即执行 `buildWaveformDataFromFile()`。因此用户打开后即使不再输入，任务仍会继续运行。
3. beta3 基线已存在一个既有风险：miniaudio 路径将整首音轨解码为 `QVector<float>`，预分配并逐块追加全部 PCM，而非在解码过程中直接归约为波形列。单声道浮点 PCM 的下界约为 `采样率 × 秒数 × 4 字节`；44.1 kHz 的 1 小时音频仅 PCM 就约 606 MiB。
4. `12d2d3eb` 在 Windows 上新增并优先执行 `decodeMonoSamplesWithBass()`。该路径先 `QFile::readAll()` 将完整压缩文件保留在 `QByteArray`，再以 `BASS_StreamCreateFile(TRUE, ...)` 解码，并将全部混音后的 PCM 追加进 `QVector<float>`。因此新路径在既有全量 PCM 占用之外，还会同时持有完整源文件、BASS 解码缓冲和最终波形数据；长音频/高采样率/内存紧张时可造成内存压力、分页和表面上的窗口假死。
5. 新路径在后台线程内调用 `BASS_GetDevice()`；若认为无设备则调用 `BASS_Init(0, ..., BASS_DEVICE_NOSPEAKER)`，退出时调用 `BASS_Free()`。该互斥锁 `bassWaveformDecodeMutex()` 只串行化多个波形任务。
6. 前台预览音频同时在 GUI 侧以 `BASS_Init(-1, ...)`、`BASS_Mixer_StreamCreate()` 建立引擎，使用另一套 `gBassDeviceRefCount` 计数，并在析构中 `BASS_Free()`。两条链不共享锁、引用计数或所有权对象；波形任务和预览引擎初始化/销毁交叠时，BASS 的进程级设备及流操作没有仓库内的统一顺序保证。
7. 因此这是“新代码触发/放大既有漏洞”的组合：基线已有无上限 PCM 缓冲；beta2 新增 Windows BASS 后端、完整文件读取和独立设备生命周期，使其在 Windows 的后台波形构建期间更容易暴露为卡死或访问异常。

#### 建议的验证与修复方向

- 优先收集复现现场的进程私有内存、提交量和线程栈；若后台线程停在 `BASS_ChannelGetData`、`QFile::readAll`、`QVector::resize` / 分页路径，或 GUI 线程等待音频/图形调用，应优先闭环 F-01。
- 启用波形日志后，确认卡死前是否出现 `event=request source=worker`、`event=build decoder=bass samples=... elapsed_ms=...`、`event=worker_done`。记录音轨时长、文件大小、缓存是否命中。
- 将波形生成改为流式归约：每读一块 PCM 即更新目标波形列，不保存整首 `QVector<float>`；对可接受的输入时长、帧数和文件大小设置上限/取消机制。
- 让 BASS 仅由一个明确的进程级所有者初始化、释放和串行调度；波形解码应复用该所有者的受控上下文，或完全改回不触及 BASS 设备状态的解码器。不能仅保留波形任务内部的 mutex。
- 回归覆盖：无磁盘缓存的长音频、快速打开/关闭文档、预览引擎初始化与波形任务交叠、内存受限环境，以及同一音轨重复请求合并。

### F-02：QuickShell 预览面板的几何变化会同步驱动布局、定时器与视频输出重新绑定

- 状态：**P1 / 中等可信候选**。存在可达的 GUI 线程高频链，但静态审查尚未证明“完全静置”时必然自循环。
- 提交记录：`0fa6b90eaf0d4b4e3232f8306e1e1dfdcd0f13e3` — `Fix QuickShell preview startup sizing`（2026-06-25）。改动 `QuickShellMain.qml` 157 行、`QuickShellPreviewSurface.qml` 52 行、`PreviewDCompSurface.cpp` 19 行。
- 对应版本：基线 `0.5.2-beta3` 不含此次改动；故障 beta2 已包含。
- 对应模块：QuickShell 主界面布局、嵌入式预览面、Qt Quick 视频输出、DComp 预览表面。
- 文件路径：
  - `src/app/quick_shell/qml/QuickShellMain.qml`：`syncPreviewPaneWidth()`、`embeddedInlineSurfaceActivateTimer`、`requestEmbeddedInlineSurfaceActivation()`、`workspaceRow.onWidthChanged/onHeightChanged`。
  - `src/app/quick_shell/qml/QuickShellPreviewSurface.qml`：`geometryLogTimer`、`syncVideoOutputBinding()`、`attachVideoOutputObject()` / `detachVideoOutputObject()`。
  - `src/render/backend_d3d11/PreviewDCompSurface.cpp`：追踪预览目标的延后重试。
  - `src/app/main.cpp`：`QuickShellBootstrap` 负责 GUI 启动；`--quick-shell-beta` 会额外设定 `MIACODE_PREVIEW_USE_DCOMP=1`。

#### 问题触发链路 / 原因

1. GUI 启动后会由 `QuickShellBootstrap` 建立 QuickShell。`workspaceRow` 的宽/高变化直接调用 `syncPreviewPaneWidth()`，该函数会重算 `previewPaneWidth`、最小/最大尺寸和持久化尺寸选择。
2. 预览帧尺寸变化会调用 `requestEmbeddedInlineSurfaceActivation()`，重启一个 16 ms 的单次定时器。定时器在窗口可见、启动布局完成且预览帧至少 `64×64` 时激活嵌入预览 Loader。
3. `QuickShellPreviewSurface.qml` 对 `x/y/width/height` 的每次变化重启 0 ms 单次定时器；触发后调用 `syncVideoOutputBinding()`，按可见性、尺寸和宿主窗口状态执行 `attachVideoOutputObject()` 或 `detachVideoOutputObject()`。
4. 这条链还包含 `WindowContainer` 的原生窗口尺寸同步，以及 DComp 目标发现的 0/50/150 ms 延后重试。Windows 的 DPI 变化、显示器切换、最小化/恢复、原生子窗口可见性变化，或预览面宽度被约束反复修正时，可能形成大量 GUI 线程布局和视频输出绑定工作。
5. 审查未找到一个不依赖外部几何变化的显式无限 restart；所以不能把它断言为纯空闲死循环。但 beta2 在该处显著增加了触发边和副作用，且视频输出绑定涉及 Qt 多媒体/原生窗口边界，应作为不含长音轨时的首要排查项。

#### 建议的验证与修复方向

- 复现时记录是否经历显示器/DPI/锁屏恢复、最小化恢复、全屏切换和拖拽窗口；同时记录是否使用 `--quick-shell-beta` 或显式 DComp 环境变量。注意：未使用参数不能排除本 F-02 的 QML 布局部分，只能排除其中由该参数开启的 DComp 分支。
- 卡死日志中统计 `preview_pane_layout`、`preview_surface_geometry_changed`、`preview_surface_video_output_attach/detach` 和 DComp target retry 的频率；每秒持续增长即支持该链。
- 修复上将“仅记录日志”的几何定时器与“改变视频绑定”的动作分离；对相同几何/宿主状态做幂等去重和帧级节流；限制 DComp 目标发现重试次数，并在窗口不可见时取消。
- 回归覆盖：多显示器 DPI 切换、反复最小化/恢复、全屏进出、预览视频加载/卸载，以及静置 30 分钟后的 GUI 心跳和 attach/detach 计数。

### F-03：默认 BGM tempo 窗口配置扩大了 BASS_FX 预览音频的状态复杂度

- 状态：**P2 / 条件候选**。与“已播放预览后再静置”有关；不适用于从未创建或播放预览音频的复现。
- 提交记录：`470d0e4e9923e434ca4e097e405008e3402d0c13` — `Default BGM preview to compact tempo mode`（2026-06-24）。
- 对应版本：基线 `0.5.2-beta3` 不含此默认配置；故障 beta2 已包含。
- 对应模块：BASS 预览音频、BASS_FX tempo 流、BGM 资源创建。
- 文件路径：
  - `src/audio/BassPreviewAudioBackendImpl.h`：tempo 窗口配置及默认 `compact40` 参数。
  - `src/audio/BassPreviewAudioBackendSample.h`：创建 tempo 流后写入 sequence/seek/overlap 属性。
  - `src/audio/BassPreviewAudioBackend_Assets.cpp`：BGM sample 创建和默认 speed mode 选择。

#### 问题触发链路 / 原因

1. 创建背景音乐 sample 时，beta2 默认选用 tempo 模式并创建 BASS_FX tempo 流。
2. 随后向该流写入 sequence、seek window、overlap 三个参数；预览播放、暂停、seek 或速率变化会进入同一 BASS mixer / tempo 流状态机。
3. 该提交本身没有显示纯空闲时的轮询或阻塞等待，因此不能单独解释“打开后完全未播放即卡死”。但它增加了 beta2 与 beta3 之间预览音频后台状态的差异；若崩溃前曾播放、暂停或调速，应与 F-01 同时检查 BASS 线程栈和错误码。

#### 建议的验证与修复方向

- 将复现分为“只打开谱面，不播放”和“播放/暂停/seek 后静置”两组。仅第二组出现时，提高 F-03 优先级。
- 在 BASS 调用边界记录流句柄、tempo 参数、播放状态和错误码；卡死 dump 中确认 GUI/音频相关线程是否停在 BASS_FX 或 mixer 调用。
- 将 tempo 属性改写约束在样本未被 mixer 拉取的安全窗口，并为创建、销毁、速率变更和波形 BASS 解码建立统一的所有权策略。

## 已排除或降级的项

### X-01：扩展运行时事件总线问题不适用于 beta2

- 提交记录：`35f6ad90` — `feat(extensions): add unified runtime event bus`（2026-08-03）。
- 对应版本：`1.1.0-beta.7` 及之后，不在 `0.5.2-beta3..30322d31` 范围。
- 对应模块 / 路径：`src/extensions/EmbeddedExtensionRuntime.cpp`。
- 结论：其中“JavaScript 回调重入时持有 `QVector` 迭代器”和“无批次总时间预算”仍是当前分支的独立风险，但发生时间晚于 beta2，不能解释本次故障版本。

### X-02：基线已有的全量 PCM 缓冲不是 beta2 独有根因，但被 F-01 激活并放大

- 提交记录：基线 `7e348e96` 中的 `src/common/WaveformCache.cpp` 已有 miniaudio 全量 `QVector<float>` 解码；`12d2d3eb` 在 Windows 上新增 BASS 优先路径。
- 对应版本：基线与 beta2 均存在前半部分；beta2 新增的完整源文件读取、BASS 解码和独立 BASS 设备生命周期构成差异。
- 对应模块 / 路径：`src/common/WaveformCache.cpp`。
- 结论：不能只因基线已有全量缓冲而排除它；本次正是“后续改动接通了既有脆弱点”的典型情况。

## 建议的现场取证顺序

1. 核实安装包实际版本、提交哈希、是否带有 `--quick-shell-beta` / DComp 环境变量，并区分“无响应”与“仅预览停住”。
2. 用同一谱面做两组：仅打开后静置；打开后播放、暂停、seek 再静置。记录音轨时长、采样率、文件大小、波形缓存是否已存在。
3. 复现时收集 Windows dump、GUI 线程栈、工作线程栈、私有内存/提交量曲线；优先查找 `BASS_ChannelGetData`、`BASS_Init/Free`、`QFile::readAll`、波形 `QVector` 扩容及 Qt Quick/媒体绑定栈。
4. 开启波形、预览交互、BASS 调试日志，按 F-01/F-02/F-03 中的事件名核对卡死前最后一段活动。若内存稳定且无波形 worker，再转查 QuickShell 尺寸/attach-detach 计数。

## 审查边界

本报告是提交历史、beta2 快照和跨模块调用链的静态调研，不包含二进制构建、真实 Windows 压力复现或 dump 解析。问题优先级表示代码证据与症状匹配度；最终根因须以故障机日志、线程栈和内存曲线闭环确认。
