# Cross-Chain Linkage

## Timeline playback cadence

- `TimelineQuickItem::bindRenderCadence` emits an opportunity from every
  `QQuickWindow::afterAnimating`, while `TimelineSection::onTimelineRenderCadenceTick` rate-gates
  accepted samples with `timelineTargetFrameIntervalNs()`. Keep that gate phase-locked through
  `TimelineCadenceArbitrationPolicy::renderCadenceShouldFlush`; otherwise high-refresh displays
  bypass the Timeline Refresh Rate preference and repeat editor-follow/QSG work unnecessarily.
- Every render-cadence opportunity still refreshes the liveness marker. The watchdog must yield
  to a healthy render loop even when a lower timeline rate intentionally skips samples.
- `TimelineQuickOverlayLayer` keeps fixed `QSGSimpleRectNode` slots for the playhead, cursor, and
  drag-center lines. Playback updates their rectangles in place; do not restore per-frame
  `clearChildren()` allocation on this dynamic path.

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

Validation presentation shares that analysis result. Both explicit validation and background
analysis populate `ValidationCacheEntry`, including the parser's explicit severity. The aligned
active-difficulty cache is exposed as `DocumentValidationSnapshot`; `documentValidationChanged`
updates UIv2's bottom list, navigation target and editor wave underline through one
`QmlDocumentModel::syntaxIssues` projection. `QmlAnalysisModel` reads the narrower
`MainWindow::qmlAnalysisSnapshot()` to render the Validation and Muri tabs; it receives only
row values, never a `QListWidget` or mutable `MuriAnalyzer` state. Muri rows are visible only
when their note-marker signature, active difficulty, and revision align with validation; otherwise
the entire analysis projection is pending and empty. QML reads the cached projection's
`documentRevision` / `validationRevision` / `validationPending` as one state: diagnostics
and navigation must be hidden while the revisions differ. Full metadata-source replacement is
a two-phase transaction: preflight every candidate difficulty with strict validation, retain the
old document/editor text on any error, then load and refresh the timeline only after acceptance.
Cache clearing publishes the same signal.

Backend-owned document replacement is a separate atomic boundary: startup targets, root
ChartDrop, native open/new/discard/recovery and accepted full-source replacement all funnel
through `DocumentSection::loadDocument()`, which publishes `MainWindow::documentReplaced()` only
after file identity, active difficulty, title and document-owned bookmarks are current.
`QmlDocumentModel` republishes its complete state and its own `documentReplaced` signal from that
backend event; consumers must rederive text, difficulty tabs and bookmarks rather than retaining a
previous-document cache. UIv2 explicit validation first schedules the ordinary timeline slow
refresh, then publishes its immediate validation result; the scheduled generation is responsible
for publishing the matching validation/Muri/static-reference revision. `ViewState` replacement
tab reset is presentation-only: it must not issue another difficulty-selection request after
`loadDocument()` has already activated the target field, or it creates a second timeline refresh
and validation revision. The slow-analysis completion writes validation, Muri, and static-reference
provenance before `documentValidationChanged`; synchronous observers may read the snapshot during
that signal and must never see a validation-only intermediate state.

UIv2 text input is an explicit side chain, not a direct `TextArea.text` contract:
`SourceEditor.qml` sends physical keys, committed IME text and paste payloads separately through
`QmlEditorInputBridge` to `QmlEditorController`; the controller returns one edit transaction,
completion state and controller-owned undo/redo history. The editor applies that transaction,
then publishes its current difficulty, document revision, anchor/position, focus and IME-composing
state through `QmlDocumentModel::setQmlEditorInteraction`. Its `TextArea` key route is
`Keys.BeforeItem`, so Tab/Enter/arrows/Escape reach the completion controller before native text
handling. Ctrl+touch authoring must consume only that snapshot: `QmlTouchPadAuthoringBridge`
rejects stale revisions, a different difficulty, unfocused editor and active composition.
`MainWindow` uses the QML handler exclusively while it is registered; it must not silently fall
back to the legacy hidden editor. Reverse timeline/preview navigation additionally passes through
`QmlEditorNavigationReadiness`: `SourceEditor` reports its visible, non-metadata source together
with active difficulty/revision; only a matching ready state emits
`qmlEditorNavigationRequested` and acknowledges the request. A hidden or metadata source returns
false without moving the legacy editor.

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
  layers, mine OVERRIDES break/each) ↔ timeline icons ↔ type-based SFX (`PreviewSfxTimeline`
  `buildTimeline`) plus **Muri suppression** (`MuriAnalyzer` pad windows,
  `MuriRuntimeModelBuilder` judgeable notes, and `MuriStaticChecker` cause/affected
  candidates all exclude `isMine || trackMine`) ↔ specs.
  Mines deliberately DO count in `PreviewProgressStatsCache` (product decision). Skin art =
  `<base>_mine.png`; `PreviewRenderState::useMineSkin` is presentation-only and may be controlled
  through the bundled extension, so every preview selector/layer must fall back to normal art when
  it is false without clearing the mine flags. In that normal-art mode, EX mine heads must also
  restore the normal EX overlay (`3xm` renders like `3x`, not `3`).
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
- Strict validation warns when a bracketless short lane hold has modifiers after `h` (for
  example `1hx` or `1hb`). MiaCode still parses it as a zero-length hold, but some external
  previewers require the short-hold token to end in `h`; canonical forms such as `1h` and
  duration-bearing holds such as `1hx[4:1]` remain warning-free under this compatibility rule.
- BASS preview seeks beyond the decoded BGM duration keep the BGM explicitly paused and mark it
  `backgroundTrackPastEnd`; direct start/resume, pending-offset start, and mixer-sync start must
  all respect that state. A chart may continue playing SFX/visuals beyond the music, but an
  out-of-range seek must never reuse the BASS source's previous valid cursor position.
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
  - The supplied 30 fps firework reference is the timing source: the shared lifetime is
    `PreviewGameplayConfig::kJudgeEffectFireworkDurationSeconds` (also used by timeline culling),
    and explicit clip time/life uniforms drive two batches of 12 fixed inner/outer-ring stars
    (24 total, with six inner and six outer stars per batch plus deterministic size/ring
    shuffling). Star
    angles sample the full circle directly, intentionally permitting clusters and empty arcs. The
    QSG firework node generates a fresh angle seed at each trigger/replay, holds it for that effect's
    lifetime, and passes it through `timing.z`, avoiding per-frame jitter. Each batch reveals its inner
    ring first and its outer ring about 0.010 s later; per-ring jitter is capped at 0.004 s so the
    ordering cannot invert. Staggered sine pulses make the stars continuously fade in and out. Their
    inner centres sit inside the colour-glow ring (0.40–0.47 of the judgment radius), while outer
    centres sit around 0.83–0.90 and are inset by each star's tip radius so they cannot cross the
    judgment ring. Star geometry scales with the same continuous pulse, producing the reference's
    clearly visible shrink-to-zero disappearance rather than an alpha-only fade. The sparkle keeps
    its original sector-derived tint; its shallow-concavity four-point silhouette is intentionally
    filled rather than crossing lines. Its pulse keeps a 0.115 s fade-in followed by a 0.25 s
    shrink/fade-out (0.365 s total), emitted in two spatially shuffled batches starting at 0.00 s
    and 0.08 s. Its horizontal span remains broad;
    its vertical extent is compressed to match that horizontal span, while the waist/edge concavity
    remains fine. Each star travels radially outward throughout its
    pulse: 0.02 of the judgment radius while appearing, then another 0.04 while shrinking/fading
    (0.06 total). Appearance fades in at full geometry size; scaling applies only to disappearance.
    Star visible half-extents vary deterministically from 0.040 to 0.050 of the judgment radius,
    keeping repeated playback stable. Tips remain dynamically clamped inside the judgment boundary;
    peak opacity is 0.95.
    Keep the original 15-sector/24-degree spoke layout when tuning timing or particles.
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
  preload / pause glyph); only start > 0 is partial. The UI-side decision is shared by v1 and v2 via
  `miacode::video_export::isFullRangeVideoExport` in `VideoExportSettings`; snapshot construction must copy that task value
  unchanged. The export hub's single and batch embedded panels both re-seed the audition
  clock after installation and on the shared checkbox signal; batch badge changes use the current
  batch setting but do not change its selected output difficulties.

If one side changes, inspect the other in the same patch.

### 4a. Preview-audio GUI to worker boundary

Realtime preview is not an export worker: `QtPreviewSfxRuntime` stays in the GUI thread as the
facade while `PreviewAudioWorker` owns its native backend on one `std::thread`. MainWindow startup,
timeline ticks, settings audition, latency sandbox, and `soundtouch_probe` all submit typed value
commands through the bounded queue; only probe/spec code may wait on the non-GUI completion barrier.

- Playback completions must match current `generation` and `transactionId`; reload/ready completions
  must match current `assetGeneration`; a device pause must also match the first captured
  `pauseToken`. These predicates live in `PreviewAudioWorkerProtocol.h` and are shared by the
  facade and the specs.
- A chart-path change followed by an asset reload is one asset transaction, not two independently
  replaceable commands: use `QtPreviewSfxRuntime::reloadAssetsForChart`. Window/chart SFX warm-up
  additionally uses `reloadAssetsForChartWithWarmupPaths`, which carries resolved SFX/track paths
  and the chart path on ONE `ReloadAssets` command. `PreviewAudioWorker::executeReload` applies the
  requested path state before the backend reload in that same `assetGeneration`; posting
  `setChartPath` or `setWarmupResolvedPaths` immediately before a separate reload lets
  stale-command pruning drop required state and can leave BGM unloaded. MainWindow schedules this
  combined preload only after the initial document/chart path is established and never while a
  device-cutoff barrier is active; the first explicit Play only falls back to it when no preload is
  pending or ready. `soundtouch_probe` uses the chart-only form.
- `PreviewAudioDeviceWatcher` -> MainWindow captures the wall-clock pause second and freezes the
  GUI/video state before submitting the reserved high-priority device pause. On Windows, a successful
  IMM registration is the sole hotplug source: do not construct or synchronously enumerate
  `QMediaDevices` on that path, because its AudioSes RPC can block the GUI during a switch. Qt remains
  the registration-failure and non-Windows fallback. The worker later stops audio/SFX; it does not
  advance the playhead and it must not auto-resume after a device recovers.
- `PreviewBassDeviceLease` is shared by preview, waveform, and export BASS lifecycle users. Keep
  BASS global device init/free serialization there, but keep normal channel work within its owner.

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
- **Background video never owns the main preview transport lifetime.** A PV/BG `EndOfMedia` only
  marks `PreviewStageMediaHost` video playback inactive; it deliberately leaves the last decoded
  frame retained in the video sinks so the background freezes on that frame. It must not pause the
  chart, BGM, SFX, or timeline. The sole natural-completion owner is
  `TimelineSection::onQtPreviewTickAtSecond`, using `previewPlaybackEndSeconds()` and the unified
  `max(chart end + tail, music duration)` policy. Review the QtAVPlayer and QMediaPlayer EOM branches
  in `PreviewStageMediaHost_Backend.cpp` together with the signal wiring in
  `MainWindow.PreviewStageMediaRoute.cpp`; never reconnect visual-media EOM to
  `finishQtPreviewPlaybackAndReturnToEntry`.
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

`audioBitrateKbps` and `sizePreset` are part of this boundary. Single and batch snapshot builders
must copy both fields; `VideoExportSnapshot::{toJson,fromJson}` must serialize them; and
`buildVideoExportTaskFromSnapshot` must restore them before `exportPreparedTask`. The
`UltraCompactWithPv` and `UltraCompact` tokens share encoder tuning; only `UltraCompact` suppresses
PV in the prepared export task, never in the live/export-page preview.

v1 Widgets and v2 QML are presentation adapters over the same export contracts. Fixed options,
preference tokens, timestamp parsing and zero-start range classification live in
`VideoExportSettings`; batch preflight/execution lives in `ExportSection::runBatchExport`; snapshot
serialization and rendering remain below both. A new setting or batch rule must be added once at the
shared layer, then surfaced independently in each UI without copying backend logic.

The selected intro sound and its independent `introSoundVolume` follow the same single/batch
snapshot boundary. The dialog persists the 0..2 volume (0%..200%), applies it immediately through
`QtPreviewSfxRuntime::applyLevels`, and export restores it from `intro.sound_volume` before the
prepared FFmpeg intro-audio filter applies the multiplier. This is an independent multiplier: the
preview `track_start` level must not inherit the normal global/answer SFX attenuation, matching the
export filter. Keep preview and export volume behavior aligned when changing this setting.

`fixHudTextLayout` follows the same single/batch snapshot path and is serialized as
`render.fix_hud_text_layout`. It defaults false for legacy snapshots and gates the export frame
state's device-aware, glyph-safe HUD line layout. The dialog also applies it as a temporary live
preview override while the export-video page is active; `restoreLivePreviewState` and
`endExportPreviewSession` force it false on exit. The false branch preserves the original chart-info
and object-stats baseline calculations. The enabled branch derives same-font line advances from
device-bound glyph top/bottom bounds and cross-font advances from the previous glyph bottom to the
next glyph top (with typographic ascent/descent as a floor). The chart-info first baseline also uses
the actual glyph top so custom-font overshoot cannot intrude into the configured HUD padding.
`MIACODE_PREVIEW_HUD_PAINT_DIAG=1` records the flag plus the resolved HUD font metrics, calculated
advances, and chart-info visible top.

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
- BPM/offset analysis audio is decoded independently of the audition transport through
  `src/audio/OfflineAudioDecoder.*`. `LatencyDetectionPage` persists a strict BASS/miniaudio
  selection under `latency/audioDecoder`; switching it clears both cached envelopes. BASS is the
  supported OGG Vorbis path, while miniaudio preserves the original WAV/MP3/FLAC behavior. Do not
  silently cross-fallback, because the visible selector is also the user's diagnostic control.
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
  UIv2 mirrors the controller state through `QmlPreviewModel` into
  `src/app/qml_ui/preview/PreviewTransport.qml`. Keep its lower bound, negative-time formatting,
  cached scrub-release target, and touch/mouse/wheel/key forwarding aligned with
  `QuickShellPreviewTransport.qml`.

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
2. QML v2: `MainSplitView.qml` reads `QuickShellController::bottomTabsHostHeight` as its
   one-way `SplitView.preferredHeight`; only a completed divider gesture sends the measured height
   back through a short debounce. It must not create a pixel preference, install a fixed 340px cap,
   or feed every layout `heightChanged` back to the controller — `WindowShell` is the sole clamp
   authority. `BottomPanel.qml` maps header input controls through `timelineItem.y` and supplies all
   four QSG header limits from its zoom/brightness hit geometry.
3. QSG scene: `TimelineSceneStateBuilder::buildLayoutMetrics` — **two-tier split**:
   `gridContentScale` (raw, up to 4.0) drives ONLY `laneHeight`/`timelineHeight`;
   `normalizedContentScale` (capped at 1.0) is stored as `state.contentScale` and drives note
   素材/markers, lane-label fonts, header (`headerContentScale`), margins. Notes position off
   `laneHeight` (grows), size off `contentScale` (capped) → taller grid, 100% markers.
4. Legacy `TimelineView` parity: `TimelineView.Core.cpp` `laneHeight()` uses the raw scale;
   `scaledTimelineMetric` / `headerContentScale` stay capped.
5. 语法/无理 list fonts are a fixed 90% of base (`kBottomTabsIssueListFontScale`), uniform /
   height-independent (NOT `headerScale`-driven); their scrollbars use
   `UiTheme::scrollBarStyleSheet()` like the editor. `QuickShellController::bottomTabsHeaderScale`
   (QML tab strip) still inherits the capped header scale.

**SYNC-PAIR:** the `4.0` max is duplicated as `kBottomTabsContentScaleMax`
(`MainWindow.WindowShell.cpp`), `kMaxContentScale` (`TimelineSceneStateBuilder.cpp`), and a literal
`4.0` in the `setContentScale` clamps of `TimelineView.cpp` / `TimelineView.Core.cpp` /
`TimelineQuickStateBridge.cpp` — change all together (also `hardcode-registry.md`). This scale is
**UI-only** (in-app timeline panel); it has no video-export consumer.

### v2 follow-control and cadence contract

- The four independent controls are View Lock, Timeline Sync, Follow Code, and Progress Follow.
  `QuickShellController::openTimelineFollowSettingsMenu()` owns their accessible native-menu rows;
  `TimelineQuickStateBridge` mirrors their values; `MainWindow.EditorDisplay.cpp` persists the
  corresponding `viewport_lock`, `timeline_sync`, `follow_preview`, and `follow_progress` values.
  Do not replace a saved boolean with a fixed default during either load or save.
- `TimelineSection::flushQtPreviewTimelinePosition()` may skip QSG state writes until the bridge is
  ready, but it must continue to call the code-follow tick while the validation or Muri tab is
  foreground. Bottom-tab visibility is a render-routing condition, not an editor-follow gate.

## Update this file when

### QuickShell modal-dialog native ownership

- The visible QuickShell top level is a `QQuickWindow`; `MainWindow` remains a hidden native
  QWidget backend marked `miacode.dialog_parentless`. Application-modal dialogs therefore
  must not rely on their QWidget parent or a one-shot `raise()` for native Z-order ownership.
- `MainWindow::setQuickShellRootWindow` registers the live root with
  `UiDialogs::setApplicationDialogTransientParent`. The application-wide
  `UiDialogs::DialogStackingGuard` binds shown top-level `QDialog`s that lack a visible native
  owner (including direct legacy `QMessageBox::*` calls) to that root through
  `QWindow::setTransientParent`, then restores a visible blocking modal when the application
  or root window activates. Existing visible dialog owners are preserved for nested dialogs;
  non-modal dialogs are never force-activated by the stacking guard.
- Keep this behavior in the shared dialog layer. Do not add per-dialog
  `Qt::WindowStaysOnTopHint`: that would place MiaCode dialogs above unrelated applications.

- A behavior starts/stops being mirrored across two paths; a new serialized export field is added;
  a duplicated lookup is centralized/split; a timing rule starts affecting a new subsystem.
