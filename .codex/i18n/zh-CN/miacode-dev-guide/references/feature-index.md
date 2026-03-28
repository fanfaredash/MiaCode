<!-- translation-source: .codex/skills/miacode-dev-guide/references/feature-index.md -->
<!-- translation-source-hash: b9054a089f67ce88c9d41dd64e1e0f12ac6c26276114683aa1d77dbeebd39831 -->
<!-- 说明：这是中文镜像，不作为 Codex skill 入口加载。 -->

# 功能索引

这份文件用于把用户视角的功能映射到具体的文件、类和函数入口。

## 1. 应用启动与进程模式

- 应用启动与 GUI 入口：
  - 文件：`src/app/main.cpp`
  - 函数：`main`、`setWindowsAppUserModelId`
  - 负责：Qt 应用启动、主题/字体设置、主窗口启动、启动耗时日志
- CLI 导出入口：
  - 文件：`src/app/main.cpp`
  - 函数：`wantsCliVideoExport`、`runCliVideoExport`
  - 负责：命令行导出参数解析与直接导出调用
- 导出 worker 入口：
  - 文件：`src/app/main.cpp`
  - 函数：`wantsCliVideoExportWorker`、`runCliVideoExportWorker`
  - 负责：snapshot 读取、worker 协议、后台导出执行

## 2. 主窗口与页面级编排

- 主窗口表层与共享状态：
  - 文件：`src/app/mainwindow/MainWindow.h`、`src/app/mainwindow/MainWindow.cpp`
  - 类：`MainWindow`
  - 负责：顶层状态、共享控件、预览运行时实例、导出 worker 进程、portable/project 设置
- 分片总览：
  - 文件：`src/app/mainwindow/sections/README.md`
  - 负责：说明 MainWindow 各功能切片的落点
- 窗口骨架、菜单、工具栏、布局外壳：
  - 文件：`src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
  - 负责：action、菜单绑定、splitter/dock/card 组装、preview canvas 启动

## 3. 文档模型、字段与文件流

- 谱面元数据与难度的存储模型：
  - 文件：`src/simai/document/SimaiDocument.h`、`src/simai/document/SimaiDocument.cpp`
  - 类：`SimaiDocument`
  - 关键函数：`createEmpty`、`fromText`、`toText`、`parseRawFields`、`serializeRawFields`、`ensureDifficulty`、`removeDifficulty`
- 新建/打开/保存与字段切换：
  - 文件：`src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp`
  - 关键函数：`applyCurrentFieldToDocument`、`onNewFile`、`onOpenFile`、`onSaveFile`、`onSaveFileAs`、`rebuildFieldSidebar`、`populateMetadataPage`、`populateDifficultyPage`、`switchToMetadataField`、`switchToDifficultyField`、`loadDocument`
- 批量编辑文本表面：
  - 文件：`src/editor/PlainCodeEditor.h`、`src/editor/PlainCodeEditor.cpp`
  - 类：`PlainCodeEditor`
  - 负责：行号、上下文菜单里的变换 action、编辑器显示行为

## 4. 解析器、语法校验与标记生成

- 解析器 API：
  - 文件：`src/simai/parser/SimaiNativeParser.h`、`src/simai/parser/SimaiNativeParser.Driver.cpp`
  - 类：`SimaiNativeParser`
  - 关键函数：`parseForTimeline`、`validateSyntax`、`buildValidationReport`
- 解析器内部实现：
  - 文件：`src/simai/parser/SimaiNativeParser.cpp`、`src/simai/parser/SimaiNativeParser.Slide.cpp`、`src/simai/parser/SimaiNativeParser.TouchTap.cpp`、`src/simai/parser/SimaiNativeParser.StrictChecks.cpp`
  - 负责：note 解析、slide/wifi 语义、touch/tap 解析、strict/lenient 校验
- 主窗口校验 UI：
  - 文件：`src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`
  - 关键函数：`runValidateSimaiSilently`、`runValidateSimai`、`addValidationError`、`addValidationDecoration`、`refreshValidationPanelForActiveField`

## 5. 时间轴数据、光标映射与预览同步

- 时间轴数据模型与控件：
  - 文件：`src/timeline/TimelineView.h`、`src/timeline/TimelineView.cpp`、`src/timeline/TimelineView.Core.cpp`、`src/timeline/TimelineView.Interaction.cpp`、`src/timeline/TimelineView.Paint.cpp`
  - 类：`TimelineView`
  - 负责：beat/note 可视化、playhead/cursor 移动、波形条、跟随预览行为
- 时间轴刷新与 marker 分发：
  - 文件：`src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - 关键函数：`refreshWaveformCache`、`scheduleTimelineRefresh`、`refreshTimelineMetadata`、`seekTimelineToCursor`、`syncTimelineToEditorCursor`、`navigateTimelineToSecond`、`updatePreviewSliderRange`、`updatePreviewObjectStats`、`updatePreviewWorkspaceLayout`、`updatePreviewPanelLayout`
- 时间 getter 与时间写回：
  - 文件：`src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - 关键函数：`parsedFirstSeconds`、`parsedWholeBpm`、`parsedLatencyMeterId`、`applyLatencyDetectorOffset`、`applyLatencyDetectorBpm`、`applyLatencyDetectorMeter`

## 6. 预览视频、媒体与渲染状态

- 预览画布核心：
  - 文件：`src/preview/video/PreviewCanvas.h`、`src/preview/video/PreviewCanvas.cpp`
  - 类：`PreviewCanvas`
  - 负责：渲染状态、基于资源的绘制、特效曲线、缓存、离屏导出渲染支持
- PreviewCanvas 分拆文件：
  - `PreviewCanvas.Runtime.cpp`：状态变更、离屏渲染器初始化、帧生成
  - `PreviewCanvas.Objects.cpp`：实际物件/特效/HUD 绘制
  - `PreviewCanvas.Render.cpp`：渲染循环与 painter 路径
  - `PreviewCanvas.GLAndTransforms.cpp`：GL 与变换辅助
  - `PreviewCanvas.SkinAndAtlas.cpp`：异步皮肤加载、atlas 重建、纹理预热
- 预览媒体控制器：
  - 文件：`src/preview/video/PreviewMediaController.h`、`src/preview/video/PreviewMediaController.cpp`
  - 类：`PreviewMediaController`
  - 关键函数：`setChartPath`、`setBackgroundTrackPath`、`setTimelineOffsetSeconds`、`startPlayback`、`syncPlayback`、`resolveMediaPath`
- 预览集成辅助：
  - 文件：`src/preview/PreviewIntegration.h`、`src/preview/PreviewIntegration.cpp`
  - 负责：旧预览窗口并排摆放辅助

## 7. 预览音频与 SFX 调度

- 预览 SFX 运行时：
  - 文件：`src/preview/audio/QtPreviewSfxRuntime.h`、`src/preview/audio/QtPreviewSfxRuntime.cpp`
  - 类：`QtPreviewSfxRuntime`
  - 负责：miniaudio 引擎状态、音效 bank、touchhold 声部控制、背景轨播放
- 分拆职责：
  - `QtPreviewSfxRuntime.Assets.cpp`：谱面音轨解析、SFX 目录解析、bank 重置
  - `QtPreviewSfxRuntime.Timeline.cpp`：从 `TimelineNoteMarker` 生成事件
  - `QtPreviewSfxRuntime.Background.cpp`：BGM 启动/seek/同步/audition
  - `QtPreviewSfxRuntime.Engine.cpp`、`QtPreviewSfxRuntime.Voices.cpp`：引擎与声部内部实现
- 主窗口挂接点：
  - 文件：`src/app/mainwindow/MainWindow.cpp`
  - 关键函数：`ensurePreviewSfxRuntimePrepared`、`applyPreviewAudioSettingsToRuntime`、`schedulePreviewSubsystemWarmup`

## 8. 视频导出 UI、Snapshot 边界与编码管线

- 导出对话框：
  - 文件：`src/tools/video_export/VideoExportDialog.h`、`src/tools/video_export/VideoExportDialog.cpp`
  - 类：`VideoExportDialog`
  - 负责：导出参数、对话框内预览、范围选择、实时预览控制
- 导出任务与控制器：
  - 文件：`src/tools/video_export/VideoExportController.h`、`src/tools/video_export/VideoExportController.cpp`、`src/tools/video_export/RawVideoPipeTransport.h`、`src/tools/video_export/RawVideoPipeTransport.cpp`
  - 类：`VideoExportController`
  - 关键函数：`exportFullPreview`、`exportPreparedTask`、`chooseVideoEncoder`、`miacode::video_export::raw_pipe::enqueueRawVideoFrame`
- 导出 snapshot 边界：
  - 文件：`src/tools/video_export/VideoExportSnapshot.h`、`src/tools/video_export/VideoExportSnapshot.cpp`
  - 结构体：`VideoExportSnapshot`
  - 关键函数：`toJson`、`fromJson`、`buildVideoExportTaskFromSnapshot`
- 主窗口导出归属：
  - 文件：`src/app/mainwindow/MainWindow.cpp`
  - 关键函数：`onExportPreviewVideo`、`buildVideoExportSnapshot`、`launchVideoExportWorker`、`handleVideoExportWorkerEvent`

## 9. BPM 与偏移检测

- 对话框外壳：
  - 文件：`src/tools/latency/LatencyDetectorDialog.h`、`src/tools/latency/LatencyDetectorDialog.cpp`
  - 类：`LatencyDetectorDialog`
  - 负责：对话框生命周期、波形控件、拍号预设、解码音频缓冲
- 分析部分：
  - 文件：`src/tools/latency/LatencyDetectorDialog.Analysis.cpp`
  - 关键函数：`detectBpm`、`detectOffset`、`parsedBpm`、`parsedOffset`、`selectedOffsetSnapModeId`
- 播放与 UI：
  - 文件：`src/tools/latency/LatencyDetectorDialog.Playback.cpp`、`src/tools/latency/LatencyDetectorDialog.Ui.cpp`
  - 负责：本地传输控制、beat audition、可视范围跟踪
- 主窗口入口：
  - 文件：`src/app/mainwindow/MainWindow.cpp`
  - 关键函数：`onOpenLatencyDetector`

## 10. Muri 分析与静态诊断

- 运行时风格分析：
  - 文件：`src/tools/muri/MuriAnalyzer.h`、`src/tools/muri/MuriAnalyzer.cpp`
  - 类：`MuriAnalyzer`
  - 关键函数：`analyze`
- 静态引用与阈值：
  - 文件：`src/tools/muri/MuriStaticChecker.h`、`src/tools/muri/MuriStaticChecker.cpp`
  - 关键函数：`buildStaticMuriReferences`
- 主窗口使用点：
  - 文件：`src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - 关键函数：`rebuildStaticMuriReferences`

## 11. 批量变换与作者辅助

- 谱面变换：
  - 文件：`src/simai/transform/ChartBatchTransform.h`、`src/simai/transform/ChartBatchTransform.cpp`
  - 命名空间：`miacode::chart_transform`
  - 关键函数：`toggleBreakForSelection`、`toggleExForSelection`、`toggleFireworkForSelection`、`randomRotateForSelection`
- 主窗口 action 入口：
  - 文件：`src/app/mainwindow/MainWindow.cpp`
  - 关键函数：`onMirrorLeftRight`、`onMirrorUpDown`、`onRotate180`、`onRotate45CounterClockwise`、`onRotate45Clockwise`、`onToggleBreakSelection`、`onToggleExSelection`、`onToggleFireworkSelection`、`onRandomRotateSelection`

## 12. 构建、打包与仅开发态辅助二进制

- CMake targets：
  - 文件：`CMakeLists.txt`
  - 负责：应用主 target，以及 `miacode_muri_dump`、`simai_native_dump`、`soundtouch_probe`、`simai_parser_spec`、`chart_batch_transform_spec` 等开发辅助二进制
- 打包脚本：
  - 文件：`scripts/build-win.ps1`、`scripts/package-win.ps1`、`scripts/build-macos.sh`、`scripts/package-mac.sh`
- ffmpeg 准备：
  - 文件：`scripts/ensure-windows-ffmpeg.ps1`、`scripts/ensure-macos-ffmpeg.sh`、`third_party/ffmpeg/README.md`
- 分析脚本：
  - 文件：`scripts/` 目录下的辅助脚本
  - 负责：导出诊断、重复帧分析、轨迹对比、裁切分析

## 在这些情况下更新本文件

- 某个功能的主归属文件发生迁移。
- 某个能力新增了第二条镜像实现路径。
- 某个类或函数成为新的功能主入口。
- 某个工具或辅助二进制变成某块功能调试时的常规依赖。
