# Feature Index

Use this file to map a user-facing feature to the concrete file, class, and function entry points that own it.

## 1. App Boot And Process Modes

- App startup and GUI entry:
  - File: `src/app/main.cpp`
  - Functions: `main`, `setWindowsAppUserModelId`
  - Owns: Qt app startup, theme/font setup, window launch, startup timing log
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
  - Owns: top-level state, shared widgets, preview runtime instances, export worker process, portable/project settings
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
- File open/save/new and field switching:
  - File: `src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp`
  - Key functions: `applyCurrentFieldToDocument`, `onNewFile`, `onOpenFile`, `onSaveFile`, `onSaveFileAs`, `rebuildFieldSidebar`, `populateMetadataPage`, `populateDifficultyPage`, `switchToMetadataField`, `switchToDifficultyField`, `loadDocument`
- Batch text editing surface:
  - Files: `src/editor/PlainCodeEditor.h`, `src/editor/PlainCodeEditor.cpp`
  - Class: `PlainCodeEditor`
  - Owns: line numbers, transform actions in context menu, editor display behavior

## 4. Parser, Syntax Validation, And Marker Generation

- Parser API:
  - Files: `src/simai/parser/SimaiNativeParser.h`, `src/simai/parser/SimaiNativeParser.Driver.cpp`
  - Class: `SimaiNativeParser`
  - Key functions: `parseForTimeline`, `validateSyntax`, `buildValidationReport`
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
  - Key functions: `applyTimelineQuickChange`, `refreshTimelineQuickModelFromCurrentText`, `scheduleTimelineRefresh`, `requestTimelineSlowRefresh`, `dispatchTimelineSlowRefresh`, `seekTimelineToCursor`, `syncTimelineToEditorCursor`, `navigateTimelineToSecond`, `updatePreviewSliderRange`, `updatePreviewObjectStats`, `updatePreviewWorkspaceLayout`, `updatePreviewPanelLayout`
- Timeline slow refresh workers:
  - Files: `src/timeline/TimelineSlowRefresh.h`, `src/timeline/TimelineSlowRefresh.cpp`
  - Owns: full parser refresh, shifted marker generation, validation report building, and latest-only Muri background work
- Timing getters and timing writes:
  - File: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Key functions: `parsedFirstSeconds`, `parsedWholeBpm`, `parsedLatencyMeterId`, `applyLatencyDetectorOffset`, `applyLatencyDetectorBpm`, `applyLatencyDetectorMeter`

## 6. Preview Video, Media, And Render State

- Preview canvas core:
  - Files: `src/preview/video/PreviewCanvas.h`, `src/preview/video/PreviewCanvas.cpp`
  - Class: `PreviewCanvas`
  - Owns: render state, asset-backed drawing, effect curves, caches, offscreen export rendering support
- Preview canvas split files:
  - `PreviewCanvas.Runtime.cpp`: state mutation, offscreen renderer setup, frame generation
  - `PreviewCanvas.Objects.cpp`: actual object/effect/HUD drawing
  - `PreviewCanvas.Render.cpp`: render loop and painter path
  - `PreviewCanvas.GLAndTransforms.cpp`: GL and transform helpers
  - `PreviewCanvas.SkinAndAtlas.cpp`: async skin load, atlas rebuild, texture prewarm
- Preview media controller:
  - Files: `src/preview/video/PreviewMediaController.h`, `src/preview/video/PreviewMediaController.cpp`
  - Class: `PreviewMediaController`
  - Key functions: `setChartPath`, `setBackgroundTrackPath`, `setTimelineOffsetSeconds`, `startPlayback`, `syncPlayback`, `resolveMediaPath`
- Preview integration helper:
  - Files: `src/preview/PreviewIntegration.h`, `src/preview/PreviewIntegration.cpp`
  - Owns: side-by-side legacy preview placement helpers

## 7. Preview Audio And SFX Scheduling

- Preview SFX runtime:
  - Files: `src/preview/audio/QtPreviewSfxRuntime.h`, `src/preview/audio/QtPreviewSfxRuntime.cpp`
  - Class: `QtPreviewSfxRuntime`
  - Owns: miniaudio engine state, clip banks, touchhold voice control, background track playback
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
  - Files: `src/tools/video_export/VideoExportDialog.h`, `src/tools/video_export/VideoExportDialog.cpp`
  - Class: `VideoExportDialog`
  - Owns: export parameters, preview-in-dialog, range selection, live preview controls
- Export task and controller:
  - Files: `src/tools/video_export/VideoExportController.h`, `src/tools/video_export/VideoExportController.cpp`, `src/tools/video_export/RawVideoPipeTransport.h`, `src/tools/video_export/RawVideoPipeTransport.cpp`
  - Class: `VideoExportController`
  - Key functions: `exportFullPreview`, `exportPreparedTask`, `chooseVideoEncoder`, `miacode::video_export::raw_pipe::enqueueRawVideoFrame`
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
  - Key functions: `scheduleDeferredMuriRefresh`, `refreshDeferredMuriDiagnostics`

## 11. Batch Transforms And Authoring Helpers

- Chart transforms:
  - Files: `src/simai/transform/ChartBatchTransform.h`, `src/simai/transform/ChartBatchTransform.cpp`
  - Namespace: `miacode::chart_transform`
  - Key functions: `transformChartText`, `toggleBreakForSelection`, `toggleExForSelection`, `toggleFireworkForSelection`, `randomRotateForSelection`
- Main window action entry points:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key functions: `onMirrorLeftRight`, `onMirrorUpDown`, `onRotate180`, `onRotate45CounterClockwise`, `onRotate45Clockwise`, `onToggleBreakSelection`, `onToggleExSelection`, `onToggleFireworkSelection`, `onRandomRotateSelection`

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
