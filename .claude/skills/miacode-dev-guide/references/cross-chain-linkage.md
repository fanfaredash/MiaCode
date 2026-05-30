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
- Firework visuals use the custom `PreviewQuickJudgeFireworkLayer` material; state in
  `src/core/scene/PreviewJudgeFireworkLayerState.*`, shader in
  `src/preview/quick_scene/shaders/PreviewFireworkMaterial.*`.
- During playback: slow-refresh markers feed validation/Muri inputs, but preview audio/canvas/stats
  stay on the frozen play-start snapshot until stop; validation/Muri UI may defer to a paused edge.

## 2. `&first` / timing-offset chain

- `SimaiDocument` stores raw `first`; `MainWindow::parsedFirstSeconds` is the getter; preview/export
  use the finite parsed raw `&first` directly (no inverted "effectiveFirst").
- `TimelineQuickModel` receives `first` on every rebuild; `buildTimelineSlowRefreshResult` shifts
  markers by `first`; `MainWindow::applyLatencyDetectorOffset` writes raw `first` back.
- Review together on change: `sections/timeline/MainWindow.PreviewTimelineFlow.cpp`,
  `src/tools/latency/` analysis, `src/tools/video_export/VideoExportSnapshot.cpp`,
  `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`.

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
- `&first` finite-raw direct use; `audioOffset` = whole-SFX chart shift; `displayOffset` advances
  answer/judge families; `answerOffset`/`judgeOffset` family-specific; `1/60 s` pre-trigger for
  answer + tap/hold/touch-family judge only.
- hold tails emit answer+judge (EX/break/break-EX tails answer-only); touch-hold tails answer-only;
  break-slide tail = flagged `break` + `judge_break_slide` routed through `break_slide_tail_break`.
- `&clock_count=` is export-only count-in (`src/common/ChartClockCount.h`); full-range lead-in `2.0s`,
  partial preload `1.0s` (`src/common/VideoExportConfig.h`).

If one side changes, inspect the other in the same patch.

## 5. Background-media resolution & host route ownership

> Updated 2026-05-29: `PreviewMediaController` and `src/preview/video/` were removed. Background
> media (image + video) is now owned by `PreviewStageMediaHost`; `PreviewRuntime` exposes
> stage-background state setters for the QSG stage layer. Verify the exact widget-shell vs
> quickshell split in `sections/preview/MainWindow.PreviewStageMediaRoute.cpp`.

- Shared resolver: `miacode::chart_assets::resolveBackgroundMediaPath`.
- Route coordinator: `sections/preview/MainWindow.PreviewStageMediaRoute.cpp`.
- Shared preview-time clock getter: `MainWindow::currentPreviewAuthoritativeAudioClockSecond`
  (UI follow, export-dialog current second, weak-video late-start must read this — do not branch on
  `PreviewStageMediaHost::currentPlaybackSecond()`, which is video-local observability only).
- Quickshell presentation split: images inline in `QuickShellPreviewSurface.qml`; video moves to
  `QuickShellPreviewCompositeSurface` (own `QQuickView`).
- Export consumes the shared resolver via `VideoExportController`; Windows export audio = single
  mixed WAV via `BassExportAudioBackend`, non-Windows = `LegacyExportAudioBackend` fallback.
- Filenames: `bg.mp4`, `pv.mp4`, `bg.{jpg,png,jpeg}`. Keep preview + export aligned.

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
- Leaving the page (`setOnPage(false)`) stops the transport and restores the previous chart's
  preview state + audio settings.
- Review together on change: `src/tools/latency/LatencySandboxController.*`,
  `sections/timeline/MainWindow.PreviewPlaybackGlue.cpp`,
  `sections/timeline/MainWindow.PreviewTimelineFlow.cpp` (`hasPreviewableChart`),
  `sections/document/MainWindow.DocumentUi.cpp` (`switchToLatencyField`).

## Update this file when

- A behavior starts/stops being mirrored across two paths; a new serialized export field is added;
  a duplicated lookup is centralized/split; a timing rule starts affecting a new subsystem.
