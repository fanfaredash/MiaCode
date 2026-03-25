# Cross-Chain Linkage

Use this file before changing behavior that crosses parser, preview, audio, export, or tooling boundaries.

## 1. Edit To Parse To Timeline To Preview

Primary chain:

1. `MainWindow::applyCurrentFieldToDocument`
2. `MainWindow::refreshTimelineMetadata`
3. `SimaiNativeParser::parseForTimeline`
4. shifted beat/note markers
5. `TimelineView::setTimelineData`
6. `QtPreviewSfxRuntime::configureTimeline`
7. `MuriAnalyzer::analyze`
8. `MainWindow::rebuildStaticMuriReferences`
9. `PreviewCanvas::setNoteMarkers`

Implication:

- A parser change is rarely parser-only.
- A new note property or timing rule usually needs timeline, preview, audio, export, and Muri review.

## 2. `&first` And Timing Offset Chain

Current contract:

- `SimaiDocument` stores raw `first`.
- `MainWindow::parsedFirstSeconds` is the main getter.
- `MainWindow::refreshTimelineMetadata` shifts parser-produced beat/note markers by `first`.
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
- `trackBreak` vs `headBreak`
- touchhold span semantics
- firework timing offsets

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

## 8. Shared Render State Flows Through Preview And Export

Shared render settings include:

- background brightness outer and inner
- layout square scale
- smooth brightness
- background scale mode
- note flow speed
- timestamp/object-stats HUD flags
- Muri render options

Owners:

- Persistent state: `MainWindow::loadProjectRenderState`, `saveProjectRenderState`
- Preview application: `PreviewCanvas` setters and `PreviewMediaController`
- Export application: `MainWindow::buildVideoExportSnapshot`, `buildVideoExportTaskFromSnapshot`, `VideoExportController`

If you add a new render setting, wire preview persistence and export reconstruction together.

## 9. Parser Output Feeds Muri On Both Preview And Export Paths

Current flow:

- live preview path: `refreshTimelineMetadata` -> `MuriAnalyzer::analyze`
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
  - Check `MuriStaticChecker.h`
  - Check any UI or settings entry that surfaces the threshold

## Update This File When

- A behavior starts or stops being mirrored across two code paths.
- A new serialized export field is introduced.
- A duplicated lookup rule is centralized or split further.
- A timing rule starts affecting another subsystem that did not previously depend on it.
