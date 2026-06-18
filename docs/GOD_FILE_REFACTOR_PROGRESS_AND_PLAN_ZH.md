# 上帝文件拆分 —— 进度、方法论与完整知识存档

> 分支：`refactor/god-file-split`（从 `test` 切出）
> 状态：**✅ 全部完成 13 / 13（计划内产品代码），均构建 + CTest 25/25 验证**，5 个重构提交已落地（见末尾 §11 完成更新）
> 本文档为可续接的交接档：包含完整审计结果、已验证的拆分配方、每个文件的拆分设计与语义风险、计划外文件清单、踩过的坑与回滚方式。

---

## 0. 一句话总览

对仓库中**按行数判定的上帝文件**做语义不变的物理拆分（拆翻译单元 TU），降低单文件体量、改善增量编译与可维护性，**不改任何类的公开接口、不改可观测行为**。判定与执行严格遵循用户定的规则：

- **按行数大** → 实际拆分修复。
- **按"类/对象"大（单一高耦合巨类，瘦身需抽新类/改接口）** → 仅给拆分**建议**，不机械拆。
- 拆分必须**逐字节搬运**（sed/字节级提取，绝不重打或改写函数体），并以 **构建 + CTest 25/25** 作为语义不变的硬门禁。

---

## 1. 背景、判定标准与范围决定

### 1.1 判定标准（用户定义）
1. 文件大小（行数）——主标准。
2. 对象大小（类）——无严格阈值；**若文件之所以大是因为单一巨类，则只给拆分建议**（真正瘦身=抽新类/改设计/改接口，有真实语义风险）。

仓库自身软目标：**~800 行 / 文件、一个清晰职责**（见 `.claude/skills/miacode-dev-guide` 的结构守则）。

### 1.2 范围决定
用户选择 **「产品代码全拆（推荐）」**：拆全部产品代码 SPLIT_SAFE 文件，**排除**：
- 2 个默认关闭、正在解耦的 DComp 渲染器（`render/backend_d3d11/*`）；
- 测试 spec（`*Spec.cpp`）。

并要求**新分支 + 每文件构建+CTest 验证**后再推进。

### 1.3 验证基线
`build/` 已配置（VS2022 generator、`MIACODE_BUILD_DEV_TOOLS=ON`），`cmake` 4.1.2 + `ctest` 可用。基线 **CTest 25/25 全绿（~4 秒）**，这是"语义不变"的参照点。

---

## 2. 已验证的拆分配方（PROVEN RECIPE）

> 已在 8 个文件上验证：构建通过 + CTest 25/25。

1. **读全文件 + 备份**：`cp` 到 `/tmp/<name>.bak`，**以备份为 sed 源**，避免编辑过程中行号漂移。
2. **定位精确边界**：`grep -nE '^[A-Za-z].*::method\('` 找列 0 的成员函数定义；`grep -nE '^namespace \{|^\}\s*//\s*namespace'` 找匿名命名空间块。读边界附近几行确认每个块的精确 `[start,end]`，绝不切到函数体中间。优先**连续行区间**（单次 sed 提取最简单）。
3. **处理文件级共享符号（最大坑）**：匿名命名空间符号是**内部链接**，移动后跨 TU 不可见 → 链接错误。逐符号判定：
   - 仅被**一个**结果 TU 使用 → 原样搬进该 TU 自己的匿名命名空间。
   - **无状态** helper/常量被 **>1 个** TU 使用 → 原样搬进新内部头 `<Base>.Internal.h`，放进**具名 detail 命名空间**，函数加 `inline`、命名空间作用域常量加 `inline constexpr`；每个 TU `#include` 该头 + `using namespace <detail-ns>;`。
   - **可变共享状态**（静态计数器/单例）→ 内部头里 `extern` 声明，仅在**一个** TU 中定义。
   - 跨 TU 的**宏**（如 `MC_OP`）→ `#define` 移进内部头。
   - 跨 TU 用到的**结构体/枚举/文件局部类**（完整类型）→ 移进内部头。
4. **新 TU 前导**：逐字节拷贝原文件的整个 `#include` 块 + 内部头 include + 原有的 `using namespace ...;` + detail using；其后 `sed -n 'A,Bp' /tmp/...bak` 追加字节精确的函数体。
5. **瘦身原文件**：用 sed **拼接保留区间**重建（不要手删大块）。保留其 include + 加内部头 include/using。
6. **忠实性强制**：所有函数体只能 sed 字节精确搬运，**绝不**重打/重排/改写/重格式化。
7. **不碰 CMakeLists、不在 prep 阶段构建**（由集成者统一登记+构建）；**不碰类的 `.h`**（Q_OBJECT/MOC 留在原头；只搬 .cpp 方法体；方法体内的 lambda/slot 随方法走）。
8. **自检**：(a) 成员函数定义清单跨「瘦身原文件 + 全部新 TU」与备份**完全一致**（无丢失、无重复，排序后 diff）；(b) `sort|uniq -d` 为空；(c) 每个新文件花括号配平（注意 `'{'` 字符字面量会让裸计数非零，需剥离字面量/注释或以编译为准）；(d) `grep -n <basename> CMakeLists.txt` 报告**所有**列出该文件的目标（产品 `MiaCode` + 可能的 spec 目标）。

### 2.1 关键不变式（为何字节搬运 + 构建绿 ⇒ 语义不变）
逐字节搬运不可能**静默改变行为**——只可能编译/链接失败（被构建抓到）。唯一能静默改语义的是**名字解析变化**（如某 helper 解析到了不同定义/重载）；具名 detail 命名空间 + using 指令保持解析一致；任何"缺符号"都是链接错误（被构建抓到）。故 **构建绿 + CTest 绿 ⇒ 高置信语义不变**。

### 2.2 工程化要点
- **CMake 是显式源列表（无 glob）**：每个新 `.cpp` 必须登记进 `target_sources(MiaCode ...)`/`add_executable(MiaCode ...)`，**且**若文件还喂 dev-tool spec（`miacode_add_dev_tool(... SOURCES ...)`），两处都要加。
- 内部头（无 Q_OBJECT）**不必**登记进 CMake（靠 `#include`）；登记也无害（仓库惯例会列 `.h`）。
- 集成顺序约束：原文件被掏空后，其符号定义在新 TU；**必须先登记新 TU 才能成功链接 MiaCode**。并行 prep（不动 CMake）→ 集成者按批统一登记 + 构建，能隔离错误面。
- 行尾：工作树多为 CRLF（`core.autocrlf=true`，仓库存 LF）。Git Bash 的 `sed` 会剥 CR → 用 Python 字节切片更安全（保留 CRLF）。`git` 提交时统一规范化，"LF will be replaced by CRLF"告警可忽略。

---

## 3. 完整审计结果（19 个深审文件）

第一轮并行审计（19 个 read-only agent）覆盖：`src/` 下 .cpp >~1580 行 + 2 个大 QML。分类：

- **SPLIT_SAFE = 16**（其中 13 个产品代码进计划）
- **SUGGEST_ONLY = 1**：`QuickShellMain.qml`
- **ALREADY_MODULAR = 2**：`TimelineModelSpec.cpp`、`MaimaiBannerCard.qml`

> 注：`PreviewDCompSurface.cpp`、`PreviewDCompSpritePipeline.cpp` 是 SPLIT_SAFE，但因 DComp 默认关闭/解耦中被排除执行；`ChartBatchTransformSpec.cpp` SPLIT_SAFE 但为测试 spec 排除。

### 3.1 必须守住的语义风险类型（审计共识）
1. **匿名命名空间=内部链接**（最普遍坑）。
2. **MOC/Q_OBJECT**：QObject 类声明留头文件，只搬 .cpp 方法体。
3. **宏可见性**（`MC_OP` 等）需在内部头集中。
4. **条件编译双路径**（`MIACODE_USE_QTAVPLAYER` / `HAVE_QT_MULTIMEDIA`）必须逐个函数原样保留 `#ifdef`。
5. **CMake 显式源 + spec 目标**双处登记。
6. **QML** 子组件需进 `.qrc`（AUTORCC）；属性/定时器/函数链高耦合时仅建议、不机械搬。
7. **共享可变状态/单例**：跨 TU 必须 `extern` 单点定义，否则状态被复制分裂（语义破坏）。
8. **文件局部类型**（结构体/内部类）被多 TU 用 → 移进共享内部头。

---

## 4. 进度：已完成 8 / 13（均构建 + CTest 25/25）

### 4.1 提交
| commit | 内容 |
|---|---|
| `9d6f7d0` | `MainWindow.TimelinePlayback.cpp` → 4 TU + Internal.h |
| `4735223` | 4 个 MainWindow section 文件 → 14 TU（+2 Internal.h） |
| `96d08a5` | `PreviewStageMediaHost` / `VideoExportDialog` / `PlainCodeEditor` → 13 TU（+3 Internal.h） |

### 4.2 每文件拆分明细
| # | 原文件 | 行数 | 拆出（行数） | 内部头 / 备注 |
|---|---|--:|---|---|
| 1 | `app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp` | 2404→**861** | `PreviewSeek`(393) / `PreviewPlaybackState`(488) / `PreviewIntroRegion`(324) / `PreviewTick`(384) | `MainWindow.TimelinePlayback.Internal.h`（ns `timeline_playback_detail`：4 个日志 wrapper + `kTimelineZeroSecondTolerance` + `kQuickShellTransportSeekProperty` + toast 常量 + `previewVisualLeadInStartSecond`）。第二个匿名 ns 的 `introLeadInBannerTemplateMap` 随 IntroRegion 搬。 |
| 2 | `app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp` | 3476→**260** | `AudioSettings`(1473) / `MediaTools`(1288) / `TrackMetadata`(264) / `ExportSettings`(359) | **无内部头**：整个匿名 ns（文件锁/ffmpeg/媒体处理 helper）只被 MediaTools 用 → 整块搬入 MediaTools；`readTrackTagField` 只被 TrackMetadata 用 → 搬入。核心 `releasePreviewMediaForFileOperation` 不碰任何被搬 helper（已核实）。 |
| 3 | `app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp` | 2386→**798** | `DocumentFileFlow`(486) / `DocumentDesignerFlow`(474) / `DocumentAutosaveFlow`(625) | `MainWindow.DocumentFlow.Internal.h`（7 个被 >1 TU 用的共享 helper + `showUnsavedChangesDialog`/`backupRestoreEntriesForAutosaveDirectory`）。 |
| 4 | `app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp` | 2048→**1478** | `TimelinePreviewFollowSync`(366) / `TimelineAnalysisFlow`(218) / `TimelineQuickParse`(108) | `MainWindow.PreviewTimelineFlow.Internal.h`（`kTimelineZeroSecondTolerance`/`kTimelineAnalysisIdleDelayMs` 等）。核心仍 1478（可进一步细分，见 §6C）。 |
| 5 | `app/mainwindow/sections/timeline/MainWindow.TimelineLayout.cpp` | 1791→**91** | `TimelineFramePacing`(651) / `TimelineFullscreenUI`(366) / `TimelineLayoutUI`(560) / `TimelineForwarding`(284) | 无内部头（helper 各随单一消费 TU）。 |
| 6 | `preview/runtime/PreviewStageMediaHost.cpp` | 3335→**165** | `_Backend`(893) / `_Media`(564) / `_Playback`(882) / `_Diagnostics`(700) / `_Timeout`(426) | `PreviewStageMediaHostInternal.h`（ns `psmh_detail`：`kPausedSeekAckToleranceMs`、`currentBeaconTid`、`appendPreviewStageMediaLog`、`playbackStateName`/`playerPlaybackState`——后两者在 `HAVE_QT_MULTIMEDIA && !MIACODE_USE_QTAVPLAYER` 守卫内）。**有状态 `isIntegratedRenderAdapter` 缓存留在 Backend 单 TU**；5 个方法的 `#ifdef MIACODE_USE_QTAVPLAYER` 被拆到 3 个 TU，各自重新包守卫。 |
| 7 | `tools/video_export/VideoExportDialog.cpp` | 3105→**1901** | `.SettingsPersistence`(271) / `.IntroControls`(256) / `.ExportFlow`(850) | `VideoExportDialogInternal.h`（ns `dialog_detail`，无状态 helper）。**文件局部 QWidget 子类 `TimestampSpinBox`/`ExportRangeTrack` + 1033 行构造函数 + range/preview 簇留核心**（这些方法 `static_cast<ExportRangeTrack*>`，必须与唯一定义同 TU）。核心仍 1901（见 §6C）。 |
| 8 | `editor/PlainCodeEditor.cpp` | 2019→**111** | `.Layout`(291) / `.HighlightAndCaret`(370) / `.BracketCompletion`(445) / `.Input`(819) / `.Bookmarks`(165) | `PlainCodeEditor.Internal.h`（ns `pce_detail`：`kEditorCursorVisibleWidth`） **+ 内部类 `LineNumberArea` 整类移入该头**（被 Layout 与 Bookmarks 两个 TU 共用，纯 QWidget 无 Q_OBJECT）。同时喂 `plain_code_editor_spec` 目标——CMake 两处登记。`miacode::editor` 半角归一化块在头中声明=外部链接，整块留 Input TU。 |

### 4.3 语义保证证据（每文件均验证）
- 成员函数定义清单（瘦身核心 + 新 TU 的并集）与原文件**逐一相等**，无丢失无重复；
- `git diff` 仅删除无新增（核心文件逐行字节相同）；花括号配平；
- 构建 exit 0、`MiaCode.exe` 重链；`ctest -C Release` **25/25**。

---

## 5. 剩余计划内：5 / 13（含每文件拆分设计与风险）

> 全部 SPLIT_SAFE。下面是基于审计的拆分设计 + 关键风险；执行时仍按 §2 配方 + 构建/CTest 门禁。

### 5.1 🔴 `src/tools/video_export/VideoExportController.cpp`（5144，旗舰/最难）
- **结构**：类 `VideoExportController` 只有 2 个静态方法；体量来自 **63–3102 行的单个匿名命名空间（~3040 行，~50 个自由 helper + ~16 个 struct/enum）** + `exportPreparedTask`（3128–5144，~2017 行单函数）。
- **唯一可变共享状态**：`ExportTempDirRegistry`（Meyer 单例，`instance()` 内 `static`），只被 `ScopedExportTempDirTracker` 与 `exportPreparedTask` 用（376/381/3298/3306）→ **随 `exportPreparedTask` 同 TU 搬，无状态分裂风险**。其余 helper 全部无状态。
- **`exportPreparedTask` 调用的 helper**：`appendVideoExportLog`(93×)、`chooseVideoEncoder`、`chooseVideoBitratePlan`、`loadExportRuntimeConfig`、`resolveFfmpeg/Ffprobe`、`collectVisibleObjectTrace`、`estimateFrameLayerActivity`、`renderExportFrameWithConfiguredBackend`、`drainPendingExportFrame`、`createExportAudioBackend`、`waitForProcessWithProgress`、`preparePackedRgbaFrame`、`sampledFrameSignature`、`fullFrameSignature`、`objectOnlyFrameSignature`、`probeExportedVideoSummary`、`stageStaticBackgroundImageForExport`、`replaceOutputFileAtomicallyBestEffort` 等。
- **拆分方案**：
  1. `namespace {`(63) → `namespace miacode::video_export::detail {`（就地改名，定义留各 .cpp）。
  2. 新建 `VideoExportControllerInternal.h`：把 ~16 个 struct/enum（`VideoEncoderConfig`/`SystemMemoryInfo`/`VideoBitratePlan`/`EncoderAutoMode`/`ExportRenderBackendOptions`/`ExportDiagOptions`/`ExportRuntimeConfig`/`FrameTimingStats`/`ExportPipeBackpressurePlan`/`Stats`/`FrameLayerActivityStats`/`ObjectTraceItem`/`ReadyFramePayload`/`PendingPboFrame`/`ExportFrameRenderStatus`/`DiagTapApproachSample`）原样搬入 `detail` ns，并**声明**所有跨 TU 调用的 helper（最繁琐、最易错——签名需逐字对齐，多行签名小心）。
  3. `VideoExportPreparedTask.cpp`：`exportPreparedTask` + `ExportTempDirRegistry`/`ScopedExportTempDirTracker`（后两者留该 TU 局部）。
  4. `VideoExportEncoder.cpp`：编码器/码率/内存选择 + ffmpeg 解析（`chooseVideoEncoder`/`chooseVideoBitratePlan`/`querySystemMemoryInfo`/`resolveFfmpeg/Ffprobe`/`probeEncoderRuntimeAvailability`/`loadExportRuntimeConfig`/…）。
  5. `VideoExportDiagnostics.cpp`：物体轨迹 + diag 采样 + 帧签名（`collectVisibleObjectTrace`/`estimateFrameLayerActivity`/`diag*`/`meanAbsDiff*`/`*Signature`）。
  6. `VideoExportFrame.cpp`：帧 I/O + 渲染 + 日志（`buildReadyFramePayload`/`renderExportFrameWithConfiguredBackend`/`drainPendingExportFrame`/`preparePackedRgbaFrame`/`writeAllTo*`/背压/`appendVideoExportLog` + 日志 helper/进程日志/`createExportAudioBackend`）。
  7. 核心保留 `exportFullPreview` + 余量。
- **保守回退方案**（若全拆风险过高）：只抽 `exportPreparedTask`(+registry) 一个 TU（核心 5144→~3023），仅需声明 `exportPreparedTask` 用到的 ~17 个 helper，风险显著更低。
- **建议**：此文件**由人亲自做**（不交并行 agent），逐字 sed + 严格构建。CMake 仅 `MiaCode` 一个目标（行号见 grep）。

### 5.2 🟡 `src/timeline/TimelineQuickModel.cpp`（2614）
- 单类多方法 + 755 行匿名解析 helper。拆 `TimelineQuickModelCore` / `TimelineQuickModelParser`（`parseLine`/`parseNoteToken`/`rebuildSlideDerivedFlags`/`finalizeEachGroup` + 匿名 helper）/ `TimelineQuickModelSnapshot`（`rebuildSnapshotDuration` 等）/ `TimelineQuickModelIndexing`（anchor/follow-selection）。
- **内部头 `TimelineQuickModelPrivate.h`**：暴露文件级共享结构体 `ParseState`/`LineState`/`LineCursorCache`(+`FollowSelectionRange`/`Span`)/`AbsoluteCursorAnchor` + 常量 `kTimelineEpsilon`（被多 TU 用）。**注意区分**：类成员变量（`lines_`/`snapshot_`/…）经 `this` 访问，跨 TU 拆方法**不需要**头；只有**文件级类型/helper**需要进 Private.h。
- CMake 双目标：`MiaCode` + `timeline_model_spec`。

### 5.3 🟡 `src/audio/BassPreviewAudioBackend.cpp`（2487）
- 单类 + 嵌套 `Sample` struct + ~70 方法。拆 `_SampleImpl` / `_EngineInit` / `_Assets` / `_Transport` / `_PlaybackClock` / `_EventDrain`。
- **内部头 `BassPreviewAudioBackendImpl.h`**：`extern int gBassDeviceRefCount;`（**可变共享状态**，在 `_EngineInit.cpp` 单点定义）+ 无状态 helper（`noteBassErr`/`appendAudioDebugLog`/标签转换/constexpr）。
- CMake 多目标：`MiaCode` + bass specs（`bass_preview_retained_state_spec`/`bass_preview_debug_log_routing_spec`）+ `soundtouch_probe`。

### 5.4 🟡 `src/core/chart/transform/ChartBatchTransform.cpp`（2302）
- 自由函数（解析/细分/选区/变换算法），匿名 ns。拆 `.Parsers` / `.Subdivision` / `.Selection` / `.Transform`。
- **内部头 `ChartBatchTransform.Internal.h`**：共享 struct（`TouchTokenParts`/`NoteTokenParts`/`SlideTokenParts`/`SelectionEdgeSplit`/`ToggleStats`）+ helper 声明 + **`MC_OP` 宏 `#define`**（多 TU 用）。
- CMake 双目标：`MiaCode` + `chart_batch_transform_spec`。
- ⚠ 同模块还有 `ChartNormalization.cpp`(1707)——计划外，但成对，可一并考虑。

### 5.5 🟢 `src/app/main.cpp`（1864）
- 匿名 ns 自由函数按子系统分组：Win32 启动诊断(~390) / 图形后端选择(~150) / CLI 视频导出(~300) / 导出 worker(~150) / `main()` 引导(~540)。
- 拆 `startup_diagnostics_win32.cpp` / `graphics_backend.cpp` / `cli_video_export.cpp` / `cli_video_export_worker.cpp` / `cli_shared.cpp` + 瘦身 `main.cpp`。
- **内部头**：`startup_diagnostics.h`（声明跨 TU 调用的函数；`main()` 调它们）。CMake 仅 `MiaCode`。

---

## 6. 计划外大文件清单

> 软目标 ~800 行/文件；下面以 >1000 行为列举线。

### 6A. 审计过、按范围**主动排除**（6）
| 文件 | 行数 | 原因 |
|---|--:|---|
| `render/backend_d3d11/PreviewDCompSurface.cpp` | 1793 | SPLIT_SAFE，但 DComp 默认关闭/解耦中，价值低 |
| `render/backend_d3d11/PreviewDCompSpritePipeline.cpp` | 1777 | 同上（DComp） |
| `tools/chart_transform/ChartBatchTransformSpec.cpp` | 2125 | SPLIT_SAFE，但测试 spec |
| `app/quick_shell/qml/QuickShellMain.qml` | 1582 | **SUGGEST_ONLY**：38 属性+定时器+布局调用链高耦合，需先抽 C++ 控制器（设计改动） |
| `tools/timeline/TimelineModelSpec.cpp` | 1970 | **ALREADY_MODULAR**：综合测试，体量合理 |
| `intro/qml/MaimaiBannerCard.qml` | 988 | **ALREADY_MODULAR**：单一卡片场景，Z 序刻意 |

### 6B. 深审阈值以下、**从未纳入审计**的盲区（~30 个，1013–1712 行）
> 这些按 ~800 软目标都超标，但风险/收益各异，需要时应**先跑一轮并行审计分类**再决定。

非 MainWindow 重点：
- `preview/runtime/PreviewRuntime.cpp` **1712**
- `core/chart/transform/ChartNormalization.cpp` **1707**（与 ChartBatchTransform 成对）
- `core/chart/parser/SimaiNativeParser.cpp` **1634** + `SimaiNativeParser.Driver.cpp` 1333 ⚠ 是 `#include "*.cpp"` 的 **unity-split**，处理方式特殊（指南：改 multi-TU 或把 include 改 `.inc`/`.ipp`）
- `timeline/TimelineSceneStateBuilder.cpp` 1518
- `preview/runtime/PreviewQuickExportSession.cpp` 1446
- `app/quick_shell/QuickShellBootstrap.cpp` 1383
- `tools/muri/MuriAnalyzer.cpp` 1377 ⚠ 指南：**已分解**为 `miacode::muri::detail` 多 TU，**别再拆/别回长**
- `tools/cover_export/ExportCoverDialog.cpp` 1368
- `timeline/quick/TimelineQuickItem.cpp` 1343
- `preview/quick_scene/PreviewQuickSceneRoot.cpp` 1196
- `common/DebugLog.cpp` 1156、`app/ui/UiTheme.cpp` 1145
- `timeline/TimelineView.Paint.cpp` 1076 + `timeline/TimelineView.cpp` 1062
- `app/quick_shell/QuickShellController.cpp` 1013
- 工具/测试：`tools/muri/MuriDump.cpp` 1238、`tools/simai_parser/SimaiParserSpec.cpp` 1216、`tools/video_export/BatchVideoExportDialog.cpp` 1239、`render/backend_d3d11/TimelineRenderView.cpp` 1021(DComp)

MainWindow.* section（既有 partial-class 切片，可同模式细分，~11 个 1090–1669）：
`MainWindow.FrameBootstrap.cpp` 1669、`MainWindow.ExportWorker.cpp` 1520、`MainWindow.PreferencesDialog.cpp` 1499、`MainWindow.EditorDisplay.cpp` 1449、`MainWindow.WindowInteraction.cpp` 1424、`MainWindow.ValidationFlow.cpp` 1340、`MainWindow.DocumentUi.cpp` 1259、`MainWindow.ExportFlow.cpp` 1244、`MainWindow.WindowShell.cpp` 1197、`MainWindow.ExportSnapshot.cpp` 1130、`MainWindow.ValidationRuntime.cpp` 1090。

QML：`app/quick_shell/qml/QuickShellPreviewTransport.qml` 715（首轮扫到、未深审）。

### 6C. 我已拆、但"核心/产物"仍 >1000（可选进一步细分）
- `tools/video_export/VideoExportDialog.cpp` **1901**（1033 行构造函数受文件局部类约束留核心）
- `app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp` **1478**
- `app/mainwindow/sections/dialogs/MainWindow.Dialogs.AudioSettings.cpp` **1473**（`openPreviewSettingsDialog` 单函数 ~1430，进一步拆需抽 helper 方法=改 header）
- `app/mainwindow/sections/dialogs/MainWindow.Dialogs.MediaTools.cpp` **1288**

---

## 7. 踩过的坑 / 教训

1. **匿名命名空间内部链接**是头号坑——每个文件都要逐符号判定归属（随消费 TU / 进内部头 / extern 单点）。
2. **`PlainCodeEditor::LineNumberArea`**：被两个 TU 共用的文件局部内部类，初次拆后 `Bookmarks.cpp` 报 `C2027 未定义类型`。修法=整类（纯 QWidget 无 Q_OBJECT）搬进共享 `PlainCodeEditor.Internal.h`，两 TU `#include`。**教训：文件局部 class/struct 被多 TU 用 → 共享头。**
3. **有状态单例/缓存**（`isIntegratedRenderAdapter` 静态缓存、`gBassDeviceRefCount`、`ExportTempDirRegistry`）：绝不能放头里被多 TU 各自实例化（状态分裂）；要么留单 TU，要么 `extern` 单点定义。
4. **条件编译**：拆开 `#ifdef MIACODE_USE_QTAVPLAYER` 包裹的多个方法时，每个移出方法要**各自重包守卫**。
5. **CRLF**：用 Python 字节切片而非 `sed` 保留行尾。
6. **CMake 显式源 + spec 双目标**：`PlainCodeEditor`/`TimelineQuickModel`/`BassPreviewAudioBackend`/`ChartBatchTransform` 都喂 spec 目标，两处登记。
7. **行号会因前批 CMake 编辑漂移** → 集成时用**字符串匹配**（含上下文）而非行号编辑；同名不同缩进（如 `PlainCodeEditor.cpp` 在 4 空格 MiaCode 与 12 空格 spec 两处）要用缩进区分。
8. **并行 prep + 串行集成**是最佳节奏：agent 并行做字节搬运（不碰 CMake/不构建），人统一登记 CMake + 分批构建（隔离错误面）+ CTest。Batch1（4 文件）一次构建即过；Batch2（3 文件）仅 1 处人工修（LineNumberArea）。

---

## 8. 验证与回滚

- **验证单文件**：`cmake --build build --config Release [--target MiaCode]`（exit 0 + `.exe` 重链）→ `ctest --test-dir build -C Release`（**25/25**）。
- **回滚整个工作**：`git checkout test && git branch -D refactor/god-file-split`（分支隔离，可随时丢弃）。
- **逐文件回滚**：每文件/每批一个独立 commit，可单独 `git revert`。
- **预存在改动**：`.claude/skills/miacode-dev-guide/references/debug-and-logging.md` 在本会话开始前即为 modified，**非本次改动**，未纳入任何提交。

---

## 9. 收尾待办（落地后）

- 更新 `.claude/skills/miacode-dev-guide/references/architecture-and-layout.md` 的**上帝文件 watch-list**（§5）：移除已拆文件、登记新 TU 命名约定（`MainWindow.<Section>.cpp` / `<Class>_<Area>.cpp` / `<Class>.<Area>.cpp` / `*.Internal.h` 具名 detail ns 模式）。
- 若决定纳入 §6B 盲区：先跑与首轮相同的并行审计（read-only）产出分类 + 拆分建议，再选择性执行。
- 决定 §6C 的核心/产物是否进一步细分（多数受单一大函数/文件局部类约束，需轻度设计改动）。

---

## 10. 目标拆分后仓库结构（计划内 13 个全部完成后）

```text
src/tools/video_export/
  VideoExportController.cpp        (核心: exportFullPreview + 余量)
  + VideoExportPreparedTask/Diagnostics/Encoder/Frame.cpp + VideoExportControllerInternal.h
  VideoExportDialog.cpp            (核心: ctor + 文件局部类 + range/preview)
  + .SettingsPersistence/.IntroControls/.ExportFlow.cpp + VideoExportDialogInternal.h   ✅
src/app/mainwindow/sections/        (沿用 MainWindow.<Section>.cpp partial-class 惯例)  ✅(4 文件已拆)
  dialogs/  + .AudioSettings/.MediaTools/.TrackMetadata/.ExportSettings
  timeline/ + .PreviewSeek/.PreviewPlaybackState/.PreviewIntroRegion/.PreviewTick
            + .TimelinePreviewFollowSync/.TimelineAnalysisFlow/.TimelineQuickParse
            + .TimelineFramePacing/.TimelineFullscreenUI/.TimelineLayoutUI/.TimelineForwarding
  document/ + .DocumentFileFlow/.DocumentDesignerFlow/.DocumentAutosaveFlow
src/preview/runtime/
  PreviewStageMediaHost.cpp + _Backend/_Media/_Playback/_Diagnostics/_Timeout.cpp + Internal.h  ✅
src/audio/
  BassPreviewAudioBackend.cpp + _SampleImpl/_EngineInit/_Assets/_Transport/_PlaybackClock/_EventDrain.cpp + Impl.h
src/timeline/
  TimelineQuickModel{Core,Parser,Snapshot,Indexing}.cpp + TimelineQuickModelPrivate.h
src/core/chart/transform/
  ChartBatchTransform.{Parsers,Subdivision,Selection,Transform}.cpp + ChartBatchTransform.Internal.h
src/editor/
  PlainCodeEditor.{Layout,HighlightAndCaret,BracketCompletion,Input,Bookmarks}.cpp + PlainCodeEditor.Internal.h  ✅
src/app/
  main.cpp(瘦身) + startup_diagnostics_win32/graphics_backend/cli_video_export/cli_video_export_worker/cli_shared.cpp + startup_diagnostics.h

仅建议(不机械拆): app/quick_shell/qml/QuickShellMain.qml — 先抽 surface 路由状态机 + 布局引擎为 C++ 控制器
已合理(不动): tools/timeline/TimelineModelSpec.cpp, intro/qml/MaimaiBannerCard.qml
```

（✅ = 已完成并验证）

---

## 11. 完成状态更新（2026-06-19）

**✅ 计划内 13 / 13 全部完成**，每个文件均 byte-faithful 拆分 + 构建 + CTest 25/25 验证后提交。

旗舰收尾（§5.1 已落地）：
- `VideoExportController.cpp` 5144 → **94**（仅 `exportFullPreview`）+ `VideoExportControllerInternal.h`（17 结构体 + 68 原型 + using/常量，`miacode::video_export::detail`）+ `VideoExportEncoder/Diagnostics/FrameRender/Pipeline/PreparedTask.cpp`。匿名命名空间→具名 detail；4 个默认参数迁到头原型（ODR）；71 定义清单一致、重建体逐行字节相同；首次构建即过。
- ⚠ `VideoExportPreparedTask.cpp` 仍 **2197 行** = 单个 2017 行的 `exportPreparedTask` 方法 + `ExportTempDirRegistry` 单例。这是不可机械再拆的内核（进一步缩减需把方法拆成子函数=设计改动），属后续工作。

提交序（分支 `refactor/god-file-split`，与并行 Codex 提交交错，但每个重构 commit 仅含本次拆分文件）：
`9d6f7d0`(TimelinePlayback) → `4735223`(MainWindow×4) → `96d08a5`(PreviewStageMediaHost/VideoExportDialog/PlainCodeEditor) → `6926238`(TimelineQuickModel/Bass/ChartBatchTransform/main) → `bc0c445`(VideoExportController)；文档 `2a9a2dc`。

新增教训（补 §7）：
9. **文件局部「类型」被多 TU 用 = 完整类型需求**：`PlainCodeEditor::LineNumberArea`、`BassPreviewAudioBackend::Sample`（嵌套类/结构体，定义原在 .cpp）拆开后其它 TU 只见前向声明 → `C2027 未定义类型` / `unique_ptr 不能删不完整类型`。修法 = 把类型定义整体搬进共享内部头。凡 .cpp 内定义、被 >1 TU 用的 class/struct（含持有 `unique_ptr<T>` 成员、其析构在另一 TU 的情况），都要进共享头。
10. **默认参数 ODR**：自由函数拆「头原型 + .cpp 定义」时默认实参只能出现一处（放头原型、定义里删），否则 `redefinition of default argument`。VideoExportController 有 4 个：truncateForLog / appendVideoExportLog / writeAllToProcess / processOutputAndErrorForLog。
11. **共享 git index 并发**：与并行会话共用工作树时，`git add … && git commit`（无 pathspec）会把对方 staged 文件扫进我的 commit。全程改用 `git commit <pathspec>` 或先核 `git diff --cached --name-status`。曾有一次 doc commit 被污染，用 `reset --soft` + 重组三个 commit 修干净（最终 tree 与污染前 1:1 相同）。
12. **链接锁**：`MiaCode.exe` 重链接前需关掉运行中的实例（`LNK1104` 文件锁）；dev-tool spec 二进制不依赖 MiaCode.exe，可独立跑 CTest。

剩余（计划外，见 §6 / §10）：DComp×2（默认关、解耦中）、`ChartBatchTransformSpec.cpp`（测试）、`QuickShellMain.qml`（仅建议）、§6B ~30 文件盲区（未审计）。dev-guide 的 god-file watch-list（`architecture-and-layout.md §5`）已同步更新。
