# Timeline Layer Stack & Slide Stacking Order Spec

This document captures two related runtime invariants that landed in beta21
and went through several rounds of debugging before stabilising:

1. The **timeline render layer order** — which visual layer paints over which,
   in both the DComp (default) and QSG fallback rendering paths.
2. The **slide stacking order** Video Settings option — what it controls, and
   crucially what it does **not** control.

Both areas have repeatedly produced visual bugs (lines hidden behind
waveform; head stars shuffling on option toggle). Use this doc as the
canonical reference when touching:

- Anything in `src/sources/timeline/Timeline*Source.{h,cpp}` (DComp pipeline)
- `src/timeline/quick/TimelineQuick*Layer.{h,cpp}` (QSG pipeline)
- `src/core/scene/PreviewPreparedSceneCache.{h,cpp}` (prepared draw rank)
- `src/core/scene/Preview*LayerState.cpp` (per-layer sort decisions)

## 1. Two parallel timeline render paths

The timeline can render through one of two backends, gated by the
`MIACODE_TIMELINE_USE_DCOMP` env flag (default: **true** when the DComp
preview is enabled, which is the default since beta20).

| Env state | Active path | Implementation root |
|---|---|---|
| `MIACODE_TIMELINE_USE_DCOMP=1` (default) | **DComp** — D3D11 child HWND, IPreviewSource pipeline | `TimelineRenderView` in `src/render/backend_d3d11/` |
| `MIACODE_TIMELINE_USE_DCOMP=0` | **QSG** — Qt Quick Scene Graph layers | `TimelineQuickItem` in `src/timeline/quick/` |

Both paths consume the same `TimelineSceneState` (built by
`TimelineSceneStateBuilder`) and the same palette (`UiTheme.cpp`); they
just differ in *how* they composite the layers onto the screen. **All layer-
order and color-routing rules below apply to both paths in parallel.** Any
fix that lands in only one path is incomplete.

## 2. Timeline z-stack — DComp source pipeline

The `TimelineRenderView` registers a series of `IPreviewSource`s in
`src/render/backend_d3d11/TimelineRenderView.cpp::ensureCompositorInitialized`.
Each source has a fixed `zOrder()` that determines render order:

| zOrder | Source | Header | What it emits |
|---|---|---|---|
| **0** | `TimelineGridSource` | `src/sources/timeline/TimelineGridSource.h` | `baseBackgroundRects` (timeline / sidebar / header fill), `frameRects`, `frameLines` |
| **1** | `TimelineWaveformSource` | `TimelineWaveformSource.h` | `waveformBars` — opaque audio amplitude rectangles |
| **2** | `TimelineLaneOverlaySource` | `TimelineLaneOverlaySource.h` | `laneOverlayRects` — translucent (α=190 light / α=210 dark) row-stripe fills that mute the waveform |
| **3** | `TimelineGridLinesSource` | `TimelineGridLinesSource.h` | `gridLines` — bar lines (per-measure) and per-comma note ticks |
| **4** | `TimelineNotesSource` | `TimelineNotesSource.h` | `fireworkBands`, track sprites, hold spans, touch-hold lines, muri dots, note sprites |
| **5** | `TimelineHeaderSource` | `TimelineHeaderSource.h` | sidebar mask, frame line re-emit, header markers (measure-number triangles + line labels) |
| **6** | `TimelineOverlaySource` | `TimelineOverlaySource.h` | playhead, cursor, drag-center crosshair, lane labels |

Two structural rules to remember:

1. **Lane overlay must sit between waveform and grid lines.** Its
   translucent fill is supposed to mute the waveform, not the bar lines. If
   you need to add another translucent decoration that should not affect
   the bar lines, place it at z ≤ 2.
2. **Grid lines must sit above the waveform AND above the lane overlay.**
   `c.timelineGridMajor` opaque + `c.timelineGridMinor` translucent are
   both designed to render against the lane fill, not against the
   waveform.

### History — why these zOrders are what they are

- Pre-beta21: `TimelineGridSource` emitted `gridLines` itself at z=0.
  Result: bar/note lines drew before everything else, so the waveform,
  lane overlay, and notes all painted over them. `c.timelineGridMajor`
  was visible only as a heavily-blended residual through the lane
  overlay's α=190/210. The user-visible symptom was "lines too faint /
  lines below waveform / lines invisible against busy waveform sections."
- Beta21-fix7: split `gridLines` into `TimelineGridLinesSource` at z=2,
  cascade-bumped Notes/Header/Overlay z up by 1.
- Beta21-fix12: split `laneOverlayRects` out of `TimelineNotesSource`
  into `TimelineLaneOverlaySource` at z=2; pushed `TimelineGridLinesSource`
  to z=3. This was the final structural fix — the lane overlay was
  *also* painting over the bar lines because it lived inside `NotesSource`
  at z=3, above the grid lines that were then at z=2.

## 3. Timeline z-stack — QSG layer pipeline (fallback)

`TimelineQuickItem::updatePaintNode` iterates a fixed slot list. Slot
ordering matches the DComp z-stack:

| Slot | Layer | Header |
|---|---|---|
| 0 | `gridLayer` (basebackgrounds) | `TimelineQuickGridLayer.h` |
| 1 | `waveformLayer` | `TimelineQuickWaveformLayer.h` |
| 2 | `headerLayer` (lane overlays + measure-marker triangles + labels — **no grid lines**) | `TimelineQuickHeaderLayer.h` |
| 3 | `gridLinesLayer` (bar lines + note lines, dedicated slot) | `TimelineQuickGridLinesLayer.h` |
| 4 | `notesLayer` | `TimelineQuickNotesLayer.h` |
| 5 | `overlayLayer` | `TimelineQuickOverlayLayer.h` |

Slot count is `kTimelineLayerSlotCount = 6` in
`src/timeline/quick/TimelineQuickItem.cpp`.

### QSG-specific gotcha — `Blending` flag on alpha-255 lines

Qt 6's RHI batched renderer is allowed to reorder *opaque* (`Blending=false`)
draws across slots for early-Z optimisation. Bar lines drawn through
`QSGFlatColorMaterial` with α=255 colours land in the opaque batch by
default — and on D3D11 that batch can fire BEFORE the waveform's opaque
batch even though the bar lines' scene-graph slot is higher.

`TimelineQuickFlatColorBatchBuilder::flush()`
(`src/timeline/quick/TimelineQuickLayerUtils.cpp`) **forces
`material->setFlag(QSGMaterial::Blending, true)`** on every emitted
material. Forcing the translucent path keeps draw order strictly tied to
scene-graph order. Do not remove this — opaque-batch reordering will
re-introduce the "lines below waveform" symptom in the QSG path.

## 4. Color routing — which palette entry feeds which line

Both rendering paths read the same palette. As of beta21:

| On-screen element | Palette source | Defined in |
|---|---|---|
| Sidebar boundary line (left edge of timeline content) | `c.timelineAxis` | `UiTheme.cpp` |
| **Bar lines (per-measure)** | **`c.timelineGridMajor`** | `UiTheme.cpp` |
| **Note lines (per-comma)** | **`c.timelineGridMinor`** | `UiTheme.cpp` |
| Lane row stripes | `c.timelineLaneEven` / `c.timelineLaneOdd` | `UiTheme.cpp` |
| Lane number / row labels | `c.timelineLabel` | `UiTheme.cpp` |
| Frame border lines | `c.timelineBorder` | `UiTheme.cpp` |

Wiring lives in two files; both must agree:

- DComp pipeline: `src/common/TimelineThemeConfig.h::timelineThemeColors()`
  populates `theme.gridMajor` from `c.timelineGridMajor` and
  `theme.gridMinor` from `c.timelineGridMinor`. The
  `TimelineSceneStateBuilder` then writes those into `state.gridLines`.
- QSG fallback: `src/timeline/TimelineView.Paint.cpp::paintEvent()`
  constructs `majorBeatPen` from `c.timelineGridMajor` and
  `minorBeatPen` from `c.timelineGridMinor`.

To re-tune bar/note line colours, edit *only* the palette values for
`timelineGridMajor` / `timelineGridMinor` in `UiTheme.cpp`. Both paths
pick up the new values automatically.

## 5. The "Slide Stacking Order" Video Settings option

UI: Video Settings dialog → Gameplay group → "Slide Stack Order" with
two choices, "DX Style" and "FiNALE Style." Persisted in JSON as
`slide_earlier_second_and_text_on_top` (bool) and propagated through
`PreviewFrameState::render::slideEarlierSecondAndTextOnTop`.

### Scope (what the option DOES control)

The option determines internal stacking order **within the slide track
layer family only.** Specifically:

- The slide *track* (sliding-arrow trail) — `PreviewTrackLayerState.cpp`
- The slide *motion* sprites (animating arrowheads) —
  `PreviewSlideMotionLayerState.cpp`
- The cached prepared draw rank for the `slideLikeLayer_` window in
  `PreviewPreparedSceneCache::rebuild`

When `earlierOnTop = true` (DX Style), earlier-in-time slide tracks
draw on top of later ones; FiNALE inverts. Within a single layer's set
of overlapping slides, this is purely a stacking-within-the-layer
decision.

### Out of scope (what the option MUST NOT control)

- **Slide head stars / head layer.** Heads are deterministic and
  option-independent. Two enforcement points:
  1. `PreviewHeadLayerState.cpp` lines 310–322 force
     `kHeadStarAlwaysEarlierOnTop = true` on the fallback (uncached)
     head-layer sort.
  2. `PreviewPreparedSceneCache.cpp::rebuild` forces
     `kHeadLayerAlwaysEarlierOnTop = true` when computing prepared
     draw ranks for `headLayer_`. This is the cached-mode mirror of
     point 1; both must hold or toggling the option will shuffle
     which head star renders on top.
- Any non-track layer: touch, judge effect, judge firework, hold body,
  chart-review markers, muri overlay, header markers. None of these
  read `slideEarlierSecondAndTextOnTop`.

Audit confirms (as of beta21-fix13) the only call sites reading
`state.render.slideEarlierSecondAndTextOnTop` are:

- `PreviewTrackLayerState.cpp:98`
- `PreviewSlideMotionLayerState.cpp:94`
- `PreviewPreparedSceneCache.cpp:382` (slideLikeLayer rebuild)
- `PreviewPreparedSceneCache.cpp:129` (cache key — for invalidation,
  not for ordering)

If a future change adds a fifth call site, audit it carefully — the
default assumption is that any new layer should NOT consume this flag.

### Cache invalidation behaviour

The flag is part of `PreviewPreparedSceneCacheKey` so toggling the
Video Settings option re-keys the prepared scene cache and forces a
full rebuild. That is intentional — slide track sprites need new
ranks. But the cache rebuild applies the head-layer's own *fixed* DX
rule, so head sprites get re-ranked identically before/after the
toggle. From the user's perspective slide stack order changes; head
star order stays put.

## 6. Diagnostic checklist when bar/note lines look wrong

If bar lines or per-comma note lines render in unexpected colours,
positions, or stacking, check in this order:

1. **Confirm which path is active.** `MIACODE_TIMELINE_USE_DCOMP` env or
   default-on (DComp). The DComp path uses the IPreviewSource z-stack;
   the QSG path uses the slot list. A QSG-only fix will not show on
   the DComp path and vice versa.
2. **Confirm grid lines are actually emitted.** Search for
   `gridLines` consumer in the active path. As of beta21 the only
   consumer in DComp is `TimelineGridLinesSource` (z=3); in QSG it's
   `TimelineQuickGridLinesLayer` (slot 3). Anything else means
   somebody added a duplicate emit.
3. **Confirm Blending is on (QSG only).** `git grep "Blending, true"
   src/timeline/quick/TimelineQuickLayerUtils.cpp` should still match
   in `TimelineQuickFlatColorBatchBuilder::flush()`. Removing it
   regresses the QSG-path order.
4. **Confirm palette wiring.** `c.timelineGridMajor` for bar lines,
   `c.timelineGridMinor` for note lines. `git grep "c\.timelineGrid"`
   should hit `TimelineThemeConfig.h` and `TimelineView.Paint.cpp`
   only.

## 7. Diagnostic checklist when slide head stars shuffle on option toggle

1. Verify `PreviewHeadLayerState.cpp:322` still has
   `constexpr bool kHeadStarAlwaysEarlierOnTop = true;`.
2. Verify `PreviewPreparedSceneCache.cpp:rebuild` still passes
   `kHeadLayerAlwaysEarlierOnTop = true` to
   `rebuildPreviewPreparedMarkerDrawOrder` for `headLayer_` (NOT
   `state.render.slideEarlierSecondAndTextOnTop`).
3. Re-audit consumers of `slideEarlierSecondAndTextOnTop`:
   ```
   git grep -n "slideEarlierSecondAndTextOnTop" src/core/ src/preview/
   ```
   Expected hits: PreviewFrameState.h (declaration), PreviewRuntime.cpp
   (sets the flag from external setter), PreviewPreparedSceneCache key
   + slideLikeLayer rebuild, PreviewTrackLayerState, and
   PreviewSlideMotionLayerState. Anything else is a regression.
