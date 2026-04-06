# Cross-Chain Linkage

Use this file before changing behavior that crosses parser, preview, audio, export, or tooling boundaries.

## 1. Edit To Parse To Timeline To Preview

Primary chain:

1. editor `contentsChange` or an explicit full refresh entry such as `scheduleTimelineRefresh`
2. `MainWindow::applyTimelineQuickChange` / `MainWindow::refreshTimelineQuickModelFromCurrentText`
3. `TimelineQuickModel::applyContentsChange` or `TimelineQuickModel::rebuildFromText`
4. `TimelineView::setTimelineData`
5. `MainWindow::requestTimelineSlowRefresh`
6. `SimaiNativeParser::parseForTimeline`
7. `buildTimelinePreviewRefreshState`
8. latest preview snapshot publication plus `MainWindow::applyLatestTimelinePreviewStateToPausedPreview` when playback is paused
9. `MainWindow::scheduleTimelineAnalysisRefresh`
10. `buildTimelineAnalysisRefreshResult`
11. `MainWindow::applyDeferredAnalysisUiUpdates`
12. `PreviewRuntime::setNoteMarkers`

Implication:

- A parser change is rarely parser-only.
- A new note property or timing rule usually needs timeline, preview, audio, export, and Muri review.
- Slide/tap head-material flags such as `$`, `$$`, `@`, `?`, and `!` are mirrored data: keep `SimaiNativeParser`, `TimelineQuickModel`, `PreviewSkinSelectors`, timeline icons, and chart-transform token preservation aligned in the same patch.
- Timeline note-head art selection should mirror preview base/overlay precedence: break or each chooses the base icon first, and EX overlays on top of that base instead of replacing break/each state in the timeline.
- `TimelineQuickModel` is now the owner of comma-only `C` anchor lookup for editor cursor sync, header/timeline `R -> C` jumps, and playback follow.
- Timeline beat-grid semantics are mirrored between `SimaiNativeParser` and `TimelineQuickModel`: every comma remains a beat line, while measure lines are generated on an independent meter timeline. The current meter now comes from shared `SimaiTimingMetadata` (`&whole_time_signature=`), inline `|| x/y` comments restart that meter timeline at the exact comment position, `{beats}` only changes comma spacing, and `(BPM)` changes restart the independent measure-line timeline at the BPM-change position.
- Guide-layer state should group each-guide connectors by parser-derived `eachGroupId` when available; do not merge backtick-separated groups just because their `marker.second` matches.
- Timeline note sprite stacking is intentionally preview-mirrored for overlapping markers: `TimelineView::paintEvent` keeps slide/wifi tracks behind note heads, uses the preview-style descending-`second` stack for tap/hold/slide/wifi heads, and then draws touch above that stack with touch-hold above touch. If preview object-layer order changes, review `src/timeline/TimelineView.Paint.cpp`, `src/preview/scene/PreviewLayerOrder.h`, and `src/preview/quick_scene/*` together.
- On-screen preview and export now flow through `PreviewRuntime` / `PreviewQuickExportSession` plus the active layers in `src/preview/quick_scene/*`. Shared assets now come from `PreviewSceneAssetLoader` and `PreviewSceneAssetRepository`. If you change preview setters, frame pacing hooks, layer data contracts, or export-session ownership, review both `src/preview/runtime/*` and `src/preview/quick_scene/*` in the same patch.
- The realtime and export Quick scene roots now also share `PreviewPreparedSceneCache`-driven note windows. If you change note-driven layer inputs, visible-window timing, or scene-content revision invalidation, review `src/preview/scene/PreviewPreparedSceneCache.*`, `src/preview/quick_scene/PreviewQuickSceneRoot.*`, and the affected `PreviewQuick*Layer` wrappers together.
- Runtime and export layer order are both owned by `PreviewQuickSceneRoot` plus `PreviewLayerOrder.h`. If you change layer ordering or add a new visible layer, review `src/preview/scene/PreviewLayerOrder.h`, `src/preview/quick_scene/*`, and `src/tools/video_export/VideoExportQuickRenderBackend.*` together.
- Firework overlay visuals now depend on the custom `PreviewQuickJudgeFireworkLayer` material path rather than pie-sector geometry plus sprite overlays. If you change firework timing curves, additive blending, source texture use, hole-mask math, or stage clipping, review `src/preview/scene/PreviewJudgeFireworkLayerState.*`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.*`, `src/preview/quick_scene/shaders/PreviewFireworkMaterial.*`, and the historical `PreviewCanvas` reference behavior together. The legacy contract is a playfield-centered judgment-ring clip, not a second local clip around the trigger point.
- While preview playback is running, slow-refresh note-marker updates still feed the latest validation and Muri worker inputs, but preview audio/canvas/object stats stay on the frozen play-start snapshot until playback stops; validation and Muri panel/decorations may defer their visible UI apply until playback returns to a paused state.
- Preview object stats now share one `PreviewProgressStatsCache` across realtime HUD, the main-window side stats card, and export HUD rendering. Hold-family played counts and score-style progress must use judge/end timing in every consumer; if you touch the cache, keep that timing aligned with the HUD's finale/deluxe progression.
- Analysis-only setting changes such as Muri render mode or the static tap-on-slide threshold should prefer reusing the latest preview snapshot and cached parse result instead of forcing another full slow refresh.
- Preview play/resume must use the latest in-memory field state, not a forced disk save. If slow refresh is still behind `timelineRevision_`, playback start may synchronously rebuild a preview-only note-marker snapshot once before audio/video start so the next resume does not wait for validation or Muri workers.
- Slow refresh may publish the preview-only note-marker snapshot before validation finishes. Resume-time preview freshness should not wait for the strict validation half of slow refresh.
- If preview play/resume is requested before the current revision's preview snapshot is ready, the request should wait in memory and auto-start once the preview snapshot for that same revision and difficulty lands. Validation completion must not gate that auto-start.
- Paused preview is intentionally silent for touch-hold sustain audio: `MainWindow::applyLatestTimelinePreviewStateToPausedPreview` must keep touch-hold voices paused, and only playback-start/resume paths should call `QtPreviewSfxRuntime::restoreTouchholdVoices`.
- Paused preview and timeline overlays must only apply a `MuriAnalysisReport` whose `noteMarkerSignature` matches the currently published preview snapshot. If slow refresh publishes new note markers before analysis finishes, clear/rehold the review overlay instead of mixing a stale report with fresh markers.

## 2. `&first` And Timing Offset Chain

Current contract:

- `SimaiDocument` stores raw `first`.
- `MainWindow::parsedFirstSeconds` is the main getter.
- `TimelineQuickModel` receives `first` on every fast-path rebuild or incremental edit apply.
- `buildTimelineSlowRefreshResult` shifts parser-produced beat/note markers by `first`.
- `MainWindow::applyLatencyDetectorOffset` writes raw `first` back into the document.
- `LatencyDetectorDialog` reads and writes the raw value rather than maintaining an inverted shadow value.
- Export task reconstruction uses parsed document text plus shifted markers again.

If you change `&first` semantics, review all of:

- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `src/tools/latency/LatencyDetectorDialog.Analysis.cpp`
- `src/tools/video_export/VideoExportSnapshot.cpp`
- `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`

## 3. Timing Metadata And Default Meter Chain

Current contract:

- `SimaiTimingMetadata` is the shared payload for parser, quick timeline, slow refresh, validation cache, normalization, export snapshot rebuild, and CLI/dev helpers.
- `MainWindow::currentTimingMetadata` reads live metadata text from the metadata editor when available, so unsaved `&whole_time_signature=` edits still affect validation and timeline refresh.
- `parsedLatencyMeterId` now reads the effective chart default meter from timing metadata for latency-detector defaults; latency detection still writes `&first` and `&wholebpm`, but it no longer writes meter metadata back into the chart.
- Any caller that uses `SimaiNativeParser::parseForTimeline` or `buildValidationReport` should pass timing metadata when document metadata is available, or fast/slow preview, export, and tooling timelines will drift.

If you change timing-metadata semantics, review all of:

- `src/simai/document/SimaiTimingMetadata.cpp`
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`
- `src/timeline/TimelineQuickModel.cpp`
- `src/timeline/TimelineSlowRefresh.cpp`
- `src/tools/video_export/VideoExportSnapshot.cpp`
- `src/tools/muri/MuriDump.cpp`

## 4. Runtime SFX And Export SFX Must Stay In Sync

Canonical sync pair:

- Runtime: `src/preview/audio/QtPreviewSfxRuntime.Timeline.cpp`, `QtPreviewSfxRuntime::configureTimeline`
- Export: `src/tools/video_export/VideoExportController.cpp`, `buildSfxTimeline`

Shared concerns:

- which note kinds emit `answer`, `judge`, `break`, `ex`, `touch`, `touchhold`, `firework`
- touch and touch-hold still emit `answer` when `isFirework` is set; firework is additive rather than replacing the hit-confirm sound
- head-star behavior for slide and wifi
- `hasHeadStar` gates only the pre-head object / head SFX / head judge path; `headlessImmediate` only changes the waiting-star visual ramp
- `sameHeadSlide` behavior
- `headEach` vs `slideEach`: `headEach` comes from synchronous note-head grouping, while `slideEach` must stay aligned between `SimaiNativeParser` and `TimelineQuickModel` by grouping only slide/wifi notes that share both the same each-group and the same `slideTraceSecond`
- `trackBreak` vs `headBreak`
- touchhold span semantics
- firework timing offsets
- partial export timing: when the export request is not marked as full-range, export now uses a 1.5-second preload, but the exported marker set is filtered up front by `marker.second` within `[L, R]`; preview/export rendering, Muri overlays, and export SFX all consume that same filtered marker set

If one side changes, inspect the other side in the same patch.

## 5. Background Media Resolution Exists In Two Places

Current duplicated logic:

- Preview-time: `PreviewMediaController::resolveMediaPath`
- Export-time: `resolveBackgroundMediaPath` in `VideoExportController.cpp`

Current filename convention:

- `bg.mp4`
- `pv.mp4`
- `bg.jpg`
- `bg.png`
- `bg.jpeg`

If you add or remove supported media names, keep preview and export aligned.

## 6. Track Path Resolution Exists In Multiple Places

Current lookup owners:

- `MainWindow::resolveDefaultTrackPath`
- `MainWindow::resolveLatencyDetectorTrackPath`
- `QtPreviewSfxRuntime::resolveTrackPath`

Current convention:

- chart-directory sibling `track.mp3`
- optional environment override for some paths via `MIACODE_TRACK_PATH` on main-window export path

If you support new track filenames or lookup rules, update all relevant owners and `assets-and-tools.md`.

## 7. Skin And Asset Lookup Flows Into Both Preview And Export

Asset root:

- `miacode::assets::findAssetRoot`
- `miacode::assets::assetPath`

Preview-time consumers:

- `MainWindow::resolvePreviewSkinDir`
- `PreviewRuntime::setSkinDirectory`
- `miacode::preview_sfx::resolveSfxDirectory`

Export-time consumers:

- `MainWindow::buildVideoExportSnapshot`
- `VideoExportSnapshot::buildVideoExportTaskFromSnapshot`
- `VideoExportController::exportPreparedTask`

If skin or SFX lookup changes, review both preview and export.

## 8. Export Snapshot Boundary Is A Contract

Export worker boundary:

1. `MainWindow::buildVideoExportSnapshot`
2. `VideoExportSnapshot::toJson`
3. `runCliVideoExportWorker`
4. `VideoExportSnapshot::fromJson`
5. `buildVideoExportTaskFromSnapshot`
6. `VideoExportController::exportPreparedTask`

Implication:

- New export settings must be added on both serialization and deserialization sides.
- Worker protocol changes must be reflected in both `main.cpp` and MainWindow worker-event handling.
- `snapshot.outputPath` should already be the final `.mp4` path by the time the worker starts; `MainWindow` resolves missing suffixes and duplicate-name fallbacks before launching the worker so completion UI and worker results can treat it as authoritative.
- Static Muri thresholds that affect analyzer timing, such as the tap-on-slide threshold, must also cross this boundary; otherwise preview-time diagnostics and export-time overlays will drift.

## 9. Shared Render State Flows Through Preview And Export

Wifi-specific note:

- `RenderMode::MaimuriDxStyle` wifi track erasure is not driven by static `wifiTrackAreaCheckpoints`.
- `MuriAnalyzer` reconstructs runtime lane progress in `MarkerMuriState::wifiLaneProgressSeconds`, mirrors judged lane areas in `MarkerMuriState::wifiLaneAreas`, and records the actual `C` release time in `MarkerMuriState::wifiPadCSecond`.
- `PreviewTrackLayerState` must trim the shared middle-track body by the slowest lane's current area index, using `wifiLaneProgressSeconds` first and `wifiLaneAreas` as a fallback if the progress array is unavailable.
- In `RenderMode::MaimuriDxStyle`, wifi track completion should stay erased after the runtime clear; do not repaint a full-track flash on top of the erased body. When `wifiNeedC` is enabled, the last area must still remain visible until `wifiPadCSecond`.

Shared render settings include:

- background brightness outer and inner
- layout square scale
- smooth brightness
- background scale mode
- note flow speed
- chart-review judge overlay toggles for slide/wifi-family and tap/hold-family effects
- timestamp/object-stats HUD flags
- Muri render options

Muri warning/render note:

- `RenderMode::Native` chart-review overlays should stay parser-timed; do not retime them from Muri warning metadata.
- `RenderMode::MaimuriDxStyle` simple-note Muri overlays may still use analyzer timing metadata to switch the rendered simple effect between early-`GOOD` and early-`PERFECT`.

Owners:

- Persistent state: `MainWindow::loadProjectRenderState`, `saveProjectRenderState`, `MainWindow::loadPortableState`, `MainWindow::savePortableState`, `miacode::video_export::loadDialogPreferences`, `miacode::video_export::saveDialogPreferences`
- Preview application: `PreviewRuntime` setters, `PreviewSceneAssetRepository`, and `PreviewMediaController`
- Runtime host application: `PreviewRuntime`, cached-frame refresh, and `PreviewQuickRuntimeSurface`
- Quick layer application: `PreviewQuickSceneRoot`, `PreviewQuickStageBackgroundLayer`, `PreviewQuickBackdropLayer`, `PreviewQuickHudLayer`
- Export application: `MainWindow::buildVideoExportSnapshot`, `buildVideoExportTaskFromSnapshot`, `VideoExportController`

If you add a new render setting, wire preview persistence and export reconstruction together. Shared preview/export settings should stay canonical in the preview state; export-only choices such as resolution/FPS should persist through `VideoExportPreferences` without overriding those shared preview values on dialog open.

## 10. Parser Output Feeds Muri On Both Preview And Export Paths

Current flow:

- live preview path: `requestTimelineSlowRefresh` -> `SimaiNativeParser::parseForTimeline` -> `buildTimelinePreviewRefreshState` -> `scheduleTimelineAnalysisRefresh` -> `buildTimelineAnalysisRefreshResult`
- export path: `buildVideoExportTaskFromSnapshot` -> `MuriAnalyzer::analyze`
- full-chart normalization path: `MainWindow::onNormalizeWholeChart` -> `normalizeChartText` -> `SimaiNativeParser::buildValidationReport`

Implication:

- A marker-field change affects both live diagnostics and exported overlay behavior.
- Parser timing changes also affect whole-chart normalization output because the normalizer validates first and then rebuilds measures using the same metadata-driven timing defaults.

## 11. Common "Change Here, Check There" Pairs

- Change `SimaiNativeParser`:
  - Check `MainWindow.ValidationFlow.cpp`
  - Check `MainWindow.PreviewTimelineFlow.cpp`
  - Check `TimelineQuickModel.cpp`
  - Check `ChartNormalization.cpp`
  - Check `VideoExportSnapshot.cpp`
  - Check `MuriAnalyzer.cpp`
- Change preview SFX mapping:
  - Check `VideoExportController.cpp::buildSfxTimeline`
  - Check `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- Change export media rules:
  - Check `PreviewMediaController.cpp`
  - Check packaging or ffmpeg assumptions if format support changes
- Change preview timing constants:
  - Check `PreviewGameplayConfig.h`
  - Check `src/preview/scene/PreviewOpacityCurves.cpp`
  - Check `VideoExportController.cpp` diagnostics and timeline assumptions
- Change Muri static thresholds:
  - Check `MuriConfig.h`
  - Check any UI or settings entry that surfaces the threshold
  - Check `VideoExportSnapshot.cpp` and `VideoExportController.cpp` so export keeps the same analyzer threshold as preview
- Change Muri list anchoring or overlap dedupe:
  - Check `MuriPanelEntries.cpp`
  - Check `MainWindow.ValidationFlow.cpp`
  - Check `MuriSpec.cpp`

## Update This File When

- A behavior starts or stops being mirrored across two code paths.
- A new serialized export field is introduced.
- A duplicated lookup rule is centralized or split further.
- A timing rule starts affecting another subsystem that did not previously depend on it.
