# Export Range Selector Design

## Goal

Restore the v1 visual export-range selector in the v2 QML export page, while
removing the redundant two “set to current preview position” buttons. The
range is edited directly by dragging its start or end handle and still has a
precise, keyboard-accessible numeric path.

## User Experience

- The Export Range tab shows one full-chart lane, a blue selected segment, and
  two blue handles. The start and end labels above the handles show precise
  `MM:SS.mmm` values while the lower fields retain keyboard entry.
- A gold playhead is read-only. It mirrors the preview's current time and the
  already-visible right-side `PreviewTransport`; a click on empty lane space
  does nothing.
- Pressing a range handle begins the existing preview-scrub lifecycle, which
  pauses active playback. Moving it clamps the endpoint, updates the selected
  export range, and seeks the preview through that same lifecycle. Releasing
  ends the scrub and leaves preview paused.
- Start never exceeds end; end never precedes start. Text entry and dragging
  update the same `QmlExportSession` range values.

## Architecture

`ExportRangeSelector.qml` owns only presentation and pointer input. It accepts
the existing export session (range values) and preview session (single
playhead/scrub owner). `ExportVideoPage.qml` hosts it and receives the preview
session from `MainSplitView.qml`. No export worker, serialized task, or chart
data contract changes: `QmlExportSession` already owns clamped range values;
`QmlPreviewModel` already exposes `positionSeconds` plus the scrub API.

The selector must never maintain a second playhead or seek from lane-body
clicks. It subscribes to `previewSession.positionSeconds`; range-handle drags
call `beginScrub()`, `updateScrub(second)`, and `endScrub(second)`.

## Validation

- Extend the real QML export-page harness with a fake preview session and a
  mouse drag of each range handle. Assert the intended endpoint changes,
  preview scrub calls occur in order, playback time follows the drag, and the
  opposite endpoint is never crossed.
- Assert editing either numeric field still updates the corresponding handle.
- Build Release and run the focused QML spec, then full CTest. Existing
  `qtavplayer_platform_spec` baseline failure is reported separately.

## Related Tab-Order Adjustment

The user clarified that the metadata editor tab participates in the existing
session-local drag exchange. The same `ViewState` swap API therefore accepts
any open editor key; tab presentation remains session-only and never changes
document order or serialization. A real mouse drag regression will cover a
metadata/difficulty exchange.
