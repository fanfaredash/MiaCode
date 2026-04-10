# Playback Start Latency Plan

## Summary

- Playback start/resume waits for the current revision's preview snapshot, not for validation or Muri analysis.
- The current hot path is the slow-refresh parser chain:
  `startQtPreviewPlayback -> preparePreviewStartState -> requestTimelineSlowRefresh -> SimaiNativeParser::parseForTimeline`.
- The primary parser cost was the derived marker rebuild after `noteMarkers` sort, where `tap/hold/touch/slide` relationships were computed with full scans.
- This phase replaces those scans with grouped ordered sweeps and keeps slow-refresh scheduling unchanged.
- Follow-up adjustment:
  wifi mid-path `pad_enter_times` are now treated as valid `touch.onSlide` windows instead of preserving the previous under-match.

## Playback Wait Chain

- Playback entry:
  `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  `MainWindow::startQtPreviewPlayback`
- Preview freshness gate:
  `src/app/mainwindow/MainWindow.cpp`
  `MainWindow::preparePreviewStartState`
- Slow refresh worker:
  `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  `MainWindow::dispatchTimelineSlowRefresh`
- Parse work:
  `src/simai/parser/SimaiNativeParser.Driver.cpp`
  `SimaiNativeParser::parseForTimeline`
- Preview snapshot publication:
  `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  `latestTimelinePreviewSnapshotReady_ = true`
- Analysis is enqueued after preview publication and does not gate auto-start.

## Slow Refresh Scheduling Model

- `timelineSlowRefreshPool_` is single-threaded.
- The model is not an unbounded queue.
- Runtime state is effectively:
  one running request
  one pending request slot holding the latest revision
- When a newer revision arrives during a running slow refresh:
  the pending slot is overwritten with the newest request
  the running worker is allowed to finish
  stale results are dropped on return to the UI thread
  the latest pending request is then dispatched
- Playback requests made during this period are deferred in memory and auto-start when the matching revision's preview snapshot lands.

## Phase 1 Parser Optimization

- Target region:
  the derived marker rebuild at the end of `parseInternal`
- Replaced full scans:
  `tap -> slideHead`
  `hold tail -> slideHead`
  `touch -> onSlide`
  `slide -> beforeSlide / afterSlide`
- New approach:
  group by `lane` or `pad`
  sort centers by `double second`
  run monotonic static sweeps
  keep existing boolean semantics except for the explicit wifi pad-enter fix
- Indexes used:
  `traceByLane`
  `endByLane`
  `touchWindowsByPad`
- Touch windows are expanded from marker-owned data only:
  slide head windows
  wifi head windows
  slide `slideSegmentPadEnterTimes`
  wifi `wifiPadEnterTimes`
- Hot-path `touch -> onSlide` no longer depends on JSON lookup during matching.
- `simai_native_dump` now also exposes `slide_segment_pad_enter_times` and `wifi_pad_enter_times`, so parser JSON can show the exact expanded source data used for matching.

## Regression Plan

- Tooling:
  `src/simai/parser/SimaiNativeDump.cpp`
  now supports `--document` and dumps per-difficulty parse JSON using the same `chartText + timingMetadata(document)` contract as slow refresh.
- Dataset:
  `D:\Files\偏移测试\`
  scan all first-level subdirectories
  include `maidata*.txt`
  skip `*.bak.txt`
- Compare baseline and optimized dumps under:
  `build/parser_regression/baseline/`
  `build/parser_regression/optimized/`
- Any diff in:
  `ok`
  `duration_seconds`
  `errors`
  `warnings`
  `beat_markers`
  `note_markers`
  is treated as a regression.
- Exception after the wifi follow-up:
  `touch.onSlide` may intentionally flip from `false` to `true` for touches that land on wifi mid-path `pad_enter_times`.

## Follow-Up

- If parser time drops enough after this phase, stop here.
- If playback still waits too long after the parser hotspot is reduced, investigate:
  stale worker early-exit
  preview-only fast path
  main-thread paused-preview apply cost
