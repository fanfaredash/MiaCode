# Cross-Chain Linkage

Use this file before changing behavior that crosses parser, preview, audio, export, or tooling boundaries.

## 1. Edit To Parse To Timeline To Preview

Primary chain:

1. editor `contentsChange` or an explicit full refresh entry such as `scheduleTimelineRefresh`
2. `MainWindow::applyTimelineQuickChange` / `MainWindow::refreshTimelineQuickModelFromCurrentText`
3. `TimelineQuickModel::applyContentsChange` or `TimelineQuickModel::rebuildFromText`
4. `TimelineView::setTimelineData`
5. `MainWindow::requestTimelineSlowRefresh`
6. `buildTimelineSlowRefreshResult`
7. shifted beat/note markers, validation report, and preview stats inputs
8. `QtPreviewSfxRuntime::configureTimeline`
9. `MainWindow::scheduleDeferredMuriRefresh`
10. `buildTimelineMuriRefreshResult`
11. `PreviewCanvas::setNoteMarkers`

Implication:

- A parser change is rarely parser-only.
- A new note property or timing rule usually needs timeline, preview, audio, export, and Muri review.
- `TimelineQuickModel` is now the owner of comma-only `C` anchor lookup for editor cursor sync, header/timeline `R -> C` jumps, and playback follow.
- `PreviewCanvas::drawNoteGuides` should group each-guide connectors by parser-derived `eachGroupId` when available; do not merge backtick-separated groups just because their `marker.second` matches.
- While preview playback is running, slow-refresh note-marker updates still feed validation and timeline-side diagnostics, but preview audio/canvas/object stats stay on the frozen play-start snapshot until playback stops.
- Preview play/resume must use the latest in-memory field state, not a forced disk save. If slow refresh is still behind `timelineRevision_`, playback start may synchronously rebuild a preview-only note-marker snapshot once before audio/video start so the next resume does not wait for validation or Muri workers.
- Slow refresh may publish the preview-only note-marker snapshot before validation finishes. Resume-time preview freshness should not wait for the strict validation half of slow refresh.
- If preview play/resume is requested before the current revision's preview snapshot is ready, the request should wait in memory and auto-start once the preview snapshot for that same revision and difficulty lands. Validation completion must not gate that auto-start.
- Paused preview is intentionally silent for touch-hold sustain audio: `MainWindow::applyLatestTimelinePreviewStateToPausedPreview` must keep touch-hold voices paused, and only playback-start/resume paths should call `QtPreviewSfxRuntime::restoreTouchholdVoices`.

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
- `DEVELOPMENT_PLAN.md` section 11

## 3. Runtime SFX And Export SFX Must Stay In Sync

Canonical sync pair:

- Runtime: `src/preview/audio/QtPreviewSfxRuntime.Timeline.cpp`, `QtPreviewSfxRuntime::configureTimeline`
- Export: `src/tools/video_export/VideoExportController.cpp`, `buildSfxTimeline`

Shared concerns:

- which note kinds emit `answer`, `judge`, `break`, `ex`, `touch`, `touchhold`, `firework`
- touch and touch-hold still emit `answer` when `isFirework` is set; firework is additive rather than replacing the hit-confirm sound
- head-star behavior for slide and wifi
- `sameHeadSlide` behavior
- `headEach` vs `slideEach`: `headEach` comes from synchronous note-head grouping, while `slideEach` must stay aligned between `SimaiNativeParser` and `TimelineQuickModel` by grouping only slide/wifi notes that share both the same each-group and the same `slideTraceSecond`
- `trackBreak` vs `headBreak`
- touchhold span semantics
- firework timing offsets
- partial export timing: when the export request is not marked as full-range, export now uses a 1.5-second preload, but the exported marker set is filtered up front by `marker.second` within `[L, R]`; preview/export rendering, Muri overlays, and export SFX all consume that same filtered marker set

If one side changes, inspect the other side in the same patch.

## 4. Background Media Resolution Exists In Two Places

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

## 5. Track Path Resolution Exists In Multiple Places

Current lookup owners:

- `MainWindow::resolveDefaultTrackPath`
- `MainWindow::resolveLatencyDetectorTrackPath`
- `QtPreviewSfxRuntime::resolveTrackPath`

Current convention:

- chart-directory sibling `track.mp3`
- optional environment override for some paths via `MIACODE_TRACK_PATH` on main-window export path

If you support new track filenames or lookup rules, update all relevant owners and `assets-and-tools.md`.

## 6. Skin And Asset Lookup Flows Into Both Preview And Export

Asset root:

- `miacode::assets::findAssetRoot`
- `miacode::assets::assetPath`

Preview-time consumers:

- `MainWindow::resolvePreviewSkinDir`
- `PreviewCanvas::setSkinDirectory`
- `miacode::preview_sfx::resolveSfxDirectory`

Export-time consumers:

- `MainWindow::buildVideoExportSnapshot`
- `VideoExportSnapshot::buildVideoExportTaskFromSnapshot`
- `VideoExportController::exportPreparedTask`

If skin or SFX lookup changes, review both preview and export.

## 7. Export Snapshot Boundary Is A Contract

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

## 8. Shared Render State Flows Through Preview And Export

Wifi-specific note:

- `RenderMode::MaimuriDxStyle` wifi track erasure is not driven by static `wifiTrackAreaCheckpoints`.
- `MuriAnalyzer` reconstructs runtime lane progress in `MarkerMuriState::wifiLaneProgressSeconds`, mirrors judged lane areas in `MarkerMuriState::wifiLaneAreas`, and records the actual `C` release time in `MarkerMuriState::wifiPadCSecond`.
- `PreviewCanvas::drawWifiTrack` must trim the shared middle-track body by the slowest lane's current area index, using `wifiLaneProgressSeconds` first and `wifiLaneAreas` as a fallback if the progress array is unavailable.
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

- Persistent state: `MainWindow::loadProjectRenderState`, `saveProjectRenderState`
- Preview application: `PreviewCanvas` setters and `PreviewMediaController`
- Export application: `MainWindow::buildVideoExportSnapshot`, `buildVideoExportTaskFromSnapshot`, `VideoExportController`

If you add a new render setting, wire preview persistence and export reconstruction together.

## 9. Parser Output Feeds Muri On Both Preview And Export Paths

Current flow:

- live preview path: `requestTimelineSlowRefresh` -> `buildTimelineSlowRefreshResult` -> `scheduleDeferredMuriRefresh` -> `buildTimelineMuriRefreshResult`
- export path: `buildVideoExportTaskFromSnapshot` -> `MuriAnalyzer::analyze`

Implication:

- A marker-field change affects both live diagnostics and exported overlay behavior.

## 10. Common "Change Here, Check There" Pairs

- Change `SimaiNativeParser`:
  - Check `MainWindow.ValidationFlow.cpp`
  - Check `MainWindow.PreviewTimelineFlow.cpp`
  - Check `VideoExportSnapshot.cpp`
  - Check `MuriAnalyzer.cpp`
- Change preview SFX mapping:
  - Check `VideoExportController.cpp::buildSfxTimeline`
  - Check `DEVELOPMENT_PLAN.md` section 12
- Change export media rules:
  - Check `PreviewMediaController.cpp`
  - Check packaging or ffmpeg assumptions if format support changes
- Change preview timing constants:
  - Check `PreviewGameplayConfig.h`
  - Check `PreviewCanvas.cpp`
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
