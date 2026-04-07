# Feature Index

Use this file to map a user-facing feature to the concrete file, class, and function entry points that own it.

## 1. App Boot And Process Modes

- App startup and GUI entry:
  - File: `src/app/main.cpp`
  - Functions: `main`, `setWindowsAppUserModelId`, `wantsQuickShellBeta`
  - Owns: Qt app startup, theme/font setup, window launch, startup timing log, `--quick-shell-beta` routing
- CLI export entry:
  - File: `src/app/main.cpp`
  - Functions: `wantsCliVideoExport`, `runCliVideoExport`
  - Owns: command-line export argument parsing and direct export invocation
- Export worker entry:
  - File: `src/app/main.cpp`
  - Functions: `wantsCliVideoExportWorker`, `runCliVideoExportWorker`
  - Owns: snapshot ingestion, worker protocol, background export execution

## 2. Main Window And Screen-Level Orchestration

- Main window surface and shared state:
  - Files: `src/app/mainwindow/MainWindow.h`, `src/app/mainwindow/MainWindow.cpp`
  - Class: `MainWindow`
  - Owns: top-level state, shared widgets, preview runtime instances, export worker process, portable/project settings, and the hidden Quick-shell backend mode that rehosts legacy editor/timeline surfaces for QML
- Quick shell beta bootstrap and controller bridge:
  - Files: `src/app/quick_shell/QuickShellBootstrap.h`, `src/app/quick_shell/QuickShellBootstrap.cpp`, `src/app/quick_shell/QuickShellController.h`, `src/app/quick_shell/QuickShellController.cpp`
  - Classes: `QuickShellBootstrap`, `QuickShellController`
  - Owns: `QQmlApplicationEngine` startup, QML-facing shell actions/properties, beta-window lifetime, and shell-state polling from the shared `MainWindow` backend
- Quick shell list and bridge models:
  - Files: `src/app/quick_shell/OutlineListModel.h`, `src/app/quick_shell/OutlineListModel.cpp`, `src/app/quick_shell/IssueListModel.h`, `src/app/quick_shell/IssueListModel.cpp`, `src/app/quick_shell/LegacyChartEditorSurface.h`, `src/app/quick_shell/LegacyChartEditorSurface.cpp`, `src/app/quick_shell/LegacyTimelineSurface.h`, `src/app/quick_shell/LegacyTimelineSurface.cpp`, `src/app/quick_shell/qml/QuickShellMain.qml`
  - Owns: Quick sidebar data, Quick validation/Muri data, and `QWindow*` bridge exposure for the legacy chart editor plus timeline surfaces
- Section map:
  - File: `src/app/mainwindow/sections/README.md`
  - Owns: where each MainWindow feature slice lives
- Window frame, menus, toolbar, layout shell:
  - File: `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
  - Owns: actions, menu wiring, splitter/dock/card composition, preview canvas bootstrap

## 3. Document Model, Fields, And File Flow

- Storage model for chart metadata and difficulties:
  - Files: `src/simai/document/SimaiDocument.h`, `src/simai/document/SimaiDocument.cpp`
  - Class: `SimaiDocument`
  - Key functions: `createEmpty`, `fromText`, `toText`, `parseRawFields`, `serializeRawFields`, `ensureDifficulty`, `removeDifficulty`
- Timing metadata extraction for parser / timeline / normalization:
  - Files: `src/simai/document/SimaiTimingMetadata.h`, `src/simai/document/SimaiTimingMetadata.cpp`
  - Namespace: `miacode::simai`
  - Key functions: `buildTimingMetadata`, `buildTimingMetadataFromRawText`, `parseInlineTimeSignatureComment`, `latencyMeterIdForTimingMetadata`
- File open/save/new and field switching:
  - File: `src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp`
  - Key functions: `applyCurrentFieldToDocument`, `onNewFile`, `onOpenFile`, `onSaveFile`, `onSaveFileAs`, `runAutosaveCheck`, `rebuildFieldSidebar`, `populateMetadataPage`, `populateDifficultyPage`, `switchToMetadataField`, `switchToDifficultyField`, `loadDocument`
  - Owns: user-initiated file I/O plus background autosave snapshots under the project-local `.autosave/` directory
- Batch text editing surface:
  - Files: `src/editor/PlainCodeEditor.h`, `src/editor/PlainCodeEditor.cpp`
  - Class: `PlainCodeEditor`
  - Owns: line numbers, transform actions in context menu, editor display behavior

## 4. Parser, Syntax Validation, And Marker Generation

- Parser API:
  - Files: `src/simai/parser/SimaiNativeParser.h`, `src/simai/parser/SimaiNativeParser.Driver.cpp`
  - Class: `SimaiNativeParser`
  - Key functions: `parseForTimeline`, `validateSyntax`, `buildValidationReport`
  - Owns: metadata-aware default meter input, inline `|| x/y` measure restarts, validation/strict-check awareness for time-signature comments
- Parser internals:
  - Files: `src/simai/parser/SimaiNativeParser.cpp`, `src/simai/parser/SimaiNativeParser.Slide.cpp`, `src/simai/parser/SimaiNativeParser.TouchTap.cpp`, `src/simai/parser/SimaiNativeParser.StrictChecks.cpp`
  - Owns: note parsing, slide/wifi semantics, touch/tap parsing, strict-vs-lenient checks
- Main window validation UI:
  - File: `src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`
  - Key functions: `runValidateSimaiSilently`, `runValidateSimai`, `addValidationError`, `addValidationDecoration`, `refreshValidationPanelForActiveField`

## 5. Timeline Data, Cursor Mapping, And Preview Synchronization

- Timeline quick model:
  - Files: `src/timeline/TimelineQuickModel.h`, `src/timeline/TimelineQuickModel.cpp`, `src/timeline/TimelineRenderData.h`
  - Class: `TimelineQuickModel`
  - Owns: lightweight timeline parsing, cursor anchors, preview-follow lookup, and incremental edit application for the editor fast path
- Timeline data model and widget:
  - Files: `src/timeline/TimelineView.h`, `src/timeline/TimelineView.cpp`, `src/timeline/TimelineView.Core.cpp`, `src/timeline/TimelineView.Interaction.cpp`, `src/timeline/TimelineView.Paint.cpp`
  - Class: `TimelineView`
  - Owns: lightweight timeline rendering, visible-range painting, playhead/cursor movement, waveform strip, follow-preview behavior
- Timeline fast/slow refresh orchestration:
  - File: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Key functions: `applyTimelineQuickChange`, `refreshTimelineQuickModelFromCurrentText`, `scheduleTimelineRefresh`, `requestTimelineSlowRefresh`, `dispatchTimelineSlowRefresh`, `scheduleTimelineAnalysisRefresh`, `scheduleTimelineAnalysisRefreshFromLatestPreviewState`, `dispatchTimelineAnalysisRefresh`, `seekTimelineToCursor`, `syncTimelineToEditorCursor`, `navigateTimelineToSecond`, `updatePreviewSliderRange`, `updatePreviewObjectStats`, `updatePreviewWorkspaceLayout`, `updatePreviewPanelLayout`
- Timeline slow refresh workers:
  - Files: `src/timeline/TimelineSlowRefresh.h`, `src/timeline/TimelineSlowRefresh.cpp`
  - Owns: full parser refresh, preview snapshot publication, and combined latest-only analysis result building for validation plus Muri
- Timing getters and timing writes:
  - File: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Key functions: `currentTimingMetadata`, `parsedFirstSeconds`, `parsedWholeBpm`, `parsedLatencyMeterId`, `applyLatencyDetectorOffset`, `applyLatencyDetectorBpm`
  - Owns: metadata-driven default meter read path for timeline / validation / latency-detector defaults; latency detector no longer writes meter fields back into the document

## 6. Preview Video, Media, And Render State

- Runtime preview host and Quick bridge:
  - Files: `src/preview/runtime/PreviewRuntime.h`, `src/preview/runtime/PreviewRuntime.cpp`, `src/preview/runtime/PreviewSceneAssetLoader.h`, `src/preview/runtime/PreviewSceneAssetLoader.cpp`, `src/preview/runtime/PreviewSceneAssetRepository.h`, `src/preview/runtime/PreviewSceneAssetRepository.cpp`, `src/preview/runtime/PreviewQuickRuntimeSurface.h`, `src/preview/runtime/PreviewQuickRuntimeSurface.cpp`, `src/preview/runtime/PreviewQuickExportSession.h`, `src/preview/runtime/PreviewQuickExportSession.cpp`, `src/preview/runtime/qml/PreviewRuntimeView.qml`
  - Classes: `PreviewRuntime`, `PreviewSceneAssetLoader`, `PreviewSceneAssetRepository`, `PreviewQuickRuntimeSurface`, `PreviewQuickExportSession`
  - Owns: on-screen preview host window (`QQuickView`), frame-swapped pacing signal, the runtime-facing preview setter surface used by `MainWindow`, shared skin/outline/judge asset ownership for both realtime preview and export, and the headless Qt Quick export session that renders `PreviewQuickSceneRoot` from a direct `PreviewFrameState` plus layer flags
- Backend-neutral scene-state and timing helpers:
  - Files: `src/preview/scene/PreviewFrameState.h`, `src/preview/scene/PreviewLayerOrder.h`, `src/preview/scene/PreviewOpacityCurves.h`, `src/preview/scene/PreviewOpacityCurves.cpp`, `src/preview/scene/PreviewSceneGeometry.h`, `src/preview/scene/PreviewSceneGeometry.cpp`, `src/preview/scene/PreviewHudState.h`, `src/preview/scene/PreviewHudState.cpp`, `src/preview/scene/PreviewProgressStatsCache.h`, `src/preview/scene/PreviewProgressStatsCache.cpp`, `src/preview/scene/PreviewPreparedSceneCache.h`, `src/preview/scene/PreviewPreparedSceneCache.cpp`
  - Owns: shared preview frame payloads, layer flags/order, opacity/time curves, stage/playfield geometry helpers, HUD stats/time formatting, the prebuilt stats cache shared by Quick HUD/main-window side stats/export HUD rendering, and the per-layer prepared note windows used by realtime preview and export Quick scene roots
- Quick scene-graph layers:
  - Files: `src/preview/quick_scene/PreviewQuickSceneRoot.h`, `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`, `src/preview/quick_scene/PreviewQuickStageBackgroundLayer.h`, `src/preview/quick_scene/PreviewQuickStageBackgroundLayer.cpp`, `src/preview/quick_scene/PreviewQuickBackdropLayer.h`, `src/preview/quick_scene/PreviewQuickBackdropLayer.cpp`, `src/preview/quick_scene/PreviewQuickGuideLayer.h`, `src/preview/quick_scene/PreviewQuickGuideLayer.cpp`, `src/preview/quick_scene/PreviewQuickTrackLayer.h`, `src/preview/quick_scene/PreviewQuickTrackLayer.cpp`, `src/preview/quick_scene/PreviewQuickSlideMotionLayer.h`, `src/preview/quick_scene/PreviewQuickSlideMotionLayer.cpp`, `src/preview/quick_scene/PreviewQuickJudgeEffectLayer.h`, `src/preview/quick_scene/PreviewQuickJudgeEffectLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchJudgeLayer.h`, `src/preview/quick_scene/PreviewQuickTouchJudgeLayer.cpp`, `src/preview/quick_scene/PreviewQuickHeadLayer.h`, `src/preview/quick_scene/PreviewQuickHeadLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchLayer.h`, `src/preview/quick_scene/PreviewQuickTouchLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchHoldLayer.h`, `src/preview/quick_scene/PreviewQuickTouchHoldLayer.cpp`, `src/preview/quick_scene/PreviewQuickChartReviewLayer.h`, `src/preview/quick_scene/PreviewQuickChartReviewLayer.cpp`, `src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h`, `src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.cpp`, `src/preview/quick_scene/PreviewQuickMuriPadLayer.h`, `src/preview/quick_scene/PreviewQuickMuriPadLayer.cpp`, `src/preview/quick_scene/PreviewQuickMuriActionLayer.h`, `src/preview/quick_scene/PreviewQuickMuriActionLayer.cpp`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.h`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.cpp`, `src/preview/quick_scene/PreviewQuickCircleNodes.h`, `src/preview/quick_scene/PreviewQuickCircleNodes.cpp`, `src/preview/quick_scene/PreviewQuickSpriteNodes.h`, `src/preview/quick_scene/PreviewQuickSpriteNodes.cpp`, `src/preview/quick_scene/PreviewQuickArcNodes.h`, `src/preview/quick_scene/PreviewQuickArcNodes.cpp`, `src/preview/quick_scene/PreviewQuickSectorNodes.h`, `src/preview/quick_scene/PreviewQuickSectorNodes.cpp`, `src/preview/quick_scene/PreviewQuickHudLayer.h`, `src/preview/quick_scene/PreviewQuickHudLayer.cpp`, `src/preview/quick_scene/PreviewTextureRepository.h`, `src/preview/quick_scene/PreviewTextureRepository.cpp`, `src/preview/quick_scene/PreviewQuickSceneInvalidationPolicy.h`
  - Owns: the active Qt Quick/QSG realtime preview and export render stack, including object/effect/diagnostic overlays, shared QSG helper node builders for sprites/arcs/circles/sectors, the additive firework custom-material path, texture caching for Quick textures, invalidation flags, and layer-flag driven sub-selection for headless export
- Preview media controller:
  - Files: `src/preview/video/PreviewMediaController.h`, `src/preview/video/PreviewMediaController.cpp`
  - Class: `PreviewMediaController`
  - Key functions: `initializeBackendObjects`, `setWarmupResolvedMediaPath`, `setChartPath`, `setBackgroundTrackPath`, `setTimelineOffsetSeconds`, `startPlayback`, `syncPlayback`, `resolveMediaPath`
  - Owns: dedicated media-thread runtime objects (`QMediaPlayer` / `QVideoSink` / `QAudioOutput`) plus warmup-resolved media-path reuse
- Preview integration helper:
  - Files: `src/preview/PreviewIntegration.h`, `src/preview/PreviewIntegration.cpp`
  - Owns: side-by-side legacy preview placement helpers

## 7. Preview Audio And SFX Scheduling

- Preview SFX runtime:
  - Files: `src/preview/audio/QtPreviewSfxRuntime.h`, `src/preview/audio/QtPreviewSfxRuntime.cpp`
  - Class: `QtPreviewSfxRuntime`
  - Owns: miniaudio engine state, prepared asset paths, prepared timeline program, playback session state, clip banks, touchhold voice control, background track playback
- Split responsibilities:
  - `QtPreviewSfxRuntime.Assets.cpp`: chart track resolution, SFX dir resolution, bank resets
  - `QtPreviewSfxRuntime.Timeline.cpp`: event generation from `TimelineNoteMarker`
  - `QtPreviewSfxRuntime.Background.cpp`: BGM start/seek/sync/audition
  - `QtPreviewSfxRuntime.Engine.cpp`, `QtPreviewSfxRuntime.Voices.cpp`: engine and voice internals
- Main window hooks:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key functions: `ensurePreviewSfxRuntimePrepared`, `applyPreviewAudioSettingsToRuntime`, `schedulePreviewSubsystemWarmup`

## 8. Video Export UI, Snapshot Boundary, And Encoder Pipeline

- Export dialog:
  - Files: `src/tools/video_export/VideoExportDialog.h`, `src/tools/video_export/VideoExportDialog.cpp`, `src/tools/video_export/VideoExportPreferences.h`
  - Class: `VideoExportDialog`
  - Owns: export parameters, export-only preference persistence, preview-in-dialog, range selection, live preview controls
- Batch export dialog:
  - Files: `src/tools/video_export/BatchVideoExportDialog.h`, `src/tools/video_export/BatchVideoExportDialog.cpp`, `src/tools/video_export/VideoExportPreferences.h`
  - Class: `BatchVideoExportDialog`
  - Owns: chart-folder batch export setup, shared export settings UI, and application-scoped export preset / resolution / FPS persistence for batch runs
- Export task and controller:
  - Files: `src/tools/video_export/VideoExportController.h`, `src/tools/video_export/VideoExportController.cpp`, `src/tools/video_export/VideoExportQuickRenderBackend.h`, `src/tools/video_export/VideoExportQuickRenderBackend.cpp`, `src/tools/video_export/RawVideoPipeTransport.h`, `src/tools/video_export/RawVideoPipeTransport.cpp`
  - Classes: `VideoExportController`, `VideoExportQuickRenderBackend`
  - Key functions: `exportFullPreview`, `exportPreparedTask`, `chooseVideoEncoder`, `VideoExportQuickRenderBackend::bootstrap`, `miacode::video_export::raw_pipe::enqueueRawVideoFrame`
- Export snapshot boundary:
  - Files: `src/tools/video_export/VideoExportSnapshot.h`, `src/tools/video_export/VideoExportSnapshot.cpp`
  - Struct: `VideoExportSnapshot`
  - Key functions: `toJson`, `fromJson`, `buildVideoExportTaskFromSnapshot`
- Main window export ownership:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key functions: `onExportPreviewVideo`, `buildVideoExportSnapshot`, `launchVideoExportWorker`, `handleVideoExportWorkerEvent`

## 9. BPM And Offset Detection

- Dialog shell:
  - Files: `src/tools/latency/LatencyDetectorDialog.h`, `src/tools/latency/LatencyDetectorDialog.cpp`
  - Class: `LatencyDetectorDialog`
  - Owns: dialog lifetime, waveform widget, meter presets, decoded audio buffers
- Analysis:
  - File: `src/tools/latency/LatencyDetectorDialog.Analysis.cpp`
  - Key functions: `detectBpm`, `detectOffset`, `parsedBpm`, `parsedOffset`, `selectedOffsetSnapModeId`
- Playback and UI:
  - Files: `src/tools/latency/LatencyDetectorDialog.Playback.cpp`, `src/tools/latency/LatencyDetectorDialog.Ui.cpp`
  - Owns: local transport controls, beat audition, visible-range tracking
- Main window entry:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key function: `onOpenLatencyDetector`

## 10. Muri Analysis And Static Diagnostics

- Runtime-style analysis:
  - Files: `src/tools/muri/MuriAnalyzer.h`, `src/tools/muri/MuriAnalyzer.cpp`
  - Class: `MuriAnalyzer`
  - Key function: `analyze`
- Static references and thresholds:
  - Files: `src/tools/muri/MuriStaticChecker.h`, `src/tools/muri/MuriStaticChecker.cpp`
  - Key function: `buildStaticMuriReferences`
- Panel entry shaping and visible-list dedupe:
  - Files: `src/tools/muri/MuriPanelEntries.h`, `src/tools/muri/MuriPanelEntries.cpp`
  - Key function: `buildVisibleMuriPanelEntries`
- Main window usage:
  - File: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Key functions: `scheduleTimelineAnalysisRefresh`, `scheduleTimelineAnalysisRefreshFromLatestPreviewState`, `dispatchTimelineAnalysisRefresh`

## 11. Batch Transforms And Authoring Helpers

- Chart transforms:
  - Files: `src/simai/transform/ChartBatchTransform.h`, `src/simai/transform/ChartBatchTransform.cpp`
  - Namespace: `miacode::chart_transform`
  - Key functions: `transformChartText`, `toggleBreakForSelection`, `toggleExForSelection`, `toggleFireworkForSelection`, `randomRotateForSelection`
- Whole-chart normalization:
  - Files: `src/simai/transform/ChartNormalization.h`, `src/simai/transform/ChartNormalization.cpp`
  - Namespace: `miacode::chart_transform`
  - Key function: `normalizeChartText`
  - Owns: current-difficulty full-chart normalization, one-measure-per-line rebuild, canonical modifier order, metadata-aware measure splitting, ordinary `||` comment preservation via standalone-line splits, per-beat subdivision minimization, and syntax-error blocking
- Main window action entry points:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key functions: `onMirrorLeftRight`, `onMirrorUpDown`, `onRotate180`, `onRotate45CounterClockwise`, `onRotate45Clockwise`, `onNormalizeWholeChart`, `onToggleBreakSelection`, `onToggleExSelection`, `onToggleFireworkSelection`, `onRandomRotateSelection`

## 12. Build, Packaging, And Dev-Only Helper Binaries

- CMake targets:
  - File: `CMakeLists.txt`
  - Owns: app target plus dev helper binaries such as `miacode_muri_dump`, `simai_native_dump`, `soundtouch_probe`, `simai_parser_spec`, `chart_batch_transform_spec`
- Packaging scripts:
  - Files: `scripts/build-win.ps1`, `scripts/package-win.ps1`, `scripts/build-macos.sh`, `scripts/package-mac.sh`
- ffmpeg provisioning:
  - Files: `scripts/ensure-windows-ffmpeg.ps1`, `scripts/ensure-macos-ffmpeg.sh`, `third_party/ffmpeg/README.md`
- Analysis scripts:
  - Files under `scripts/`
  - Owns: export diagnostics, duplicate frame checks, trajectory comparisons, crop analysis

## Update This File When

- A feature owner moves to another file.
- A capability gains a second mirrored implementation path.
- A class or function becomes the new canonical entry point for a feature.
- A tool or helper binary becomes required for debugging a feature area.
