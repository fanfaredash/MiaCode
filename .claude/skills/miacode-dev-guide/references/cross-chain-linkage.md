# Cross-Chain Linkage

Read before changing behavior that crosses parser / timeline / preview / audio / export / Muri
boundaries. Ported from the prior guide with paths corrected (2026-05-29); contracts are believed
current but **code is source of truth** — verify and fix drift in the same change.

## 1. Edit → parse → timeline → preview chain

1. editor `contentsChange` / `scheduleTimelineRefresh`
2. `MainWindow::applyTimelineQuickChange` / `refreshTimelineQuickModelFromCurrentText`
3. `TimelineQuickModel::applyContentsChange` / `rebuildFromText`
4. `TimelineView::setTimelineData`
5. `MainWindow::requestTimelineSlowRefresh`
6. `SimaiNativeParser::parseForTimeline`
7. preview snapshot publication + `applyLatestTimelinePreviewStateToPausedPreview` (when paused)
8. `MainWindow::scheduleTimelineAnalysisRefresh` → analysis result build → deferred UI apply
9. `PreviewRuntime::setNoteMarkers`

Implications:

- A parser change is rarely parser-only; a new note property/timing rule usually touches timeline,
  preview, audio, export, and Muri.
- Head-material flags `$ $$ @ ? !` are mirrored data — keep `SimaiNativeParser`,
  `TimelineQuickModel`, `core/scene/PreviewSkinSelectors`, timeline icons, and chart-transform
  token preservation aligned in one patch.
- The mine modifier `m` (note property `isMine` tap/hold/touch/touch_hold + `trackMine`/`headMine`
  slide; timeline flags `TimelineRenderFlagIsMine`/`TrackMine`) is the same mirrored-data set as
  above PLUS suppression: parser (`SimaiNativeParser.{cpp,TouchTap,Slide}`) ↔ mirror
  (`TimelineQuickModel`) ↔ `ChartBatchTransform` (must accept+emit it, not `return false`) ↔
  `ChartNormalization` round-trip ↔ skin selectors (`PreviewSkinSelectors` + the touch/touch-hold
  layers, mine OVERRIDES break/each) ↔ timeline icons ↔ **SFX/Muri suppression** (`PreviewSfxTimeline`
  `buildTimeline`, `MuriAnalyzer` pad windows, `MuriRuntimeModelBuilder` judgeable notes) ↔ specs.
  Mines deliberately DO count in `PreviewProgressStatsCache` (product decision). Skin art =
  `<base>_mine.png` (skinSTD only; skinDX deferred).
- Negative HS (`<HS*-N>`, ON by default — `SimaiNativeParser::g_allowNegativeHs` defaults true;
  opt-out `MIACODE_PREVIEW_REJECT_NEGATIVE_HS` at boot sets it false): sign lives in `PreviewTapTiming.directionSign`
  (magnitude/sign split in `previewTapTimingForEffectiveFlowSpeed`); `sampleTapApproach` reverse
  branch. Per-type sign policy is a sync set — tap/star/each-line pass signed `hsMultiplier`,
  hold/touch/slide pass `qAbs` — across `PreviewHeadLayerState`, `PreviewGuideLayerState`, and any
  new note layer. Forward (default) must stay byte-identical (`directionSign=+1`).
- Beat-grid semantics are mirrored between `SimaiNativeParser` and `TimelineQuickModel`: every
  comma is a beat line; measure lines run on an independent meter timeline from shared
  `SimaiTimingMetadata` (`&whole_time_signature=`); inline `|| x/y` restarts the meter; `{beats}`
  only changes comma spacing; `(BPM)` restarts the measure-line timeline.
- Same-second slide/head/track/motion stacking is shared by `src/core/scene/PreviewMarkerDrawOrder.*`
  + prepared `drawOrder` in `PreviewPreparedSceneCache`. Changing "who's on top" → update the helper
  and review `PreviewHeadLayerState.cpp`, `PreviewTrackLayerState.cpp`,
  `PreviewSlideMotionLayerState.cpp` (all in `src/core/scene/`) plus the preview specs together.
- Timeline note stacking mirrors preview order: see `src/timeline/TimelineView.Paint.cpp`,
  `src/core/scene/PreviewLayerOrder.h`, and `src/preview/quick_scene/*` together.
- Realtime preview + export Quick scene roots share `PreviewPreparedSceneCache`-driven note
  windows; layer order is owned by `PreviewQuickSceneRoot` + `PreviewLayerOrder.h`. Adding/changing
  a visible layer → review `src/core/scene/PreviewLayerOrder.h`, `src/preview/quick_scene/*`, and
  `src/tools/video_export/VideoExportQuickRenderBackend.*` together.
- Slide-judge ("just" rings) vs per-note judge TEXT are split across slots to match MajdataPlay z-order:
  the `ChartReviewLayer`/`MaimuriDxJudgeLayer` builders take a `SlideJudgeRenderGroup` filter
  (`PreviewJudgeOverlayShared.h`). `PreviewQuickSceneRoot` composites each enum layer as TWO slots — a
  `SlideShapeOnly` slot between `slide_motion` and `judge_effect` (below notes/touch/tap-judge), and a
  `JudgeTextOnly` slot on top. So each judge enum bit drives 2 slots; keep
  `kPreviewQuickSceneLayerSlotCount` equal to the number of `layerSlotAt(root, slotIndex++)` calls when
  adding/removing slots. The off-by-default DComp path (`src/sources/chart/ChartReviewSource.*`,
  `MaimuriDxJudgeSource.*`, sorted by `zOrder()`) still draws both groups in one pass — a known divergence.
- Firework visuals use the custom `PreviewQuickJudgeFireworkLayer` material; state in
  `src/core/scene/PreviewJudgeFireworkLayerState.*`, shader in
  `src/preview/quick_scene/shaders/PreviewFireworkMaterial.*`.
  - **Firework PSO/texture warm-up is a 3-file contract — don't break the loop.** Qt RHI compiles
    the firework pipeline + uploads the colour-ball texture lazily on the FIRST firework draw
    (a render-thread stall, worst on weak iGPUs). `PreviewRuntime` warms it by injecting a synthetic
    off-screen firework marker (`armFireworkPsoWarmupIfReady` / `appendFireworkWarmupMarker`).
    Completion is gated on a CONFIRMED draw, not a present count: `PreviewQuickSceneRoot::updatePaintNode`
    MUST call `runtime_->notifyFireworkLayerProducedNode()` whenever the firework layer returns a
    non-null node (render thread → atomic `fireworkLayerDrawSignal_`), and `handlePresentedFrame`
    flips `fireworkWarmupDone_` only once that signal advances past the arm snapshot (present-count cap
    = backstop). The synthetic is RE-CENTERED on the live playhead via `refreshFireworkWarmupForPlayheadChange`
    in `setPlayheadSeconds` (+ `setNoteMarkers`) so a seek / negative pre-roll can't strand it outside
    its lifecycle window. Dropping the notify call or the re-center silently regresses to the old
    probabilistic first-firework stutter. Logs: `preview/runtime action=firework_pso_warmup_arm|_done`
    (Runtime channel).
- During playback: slow-refresh markers feed validation/Muri inputs, but preview audio/canvas/stats
  stay on the frozen play-start snapshot until stop; validation/Muri UI may defer to a paused edge.

## 2. `&first` / timing-offset chain

- `SimaiDocument` stores raw `first`; `MainWindow::parsedFirstSeconds` is the getter; preview/export
  use the finite parsed raw `&first` directly (no inverted "effectiveFirst").
- **Edited from the difficulty-page header** (`firstEdit_`), not the metadata page. While a
  difficulty is active `parsedRawFirstSeconds` reads the live field text (uncommitted edits reflow
  the timeline; `editingFinished` → `refreshTimelineMetadata`); the commit to `document_.first`
  happens in `applyCurrentFieldToDocument`'s difficulty branch. The latency page still writes
  `document_.first` via `applyLatencyDetectorOffset` — same single source of truth.
- `TimelineQuickModel` receives `first` on every rebuild; `buildTimelineSlowRefreshResult` shifts
  markers by `first`; `MainWindow::applyLatencyDetectorOffset` writes raw `first` back.
- **Serialization compat contract (`SimaiDocument::toText`):** we read an empty/missing `&first`
  as `0`, but strict third-party players (e.g. MajdataPlay `double.Parse`) crash on a bare
  `&first=`. So `toText()` always emits a parseable value — empty `first` → `&first=0` — and
  drops empty *numeric* extra-metadata whose 0 is meaningless (`&wholebpm`/`&pvstart`/`&pvlen`,
  via `isDroppableWhenEmptyNumericField`) instead of writing a bare `&key=`. Do NOT revert these
  guards to an unconditional `serializeField`. Covered by `simai_document_spec`.
- Review together on change: `sections/timeline/MainWindow.PreviewTimelineFlow.cpp`,
  `src/tools/latency/` analysis, `src/tools/video_export/VideoExportSnapshot.cpp`,
  `docs/specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`.

## 3. Timing metadata / default meter chain

- `SimaiTimingMetadata` is shared by parser, quick timeline, slow refresh, validation cache,
  normalization, export snapshot rebuild, CLI helpers.
- `MainWindow::currentTimingMetadata` reads live metadata text so unsaved
  `&whole_time_signature=` edits still affect validation/timeline.
- Callers of `parseForTimeline` / `buildValidationReport` must pass timing metadata when available
  or fast/slow/export/tooling timelines drift.
- Review together: `src/core/chart/document/SimaiTimingMetadata.cpp`,
  `PreviewTimelineFlow.cpp`, `MainWindow.ValidationFlow.cpp`, `TimelineQuickModel.cpp`,
  `TimelineSlowRefresh.cpp`, `VideoExportSnapshot.cpp`, `src/tools/muri/MuriDump.cpp`.

## 4. Runtime SFX ⇄ export SFX must stay in sync

Canonical sync pair:

- Runtime: `src/audio/QtPreviewSfxRuntime.Timeline.cpp` (`configureTimeline`).
- Export: `src/tools/video_export/VideoExportAudioRenderPlan.cpp`
  (`buildVideoExportAudioRenderPlan`); `VideoExportAudioBackend::renderMixedTrackToWav`.

Shared concerns (collapse/latest-wins/offset rules live in `src/common/PreviewSfxTimeline.h`,
`PreviewSfxTiming.h`, `PreviewTimingSettings.h`):

- which kinds emit `answer/judge/break/ex/touch/touchhold/firework`; same-second same-kind collapse
  to one playback at strongest gain; every note-SFX kind is latest-wins across time on BOTH sides.
  Preview enforces latest-wins with a **single voice per kind** (`QtPreviewSfxRuntime` `configureBank(..,1)`)
  that restarts on retrigger, so two same-kind hits any distance apart collapse audibly to one. Export
  has no voice — it *emulates* this via `nextSameKindSecond`→`maxDurationSeconds` truncation. When the
  supersede gap is **sub-sample** (< `1/kMixSampleRate`), export can't express the truncation (BASS rounds
  the length to 0 bytes and treats 0 as "no limit," playing the full sample and doubling the SFX), so
  `VideoExportAudioRenderPlan.cpp` **drops** the masked playback outright to match preview. This bites when
  a chart makes two events *intend* to coincide but land ~µs apart — e.g. a hold whose absolute duration
  `[#…]` ends microseconds before a coincident tap, a gap just past the `1e-6` `buildTimeline` collapse
  epsilon. Covered by `verifySubSampleSupersedeDropsMaskedAnswer` in `VideoExportAudioRenderPlanSpec.cpp`.
- `&first` finite-raw direct use; `audioOffset` = whole-SFX chart shift; `displayOffset` advances
  answer/judge families; `answerOffset`/`judgeOffset` family-specific; `1/60 s` pre-trigger for
  answer + tap/hold/touch-family judge only.
- hold tails emit answer+judge (EX/break/break-EX tails answer-only); touch-hold tails answer-only;
  break-slide tail = flagged `break` + `judge_break_slide` routed through `break_slide_tail_break`.
- touch-hold sustain is a SINGLE shared voice (BASS `touchholdSample_` / miniaudio `touchholdVoice_`)
  driven by `touchhold_start`/`touchhold_stop` events. Ownership is **latest-wins**: the pure helper
  `preview_sfx_timeline::touchholdOwnerSpanIndexAt(spans, second)` returns the active span with the
  greatest `startSecond`, and both backends `reconcileTouchholdVoice(second)` to it on every
  start/stop event (and on pause/restore/seek). This is order-independent, so a seamless join (prev
  `endSecond` == next `startSecond`), overlap, or nesting lets the newer touch-hold take over the
  voice instead of the older span's stop clobbering it. Don't go back to per-event naive start/stop
  or a first-wins active-set. Export is unaffected (it renders each `TouchholdSpan` independently).
- `&clock_count=` is a defaulted (`4`) count-in metadata field (`src/common/ChartClockCount.h`), editable from the latency settings page + metadata "Other &xx Fields" and materialized by `SimaiDocument::ensureDefaultClockCount`; its export count-in uses full-range lead-in `2.0s`,
  partial preload `1.0s` (`src/common/VideoExportConfig.h`). Full-vs-partial classification: any range
  STARTING at chart 0 counts as full-range even if it ends early (count-down lead-in, no frozen
  preload / pause glyph); only start > 0 is partial. Decided in TWO places that must stay in sync:
  `VideoExportDialog.cpp` (`updated.fullRangeExport`) and `MainWindow.ExportSnapshot.cpp`
  (`fullRangeExport`).

If one side changes, inspect the other in the same patch.

## 5. Background-media resolution & host route ownership

> Updated 2026-05-29: `PreviewMediaController` and `src/preview/video/` were removed. Background
> media (image + video) is now owned by `PreviewStageMediaHost`; `PreviewRuntime` exposes
> stage-background state setters for the QSG stage layer. Verify the exact widget-shell vs
> quickshell split in `sections/preview/MainWindow.PreviewStageMediaRoute.cpp`.

- Shared resolver: `miacode::chart_assets::resolveBackgroundMediaPath`.
- Route coordinator: `sections/preview/MainWindow.PreviewStageMediaRoute.cpp`.
- **Decode backend (preview side):** Windows decodes PV/BG via **QtAVPlayer (FFmpeg)** inside
  `PreviewStageMediaHost` (build macro `MIACODE_USE_QTAVPLAYER`); other platforms keep `QMediaPlayer`.
  The export *encoder output* is **unaffected** — it still decodes the background with the standalone
  `ffmpeg.exe` filtergraph in `VideoExportController` (so a file that previews can also export, and
  vice-versa: both sides are now FFmpeg). The export *preview dialog* (WYSIWYG) rides the realtime
  preview path, so it inherits the QtAVPlayer backend. Speed changes use `QAVPlayer::setSpeed` (no Qt
  converter rebuild → the old `setPlaybackRate` rate-change crash class is gone).
- Shared preview-time clock getter: `MainWindow::currentPreviewAuthoritativeAudioClockSecond`
  (UI follow, export-dialog current second, weak-video late-start must read this — do not branch on
  `PreviewStageMediaHost::currentPlaybackSecond()`, which is video-local observability only).
- Quickshell presentation split: images inline in `QuickShellPreviewSurface.qml`; video moves to
  `QuickShellPreviewCompositeSurface` (own `QQuickView`).
- Export consumes the shared resolver via `VideoExportController`; Windows export audio = single
  mixed WAV via `BassExportAudioBackend`, non-Windows = `LegacyExportAudioBackend` fallback.
- Filenames: `bg.mp4`, `pv.mp4`, `bg.{jpg,png,jpeg}`. Keep preview + export aligned.
- Pause-hide option `previewForceLabeledJudgeLineWhenPaused_` (UI label key
  `dialog.render_settings.gameplay.force_labeled_judge_line_when_paused`, zh "暂停时显示判定区"):
  when ON + preview paused, the on-screen preview switches outline to `JudgeAreaLabeled` AND hides
  PV/BG. Two consumers — `PreviewSection::effectivePreviewOutlineVariant()` (outline) and
  `applyPreviewStageMediaRouteVisualSettings()` (`mediaVisible`); treat as a pair. The transient
  `exportPreviewActive_` flag (set around `ExportSection::onExportPreviewVideo`'s `dialog.exec()`)
  forces both off so the export-preview dialog shows PV/BG exactly as the exported video will. The
  exported video itself is unaffected — it already uses `previewOutlineVariant_`, not the paused
  override.

## 6. Track-path resolution lives in multiple places

Owners: `MainWindow::resolveDefaultTrackPath`, `MainWindow::resolveLatencyDetectorTrackPath`,
`QtPreviewSfxRuntime::resolveTrackPath`. Convention: sibling `track.mp3`; optional
`MIACODE_TRACK_PATH` override on the main-window export path. Update all owners + `build-and-tools.md`
on new filename/lookup rules.

## 7. Skin/asset lookup flows into both preview and export

Root: `miacode::assets::findAssetRoot` / `assetPath`. Preview consumers:
`MainWindow::resolvePreviewSkinDir`, `PreviewRuntime::setSkinDirectory`,
`miacode::preview_sfx::resolveSfxDirectory`. Export consumers: `MainWindow::buildVideoExportSnapshot`,
`buildVideoExportTaskFromSnapshot`, `VideoExportController::exportPreparedTask`. Review both on change.

## 8. Export snapshot boundary is a contract

`buildVideoExportSnapshot` → `VideoExportSnapshot::toJson` → `runCliVideoExportWorker` →
`fromJson` → `buildVideoExportTaskFromSnapshot` → `VideoExportController::exportPreparedTask`. New
export settings (and shared timing offsets, static Muri thresholds) must be added on BOTH
serialization sides; worker protocol changes reflect in both `main.cpp` and MainWindow worker-event
handling.

## 9. Shared render state flows through preview and export

Shared settings (background brightness outer/inner, layout square scale, outline diameter ratio
from `src/common/LayoutRingConfig.h`, outline/judge-line variant, smooth brightness, scale mode
`fill/fit/square_fit`, tap/touch flow speeds, chart-review overlay toggles, HUD flags, Muri render
options incl. `wifiNeedC` / `excludeTouchFromMultiTouch`) must stay aligned across preview
persistence, export snapshot, and any analyzer entry that reconstructs runtime Muri results. Owners:
`MainWindow::load/savePortableState` (app-scoped shared), `load/saveProjectRenderState` (chart-local
only), `VideoExportPreferences` (export-only). Apply via `PreviewRuntime` setters + `PreviewQuickSceneRoot`
layers; reconstruct on export via `buildVideoExportTaskFromSnapshot` + `VideoExportController`.

## 10. Parser output feeds Muri on both paths

Live: `requestTimelineSlowRefresh` → `parseForTimeline` → analysis refresh. Export:
`buildVideoExportTaskFromSnapshot` → `MuriAnalyzer::analyze`. Normalization:
`onNormalizeWholeChart` → `normalizeChartText` → `buildValidationReport`. A marker-field change
affects both live diagnostics and exported overlays.

## 11. Common "change here, check there" pairs

- `SimaiNativeParser` → `MainWindow.ValidationFlow.cpp`, `PreviewTimelineFlow.cpp`,
  `TimelineQuickModel.cpp`, `ChartNormalization.cpp`, `VideoExportSnapshot.cpp`, `MuriAnalyzer.cpp`.
- Parser validation severities / modifier acceptance rules are a THREE-WAY sync set: the parser
  emit points, the spec tests (`src/tools/simai_parser/SimaiParserSpec.cpp` AND
  `src/tools/chart_transform/ChartBatchTransformSpec.cpp` — normalization gates on
  `buildValidationReport`, so tightening the parser breaks transform fixtures too), and the
  diagnostics docs (`docs/specs/chart/CHART_DIAGNOSTICS_AND_NORMALIZATION_SPEC.md` + `_ZH.md`). Severity
  contract since the 2026-05-03 decoupling: UI severity = strict-pass emit, NO lenient downgrade.
  The 2026-05/06 drift (C1/C2 + break-slide-b + {N}∤384 flipped to Warning; touch `x` and
  uppercase `B`/`X` rejected) shipped without updating the specs and sat as 10 CTest failures
  for a month — update all three sides in the same patch.
- preview SFX mapping → `VideoExportAudioRenderPlan.cpp`, `VideoExportAudioBackend` impls.
- preview timing constants → `src/common/PreviewGameplayConfig.h`,
  `src/core/scene/PreviewOpacityCurves.cpp`, `VideoExportController.cpp`.
- Muri static thresholds → `src/common/MuriConfig.h`, settings UI, `VideoExportSnapshot.cpp` +
  `VideoExportController.cpp`.
- Muri list anchoring/dedupe → `MuriPanelEntries.cpp`, `MainWindow.ValidationFlow.cpp`,
  `src/tools/muri/MuriSpec.cpp`.

## 12. Latency-page audition reuses the main preview transport

The BPM & latency page plays its synthesized test chart through the SAME transport as a
difficulty's chart page — only the chart source differs (a non-editable, non-displayed test
chart). Do **not** reintroduce a parallel sandbox player (an earlier wall-clock/`drainEvents`
replica was the wrong approach: it bypassed the real per-frame path and drifted).

- `LatencySandboxController::installSandboxScene` / `setupSandboxPreviewState` publish the test
  chart as the preview source exactly like a slow-refresh does for a difficulty: set
  `latestTimelineNoteMarkers_` (+ signature), `latestTimelinePreviewRevision_ = timelineRevision_`,
  `latestTimelinePreviewSnapshotReady_ = true`, `PreviewRuntime::setNoteMarkers`, the bottom
  timeline via `TimelineQuickModel::rebuildFromText` + `setTimelineData`, the slider range, and the
  SFX timeline via `QtPreviewSfxRuntime::configureTimeline`.
- `latencySandboxAuditionActive_` (true while the test chart is installed) gates
  `TimelineSection::hasPreviewableChart()` = `hasActiveDifficulty() || latencySandboxAuditionActive_`.
  Playback-**start** gates use `hasPreviewableChart()` instead of `hasActiveDifficulty()`:
  `preparePreviewStartState` (early sandbox branch) and `onTogglePreviewPause` (play branch). Every
  relaxation is guarded by that flag, so normal-difficulty playback is byte-identical.
- Play/Pause/Stop run the real transport — `onTogglePreviewPause` / `pauseQtPreviewPlaybackExact` /
  `startQtPreviewPlayback` → `onQtPreviewTick` — which drives preview render, bottom timeline,
  slider, SFX, and song audio. The page button calls
  `LatencySandboxController::toggleAudition()` → `MainWindow::onTogglePreviewPause()`.
- The controller's `QTimer` is a ~30Hz UI poll ONLY: it mirrors `qtPreviewPlaying_` +
  `qtPreviewPauseSecond_` onto the page's own widgets (audition button + position label) via
  `auditionStateChanged` / `playheadAdvanced`. It does NOT drive playback.
- Leaving the page (`setOnPage(false)`) stops the transport and restores the previous chart's preview
  state, then re-dispatches audio levels (see below). `setOnPage` flips `onPage_` BEFORE running
  install/teardown, so the level dispatch reads the correct mode at both edges.
- **SFX-level isolation — two modes share ONE `previewSfxRuntime_`, and the runtime levels are a PURE
  FUNCTION of the current mode, NOT a snapshot that is restored on exit.** Single dispatch entry:
  `MainWindow::applyPreviewAudioSettingsToRuntime()`, which branches on
  `LatencySandboxController::isOnPage()`:
  - on the latency page → `makePreviewLatencyAuditionLevels(previewAudioSettings_, sfxVolumePercent())`
    (every SFX kind = the page's independent slider; song keeps its normal effective volume);
  - otherwise → the user's real `previewAudioSettings_` verbatim.
  The latency `sfxVolumePercent` is a SEPARATE input (persisted under `latency/sfxVolumePercent`); it
  never flows into `previewAudioSettings_` or persistence. Dispatch fires at every deterministic point —
  chart load (`setCurrentFilePath`), play start (`startQtPreviewPlayback`), latency page enter/leave
  (`installSandboxScene`/`teardownSandboxScene`), latency slider (`setSfxVolumePercent`), and
  audio-settings dialog apply (`commitAudioSettingsChange`). Because levels are re-derived from the
  settled mode every time, a missed page-exit cannot linger an override into the normal preview — the
  next dispatch self-corrects. `latencySandboxAuditionActive_` no longer gates audio; it only gates
  *playback* (`hasPreviewableChart`).
- **Leave teardown must NOT be gated on `activeOutlineKey_`.** The sidebar click handler
  (`MainWindow.FrameBootstrap.cpp`) overwrites `activeOutlineKey_` with the destination BEFORE calling
  `switchToXField`, so a `== "latency"` guard there is always false. The three
  `switchTo{Difficulty,Metadata,Welcome}Field` functions therefore call `onPageLeft()` UNCONDITIONALLY
  (it is idempotent — `setOnPage(false)` no-ops when not on the page). The old `== "latency"` guard
  silently skipped teardown on every sidebar exit — the historical SFX-volume leak (fixed 2026-06-01).
- Review together on change: `src/audio/PreviewAudioSettings.*` (`makePreviewLatencyAuditionLevels`),
  `sections/preview/MainWindow.PreviewWarmupAndSettings.cpp` (`applyPreviewAudioSettingsToRuntime`),
  `src/tools/latency/LatencySandboxController.*`,
  `sections/timeline/MainWindow.PreviewPlaybackGlue.cpp`,
  `sections/timeline/MainWindow.PreviewTimelineFlow.cpp` (`hasPreviewableChart`),
  `sections/document/MainWindow.DocumentUi.cpp` (`switchToLatencyField` + the `onPageLeft` calls).

### 12a. Export-page preview reuses the same transport (2026-06-13)

The export hub's video sub-page applies the SAME pattern as the latency page: the badge-selected
difficulty is installed as a playable preview source so the NORMAL transport plays/seeks it even
though `activeDifficultyId_ == 0` (D4). `ExportSection::installExportPreviewAuditionScene`
(`MainWindow.ExportSnapshot.cpp`) mirrors `installSandboxScene` (publish markers + snapshot-ready,
bottom-timeline rebuild, slider range, SFX `configureTimeline`), and
`state_.exportPreviewAuditionActive_` is OR'd into `hasPreviewableChart()` alongside
`latencySandboxAuditionActive_`. Install is in `createEmbeddedVideoExportPanel` (re-runs on badge
switch — the panel is recreated); teardown is in `endExportPreviewSession`
(`teardownExportPreviewAuditionScene` — stop + clear flag + invalidate snapshot; no cache/restore,
the destination field reinstalls its own preview). Unlike latency, audio levels are NOT
mode-switched (export uses the user's normal mix). Export progress is status-bar-only and never
touches the transport — preview and a running export are independent. Review together:
`MainWindow.ExportSnapshot.cpp`, `MainWindow.ExportFlow.cpp` (lifecycle),
`PreviewTimelineFlow.cpp` (`hasPreviewableChart`), `MainWindow.ExportWorker.cpp` +
`WindowSection.cpp` (progress decoupling). Supersedes the reverted "导出效果预览".

- **The visible transport is QML, not the QWidget `ui_.previewSlider_`** (which is null on the
  export page). Any preview-transport state that QML needs (range, position, lower bound, export
  progress…) flows owner → `QuickShellContracts` (`shellPreview*` virtuals) →
  `QuickShellController` (`assignIfChanged` in its state poll → `Q_PROPERTY`) →
  `QuickShellPreviewTransport.qml`. Example: the negative-time 片头 lower bound is
  `exportIntroLowerBoundSeconds()` → `shellPreviewLowerBoundSeconds()` →
  `previewLowerBoundSeconds` → the slider's `from`. Add a transport field? Touch all four layers,
  and **touch `resources/quick_shell_qml.qrc`** so AUTORCC re-bundles the QML.

## 13. Bottom-tab content scale → timeline two-tier scale

The bottom-tab (时间轴/语法/无理 panel) divider height maps to `bottomTabsContentScale_`
(persisted as `bottom_tabs_content_scale`). It can now exceed 100% — clamped to
`[0.5, 4.0]` (`kBottomTabsContentScaleMax`, a safety bound; the practical ceiling above 100%
is `kBottomTabsMaxWindowHeightFraction = 2/3` of the window height, enforced in
`setShellBottomTabsHeight`). Propagation:

1. `MainWindow.WindowShell.cpp`: `setShellBottomTabsHeight` (drag) / restore →
   `applyBottomTabsContentScale` → `timelineQuickStateBridge_->setContentScale` +
   `timelineView_->setContentScale`. Device height via `scaledBottomTabsTimelineContentHeight` /
   `bottomTabsContentScaleForTimelineContentHeight` (piecewise inverse — header caps at 100%,
   lanes grow). `bottomTabsHeaderScaleForContentScale` = `0.5 + min(scale,1)*0.5` (caps at 1.0).
2. QSG scene: `TimelineSceneStateBuilder::buildLayoutMetrics` — **two-tier split**:
   `gridContentScale` (raw, up to 4.0) drives ONLY `laneHeight`/`timelineHeight`;
   `normalizedContentScale` (capped at 1.0) is stored as `state.contentScale` and drives note
   素材/markers, lane-label fonts, header (`headerContentScale`), margins. Notes position off
   `laneHeight` (grows), size off `contentScale` (capped) → taller grid, 100% markers.
3. Legacy `TimelineView` parity: `TimelineView.Core.cpp` `laneHeight()` uses the raw scale;
   `scaledTimelineMetric` / `headerContentScale` stay capped.
4. 语法/无理 list fonts are a fixed 90% of base (`kBottomTabsIssueListFontScale`), uniform /
   height-independent (NOT `headerScale`-driven); their scrollbars use
   `UiTheme::scrollBarStyleSheet()` like the editor. `QuickShellController::bottomTabsHeaderScale`
   (QML tab strip) still inherits the capped header scale.

**SYNC-PAIR:** the `4.0` max is duplicated as `kBottomTabsContentScaleMax`
(`MainWindow.WindowShell.cpp`), `kMaxContentScale` (`TimelineSceneStateBuilder.cpp`), and a literal
`4.0` in the `setContentScale` clamps of `TimelineView.cpp` / `TimelineView.Core.cpp` /
`TimelineQuickStateBridge.cpp` — change all together (also `hardcode-registry.md`). This scale is
**UI-only** (in-app timeline panel); it has no video-export consumer.

## Update this file when

- A behavior starts/stops being mirrored across two paths; a new serialized export field is added;
  a duplicated lookup is centralized/split; a timing rule starts affecting a new subsystem.
