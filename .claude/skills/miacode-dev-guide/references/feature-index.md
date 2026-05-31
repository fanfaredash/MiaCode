# Feature Index

Map a user-facing feature to the files / classes / functions that own it. Paths verified against
`CMakeLists.txt` on 2026-05-29. Code is source of truth — if an anchor moved, fix it here.

## 1. App boot & process modes — `src/app/main.cpp`

- GUI entry: `main`, `setWindowsAppUserModelId`, `wantsQuickShellBeta`,
  `startupOpenTargetFromArguments` (Qt startup, theme/font, window launch, startup-timing log,
  `--quick-shell-beta` routing, file/folder drag-open).
- CLI export: `wantsCliVideoExport`, `runCliVideoExport`.
- **Export** worker (this is the export subprocess — keep): `wantsCliVideoExportWorker`,
  `runCliVideoExportWorker`.
- The **preview** out-of-process worker wiring (`--preview-worker`, `MIACODE_PREVIEW_OUT_OF_PROCESS`)
  is **deprecated/slated for deletion** — do not extend.

## 2. Main window & orchestration

- Surface + shared state: `src/app/mainwindow/MainWindow.{h,cpp}`, `MainWindowShared.{h,cpp}`.
  Class `MainWindow` owns top-level state, preview runtime instances, the export-worker process,
  portable/project settings. **Orchestration only** — feature bodies live in `sections/`.
- Section map: `src/app/mainwindow/sections/README.md`. Slices live under
  `sections/{frame,document,timeline,validation,editor,preferences,preview,export,window,dialogs}/`.
- Frame/menus/toolbar/layout: `sections/frame/MainWindow.BootstrapAndMenus.cpp`,
  `MainWindow.FrameBootstrap.cpp`, `MainWindow.FrameBootstrapFinalize.cpp`.
- QuickShell beta: `src/app/quick_shell/` (`QuickShellBootstrap`, `QuickShellController`,
  `QuickShellNativeSurfaceHost`, `QuickShellPreviewCompositeSurface`, `QuickShellStyleBridge`,
  `qml/QuickShellMain.qml`).

## 3. Document model & file flow

- Storage: `src/core/chart/document/SimaiDocument.{h,cpp}` (`createEmpty`, `fromText`, `toText`,
  `parseRawFields`, `serializeRawFields`, `ensureDifficulty`, `removeDifficulty`).
- Timing metadata: `src/core/chart/document/SimaiTimingMetadata.{h,cpp}` (`buildTimingMetadata`,
  `buildTimingMetadataFromRawText`, `parseInlineTimeSignatureComment`).
- Open/save/new/switch + autosave: `sections/document/MainWindow.DocumentFlow.cpp`
  (`onNewFile`, `onOpenFile`, `openStartupTarget`, `onSaveFile`, `runAutosaveCheck`,
  `loadDocument`, `rebuildFieldSidebar`, `populateMetadataPage`, `populateDifficultyPage`).
- Editor header/page-mode UI: `sections/document/MainWindow.DocumentUi.cpp`.
- Chart text editor: `src/editor/PlainCodeEditor.{h,cpp}` (line numbers, transform context menu,
  half-width normalization, `normalizedViewportHitPosition`, bracket auto-close
  `tryAutoCloseBracket`, empty-pair backspace `tryDeleteBracketPair` (deletes both glyphs of
  `[|]` in one undo step, gated by the auto-close pref), hold `h[]` expand `tryAutoExpandH`).
- Bracket-completion dropdown ("tab 补全"): typing `( [ {` (and `h`, which expands to `h[]`
  then offers the `[` durations) pops a simai-aware suggestion list under the caret. Candidate
  data + scans: `src/editor/SimaiCompletionCatalog.{h,cpp}` (pure, spec at
  `src/tools/editor/SimaiCompletionCatalogSpec.cpp`); the non-focusing popup widget:
  `src/editor/BracketCompletionPopup.{h,cpp}`; editor glue (`tryBracketInput`, `tryHoldExpand`,
  `maybeOpenBracketCompletion`, `handleCompletionPopupKey`, `acceptCompletionCandidate`,
  filter via `cursorPositionChanged`) in `PlainCodeEditor.cpp`. `(` BPM list needs the
  `&wholebpm` value pushed via `setWholeBpmCandidate` from
  `DocumentSection::setEditorText`. Preference `editor_bracket_completion` (default on) wired
  the same way as auto-close (`applyEditorBracketCompletionEnabled`, checkbox in
  `MainWindow.PreferencesDialog.cpp`). Keys: ↑↓ navigate, Tab/Enter accept (Enter swallows the
  newline), Esc/keep-typing dismiss.

## 4. Parser, validation, markers

- API: `src/core/chart/parser/SimaiNativeParser.h` + `SimaiNativeParser.Driver.cpp`
  (`parseForTimeline`, `validateSyntax`, `buildValidationReport`).
- Internals (include-split into `SimaiNativeParser.cpp`): `.Slide.cpp`, `.TouchTap.cpp`,
  `.StrictChecks.cpp` (see `SimaiNativeParser.cpp:1584`).
- Validation UI: `sections/validation/MainWindow.ValidationFlow.cpp` (`runValidateSimai*`,
  `addValidationError`, `addValidationDecoration`).

## 5. Timeline, cursor mapping, preview sync

- Quick model: `src/timeline/TimelineQuickModel.{h,cpp}` (lightweight parse, cursor anchors,
  preview-follow buckets, incremental edit apply; owns comma-only `C` anchor lookup).
- Widget timeline: `src/timeline/TimelineView.{h,cpp}` + `.Core/.Interaction/.Paint.cpp`
  (include-split). Visible-range paint, playhead/cursor, waveform, follow-preview.
- Scene-state + Quick surface: `src/timeline/TimelineSceneState*`, `TimelineSceneStateBuilder.*`,
  `TimelineNoteAssets.*`, `src/timeline/quick/TimelineQuick*Layer.*`, `TimelineQuickItem.*`,
  `TimelineQuickStateBridge.*`, `src/common/TimelineThemeConfig.h`.
- Refresh orchestration: `sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  (`applyTimelineQuickChange`, `refreshTimelineQuickModelFromCurrentText`, `scheduleTimelineRefresh`,
  `requestTimelineSlowRefresh`, `scheduleTimelineAnalysisRefresh`, `seekTimelineToCursor`).
- Slow refresh workers: `src/timeline/TimelineSlowRefresh.{h,cpp}`.
- Timing getters: same `PreviewTimelineFlow.cpp` (`currentTimingMetadata`, `parsedFirstSeconds`,
  `parsedWholeBpm`, `parsedLatencyMeterId`, `applyLatencyDetectorOffset`).

## 6. Preview video, media, render state

- Runtime host + export session: `src/preview/runtime/PreviewRuntime.{h,cpp}`,
  `PreviewSceneAssetLoader.*`, `PreviewSceneAssetRepository.*`, `PreviewQuickRuntimeSurface.*`,
  `PreviewQuickExportSession.*`, `qml/PreviewRuntimeView.qml`. `PreviewRuntime` is the on-screen
  preview host (`QQuickView`) and the export session renders `PreviewQuickSceneRoot` headlessly.
- Stage-media host (background image+video, both shells): `src/preview/runtime/PreviewStageMediaHost.{h,cpp}`,
  `qml/PreviewStageMediaItem.qml`. **`PreviewMediaController` / `src/preview/video/` was removed**
  — background media now flows through `PreviewStageMediaHost` plus `PreviewRuntime`'s
  stage-background setters (`setStageMediaAvailable`, `setStageMediaPresentationMode`,
  `setExternalStageMedia*`). Verify exact widget-shell vs quickshell routing in
  `sections/preview/MainWindow.PreviewStageMediaRoute.cpp`.
- Backend-neutral scene state (NO GPU deps): `src/core/scene/` — `PreviewFrameState.h`,
  `PreviewLayerOrder.h`, `PreviewOpacityCurves.*`, `PreviewSceneGeometry.*`, `PreviewHudState.*`,
  `PreviewProgressStatsCache.*`, `PreviewPreparedSceneCache.*`, `PreviewMarkerDrawOrder.*`,
  `Preview*LayerState.*`, `PreviewSkinSelectors.*`, `PreviewAnimatedSpriteHelpers.*`.
- Active QSG layers: `src/preview/quick_scene/` — `PreviewQuickSceneRoot.*`, `PreviewQuick*Layer.*`,
  `PreviewQuick{Sprite,Circle,Arc,Sector}Nodes.*`, `PreviewTextureRepository.*`,
  `shaders/PreviewSpriteMaterial.*` / `PreviewFireworkMaterial.*` / `PreviewStageDimMaterial.*`.
- DComp path (**OFF by default**): `src/sources/*Source` → `src/render/compositor` →
  `src/render/backend_d3d11/PreviewDComp*` + `TimelineRenderView`.

## 7. Preview audio & SFX scheduling — `src/audio/`

- Facade: `QtPreviewSfxRuntime.{h,cpp}` (+ include-split `.Assets/.Timeline/.Background/.Engine/.Voices.cpp`)
  — backend selection, prepare/commit/pause/resume/seek surface.
- Backends behind `src/audio/PreviewAudioBackend.h`: `BassPreviewAudioBackend.{h,cpp}` (Windows,
  real BASS, master mixer clock, preloaded SFX channels, BASS_FX tempo) and
  `MiniaudioPreviewAudioBackend.{h,cpp}` (non-Windows compatibility, SoundTouch stretch).
- Settings/semantics: `src/audio/PreviewAudioSettings.*`, `src/common/PreviewSfxAssets.h`,
  `PreviewSfxSemantics.h`, `PreviewSfxTimeline.h`, `PreviewSfxTiming.h`.
- MainWindow hooks: `MainWindow.cpp` (`ensurePreviewSfxRuntimePrepared`,
  `applyPreviewAudioSettingsToRuntime`); playback clock authority:
  `sections/timeline/MainWindow.TimelinePlayback.cpp` (`currentPreviewAuthoritativeAudioClockSecond`).

## 8. Video export — `src/tools/video_export/`

- Dialogs: `VideoExportDialog.{h,cpp}`, `BatchVideoExportDialog.{h,cpp}`, `VideoExportPreferences.h`.
- Controller + pipeline: `VideoExportController.{h,cpp}` (⚠ ~5000 lines — see god-file list),
  `VideoExportQuickRenderBackend.*`, `VideoExportAudioRenderPlan.*`, `VideoExportAudioBackend.h`,
  `BassExportAudioBackend.*`, `LegacyExportAudioBackend.*`, `RawVideoPipeTransport.*`,
  `VideoExportRuntimePolicy.*`. Key fns: `exportFullPreview`, `exportPreparedTask`,
  `buildVideoExportAudioRenderPlan`, `chooseVideoEncoder`.
- Snapshot boundary (contract): `VideoExportSnapshot.{h,cpp}` (`toJson`, `fromJson`,
  `buildVideoExportTaskFromSnapshot`).
- MainWindow ownership: `MainWindow.cpp` / `sections/export/*` (`onExportPreviewVideo`,
  `buildVideoExportSnapshot`, `launchVideoExportWorker`, `handleVideoExportWorkerEvent`).

## 9. Latency settings (BPM & offset) — `src/tools/latency/`

- `LatencyDetectionPage.*`, `LatencyAnalysis.*`, `LatencySandboxController.*`,
  `LatencyTestChartBuilder.*` (an in-sidebar page + sandbox audition).
- UI title is **"延迟设置" / "Latency Settings"** (header + sidebar item + tooltip in
  `sections/document/MainWindow.DocumentUi.cpp`, gated on `activeOutlineKey_=="latency"`).
- Entry: `MainWindow.cpp` (`onOpenLatencyDetector` / latency page activation);
  `switchToLatencyField` in `sections/document/MainWindow.DocumentUi.cpp`.
- **Audition reuses the main preview transport** — it is NOT a separate player. The page
  installs the synthesized test chart as the preview source and plays it through the real
  `startQtPreviewPlayback`/`onQtPreviewTick`. `LatencySandboxController` is a thin
  install/teardown + UI-poll layer; the page button → `toggleAudition()` →
  `MainWindow::onTogglePreviewPause()`. See `cross-chain-linkage.md` §12.
- **Live param edits (incl. mid-playback):** `setBpm/setOffsetSeconds/setSubdivision` funnel into
  `regenerateAndPushIfActive()` → `setupSandboxPreviewState()`. The song audio is the fixed master
  clock and is never re-seeked here; only the test-chart notes + SFX timeline are rebuilt. Because
  `QtPreviewSfxRuntime::configureTimeline()` resets the SFX event cursor to 0, while playing we
  follow it with `resetCursor(currentPreviewAuthoritativeAudioClockSecond(), false)` so taps before
  "now" aren't re-fired (the music keeps playing untouched).
- **Ctrl+S (works):** the app has no global undo stack (undo = text editor only). Ctrl+S on this page
  is routed via an **app-level `eventFilter`** (installed on `qApp`) → `MainWindow::onSaveFile()`, NOT a
  page `QShortcut` — a page QShortcut on the same key as the global Save `QAction` is *ambiguous* (Qt
  fires neither). Scoped to `isVisible() && (target==this || isAncestorOf(target))`; `onPageEntered()`
  takes focus so it catches the key on entry. Spin boxes use `keyboardTracking(false)` so a half-typed
  value is never applied.
- **Page-local Ctrl+Z/Y undo — ATTEMPTED & REVERTED (failed).** A dedicated BPM/offset history plus an
  eventFilter Undo/Redo branch was tried; the keys never reached the page-local handler reliably.
  Removed — Ctrl+Z/Y fall through to the global Undo/Redo handler. Failure noted in code at the
  `LatencyDetectionPage` ctor + `eventFilter()`. Don't re-add without a different mechanism.
- **SFX-volume audition tap — ATTEMPTED & REVERTED (failed).** Dragging the SFX slider used to fire a
  one-shot tap (`LatencySandboxController::playSfxAuditionSample()` → `QtPreviewSfxRuntime::audition`);
  it could not be kept silent during playback (kept sounding despite guarding on `qtPreviewPlaying_`,
  primary suspicion: a stray play/audition event scheduled elsewhere). Removed — the slider now only
  sets the volume. Failure noted in code at `onSfxVolumeChanged` + the deleted-method comment in
  `LatencySandboxController.cpp`. Default SFX volume = `kDefaultSfxVolumePercent` (**50**; stored
  per-user under `latency/sfxVolumePercent`).
- The three gray hint labels under BPM / Offset / SFX-volume were removed from `buildUi`.
- The "auto-detect Offset" button is currently hidden (`detectOffsetButton_->setVisible(false)` in
  `LatencyDetectionPage::buildUi`) — code/wiring kept; auto-detect BPM is unaffected.

## 10. Muri analysis — `src/tools/muri/`

- `MuriAnalyzer.{h,cpp}` (key fn `analyze`, now a ~187-line thin orchestrator; ~1300 lines of
  cross-stage shared primitives + that orchestrator), `MuriStaticChecker.*`
  (`buildStaticMuriReferences`), `MuriPanelEntries.*` (`buildVisibleMuriPanelEntries`),
  `MuriDump.cpp` (dev tool). Types: `src/common/MuriTypes.*`, config `src/common/MuriConfig.h`.
- **Pipeline decomposition (✅ complete, namespace `miacode::muri::detail`):** `analyze()`
  orchestrates per-stage TUs — `MuriAnalyzerGeometry.*` (pure geometry), `MuriAnalyzerModel.h`
  (shared structs), `MuriSlideReferenceData.*` (`slide_data.json` resource layer),
  `MuriRuntimeModelBuilder.*` (Stage 1: marker → runtime note/touch model), `MuriOverlayBuilder.*`
  (Stage 2: `buildOverlayActions` pad-windows/trails + `buildSlideState`/`buildWifiState`),
  `MuriSlideWifiJudge.*` (Stage 3: `simulateRuntimeSlideAndWifiJudgments` tick-stepped
  "judged-too-fast" simulation), `MuriSimpleNoteJudge.*` (Stage 4: `collectSimpleNoteRuntimeDiagnostics`
  — simple-note judging + multi-touch + tap-on-slide/overlap diagnostics), `MuriDiagnosticLabels.*`
  (diagnostic vocabulary: labels / source anchors / alert text / marker lookups),
  `MuriDiagnosticCollector.*` (Stage 5: output-owning collector — `addDiagnostic`/`add*SpriteEvent`
  + `finalize()` sort/dedupe). Remaining cross-stage primitives (slide predicates, pad geometry,
  timing, pad-window/trail + hand-action + pad-event builders) are declared in
  `MuriAnalyzerInternal.h`, defined in `MuriAnalyzer.cpp`. History + optional follow-ups:
  `.claude/MURI_DECOMPOSITION_HANDOFF.md`.

## 11. Batch transforms — `src/core/chart/transform/`

- `ChartBatchTransform.{h,cpp}` (`transformChartText`, `toggleBreak/Ex/FireworkForSelection`,
  `randomRotateForSelection`), `ChartNormalization.{h,cpp}` (`normalizeChartText`),
  `Non384SnapTable.*`. MainWindow entries: `onMirror*`, `onRotate*`, `onNormalizeWholeChart`,
  `onToggle*Selection`.

## 12. Toolbox media utilities

- `sections/dialogs/MainWindow.Dialogs.cpp` + `MainWindow.DialogsSection.cpp` — prepend
  silence/black, compress bg video, convert track to 44100 Hz (`onPrependTrackSilence`,
  `onPrependPvBlack`, `onCompressBackgroundVideo`, `onConvertTrackTo44100Hz`). These four open
  from a single popup, `onMediaProcessingTools()` (one button + one-line description each),
  reached via the toolbox's "音频/视频处理 / Audio/Video Processing" entry — not a hover submenu.
  The shared `runFfmpegBlocking(... totalDurationSeconds, error)` helper drives a determinate
  progress bar by parsing ffmpeg `-progress pipe:1` `out_time_us=` against the expected output
  duration (falls back to an indeterminate bar when duration is unknown).
- Toolbox menu itself is built in `sections/frame/MainWindow.FrameBootstrap.cpp` (`toolboxMenu_`).
  BPM & Latency was dropped from the toolbox (still in the top Tools menu via
  `latencyDetectorAction_`); Copy Area is gated off by the local `kCopyAreaIntegratedIntoToolbox`
  constant (feature kept: `copyAreaPanel_`/`fullCopyAreaAction_`/`setFullCopyAreaVisible`).

## Update this file when

- A feature owner moves file, or a class/function becomes the new canonical entry point.
- A capability gains a second mirrored path (also note it in `cross-chain-linkage.md`).
