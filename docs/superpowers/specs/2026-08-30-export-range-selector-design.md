# Export Range Selector Design

## Goal

Restore the v1 visual export-range selector in the v2 QML export page, while
removing the redundant two “set to current preview position” buttons. The
range is edited directly by dragging its start or end handle and still has a
precise, keyboard-accessible numeric path.

## User Experience

- The Export Range tab shows one full-chart lane, a blue selected segment, and
  two blue handles. The lower start/end fields retain precise,
  keyboard-accessible entry; the lane does not reserve moving start/end labels.
- A gold playhead is read-only. It mirrors the preview's current time and the
  already-visible right-side `PreviewTransport`; a click on empty lane space
  does nothing.
- A range is never empty. Its duration is at least `min(5 seconds, chart
  duration)`: charts shorter than five seconds may export only their full
  range. This constraint applies equally to pointer and numeric input.
- The endpoint grips and selected body are separate hit targets. Dragging a
  grip changes only that endpoint and holds the opposite endpoint fixed.
  Dragging the selected body shifts both endpoints by the same amount while
  preserving its duration. Empty lane space remains inert.
- When a short valid range is visually narrower than the grips, its displayed
  selection is widened symmetrically to a minimum interactive span. This is a
  presentation affordance only: the timestamps and exported range remain the
  exact underlying values.
- Pressing either kind of target begins the existing preview-scrub lifecycle,
  which pauses active playback. Endpoint drags seek to that endpoint; body
  drags seek to the initially pointed relative position as it moves with the
  range. Releasing ends the scrub and leaves preview paused.
- Hovering the lane or dragging shows one floating `MM:SS.mmm` timestamp near
  the pointer. It is an overlay inside a statically reserved tooltip band, so
  it never changes page height or uses the words "Start" or "End".

## Architecture

`ExportRangeSelector.qml` owns only presentation and pointer input. It accepts
the existing export session (range values) and preview session (single
playhead/scrub owner). `ExportVideoPage.qml` hosts it and receives the preview
session from `MainSplitView.qml`. `QmlExportSession` gains one atomic
`setExportRangeSeconds(start, end)` operation because its task stores a start
plus duration: composing the existing one-end setters otherwise moves the
opposite endpoint. Its one-end setters and text entry delegate to the same
minimum-duration policy. No export worker or serialized task contract changes.

The selector must never maintain a second playhead. It subscribes to
`previewSession.positionSeconds`; grip and selected-body drags call
`beginScrub()`, `updateScrub(second)`, and `endScrub(second)`. Its visual
centres, hit targets, and pointer-to-second conversion use one canonical
coordinate model.

## Validation

- Cover `QmlExportSession`'s atomic and one-end range writes: they preserve
  the fixed endpoint and enforce the five-second (or full-short-chart) floor.
- Extend the real QML export-page harness with mouse drags of each endpoint,
  the selected body, and a minimum-width range. Assert the intended range,
  scrub order, preview time, and disjoint hit-target behavior.
- Assert numeric fields still update the same source of truth, and that hover
  timestamps do not alter the selector's implicit height.
- Build Release and run the focused QML spec, then full CTest. Existing
  `qtavplayer_platform_spec` baseline failure is reported separately.

## Related Tab-Order Adjustment

The user clarified that the metadata editor tab participates in the existing
session-local drag exchange. The same `ViewState` swap API therefore accepts
any open editor key; tab presentation remains session-only and never changes
document order or serialization. A real mouse drag regression will cover a
metadata/difficulty exchange.
