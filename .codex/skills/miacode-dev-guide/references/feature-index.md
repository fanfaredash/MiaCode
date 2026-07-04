# Feature Index

Use this file to map a user-facing feature to the concrete file, class, and function entry points that own it.

## 1. App Boot And Process Modes

- App startup and GUI entry:
  - File: `src/app/main.cpp`
  - Functions: `main`, `setWindowsAppUserModelId`, `wantsQuickShellBeta`, `startupOpenTargetFromArguments`
  - Owns: Qt app startup, theme/font setup, window launch, startup timing log, `--quick-shell-beta` routing, and the first non-option startup path used when dragging a file/folder onto `MiaCode.exe` or a shortcut
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
  - Files: `src/app/mainwindow/MainWindow.h`, `src/app/mainwindow/MainWindow.cpp`, `src/app/mainwindow/sections/preview/MainWindow.PreviewStageMediaRoute.cpp`
  - Class: `MainWindow`
  - Owns: top-level state, shared widgets, preview runtime instances, export worker process, portable/project settings, the route-specific preview stage-media coordinator, and the hidden Quick-shell backend mode that rehosts legacy editor/timeline surfaces for QML
- Quick shell beta bootstrap and controller bridge:
  - Files: `src/app/quick_shell/QuickShellBootstrap.h`, `src/app/quick_shell/QuickShellBootstrap.cpp`, `src/app/quick_shell/QuickShellContracts.h`, `src/app/quick_shell/QuickShellController.h`, `src/app/quick_shell/QuickShellController.cpp`, `src/app/quick_shell/QuickShellNativeSurfaceHost.h`, `src/app/quick_shell/QuickShellNativeSurfaceHost.cpp`, `src/app/quick_shell/QuickShellPreviewCompositeSurface.h`, `src/app/quick_shell/QuickShellPreviewCompositeSurface.cpp`, `src/app/quick_shell/QuickShellStyleBridge.h`, `src/app/quick_shell/QuickShellStyleBridge.cpp`, `src/app/quick_shell/qml/QuickShellMain.qml`, `src/app/quick_shell/qml/BottomTabsQuickHost.qml`, `src/app/quick_shell/qml/QuickShellPreviewSurface.qml`, `src/app/quick_shell/qml/QuickShellPreviewTransport.qml`, `src/app/quick_shell/qml/QuickShellPreviewStatsPanel.qml`, `src/app/ui/WindowParityMetrics.h`, `src/app/ui/WindowParityMetrics.cpp`
  - Classes: `QuickShellBootstrap`, `QuickShellController`, `QuickShellNativeSurfaceHost`, `QuickShellPreviewCompositeSurface`, `QuickShellStyleBridge`
  - Owns: `QQmlApplicationEngine` startup, hybrid-host beta window lifetime, coarse-grained native-region window exposure for top chrome/sidebar/workspace/bottom-tabs/status, preview transport/fullscreen bridge state, bottom-tabs current-tab plus tab-visibility bridge state, the pure-QML embedded/fullscreen preview transport and embedded stats panel, the phase-1 Quick bottom-tabs shell that drives the retained-native tab content surface, the quickshell-only inline preview surface that stacks background media plus scene plus HUD, the dedicated quickshell composite preview window used for video media, and host sizing/theme tokens shared with the `MainWindow` backend
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
  - Key functions: `applyCurrentFieldToDocument`, `onNewFile`, `onOpenFile`, `openStartupTarget`, `onSaveFile`, `onSaveFileAs`, `runAutosaveCheck`, `rebuildFieldSidebar`, `populateMetadataPage`, `populateDifficultyPage`, `switchToMetadataField`, `switchToDifficultyField`, `loadDocument`
  - Owns: user-initiated file I/O, startup file/folder open resolution, plus background autosave snapshots under the project-local `.miacode/.autosave/<chart file>/` container, including overwriteable latest backups named `<chart file>.bak`, timer-driven `history/YYYY-MM-DD-HH-MM-SS.bak` snapshots, and per-file `autosave.json` metadata rebuilds
- Document editor header / page-mode UI:
  - File: `src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp`
  - Key functions: `updateEditorHeader`, `updateEditorHeaderLayoutMode`, `switchToWelcomePage`, `switchToMetadataField`, `switchToDifficultyField`, `activateInitialField`
  - Owns: code-area header context text, difficulty-level/designer controls visibility, welcome/metadata/chart page switching, and the no-difficulty reset path used after loading or creating an empty chart
- Batch text editing surface:
  - Files: `src/editor/PlainCodeEditor.h`, `src/editor/PlainCodeEditor.cpp`
  - Class: `PlainCodeEditor`
  - Owns: line numbers, transform actions in context menu, editor display behavior, half-width chart input normalization

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
  - Files: `src/common/WaveformCache.h`, `src/common/WaveformCache.cpp`, `src/timeline/TimelineView.h`, `src/timeline/TimelineView.cpp`, `src/timeline/TimelineView.Core.cpp`, `src/timeline/TimelineView.Interaction.cpp`, `src/timeline/TimelineView.Paint.cpp`
  - Class: `TimelineView`
  - Owns: waveform cache generation, the widget reference implementation for lightweight timeline rendering, visible-range painting, playhead/cursor movement, waveform strip, follow-preview behavior, and the shared state surface currently mirrored by the Quick timeline route
- Timeline scene-state and Quick surface:
  - Files: `src/common/TimelineThemeConfig.h`, `src/timeline/TimelineNoteAssets.h`, `src/timeline/TimelineNoteAssets.cpp`, `src/timeline/TimelineSceneState.h`, `src/timeline/TimelineSceneState.cpp`, `src/timeline/TimelineSceneStateBuilder.h`, `src/timeline/TimelineSceneStateBuilder.cpp`, `src/timeline/quick/TimelineQuickStateBridge.h`, `src/timeline/quick/TimelineQuickStateBridge.cpp`, `src/timeline/quick/TimelineQuickTextureCache.h`, `src/timeline/quick/TimelineQuickTextureCache.cpp`, `src/timeline/quick/TimelineQuickLayerUtils.h`, `src/timeline/quick/TimelineQuickLayerUtils.cpp`, `src/timeline/quick/TimelineQuickGridLayer.*`, `src/timeline/quick/TimelineQuickWaveformLayer.*`, `src/timeline/quick/TimelineQuickHeaderLayer.*`, `src/timeline/quick/TimelineQuickNotesLayer.*`, `src/timeline/quick/TimelineQuickOverlayLayer.*`, `src/timeline/quick/TimelineQuickItem.h`, `src/timeline/quick/TimelineQuickItem.cpp`, `src/app/quick_shell/qml/TimelineTabSurface.qml`
  - Classes: `TimelineQuickStateBridge`, `TimelineQuickTextureCache`, `TimelineQuickGridLayer`, `TimelineQuickWaveformLayer`, `TimelineQuickHeaderLayer`, `TimelineQuickNotesLayer`, `TimelineQuickOverlayLayer`, `TimelineQuickItem`
  - Owns: the single source of truth for Quick/widget timeline state during Phase2, shared note asset loading for widget plus Quick routes, renderer-facing scene-state extraction from snapshot/view state, layered QSG rendering for grid/waveform/header/notes/overlay, the experimental Quick timeline path under quick-shell, and the Quick-side interaction surface that forwards semantic signals back into `MainWindow`
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
  - Owns: the widget-shell on-screen preview host window (`QQuickView`), frame-swapped pacing signal, the runtime-facing preview setter surface used by `MainWindow`, shared skin/outline/judge asset ownership for both realtime preview and export, and the headless Qt Quick export session that renders `PreviewQuickSceneRoot` from a direct `PreviewFrameState` plus layer flags
- Quickshell external stage-media host:
  - Files: `src/preview/runtime/PreviewStageMediaHost.h`, `src/preview/runtime/PreviewStageMediaHost.cpp`, `src/preview/runtime/qml/PreviewStageMediaItem.qml`, `src/app/quick_shell/qml/QuickShellPreviewSurface.qml`
  - Class: `PreviewStageMediaHost`
  - Key functions: `setWarmupResolvedMediaPath`, `setChartPath`, `attachVideoOutputObject`, `detachVideoOutputObject`, `startPlayback`, `syncPlayback`
  - Owns: the quickshell-beta-only background-media route for both images and videos, including shared media resolution, external `VideoOutput` binding, inline quickshell image presentation, and the media side of the dedicated quickshell video composite-surface handoff
- Backend-neutral scene-state and timing helpers:
  - Files: `src/preview/scene/PreviewFrameState.h`, `src/preview/scene/PreviewLayerOrder.h`, `src/preview/scene/PreviewOpacityCurves.h`, `src/preview/scene/PreviewOpacityCurves.cpp`, `src/preview/scene/PreviewSceneGeometry.h`, `src/preview/scene/PreviewSceneGeometry.cpp`, `src/preview/scene/PreviewHudState.h`, `src/preview/scene/PreviewHudState.cpp`, `src/preview/scene/PreviewProgressStatsCache.h`, `src/preview/scene/PreviewProgressStatsCache.cpp`, `src/preview/scene/PreviewPreparedSceneCache.h`, `src/preview/scene/PreviewPreparedSceneCache.cpp`, `src/preview/scene/PreviewMarkerDrawOrder.h`, `src/preview/scene/PreviewMarkerDrawOrder.cpp`
  - Owns: shared preview frame payloads, layer flags/order, opacity/time curves, stage/playfield geometry helpers, HUD stats/time formatting, the prebuilt stats cache shared by Quick HUD/main-window side stats/export HUD rendering, the per-layer prepared note windows used by realtime preview and export Quick scene roots, and the shared slide/head draw-order comparator used to keep head, track, and slide-motion stacking aligned
- Quick scene-graph layers:
  - Files: `src/preview/quick_scene/PreviewQuickSceneRoot.h`, `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`, `src/preview/quick_scene/PreviewQuickStageBackgroundLayer.h`, `src/preview/quick_scene/PreviewQuickStageBackgroundLayer.cpp`, `src/preview/quick_scene/PreviewQuickBackdropLayer.h`, `src/preview/quick_scene/PreviewQuickBackdropLayer.cpp`, `src/preview/quick_scene/PreviewQuickGuideLayer.h`, `src/preview/quick_scene/PreviewQuickGuideLayer.cpp`, `src/preview/quick_scene/PreviewQuickTrackLayer.h`, `src/preview/quick_scene/PreviewQuickTrackLayer.cpp`, `src/preview/quick_scene/PreviewQuickSlideMotionLayer.h`, `src/preview/quick_scene/PreviewQuickSlideMotionLayer.cpp`, `src/preview/quick_scene/PreviewQuickJudgeEffectLayer.h`, `src/preview/quick_scene/PreviewQuickJudgeEffectLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchJudgeLayer.h`, `src/preview/quick_scene/PreviewQuickTouchJudgeLayer.cpp`, `src/preview/quick_scene/PreviewQuickHeadLayer.h`, `src/preview/quick_scene/PreviewQuickHeadLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchLayer.h`, `src/preview/quick_scene/PreviewQuickTouchLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchHoldLayer.h`, `src/preview/quick_scene/PreviewQuickTouchHoldLayer.cpp`, `src/preview/quick_scene/PreviewQuickChartReviewLayer.h`, `src/preview/quick_scene/PreviewQuickChartReviewLayer.cpp`, `src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h`, `src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.cpp`, `src/preview/quick_scene/PreviewQuickMuriPadLayer.h`, `src/preview/quick_scene/PreviewQuickMuriPadLayer.cpp`, `src/preview/quick_scene/PreviewQuickMuriActionLayer.h`, `src/preview/quick_scene/PreviewQuickMuriActionLayer.cpp`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.h`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.cpp`, `src/preview/quick_scene/PreviewQuickCircleNodes.h`, `src/preview/quick_scene/PreviewQuickCircleNodes.cpp`, `src/preview/quick_scene/PreviewQuickSpriteNodes.h`, `src/preview/quick_scene/PreviewQuickSpriteNodes.cpp`, `src/preview/quick_scene/PreviewQuickArcNodes.h`, `src/preview/quick_scene/PreviewQuickArcNodes.cpp`, `src/preview/quick_scene/PreviewQuickSectorNodes.h`, `src/preview/quick_scene/PreviewQuickSectorNodes.cpp`, `src/preview/quick_scene/PreviewQuickHudLayer.h`, `src/preview/quick_scene/PreviewQuickHudLayer.cpp`, `src/preview/quick_scene/PreviewTextureRepository.h`, `src/preview/quick_scene/PreviewTextureRepository.cpp`, `src/preview/quick_scene/PreviewQuickSceneInvalidationPolicy.h`
  - Owns: the active Qt Quick/QSG realtime preview and export render stack, including object/effect/diagnostic overlays, shared QSG helper node builders for sprites/arcs/circles/sectors, the additive firework custom-material path, texture caching for Quick textures, invalidation flags, and layer-flag driven sub-selection for headless export
- Preview media controller:
  - Files: `src/preview/video/PreviewMediaController.h`, `src/preview/video/PreviewMediaController.cpp`
  - Class: `PreviewMediaController`
  - Key functions: `initializeBackendObjects`, `setWarmupResolvedMediaPath`, `setChartPath`, `setBackgroundTrackPath`, `setTimelineOffsetSeconds`, `startPlayback`, `syncPlayback`, `resolveMediaPath`
  - Owns: dedicated media-thread runtime objects (`QMediaPlayer` / `QVideoSink` / `QAudioOutput`), warmup-resolved media-path reuse, and the widget-shell background-media path for both background images and videos; quickshell video no longer uses this controller for stage-video presentation
- Preview integration helper:
  - Files: `src/preview/PreviewIntegration.h`, `src/preview/PreviewIntegration.cpp`
  - Owns: side-by-side legacy preview placement helpers

## 7. Preview Audio And SFX Scheduling

- Preview SFX runtime:
  - Files: `src/common/PreviewSfxSemantics.h`, `src/common/PreviewSfxTimeline.h`, `src/preview/audio/PreviewAudioBackend.h`, `src/preview/audio/QtPreviewSfxRuntime.h`, `src/preview/audio/QtPreviewSfxRuntime.cpp`, `src/preview/audio/MiniaudioPreviewAudioBackend.h`, `src/preview/audio/MiniaudioPreviewAudioBackend.cpp`, `src/preview/audio/BassPreviewAudioBackend.h`, `src/preview/audio/BassPreviewAudioBackend.cpp`
  - Class: `QtPreviewSfxRuntime`
  - Owns: the preview-audio facade seen by `MainWindow`, backend selection, and the stable prepare / commit / pause / resume / seek surface; `MiniaudioPreviewAudioBackend` remains the non-Windows compatibility path, while `BassPreviewAudioBackend` now owns the Windows real BASS runtime path with repo-local `bass*.dll`, no Windows-side miniaudio fallback, master mixer clock authority, preloaded sample channels inspired by NetPlay's `BassAudioSample`, and backend-side note-SFX draining
- Split responsibilities:
  - `QtPreviewSfxRuntime.Assets.cpp`: miniaudio backend chart track resolution, SFX dir resolution, bank resets
  - `QtPreviewSfxRuntime.Timeline.cpp`: miniaudio backend event generation from `TimelineNoteMarker`
  - `QtPreviewSfxRuntime.Background.cpp`: miniaudio backend BGM start/seek/sync/audition
  - `QtPreviewSfxRuntime.Engine.cpp`, `QtPreviewSfxRuntime.Voices.cpp`: miniaudio backend engine and voice internals
- Main window hooks:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key functions: `ensurePreviewSfxRuntimePrepared`, `applyPreviewAudioSettingsToRuntime`, `schedulePreviewSubsystemWarmup`
- Main window playback-second authority:
  - File: `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp`
  - Key function: `MainWindow::currentPreviewAuthoritativeAudioClockSecond`
  - Owns: the single preview-time audio clock getter for UI follow, export-dialog default position, and weak video late-start alignment

## 8. Video Export UI, Snapshot Boundary, And Encoder Pipeline

- Export dialog:
  - Files: `src/tools/video_export/VideoExportDialog.h`, `src/tools/video_export/VideoExportDialog.cpp`, `src/tools/video_export/VideoExportPreferences.h`, `src/tools/video_export/HudFontSettings.{h,cpp}`
  - Class: `VideoExportDialog`
  - Owns: export parameters, export-only preference persistence, preview-in-dialog, range selection, the export-dialog live preview controls that reuse `MainWindow` preview transport callbacks so pause/seek behavior stays aligned with the main preview route, and the owner-wired settings injection points for the Gameplay and Skin tabs. Skin / judge-line / HUD-font controls are supplied by `DialogsSection::buildSkinSettings(...)`; the old standalone export Font tab and modal HUD-font settings dialog were removed in favor of `miacode::video_export::createHudFontSettingsWidget(...)`.
- Batch export dialog:
  - Files: `src/tools/video_export/BatchVideoExportDialog.h`, `src/tools/video_export/BatchVideoExportDialog.cpp`, `src/tools/video_export/VideoExportPreferences.h`
  - Class: `BatchVideoExportDialog`
  - Owns: chart-folder batch export setup, shared export settings UI, and application-scoped export preset / resolution / FPS persistence for batch runs
- Export task and controller:
  - Files: `src/tools/video_export/VideoExportController.h`, `src/tools/video_export/VideoExportController.cpp`, `src/tools/video_export/VideoExportAudioRenderPlan.h`, `src/tools/video_export/VideoExportAudioRenderPlan.cpp`, `src/tools/video_export/VideoExportAudioBackend.h`, `src/tools/video_export/LegacyExportAudioBackend.h`, `src/tools/video_export/LegacyExportAudioBackend.cpp`, `src/tools/video_export/BassExportAudioBackend.h`, `src/tools/video_export/BassExportAudioBackend.cpp`, `src/tools/video_export/VideoExportQuickRenderBackend.h`, `src/tools/video_export/VideoExportQuickRenderBackend.cpp`, `src/tools/video_export/RawVideoPipeTransport.h`, `src/tools/video_export/RawVideoPipeTransport.cpp`
  - Classes: `VideoExportController`, `VideoExportQuickRenderBackend`, `LegacyExportAudioBackend`, `BassExportAudioBackend`
  - Key functions: `exportFullPreview`, `exportPreparedTask`, `buildVideoExportAudioRenderPlan`, `VideoExportAudioBackend::renderMixedTrackToWav`, `chooseVideoEncoder`, `VideoExportQuickRenderBackend::bootstrap`, `miacode::video_export::raw_pipe::enqueueRawVideoFrame`
- Export snapshot boundary:
  - Files: `src/tools/video_export/VideoExportSnapshot.h`, `src/tools/video_export/VideoExportSnapshot.cpp`
  - Struct: `VideoExportSnapshot`
  - Key functions: `toJson`, `fromJson`, `buildVideoExportTaskFromSnapshot`
- Main window export ownership:
  - Files: `src/app/mainwindow/MainWindow.cpp`, `src/app/mainwindow/sections/export/MainWindow.ExportSection.cpp`, `src/app/mainwindow/sections/export/MainWindow.ExportFlow.cpp`, `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.ExportSettings.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrapFinalize.cpp`, `src/tools/export_page/ExportLauncherPage.*`
  - Key functions: `onExportCover`, `onBatchExportPreviewVideo`, `onExportPreviewVideo`, `buildVideoExportSnapshot`, `launchVideoExportWorker`, `handleVideoExportWorkerEvent`, `DialogsSection::buildSkinSettings`, `DialogsSection::openSkinSettingsDialog`
  - Owns: toolbar/menu entry points for export and skin settings, including the shared Skin popup used by the main window and injected into the export dialog's Skin tab
- Cover export studio:
  - Files: `src/tools/cover_export/CoverStudioWindow.*`, `CoverStudioPanel.*`, `CoverLayerListPanel.*`, `CoverLayerListModel.*`, `CoverLayoutModel.*`, `CoverCompositionState.*`, `CoverComposerView.*`, `SceneFrameRenderer.*`, `src/intro/qml/CoverComposer.qml`, `src/app/mainwindow/sections/export/MainWindow.ExportFlow.cpp`
  - Classes: `CoverStudioWindow`, `CoverStudioPanel`, `CoverLayerListPanel`, `CoverLayerListModel`, `CoverLayoutModel`, `CoverCompositionState`, `CoverComposerView`, `SceneFrameRenderer`
  - Owns: cover composition UI, multi chart-frame layer state, custom layer-list delegate and inline visibility/lock controls, v1/v2/v3 `.miacover` JSON migration, app-scoped layout presets/recent layout files, active-live-frame editing, chart-frame inner background modes, cached still refresh, and final PNG/JPG cover export beside the chart

## 9. BPM And Offset Detection

- Latency page shell:
  - Files: `src/tools/latency/LatencyDetectionPage.h`, `src/tools/latency/LatencyDetectionPage.cpp`
  - Class: `LatencyDetectionPage`
  - Owns: embedded latency settings page, BPM / offset / `clock_count` controls, track-envelope preview, auto-detect buttons, and latency-audition UI
- Analysis:
  - Files: `src/tools/latency/LatencyAnalysis.h`, `src/tools/latency/LatencyAnalysis.cpp`
  - Namespace: `miacode::latency`
  - Owns: direct audio decoding for BPM / offset detection and audio envelope extraction
- Audition sandbox:
  - Files: `src/tools/latency/LatencySandboxController.h`, `src/tools/latency/LatencySandboxController.cpp`, `src/tools/latency/LatencyTestChartBuilder.h`, `src/tools/latency/LatencyTestChartBuilder.cpp`
  - Class: `LatencySandboxController`
  - Owns: temporary latency-audition chart/timeline state, transport controls, page-local SFX volume, and restoration of the original preview timeline
- Main window entry:
  - Files: `src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp`, `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
  - Key functions: `switchToLatencyField`, latency settings page actions
- Metadata writeback:
  - Files: `src/tools/latency/LatencyDetectionPage.cpp`, `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`, `src/core/chart/document/SimaiDocument.cpp`
  - Owns: latency-page BPM / offset / `clock_count` controls, BPM auto-detection meter-to-`clock_count` writeback, and default `&clock_count=4` materialization in metadata extra fields

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
  - Files: `src/core/chart/transform/ChartBatchTransform.h`, `src/core/chart/transform/ChartBatchTransform.cpp`
  - Namespace: `miacode::chart_transform`
  - Key functions: `transformChartText`, `toggleBreakForSelection`, `toggleExForSelection`, `toggleFireworkForSelection`, `randomRotateForSelection`
- Whole-chart normalization:
  - Files: `src/core/chart/transform/ChartNormalization.h`, `src/core/chart/transform/ChartNormalization.cpp`
  - Namespace: `miacode::chart_transform`
  - Key function: `normalizeChartText`
  - Owns: current-difficulty full-chart normalization, one-measure-per-line rebuild, canonical modifier order, metadata-aware measure splitting, ordinary `||` comment preservation via standalone-line splits, per-beat subdivision minimization, and syntax-error blocking
- Main window action entry points:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key functions: `onMirrorLeftRight`, `onMirrorUpDown`, `onRotate180`, `onRotate45CounterClockwise`, `onRotate45Clockwise`, `onNormalizeWholeChart`, `onToggleBreakSelection`, `onToggleExSelection`, `onToggleFireworkSelection`, `onRandomRotateSelection`, `onClearCompleteElementsSelection`, `onRaiseSubdivisionHalfStepSelection`, `onLowerSubdivisionHalfStepSelection`

## 12. Toolbox Media Utilities

- Prepend blank media:
  - Files: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp`, `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
  - Key functions: `MainWindow::onPrependTrackSilence`, `MainWindow::onPrependPvBlack`, `MainWindow::onCompressBackgroundVideo`, `MainWindow::onConvertTrackTo44100Hz`, `MainWindow::DialogsSection::onPrependMediaBlank`
  - Owns: the `Audio/Video Processing` toolbox submenu. It splits `x` beats at BPM `y` into separate actions for prepending silence to sibling `track.mp3` or black video to the resolved chart background video (`&video=` override first, then `bg.mp4` / `pv.mp4` fallback), writing `track_bak.mp3` or `<video-stem>_bak.mp4` backups before replacing the selected original. It also contains one-click media normalization actions: compress the resolved chart background video under 20 MiB and convert `track.mp3` to 44100 Hz. Dialog defaults use `&clock_count=` / `&clockcount=` for beat count (fallback `4`) and `&wholebpm=` before the first half-width chart BPM token such as `(185)` (fallback `120`).
- Net batch download:
  - Files: `src/tools/net/NetClient.*`, `src/tools/net/NetBatchDownloadWorker.*`, `src/tools/net/NetBatchDownloadDialog.*`, `src/app/mainwindow/sections/export/MainWindow.ExportSection.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
  - Classes: `NetClient`, `NetBatchDownloadWorker`, `NetBatchDownloadDialog`
  - Owns: top-level Tools menu and toolbox entries for querying Net public charts by uploader ID, tag, or song title, filtering by local date range against the API `timestamp`, selecting rows, streaming `track.mp3` / `bg.jpg` / `maidata.txt` into one folder per chart on a background download thread, optionally writing an extra zip after successful folder download, remembering the last valid output directory in app preferences, and showing query/download diagnostic logs with per-resource speed and slowest-resource summaries. When a user ID is provided, the query prefers the uploader list and applies local ID/tag/title filtering, with an optional fuzzy case-insensitive mode exposed in the dialog.

## 13. Build, Packaging, And Dev-Only Helper Binaries

- CMake targets:
  - File: `CMakeLists.txt`
  - Owns: app target plus dev helper binaries such as `miacode_muri_dump`, `simai_native_dump`, `soundtouch_probe`, `simai_parser_spec`, `chart_batch_transform_spec`
- Packaging scripts:
  - Files: `scripts/build/build-win.ps1`, `scripts/build/package-win.ps1`, `scripts/build/build-macos.sh`, `scripts/build/package-mac.sh`
- ffmpeg provisioning:
  - Files: `scripts/ffmpeg/ensure-windows-ffmpeg.ps1`, `scripts/ffmpeg/ensure-macos-ffmpeg.sh`, `third_party/ffmpeg/README.md`
- Analysis scripts:
  - Files under `scripts/`
  - Owns: export diagnostics, duplicate frame checks, trajectory comparisons, crop analysis

## Update This File When

- A feature owner moves to another file.
- A capability gains a second mirrored implementation path.
- A class or function becomes the new canonical entry point for a feature.
- A tool or helper binary becomes required for debugging a feature area.
