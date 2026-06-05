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
- The **preview** out-of-process worker (`--preview-worker`, `MIACODE_PREVIEW_OUT_OF_PROCESS`,
  `src/preview/ipc/*`, `PreviewWorkerSession`/`Supervisor`) was **deleted (2026-06-02)** — in-process
  QSG is the keeper; do not reintroduce.

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
  `tryAutoCloseBracket`, closing-bracket type-over `tryOverwriteClosingBracket` (typing `)]}` when
  the same glyph already sits to the caret's right steps over it instead of inserting a duplicate),
  empty-pair backspace `tryDeleteBracketPair` (deletes both glyphs of
  `[|]` in one undo step). All gated by the single auto-completion pref; hold shortcut
  `tryHoldExpand` (typing `h` inserts a bare `h` and pops the `[8:1]`-style suggestions — it
  inserts NO bracket itself, so a following `[` yields `h[]`, never the old `h[[]]`).
- Bracket-completion dropdown ("tab 补全"): typing `( [ {` pops a simai-aware suggestion list under
  the caret; typing `h` pops the full-bracket hold durations (`[8:1]` …). Candidate
  data + scans: `src/editor/SimaiCompletionCatalog.{h,cpp}` (pure — `candidatesForOpening` for the
  brackets, `holdDurationCandidates` for `h`; spec at
  `src/tools/editor/SimaiCompletionCatalogSpec.cpp`); the non-focusing popup widget:
  `src/editor/BracketCompletionPopup.{h,cpp}`; editor glue (`tryBracketInput`, `tryHoldExpand`,
  shared opener `openCompletionPopup` behind `maybeOpenBracketCompletion` / `maybeOpenHoldCompletion`,
  `handleCompletionPopupKey`, `acceptCompletionCandidate`, filter via `cursorPositionChanged`) in
  `PlainCodeEditor.cpp`. `(` BPM list needs the `&wholebpm` value pushed via `setWholeBpmCandidate`
  from `DocumentSection::setEditorText`. **One unified preference** `editor_auto_completion`
  (default on) drives auto-close, the suggestion popup, AND the `h` hold suggestions together —
  formerly three separate toggles (`editor_auto_close_brackets` / `editor_auto_insert_square_after_h`
  / `editor_bracket_completion`), now migrated in `EditorSection::loadPortableState` by falling back
  to the legacy auto-close key. Single setter `PlainCodeEditor::setAutoCompletionEnabled`, apply
  `applyEditorAutoCompletionEnabled`, one checkbox ("自动补全") in `MainWindow.PreferencesDialog.cpp`.
  Keys: ↑↓ navigate, Tab/Enter accept (Enter swallows the newline), Esc/keep-typing dismiss.
- **Offset (`&first`) field location:** the chart-wide timing offset is edited from the
  **difficulty-page header** (`firstEdit_`, label `difficultyFirstLabel_`, built in
  `MainWindow.FrameBootstrap.cpp` next to `&lv_N`), NOT the metadata page (the metadata `first`
  row was removed). One source of truth = `document_.first`, shared with the latency page. While a
  difficulty is active, `TimelineSection::parsedRawFirstSeconds` reads the live field text (so an
  uncommitted edit reflows the timeline; `editingFinished` triggers `refreshTimelineMetadata`);
  it commits to `document_.first` in `applyCurrentFieldToDocument`'s difficulty branch (sets
  `metadataTimingChanged`). The difficulty header no longer has a per-difficulty designer field.
- **Per-difficulty designers + unified designer:** managed from a modal dialog, NOT a persistent
  checkbox. The metadata designer row has a "管理多个难度名义" button →
  `MainWindow::onManagePerDifficultyDesigners` → `DocumentSection::openPerDifficultyDesignerDialog`
  (`MainWindow.DocumentFlow.cpp`): seven rows for `&des_1..7` plus the "所有难度采用相同名义"
  toggle, committed on OK. When the toggle is turned ON, ≤1 distinct non-empty name unifies
  silently; otherwise `promptCanonicalDesignerName` lets the user pick the canonical name or
  "clear all". Per-project pref key `unified_designer_enabled`;
  `SimaiDocument::inferUnifiedDesignerDefault()` is **always false** (never auto-enable).
  - **Chart-less `&des_N` (standalone designers):** a slot 1..7 can hold a designer name without a
    chart. `SimaiDocument` keeps these in `standaloneDesigners_` (disjoint from `difficulties_`),
    API `designerForSlot` / `setDesignerForSlot` / `standaloneDesignerIds` / `perDifficultyDesigners`.
    `fromText` post-pass moves a designer-only difficulty (empty level+chart, non-empty designer)
    into the standalone map; `toText` emits a bare `&des_N=` for it (no phantom `&lv_N`/`&inote_N`,
    no sidebar entry). The dialog writes chart-less names via `setDesignerForSlot`.
  **Unified sync invariant — every designer-touching path must preserve it** (sync set): edit-time
  broadcast `applyCurrentFieldToDocument` (metadata top-`&des` branch only now — the difficulty
  header has no designer field); apply `applyUnifiedDesignerName`; load reconcile
  `refreshUnifiedDesignerStateForLoadedDocument`; new-difficulty seed
  (`MainWindow.FrameBootstrap.cpp`); undo-delete restore re-seed
  (`MainWindow.DocumentEditorState.cpp`); autosave snapshot mirror (`currentDocumentTextForAutosave`).
  All broadcast/apply sites iterate `perDifficultyDesigners()` + `setDesignerForSlot` so standalone
  `&des_N` participate too. There is **no longer cross-page UI designer sync** (no per-difficulty
  designer line edit to mirror). The free-form "Other &xx Fields" editor must parse via
  `SimaiDocument::parseUnmanagedFields` (not `parseRawFields`) so a manually typed managed key
  (`des`/`des_N`/`lv_N`/`inote_N`/title/artist/first/video) can't bypass the model and emit a
  duplicate/divergent line.
  **Export-side fallback contract (sync set):** the exported "谱师名义" (intro banner designer +
  chart-info-HUD `chartDesigner`) uses per-difficulty `&des_N`, falling back to top `&des` when
  the per-difficulty name is **blank including whitespace** — gate on `designer.trimmed().isEmpty()`,
  never bare `.isEmpty()` (a `&des_N= ` value parses non-empty and must still fall back). All five
  sites must stay aligned: intro `MainWindow.ExportSnapshot.cpp` (`intro.designer`), single-export
  task `MainWindow.ExportSnapshot.cpp` + `MainWindow.ExportFlow.cpp` (live-preview `setChartInfo`
  path) + batch `MainWindow.ExportFlow.cpp`, and the snapshot rebuild in `VideoExportSnapshot.cpp`
  (`buildVideoExportTaskFromSnapshot`).

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
  - **Video decode backend:** on **Windows** the host decodes PV/BG via **QtAVPlayer (FFmpeg)**,
    not `QMediaPlayer` — guarded by the `MIACODE_USE_QTAVPLAYER` build macro (CMake-defined on
    Windows; other platforms keep the `QMediaPlayer` path under `#elif defined(HAVE_QT_MULTIMEDIA)`).
    QtAVPlayer source is vendored in `third_party/QtAVPlayer/`; it links the FFmpeg dev SDK under
    `third_party/ffmpeg/windows/dev/`. Decoded frames are *pushed* into the QML `VideoOutput`'s
    `QVideoSink` (`handleDecodedVideoFrame`) rather than observed; `setSpeed` (not `setPlaybackRate`)
    + `seek` drive playback, and the paused-seek / prepared-start acks settle on frame `pts`.
    The public method/signal contract is unchanged, so `MainWindow.*` callers don't change.
    See `docs/VIDEO_DECODE_BACKEND_QTAVPLAYER_MIGRATION_ZH.md`.
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

## 8b. Export as ZIP — `src/tools/zip_export/`

- Packs the current chart into a `.zip` (`maidata.txt` from `SimaiDocument::toText()` +
  canonical siblings `track.mp3` / `bg.{jpg,png,jpeg}` / sibling PV `pv.mp4`/`bg.mp4`).
- Core (backend-neutral, no Qt-widget dep, unit-testable): `ChartZipPackager.{h,cpp}`
  (`packChartToZip`, `sanitizedZipStem`). Spec: `ChartZipPackagerSpec.cpp` → CTest
  `chart_zip_packager_spec`. Compression via vendored **miniz** (`third_party/miniz/`).
- Asset resolution reuses `miacode::chart_assets` (`src/common/ChartAssetPaths.h`). Rules:
  one background image, one PV; `*_bak` backups excluded; an out-of-folder `&video=` target
  is skipped (no sibling fallback); only an empty chart body is fatal.
- Entry slot: `MainWindow::ExportSection::onPackAsZip()` in
  `sections/export/MainWindow.PackZip.cpp` (progress + result popup mirror batch export).
  UI: File menu under "Save As" + toolbox "Bookmarks" submenu, both via the single
  `packAsZipAction_` (created in `setupMenusAndActions`, reused in the toolbox build).

## 8c. Cover (difficulty-card) export — `src/tools/cover_export/`

- Renders the maimai difficulty banner card (`src/intro/qml/MaimaiBannerCard.qml`,
  the same card the intro pre-roll composites) to a single still image — the
  program-internal port of `tools/intro_remotion/qml/render-banner-from-maidata.ps1`
  + its `qml/exporter/main.cpp`.
- Renderer (offscreen `QQuickView` + `grabWindow()`, no chart scene): `IntroCoverExporter.{h,cpp}`
  (`miacode::cover_export::exportIntroCover`). Loads `qrc:/intro/qml/MaimaiBannerCard.qml`,
  injects the parsed `:/intro/templates/maimai_banner.json` via `externalTemplate` +
  the `IntroBannerSpec` fields via `trackOverrides`, renders frame 0 fully assembled
  (`revealStartFrame` stays −1). Transparent → PNG (alpha); opaque → JPG over the card's
  own blurred-jacket backdrop. Writes `card.jpg`/`card.png`, adding `(1)`,`(2)`… on collision.
- Sub-dialog (size + transparent-bg toggle): `ExportCoverDialog.{h,cpp}`; size presets
  mirror the video-export resolutions, seeded from the current video size, tracked
  independently.
- Entry slot: a "导出封面 / Export Cover" button on the **Font** tab of `VideoExportDialog`
  (`VideoExportDialog::openExportCoverDialog`). It reuses `baseTask_.intro`
  (`IntroBannerSpec`), which `MainWindow::ExportSection::onExportPreviewVideo` now seeds via
  `buildActiveDifficultyIntroBannerSpec()` (wraps the `buildIntroBannerSpec` helper in
  `MainWindow.ExportSnapshot.cpp`). Output dir = the configured video output's directory,
  falling back to the chart dir. **Note:** the QML is reused as-is — no `intro.qrc` rebundle
  needed (cf. the AUTORCC-stale caveat that only bites when *editing* `src/intro/qml/*.qml`).

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
  - **Root cause of that "stray play event" — IDENTIFIED & FIXED (2026-06-01).** The mid-playback noise
    when dragging *any* SFX/volume slider was NOT the deliberate audition: it was
    `BassPreviewAudioBackend::applyLevels()` calling `resetCursor(authoritativeSecond(), false)` after its
    group rebuild. During active playback `authoritativeSecond()` is FROZEN at the last start/seek snapshot
    (MainWindow's wall-clock is the live SFX master and is never written back to `lastAuthoritativeSecond`),
    so re-seeking to it rewound the event cursor to the playback start and the next `drainEvents(wallClock)`
    replayed every tap from there in one burst. This hit BOTH the latency page and the audio-settings
    dialog — both funnel through `applyPreviewAudioSettingsToRuntime` → `applyLevels`. Fix: anchor the
    post-rebuild `resetCursor` to the last-fired group's chart-second instead
    of `authoritativeSecond()`, preserving the fired/unfired boundary whether playing or paused. The miniaudio
    backend's `applyLevels` (volumes only, no cursor touch) was already correct. A re-attempt of the
    slider-drag audition is now viable if gated to non-playing state.
- **SFX-level isolation (audition vs normal preview) — REDESIGNED (2026-06-01).** Two modes share ONE
  `previewSfxRuntime_`; runtime levels are now a PURE FUNCTION of the current mode, re-dispatched through
  the single entry `MainWindow::applyPreviewAudioSettingsToRuntime()`. On the latency page
  (`LatencySandboxController::isOnPage()`) it pushes `makePreviewLatencyAuditionLevels(mix, sfxPercent)`;
  otherwise the user's real mix. No snapshot/restore, no `latencySandboxAuditionActive_` *audio* gate
  (that flag now only gates *playback* — `hasPreviewableChart`). **Old leak root cause:** the sidebar
  handler overwrote `activeOutlineKey_` with the destination BEFORE calling `switchToXField`, so the
  `== "latency"` teardown guard was always false and `onPageLeft()` never ran → the audition override
  stayed live on the shared runtime. Fixed by ungating `onPageLeft()` (idempotent) from
  `activeOutlineKey_`. Full contract in `cross-chain-linkage.md` ("SFX-level isolation").
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
