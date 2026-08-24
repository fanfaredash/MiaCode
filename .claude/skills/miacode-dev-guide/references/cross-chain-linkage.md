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
  adding/removing slots.
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
    in `setPlayheadSeconds` (+ `setNoteMarkers` + `reset`) so a seek / negative pre-roll can't strand it
    outside its lifecycle window. Dropping the notify call or the re-center silently regresses to the old
    probabilistic first-firework stutter. Logs: `preview/runtime action=firework_pso_warmup_arm|_done`
    (Runtime channel).
    - **The re-center is SLACK-GATED, not per-frame.** The travel test lives in
      `src/core/scene/PreviewFireworkWarmupPolicy.h` (`fireworkWarmupNeedsRecenter` /
      `fireworkWarmupMarkerSecond`) and is calibrated against the layer's real lifecycle window by
      `preview_firework_warmup_policy_spec`. It used to fire on EVERY playhead change — i.e. once per
      preview frame for as long as the warm-up stayed armed, and it stays armed until a frame that
      actually draws the synthetic is presented, which on an idle paused preview does not happen until
      the user's first playback. Each re-center rewrites the whole marker vector and bumps
      `sceneContentRevision`, which invalidates `PreviewPreparedSceneCache` and forces a full ten-layer
      rebuild; that per-frame rebuild was the bulk of the "first playback after startup stutters"
      report, and it recurred mid-session on every visible-window rebind (F11 re-arms via
      `setVisibleHostWindow`). Do not restore an unconditional per-playhead re-center.
    - **INVARIANT: while armed-but-not-done, exactly one synthetic lives in `frameState_.noteMarkers`
      and `fireworkWarmupCenterSecond_` is its centre.** Any site that clears `noteMarkers` must
      re-append it (`setNoteMarkers`, `reset`) — with the slack gate in place, a missing synthetic is
      no longer healed by the next frame's re-center.
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
  start/stop event (and on pause/restore/seek), seeking the riser to `second - span.startSecond`.
  This is order-independent, so a seamless join (prev `endSecond` == next `startSecond`), overlap, or
  nesting lets the newer touch-hold take over the voice — restarting the riser from its own start —
  instead of the older span's stop clobbering it. Don't go back to per-event naive start/stop or a
  first-wins active-set.
  **Export is a sync pair with this, NOT independent.** It has no voice, so it emulates the same
  ownership offline: `preview_sfx_timeline::buildTouchholdOwnershipSegments(spans)` flattens
  latest-wins into `{startSecond, endSecond, spanIndex, sourceOffsetSecond}` stretches (owner
  evaluated once per span boundary), and `buildTouchholdSpanPlaybacks`
  (`VideoExportAudioRenderPlan.cpp`) mixes ONE riser clip per stretch at that source offset into
  `VideoExportAudioRenderPlan::touchholdSpanPlaybacks`; both export backends honour
  `sourceStartSecond`. Export used to *merge* adjacent/overlapping spans into one long clip, so the
  second of two back-to-back touch-holds sounded like a continuation of the first while preview
  restarted it — never reintroduce that merge. The partial-range pre-range clamp
  (`suppressSfxBeforePreRangeEnd`) advances `sourceStartSecond` with `mixSecond` for the same reason:
  preview seeking into a live touch-hold resumes mid-sample, so export must too. Coverage:
  `verifyTouchholdOwnershipSegments` (`PreviewSfxTimelineSpec.cpp`) for the flattening,
  `verifyTouchholdSpanLatestWinsPlaybacks` (`VideoExportAudioRenderPlanSpec.cpp`) for the mix plan.
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
- **`EndOfMedia` is never a transport event.** A background video is subordinate visual media, so
  its end may not stop BGM / SFX / chart / timeline — that coupling was the root cause in
  `docs/audit/PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md` (a 0.333 s `pv.mp4` paused the whole
  preview 31 times). The only natural end of the main transport stays
  `MainWindow.PreviewTick.cpp::onQtPreviewTickAtSecond()` against `previewPlaybackEndSeconds()`.
  Every backend `EndOfMedia` is classified by `src/core/video/PreviewEndOfMediaPolicy.h` (spec:
  `preview_end_of_media_policy_spec`), whose ONLY yardstick is the media's own duration versus the
  decoder's own progress — never the chart length, and never a "shorter than N seconds is
  suspicious" threshold. `natural` keeps the last frame; `stale` (audit
  `PREVIEW_FIRST_PLAY_RENDER_STALL_HANDOFF_AUDIT_ZH.md` §5.2 — a 121 s PV ending at 1.267 s) runs
  the bounded seek-then-reload recovery in `PreviewStageMediaHost_Timeout.cpp`; `unknown` does
  nothing. Change the classifier and the spec together, and keep both backends
  (QtAVPlayer + `QMediaPlayer`) routed through `handleVideoEndOfMedia`.
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

**Preview audio spans TWO storage tiers — keep them straight (2026-08-18):**

| Tier | Key | State | Written by |
|---|---|---|---|
| **Project** `<chartDir>/.miacode/preferences.json` | `preview_audio` | `state_.previewAudioSettings_` (the live mixer) | every slider / mute / 应用本地预设, via `MainWindow::saveProjectAudioPreferences` |
| **App** `preferences.json` | `app.preview.audio` | `state_.softwarePreviewAudioSettings_` (本地预设) | only the 保存为本地预设 button, via `savePortableState` |

The mixer is per-chart. `PreviewSection::loadProjectAudioPreferences` runs from
`TimelineSection::setCurrentFilePath`'s `pathChanged` branch — **before** `reloadAssetsForChart` /
`applyPreviewAudioSettingsToRuntime`, or the outgoing chart's levels leak into the incoming one.
The preset's ONLY role is seeding a project that has no stored `preview_audio` (new chart, or one
predating this split); it is never written back to from the project path.
`state_.previewAudioSettingsEditedWithoutProject_` carries edits made with no chart open into the
first project bind that has no stored mixer, so saving a new chart doesn't discard them.
`break_slide_tail_cheer_muted` stays app-scoped and overrides whatever the project blob carries in
that field.

Regression this replaced: `savePortableState` persisted `softwarePreviewAudioSettings_` while the
dialog sliders edited `previewAudioSettings_`, so every volume change was re-written as the
unchanged default and lost on restart. Anything new that edits `previewAudioSettings_` must call
`saveProjectAudioPreferences()`, not a second store.

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
- **Preview auto-pause on audio-device change ⇄ export's "pause the preview first" step.**
  `TimelineSection::pausePreviewForAudioDeviceChange` only acts when `qtPreviewPlaying_` is true, and
  the only reason a device hotplug can't disturb a running video export is that
  `MainWindow.ExportFlow.cpp` pauses the preview before opening the export dialog. If export ever
  starts while playback continues, that guard must be re-examined — it is the sole protection.

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

- **批量导出 sub-page: a badge switch retargets in place, so it must re-seed by hand.** The
  video sub-page gets its per-difficulty payload for free (the panel is destroyed + rebuilt from
  a fresh `buildVideoExportSeedTask`). The batch panel deliberately SURVIVES the switch (queue +
  settings are the user's work), so `ExportSection::updateEmbeddedBatchExportPreviewDifficulty`
  owns the whole re-seed: audition scene (teardown + install), chart-info HUD
  (`applyExportPreviewChartInfo`, shared with `beginExportPreviewSession`), and the shared
  settings panel's chart payload (`BatchExportPanel::updatePreviewDifficulty` →
  `VideoExportDialog::retargetChartPayload` — 片头 banner fields / markers / duration, while
  intro on-off + 片头 sound stay user-owned). Adding a difficulty-derived field to
  `buildVideoExportSeedTask` means adding it to `retargetChartPayload` too, or the batch page's
  preview silently keeps the panel's opening difficulty.

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

## 14. Timeline playback sampling is phase-locked to its own render cadence

Preview and timeline live in **different QQuickWindows** with independent render threads, so they
cannot share a present signal. Each phase-locks to its own:

- **Preview:** `PreviewQuickSceneRoot` → `QQuickWindow::frameSwapped` (render thread, queued to GUI)
  → `PreviewRuntime::framePresented` → the request/present handshake in
  `MainWindow.FrameBootstrap.cpp` → `onQtPreviewTick`.
- **Timeline:** `TimelineQuickItem::bindRenderCadence` → `QQuickWindow::afterAnimating` (GUI thread,
  fires immediately before that frame's scene-graph sync) → `TimelineQuickStateBridge::
  renderCadenceTick` → `MainWindow::onTimelineRenderCadenceTick` → `flushQtPreviewTimelinePosition`.
  `afterAnimating` rather than `frameSwapped` on purpose: it is already on the GUI thread, so the
  sampled second lands in the frame being synced right now — a fixed one-frame sample→present
  latency with no event-loop hop.

`qtPreviewTimelineTimer_` is a **watchdog, not the cadence**. It stays armed at the timeline frame
interval but `onTimelineCadenceWatchdogTick` yields while the cadence is alive; arbitration lives in
`src/timeline/TimelineCadenceArbitrationPolicy.h` (spec:
`timeline_cadence_arbitration_policy_spec`). Driving the sample from that free-running timer was the
timeline-judder bug: its phase drifts against vsync (~2.3us/frame measured over a 139s capture), so
sample→present latency wandered the whole frame interval and ~16% of on-time frames were drawn for
the wrong moment while frame delivery itself was healthy.

**SYNC-PAIR:** `TimelineQuickStateBridge::setPlaybackCadenceActive` must be set true/false with the
playback transport (`finalizeQtPreviewPlaybackStart` / `stopQtPreviewTimers`, alongside the
`qtPreviewLastTimelineCadenceMs_` reset). It is what keeps the timeline window rendering during
playback so `afterAnimating` keeps arriving; leaving it stuck true burns frames while paused,
leaving it stuck false drops the timeline onto the watchdog.

### 14b. The timeline scroll is sub-pixel (`double`)

`TimelineQuickStateBridge::horizontalScrollValue`, `TimelineSceneBuildRequest::
horizontalScrollValue` and `TimelineSceneState::horizontalScrollValue` are **`double`**. Follow
playback moves the scroll ~1-4 logical px per frame (`pixelsPerSecond = 120 * zoom`), so rounding
it quantised the scroll velocity: at zoom 0.25 the content froze on half the frames, at 0.75 it
alternated 1/2px, and at 2.0 it produced the 3/4/5px stepping measured in the judder
investigation. Phase-locking alone (§14) could not fix this — it made the sampling error white
instead of drifting, which a whole-pixel quantiser turns into *more* visible snaps, not fewer.

Consumers that genuinely need an integer round at their **own** boundary, and each is a
deliberate decision, not an oversight:

- `TimelineView` → `QScrollBar::setValue` (int API; `TimelineView`'s own
  `horizontalScrollValue()` stays int because it *reads* the scrollbar).
- `MainWindow.ExtensionHostRequests.cpp` timeline-state payload — extension-facing contract that
  has always carried a whole-pixel integer.
- `TimelineSceneStateBuilder::maxHorizontalScrollValue` — a content extent, not a position.

**SYNC-PAIR:** the Phase-7 cull bucket is derived in two places — the revision offset in
`applyDynamicSceneState` and the rebuild cache key in `currentSceneState`. Both must go through
`scrollBucketIndex()` in `TimelineQuickItem.cpp`; a bare `/` is float division now, so writing it
by hand in one place silently desyncs the two and layers rebuild on the wrong frames.

**Scroll targets must use `secondToSceneXExact`, not `secondToSceneX`.** Same for the playhead /
cursor / entry-marker X in the builder: with a sub-pixel scroll, a rounded playhead X no longer
cancels against the scroll and the playhead shimmers ±0.5px against smoothly-moving content.
Note world X (notes, non-exact grid lines) stays rounded — those are static per note, so they
cost a fixed ≤0.5px placement offset and no motion artefact.

Watch for silent `double`→`int` narrowing when touching this code; it compiles without error.
`cmake -DCMAKE_CXX_FLAGS=-Wfloat-conversion` catches it.

**The waveform layer is the one exception — it snaps its translate to the DEVICE pixel grid.**
The waveform is a column dataset (`kWaveformTopLevelColumnsPerSecond = 128`, levels halving from
there) drawn as abutting hard-edged translucent bars. Above zoom ~1.07 the finest level is coarser
than one column per logical pixel (at zoom 2.0 a column is 1.875 logical px); below it the builder's
`qMax<qreal>(1.0, x1 - x0)` clamp makes neighbours overlap. Either way, nothing in this app is
multisampled (no `setSamples` anywhere), so translating that band by a different fraction each
frame resamples it unfiltered — each bar's rasterised width flips between floor and ceil and the
band crawls.

Snapping freezes the per-frame rasterisation and kills the crawl, but the band then steps while
the notes/grid glide, so it sways against them by the snap quantum. Snap to **physical** pixels
(`round(scroll * dpr) / dpr`), not logical ones: rasterisation happens in device pixels, so a
logical-pixel snap makes that sway `dpr` times larger (2x on Retina) for nothing. A residual
one-device-pixel sway is inherent to snapping; the only way to remove it entirely is to give the
band a filter — render it to a texture and let bilinear sampling resample it.

**`TimelineQuickGridLinesLayer` snaps the same way**, for the same reason:
`tryAppendOrthogonalLine` turns every grid line into a flat-colour rect 1.0-2.0px wide
(subdivision 1.0 / beat-measure 1.5 / cursor 2.0), and the overlay path uses `QSGSimpleRectNode`
— all unfiltered, so a fractional per-frame translate makes each line's coverage flip between N
and N+1 device pixel columns and the lines shimmer in apparent thickness. Residual cost: lines
step while note sprites glide, so a note on a beat can sit up to half a device pixel off its
line. That is half the swing of a logical-pixel snap and much less visible than the shimmer.

**Which timeline elements are on which path — check this before adding a layer:**

| element | geometry | sub-pixel motion | needs snapping? |
|---|---|---|---|
| notes, holds, slide tracks | sprite batches, `QSGTexture::Linear` | resampled by bilinear filtering | **no** — genuinely smooth |
| waveform band | flat-colour rects | unfiltered | yes (device grid) |
| grid / measure / beat lines | flat-colour rects | unfiltered | yes (device grid) |
| playhead line | flat rect, but screen X is **constant** in follow mode (exact `playheadX` cancels the exact scroll target) | n/a | no — stable by construction |
| cursor line, header markers | flat rects/triangles that move with content | unfiltered | **not yet snapped** — same mechanism, still open |

The rule: **flat-colour geometry needs snapping; textured sprites do not**, because linear
filtering already resamples them correctly. If a new layer emits hairlines, it inherits this
problem.

**Known consequence of the exact playhead:** notes still use `secondToSceneX` (rounded world X)
while the playhead uses `secondToXExact`, so a note whose time equals the playhead's can sit up
to 0.5 logical px off the playhead line — where previously both rounded identically and matched.
Making note world X exact would fix it (sprites are linear-filtered, so fractional positions cost
nothing) but shifts every note by ≤0.5px.

**Intentional constant offset:** the timeline applies **no** lookahead bias, while the preview scene
playhead is shifted forward by `previewVisualLookaheadVsyncs` (default 1.0, see
`applyVisualClockSmoothing`). The timeline therefore trails the preview by ~1 vsync by design. Do
not "fix" that asymmetry without deciding what the offset should be — it is a constant, not drift.

## 15. `||` comment scanning is a three-place sync set

A simai `||` comment runs from the marker to the end of **its line**. Three places encode that:

1. `SimaiNativeParser.Driver.cpp:797` — per-line char loop, `break`s at the marker.
2. `TimelineQuickModelParser.cpp:646` — same shape, same `break`.
3. `src/core/chart/parser/SimaiCommentScan.*` — the flat-text form
   (`previousChartComma` / `nextChartComma` / `chartContentSpans`) for callers that scan the whole
   document as one string and so have no per-line loop to break out of. Used by
   `planTouchPadAuthoringEdit`.

Two consequences that any new flat-text scanner must respect, and that specifically broke touch
click authoring before 2026-08-12:

- **A `,` inside a comment is prose, not a beat separator.** The editor normalizes full-width `，`
  to `,` (`PlainCodeEditor.Input.cpp:43`), so Chinese comments really do contain them. Splitting on
  a raw `indexOf(',')` puts the token boundary inside the comment and authors chart text into it.
- **A comment ends at its newline, not at the token end.** One comma token can hold chart content
  on both sides of one (or several) comments, so `text.indexOf("||")` must not be treated as the
  token's content end — doing so hides real notes and produces duplicates.

If a fourth scanner appears, route it through `SimaiCommentScan` rather than re-deriving the rule.

## 16. Preview follow reaches the v1 widget and the v2 QML editor on two different channels

`MainWindow::TimelineSection::syncEditorCursorToPreviewSecond` has two modes, and they do NOT use
the same route to the editor:

- **Playing, 代码跟随 on** → it MOVES the caret. Route: `requestQmlEditorNavigation` →
  `QmlEditorNavigationRequest` → `QmlDocumentModel::qmlEditorNavigationRequested` →
  `SourceEditor.selectBackendNavigation`. The hidden `PlainCodeEditor` is only touched when no QML
  handler is installed (v1).
- **Paused, or 代码跟随 off** → it must NOT move the caret; it paints a decoration instead (the
  playhead's token span plus a visual follow caret, optionally scrolled into view). Route:
  `MainWindow::setPreviewFollowDecoration` / `clearPreviewFollowDecoration` → both the v1
  `ValidationSection` extra-selections/visual caret AND `QmlEditorFollowDecoration` →
  `QmlDocumentModel::qmlEditorFollowDecorationChanged` → `SourceEditor.applyFollowDecoration`.

That second channel was missing until 2026-08-24, which is why v2 followed while playing and did
nothing at all once paused. **Every decoration passes through that one MainWindow pair**, so feed
new decoration sources from there rather than adding a branch inside the follow sync.

Two related rules:

- The v2 decoration is read-only. It must never move the caret or selection, and its
  ensure-visible scroll must not either — that is the whole difference between it and the playing
  caret-move path.
- Both routes are gated on the same difficulty + `documentRevision` identity as the document
  projection, and both are refused on a hidden or metadata-mode editor.

An editor Ctrl/Command click is the mirror image: v1 resolves it in an event filter on the widget
viewport (`MainWindow.WindowInteraction.cpp`), v2 in `SourceEditor.qml`'s `PointHandler` →
`QmlDocumentModel::seekPreviewToEditorLocation` → `MainWindow::seekPreviewToQmlEditorLocation`.
Both perform the same sequence — resolve second, park playback, suppress timeline cursor sync
across the discrete seek, then hand the timeline its cursor. Change one, change the other.

## Update this file when

- A behavior starts/stops being mirrored across two paths; a new serialized export field is added;
  a duplicated lookup is centralized/split; a timing rule starts affecting a new subsystem.
