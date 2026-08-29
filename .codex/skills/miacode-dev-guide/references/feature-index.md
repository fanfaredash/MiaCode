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
  - Files: `src/app/quick_shell/QuickShellBootstrap.h`, `src/app/quick_shell/QuickShellBootstrap.cpp`, `src/app/quick_shell/QuickShellContracts.h`, `src/app/quick_shell/QuickShellController.h`, `src/app/quick_shell/QuickShellController.cpp`, `src/app/quick_shell/QuickShellNativeSurfaceHost.h`, `src/app/quick_shell/QuickShellNativeSurfaceHost.cpp`, `src/app/quick_shell/QuickShellPreviewCompositeSurface.h`, `src/app/quick_shell/QuickShellPreviewCompositeSurface.cpp`, `src/app/quick_shell/QuickShellStyleBridge.h`, `src/app/quick_shell/QuickShellStyleBridge.cpp`, `src/app/quick_shell/qml/QuickShellMain.qml`, `src/app/quick_shell/qml/BottomTabsQuickHost.qml`, `src/app/quick_shell/qml/TimelineTabSurface.qml`, `src/app/quick_shell/qml/QuickShellPreviewSurface.qml`, `src/app/quick_shell/qml/QuickShellPreviewTransport.qml`, `src/app/quick_shell/qml/QuickShellPreviewStatsPanel.qml`, `src/app/quick_shell/qml/components/*.qml`, `src/app/ui/WindowParityMetrics.h`, `src/app/ui/WindowParityMetrics.cpp`
  - Classes: `QuickShellBootstrap`, `QuickShellController`, `QuickShellNativeSurfaceHost`, `QuickShellPreviewCompositeSurface`, `QuickShellStyleBridge`
  - Owns: `QQmlApplicationEngine` startup, hybrid-host beta window lifetime, coarse-grained native-region window exposure for top chrome/sidebar/workspace/bottom-tabs/status, preview transport/fullscreen bridge state, bottom-tabs current-tab plus tab-visibility bridge state, shared Quick shell QML controls (`Theme`, `ShellToolButton`, `ShellCheckBox`, `ShellSlider`), the pure-QML embedded/fullscreen preview transport and embedded stats panel, the phase-1 Quick bottom-tabs shell that drives the retained-native tab content surface, the quickshell-only inline preview surface that stacks background media plus scene plus HUD, the dedicated quickshell composite preview window used for video media, and host sizing/theme tokens shared with the `MainWindow` backend
- Section map:
  - File: `src/app/mainwindow/sections/README.md`
  - Owns: where each MainWindow feature slice lives
- Shared Widgets UI components:
  - Files: `src/app/ui/UiComponents.h`, `src/app/ui/UiComponents.cpp`, `src/app/ui/UiTheme.h`, `src/app/ui/UiTheme.cpp`, `src/app/ui/DialogLocalization.h`
  - Namespace/classes: `miacode::ui`, `miacode::ui::TabbedSettingsDialog`, `UiTheme`, `UiDialogs`
  - Owns: shared QWidget dialog/card/form/slider-value row/slider option/button composition helpers, tabbed settings-dialog shell setup, dialog button-box helpers, dialog dropdown/list popup item helpers, common tab-width pinning, theme stylesheet entry points, and dialog localization/window-preparation behavior reused by MainWindow sections and tools.
  - Canonical dialog dropdown entry points (the fixed rounded-translucent popup): `miacode::ui::createDialogComboBox`/`applyDialogComboBoxStyle` — every single-select dropdown must be a real `QComboBox` through these (user decision 2026-07-10: pseudo-dropdown popups were rejected for width/height mismatch and missing native animation); `miacode::ui::createDialogDropdownButton` (QToolButton+QMenu) is only for popups a combobox cannot express, e.g. multi-select checkbox lists. Both route through `UiTheme::prepareDialogDropdownPopupWindow` (translucent popup window + single panel painter — never give the popup view an opaque QSS background, see qt-ui-layout-pitfalls W8).
  - Dropdown text alignment + height live in the ONE chokepoint `UiTheme::styleDialogComboBox` (reads the `miacode.combo_text_alignment` widget property, default centered). `createDialogComboBox(parent, maxVisible, textAlignment)` just stamps that property; direct `styleDialogComboBox` callers that aren't the factory (cover panels, HUD-font picker) set the property to `Qt::AlignLeft|Qt::AlignVCenter` themselves. Alignment is applied two ways: (a) padding — centered uses `padding-left = drop-down width(20) + padding-right(10) = 30` so text optically centres in the FULL box (the flat look suppresses the arrow via `::down-arrow{width;height}` with no image, so the reserved right column would otherwise shift centred text left); left uses `padding: 2px 24px 2px 10px`. (b) per-item `Qt::TextAlignmentRole` for the closed label + popup rows. The height (W1 `max(sizeHint,30)+4`) is fixed HERE from the FINAL populated sizeHint — never at empty-combo creation — so always re-run `applyDialogComboBoxStyle` after `clear()`/`addItem()`. Never style `::drop-down`/`::down-arrow` if you want the native Fusion arrow back — any drop-down QSS rule suppresses it. Multi-select pseudo-dropdowns that a combobox can't express (e.g. 判定效果显示) use `createDialogDropdownButton` which styles a QToolButton+QMenu via `UiTheme::dialogComboLikeButtonStyleSheet` to match a combo's closed box (same box/height, centered text via symmetric padding — no arrow reservation to offset). Tab dialogs whose pages carry these controls must call `miacode::ui::pinTabWidgetToContentHeight` after `addTab()`s or the tallest tab's bottom row clips (QSS `::pane` padding absent from tab sizeHint — qt-ui-layout-pitfalls W2).
- Window frame, menus, toolbar, layout shell:
  - File: `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
  - Owns: actions, menu wiring, splitter/dock/card composition, preview canvas bootstrap

## 3. Document Model, Fields, And File Flow

- Storage model for chart metadata and difficulties:
  - Files: `src/core/chart/document/SimaiDocument.h`, `src/core/chart/document/SimaiDocument.cpp`
  - Class: `SimaiDocument`
  - Key functions: `createEmpty`, `fromText`, `toText`, `parseRawFields`, `serializeRawFields`, `ensureDifficulty`, `removeDifficulty`
- Timing metadata extraction for parser / timeline / normalization:
  - Files: `src/core/chart/document/SimaiTimingMetadata.h`, `src/core/chart/document/SimaiTimingMetadata.cpp`
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
- Metadata-page chart media import:
  - Files: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`, `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.TrackMetadata.cpp`, `src/common/ChartMediaImport.h`
  - Key functions: `MainWindow::DialogsSection::onImportBackgroundImage`, `MainWindow::DialogsSection::onImportBackgroundVideo`, `miacode::chart_media_import::importToChartDirectory`
  - Owns: direct file import for JPG/JPEG/PNG jacket art and MP4 background video through the platform-native picker (the Windows path calls `GetOpenFileNameW` with the visible QuickShell root HWND so the Open-button result returns reliably), canonical `bg.<source-extension>` / `pv.mp4` naming, transactional replacement of shadowing same-kind candidates while preserving uniquely numbered `<stem>_bak[_N].<ext>` copies, preview-media release/reload, and `&video=pv.mp4` writeback so preview and export select the imported PV
- Batch text editing surface:
  - Files: `src/editor/PlainCodeEditor.h`, `src/editor/PlainCodeEditor.cpp`
  - Class: `PlainCodeEditor`
  - Owns: line numbers, transform actions in context menu, editor display behavior, half-width chart input normalization

## 4. Parser, Syntax Validation, And Marker Generation

- Parser API:
  - Files: `src/core/chart/parser/SimaiNativeParser.h`, `src/core/chart/parser/SimaiNativeParser.Driver.cpp`
  - Class: `SimaiNativeParser`
  - Key functions: `parseForTimeline`, `validateSyntax`, `buildValidationReport`
  - Owns: metadata-aware default meter input, inline `|| x/y` measure restarts, validation/strict-check awareness for time-signature comments
- Parser internals:
  - Files: `src/core/chart/parser/SimaiNativeParser.cpp`, `src/core/chart/parser/SimaiNativeParser.Slide.cpp`, `src/core/chart/parser/SimaiNativeParser.TouchTap.cpp`, `src/core/chart/parser/SimaiNativeParser.StrictChecks.cpp`
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
  - Files: `src/preview/runtime/PreviewRuntime.h`, `src/preview/runtime/PreviewRuntime.cpp`, `src/preview/runtime/PreviewSceneAssetLoader.h`, `src/preview/runtime/PreviewSceneAssetLoader.cpp`, `src/preview/runtime/PreviewSceneAssetRepository.h`, `src/preview/runtime/PreviewSceneAssetRepository.cpp`, `src/preview/runtime/PreviewQuickRuntimeSurface.h`, `src/preview/runtime/PreviewQuickRuntimeSurface.cpp`, `src/preview/runtime/PreviewQuickExportSession.h`, `src/preview/runtime/PreviewQuickExportSession.cpp`, `src/preview/runtime/PreviewQuickD3D11ExportSession.h`, `src/preview/runtime/PreviewQuickD3D11ExportSession.cpp`, `src/preview/runtime/qml/PreviewRuntimeView.qml`
  - Classes: `PreviewRuntime`, `PreviewSceneAssetLoader`, `PreviewSceneAssetRepository`, `PreviewQuickRuntimeSurface`, `PreviewQuickExportSession`, `PreviewQuickD3D11ExportSession`
  - Owns: the widget-shell on-screen preview host window (`QQuickView`), frame-swapped pacing signal, the runtime-facing preview setter surface used by `MainWindow`, shared skin/outline/judge asset ownership for both realtime preview and export, and the headless Qt Quick export session that renders `PreviewQuickSceneRoot` from a direct `PreviewFrameState` plus layer flags
- Quickshell external stage-media host:
  - Files: `src/preview/runtime/PreviewStageMediaHost.h`, `src/preview/runtime/PreviewStageMediaHost.cpp`, `src/preview/runtime/qml/PreviewStageMediaItem.qml`, `src/app/quick_shell/qml/QuickShellPreviewSurface.qml`
  - Class: `PreviewStageMediaHost`
  - Key functions: `setWarmupResolvedMediaPath`, `setChartPath`, `attachVideoOutputObject`, `detachVideoOutputObject`, `startPlayback`, `syncPlayback`
  - Owns: the quickshell-beta-only background-media route for both images and videos, including shared media resolution, external `VideoOutput` binding, inline quickshell image presentation, and the media side of the dedicated quickshell video composite-surface handoff
- Backend-neutral scene-state and timing helpers:
  - Files: `src/core/scene/PreviewFrameState.h`, `src/core/scene/PreviewLayerOrder.h`, `src/core/scene/PreviewOpacityCurves.h`, `src/core/scene/PreviewOpacityCurves.cpp`, `src/core/scene/PreviewSceneGeometry.h`, `src/core/scene/PreviewSceneGeometry.cpp`, `src/core/scene/PreviewHudState.h`, `src/core/scene/PreviewHudState.cpp`, `src/core/scene/PreviewProgressStatsCache.h`, `src/core/scene/PreviewProgressStatsCache.cpp`, `src/core/scene/PreviewPreparedSceneCache.h`, `src/core/scene/PreviewPreparedSceneCache.cpp`, `src/core/scene/PreviewMarkerDrawOrder.h`, `src/core/scene/PreviewMarkerDrawOrder.cpp`
  - Owns: shared preview frame payloads, layer flags/order, opacity/time curves, stage/playfield geometry helpers, HUD stats/time formatting, the prebuilt stats cache shared by Quick HUD/main-window side stats/export HUD rendering, the per-layer prepared note windows used by realtime preview and export Quick scene roots, and the shared slide/head draw-order comparator used to keep head, track, and slide-motion stacking aligned
- Quick scene-graph layers:
  - Files: `src/preview/quick_scene/PreviewQuickSceneRoot.h`, `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`, `src/preview/quick_scene/PreviewQuickStageBackgroundLayer.h`, `src/preview/quick_scene/PreviewQuickStageBackgroundLayer.cpp`, `src/preview/quick_scene/PreviewQuickBackdropLayer.h`, `src/preview/quick_scene/PreviewQuickBackdropLayer.cpp`, `src/preview/quick_scene/PreviewQuickGuideLayer.h`, `src/preview/quick_scene/PreviewQuickGuideLayer.cpp`, `src/preview/quick_scene/PreviewQuickTrackLayer.h`, `src/preview/quick_scene/PreviewQuickTrackLayer.cpp`, `src/preview/quick_scene/PreviewQuickSlideMotionLayer.h`, `src/preview/quick_scene/PreviewQuickSlideMotionLayer.cpp`, `src/preview/quick_scene/PreviewQuickJudgeEffectLayer.h`, `src/preview/quick_scene/PreviewQuickJudgeEffectLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchJudgeLayer.h`, `src/preview/quick_scene/PreviewQuickTouchJudgeLayer.cpp`, `src/preview/quick_scene/PreviewQuickHeadLayer.h`, `src/preview/quick_scene/PreviewQuickHeadLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchLayer.h`, `src/preview/quick_scene/PreviewQuickTouchLayer.cpp`, `src/preview/quick_scene/PreviewQuickTouchHoldLayer.h`, `src/preview/quick_scene/PreviewQuickTouchHoldLayer.cpp`, `src/preview/quick_scene/PreviewQuickChartReviewLayer.h`, `src/preview/quick_scene/PreviewQuickChartReviewLayer.cpp`, `src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h`, `src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.cpp`, `src/preview/quick_scene/PreviewQuickMuriPadLayer.h`, `src/preview/quick_scene/PreviewQuickMuriPadLayer.cpp`, `src/preview/quick_scene/PreviewQuickMuriActionLayer.h`, `src/preview/quick_scene/PreviewQuickMuriActionLayer.cpp`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.h`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.cpp`, `src/preview/quick_scene/PreviewQuickCircleNodes.h`, `src/preview/quick_scene/PreviewQuickCircleNodes.cpp`, `src/preview/quick_scene/PreviewQuickSpriteNodes.h`, `src/preview/quick_scene/PreviewQuickSpriteNodes.cpp`, `src/preview/quick_scene/PreviewQuickArcNodes.h`, `src/preview/quick_scene/PreviewQuickArcNodes.cpp`, `src/preview/quick_scene/PreviewQuickSectorNodes.h`, `src/preview/quick_scene/PreviewQuickSectorNodes.cpp`, `src/preview/quick_scene/PreviewQuickHudLayer.h`, `src/preview/quick_scene/PreviewQuickHudLayer.cpp`, `src/preview/quick_scene/PreviewTextureRepository.h`, `src/preview/quick_scene/PreviewTextureRepository.cpp`
  - Owns: the active Qt Quick/QSG realtime preview and export render stack, including object/effect/diagnostic overlays, shared QSG helper node builders for sprites/arcs/circles/sectors, the additive firework custom-material path, render-side per-window texture generations whose nodes/materials are destroyed before textures, atomic GUI-to-render texture-reset requests, invalidation flags, and layer-flag driven sub-selection for headless export
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
  - Files: `src/common/PreviewSfxSemantics.h`, `src/common/PreviewSfxTimeline.h`, `src/audio/PreviewAudioBackend.h`, `src/audio/QtPreviewSfxRuntime.h`, `src/audio/QtPreviewSfxRuntime.cpp`, `src/audio/MiniaudioPreviewAudioBackend.h`, `src/audio/MiniaudioPreviewAudioBackend.cpp`, `src/audio/BassPreviewAudioBackend.h`, `src/audio/BassPreviewAudioBackend*.cpp`
  - Class: `QtPreviewSfxRuntime`
  - Owns: the preview-audio facade seen by `MainWindow`, backend selection, and the stable prepare / commit / pause / resume / seek surface; `MiniaudioPreviewAudioBackend` remains the unsupported-platform compatibility path, while `BassPreviewAudioBackend` owns the Windows and macOS runtime path with bundled BASS libraries, no supported-platform miniaudio fallback, preloaded sample channels inspired by NetPlay's `BassAudioSample`, and a one-next-group `BASS_SYNC_POS | BASS_SYNC_MIXTIME` chain anchored to the always-running master mixer. The GUI drives visuals and explicit transport commands only; it never schedules live BASS note SFX.
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
  - Files: `src/core/chart/transform/ChartNormalization.h`, `src/core/chart/transform/ChartNormalization.cpp`, `src/core/chart/transform/ChartNormalizationSegmentPolicy.h`, `src/core/chart/transform/ChartNormalizationSegmentPolicy.cpp`
  - Namespace: `miacode::chart_transform`
  - Key function: `normalizeChartText`
  - Owns: current-difficulty full-chart normalization, one-measure-per-line rebuild, canonical modifier order, metadata-aware measure splitting, ordinary `||` comment preservation via standalone-line splits, per-beat subdivision minimization, segment-level subdivision policy, selection carry restoration, and syntax-error blocking
- Main window action entry points:
  - File: `src/app/mainwindow/MainWindow.cpp`
  - Key functions: `onMirrorLeftRight`, `onMirrorUpDown`, `onRotate180`, `onRotate45CounterClockwise`, `onRotate45Clockwise`, `onNormalizeWholeChart`, `onToggleBreakSelection`, `onToggleExSelection`, `onToggleFireworkSelection`, `onRandomRotateSelection`, `onClearCompleteElementsSelection`, `onRaiseSubdivisionHalfStepSelection`, `onLowerSubdivisionHalfStepSelection`

## 12. Toolbox Media Utilities

- Prepend blank media:
  - Files: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.MediaTools.cpp`, `src/tools/media/PvBatchCompressionScanner.*`, `src/tools/media/PvBatchCompressionWorker.*`, `src/tools/media/PvBatchCompressionDialog.*`, `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
  - Key functions/classes: `MainWindow::DialogsSection::onPrependMediaBlank`, `MainWindow::DialogsSection::onCompressBackgroundVideo`, `MainWindow::DialogsSection::onBatchCompressPv`, `PvBatchCompressionDialog`, `PvBatchCompressionWorker`
  - Owns: the `Audio/Video Processing` toolbox dialog. It splits `x` beats at BPM `y` into separate actions for prepending silence to sibling `track.mp3` or black video to the resolved chart background video (`&video=` override first, then `bg.mp4` / `pv.mp4` fallback), writing `track_bak.mp3` or `<video-stem>_bak.mp4` backups before replacing the selected original. It also contains one-click media normalization actions and a batch-video list: like Net batch upload, each browse appends the selected directory and its readable immediate child directories only, de-duplicates by directory path, resolves `bg.mp4` before `pv.mp4` case-insensitively, and retains folders without video so the table can report `No video`. Videos over 20 MiB are compressed sequentially on a cancellable worker thread while preserving `<video-stem>_bak.mp4`; smaller videos report that they were not compressed. Dialog defaults use `&clock_count=` / `&clockcount=` for beat count (fallback `4`) and `&wholebpm=` before the first half-width chart BPM token such as `(185)` (fallback `120`).
- Net batch download:
  - Files: `src/tools/net/NetClient.*`, `src/tools/net/NetBatchDownloadWorker.*`, `src/tools/net/NetBatchDownloadDialog.*`, `src/app/mainwindow/sections/export/MainWindow.ExportSection.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
  - Classes: `NetClient`, `NetBatchDownloadWorker`, `NetBatchDownloadDialog`
  - Owns: top-level Tools menu and toolbox entries for querying Net public charts by uploader ID, tag, or song title, filtering by local date range against the API `timestamp`, selecting rows, streaming `track.mp3` / `bg.jpg` / `maidata.txt` into one folder per chart on a background download thread, optionally writing an extra zip after successful folder download, remembering the last valid output directory in app preferences, and showing query/download diagnostic logs with per-resource speed and slowest-resource summaries. When a user ID is provided, the query prefers the uploader list and applies local ID/tag/title filtering, with an optional fuzzy case-insensitive mode exposed in the dialog.
- Net batch upload:
  - Files: `src/tools/net/NetBatchUploadWorker.*`, `src/tools/net/NetBatchUploadScanner.*`, `src/tools/net/NetBatchUploadDialog.*`, `src/common/DebugLog.*`, `src/app/mainwindow/sections/export/MainWindow.ExportSection.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
  - Classes: `NetBatchUploadWorker`, `NetBatchUploadDialog`
  - Owns: top-level Tools menu and toolbox entries for appending complete Net chart bundles from each browsed directory plus its immediate child folders (`maidata.txt`, `track.mp3`, `bg.jpg` / `bg.jpeg` / `bg.png`, optional `pv.mp4` / `bg.mp4`) into a path-deduplicated queue. Rows can be removed or the queue cleared before the entire queue is uploaded through authenticated multipart chart-upload requests on a cancellable background thread. Every completed non-fatal item pauses for 5 seconds before the next upload. Each attempt synchronously appends key events and complete failure diagnostics to the independent `net-upload.log` in the active MiaCode log directory, flushing every entry to disk and omitting credentials, cookies, and multipart bodies; upload does not start if this required log cannot be opened. Upload failures also retain copyable in-dialog diagnostics including stage, chart/directory, URL, HTTP reason, network error, selected non-sensitive response headers (`Content-Type`, `Server`, `CF-Ray`, `Retry-After`), and the complete response body. At thread completion, any unsuccessful charts are counted in a result prompt with Cancel and Upload Failed Again actions; retry starts a new authenticated worker attempt containing only those unsuccessful rows, without repeating successful charts. HTTP 413 is diagnosed as an oversized multipart request (normally an oversized PV video) and remains a per-chart failure even when Cloudflare injects a challenge-platform script into the HTML error page. HTTP 429 / Cloudflare 1015 waits for `Retry-After` (60-second fallback) and retries once; a repeated rate limit, HTTP 401/403, or a genuine Cloudflare challenge stops the batch, while per-chart validation errors continue. Timeouts are not retried because a state-changing POST may already have reached the server. The dialog can persist the username and password in application preferences only when the user enables the remember-credentials checkbox; immediate failed-only retries reuse the in-memory credentials for the open dialog session without changing that persistence choice.

## 13. Build, Packaging, And Dev-Only Helper Binaries

- CMake targets:
  - File: `CMakeLists.txt`
  - Owns: app target plus dev helper binaries such as `miacode_muri_dump`, `simai_native_dump`, `soundtouch_probe`, `simai_parser_spec`, `chart_batch_transform_spec`
- Extension system v1:
  - Files: `src/extensions/ExtensionManifest.*`, `src/extensions/ExtensionOpenBridge.*`, `src/extensions/EmbeddedExtensionRuntime.*`, `src/extensions/ExtensionManager.*`, `src/app/mainwindow/sections/frame/MainWindow.ExtensionHostRequests.cpp`, `src/app/ui/UiText.*`, `src/app/mainwindow/sections/preferences/MainWindow.PreferencesDialog.cpp`, `src/tools/extensions/ExtensionManifestSpec.cpp`, `src/tools/extensions/ExtensionOpenBridgeSpec.cpp`, `resources/extensions/README.md`, `docs/specs/extensions/EXTENSION_SYSTEM_V1.md`, `packages/miacode-extension-api/index.d.ts`, `tools/extensions/validate-extension.mjs`, `tools/extensions/check-extension-consistency.mjs`
  - Classes: `miacode::extensions::ExtensionManager`, `miacode::extensions::EmbeddedExtensionRuntime`
  - Owns: local extension discovery, VSCode-like manifest/schema/validator alignment, permission declaration with no prompt/grant persistence, data-only language packs, Preferences > Extensions enable/disable and refresh UI, user extension folder watching, Tools-menu command contributions, embedded Qt JavaScript runtime lifecycle, the small-closed-loop v1 `miacode.*` public API bridge, the Open Bridge facade registry plus experimental raw target annotations, SDK convenience wrappers over Open Bridge, controlled pet overlays that only load extension-local resources, controlled floating panels/real sidebar dock/preferences/bottom-tab UI hosts, HTML-lite views, declarative canvas views, preview scene overlays, input wheel/key/mouse gesture dispatch, editable extension shortcut registration, internal command registry reads, editor/timeline/preview deep facade queries, timeline zoom/follow facade controls, provider broker registration/collection plus host hover/completion/code-action UI display, the Preferences > Extensions DevTools panel, per-extension DevTools detail snapshots, and `extensions.log` support diagnostics. Public API statuses are exactly `implemented`, `planned`, and `blocked`; raw/unsafe targets such as shell.execute, process.spawn, native.*, internal.raw, renderer.raw, export.raw, security.*, and updates.* are discoverable as experimental raw descriptors rather than hard-blocked by permission name.
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
