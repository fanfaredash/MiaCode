# Hardcode Registry

Use this file to track where important constants live, what they mean, and whether they should stay local or move into shared config headers.

## 1. Shared Config Headers

- `src/common/AssetPaths.h`
  - Owns: asset root discovery and `assetPath(...)`
  - Scope: shared resource lookup
  - Promote new shared path rules here before duplicating them elsewhere
- `src/common/PreviewGameplayConfig.h`
  - Owns: logical canvas size, lane-distance geometry, preview flow-speed normalization, tap lifecycle timing, slide pretrace timing, judge-effect durations
  - Scope: preview and export timing assumptions
- `src/common/PreviewSkinConfig.h`
  - Owns: shared tap-head scale plus hold-width, hold-cap-slice, and slide-track sizing ratios used to keep preview and timeline skin geometry aligned
  - Scope: preview and timeline skin-asset visual parity
- `src/common/PreviewVideoGeometryConfig.h`
  - Owns: background brightness defaults, layout square scaling (current default `0.95`), dimming geometry, smooth brightness blending
  - Scope: preview and export visual geometry
- `src/common/PreviewSfxAssets.h`
  - Owns: SFX kind-to-filename mapping and SFX directory resolution
  - Scope: sound asset conventions
- `src/common/VideoExportConfig.h`
  - Owns: export lead-in constants for zero-start exports and non-zero partial-export preload
  - Scope: export timeline alignment
- `src/common/MuriConfig.h`
  - Owns: static tap-on-slide threshold min/max/default plus shared Muri timing cutoffs such as tap-on-slide warning, the slide-head no-startup-tap warning cutoff (`50 ms`), the slide-head late-warning cutoff (`150 ms`), and the slide runtime available window (current default `24 h`)
  - Scope: static and runtime Muri collision interpretation across preview, timeline refresh, dump tooling, and export

## 2. Implementation-Local Hotspots

- `src/preview/scene/*.cpp`
  - Owns: large volume of render tuning constants
  - Examples:
    - lane angle base and step
    - sprite scaling ratios
    - touch/touchhold close curve parameters
    - judge-effect curve timing and geometry
    - firework visual tuning
    - descriptor sizing and animation curve parameters
  - Current tuning note: `src/preview/scene/PreviewJudgeFireworkLayerState.cpp` keeps the firework color-ball hole ratios local, but they are intentionally pinned to the legacy pre-Qt Quick material bounds (`_InnerLB/_InnerUB/_OuterLB/_OuterUB`) so the center cutout and the 15 colored sector spokes both stay visually aligned with the old `PreviewCanvas` hole-mask fade
  - Rule: keep local only when the values are render-internal and not consumed elsewhere
- `src/preview/scene/PreviewAnimatedSpriteHelpers.cpp`
  - Owns: continuous animated-sprite wave timing (`kMaterialAnimationTimeScale`, `kMaterialAnimationPhaseScale`) plus helper-side overlay cache quantization/cap for EX-style CPU composites
  - Current tuning note: overlay cache quantization stays local at `4096.0`, and the helper-side overlay cache is currently capped at `128` entries because it is a Quick-preview reuse detail rather than shared product behavior
  - Rule: keep local while the preview/export pipelines only need shared animation timing and overlay reuse policy, but document changes if another subsystem must reason about the same wave scaling or cache budget
- `src/preview/quick_scene/PreviewQuickSpriteNodes.cpp`
  - Owns: shader-side `BreakAnimate` / `HoldShine` brightness and contrast coefficients, custom sprite-material uniform layout, and the layer-local contiguous batch policy keyed by texture only while per-sprite effect selection stays in vertex data
  - Current tuning note: this file is now the owner for runtime sprite effect math and for the “merge only adjacent compatible sprites, never reorder” rule; changes here affect both runtime preview and export preview because both share the Quick scene graph path
  - Rule: keep local while the values are renderer-internal, but document any changes that alter visual parity or layer-order guarantees
- `src/tools/latency/LatencyDetectorDialog.cpp`
  - Owns: detection windows, hop sizes, BPM scan range, offset penalties, snap thresholds
  - Rule: keep local when the values are intrinsic to the latency tool, but document any user-visible range changes
- `src/simai/parser/SimaiNativeParser.cpp`
  - Owns: parser-default geometry and timing assumptions used to derive marker behavior
  - Rule: parser-level constants can have repo-wide consequences; treat changes as cross-chain changes
- `src/tools/video_export/VideoExportController.cpp`
  - Owns: mix sample rate, encoder probe timeouts, bitrate heuristics, frame diagnostics thresholds, ffmpeg fallback behavior
  - Current tuning note: export preset mapping stays local here. `Fast` keeps the historical baseline, while `High Quality` and `High Compression` retune x264 CRF/preset/B-frames plus per-encoder bitrate or quality flags for NVENC/QSV/AMF/MF/libopenh264/mpeg4 without changing the codec-facing UI surface.
  - Rule: export heuristics may stay local, but document behavior changes that affect output compatibility or packaging assumptions
- `src/tools/video_export/RawVideoPipeTransport.cpp`
  - Owns: raw-video pipe queue depth, pipe buffer sizing, connect timeout, writer chunk size, and bounded producer blocking behavior
  - Current tuning note: `RawVideoPipePlan::maxBufferedFrames` is derived from frame size using `32 * (1920*1080) / (width*height)` and then clamped to `8..128`; this keeps the app-side rawvideo backlog near the 1080p=32-frame baseline while remaining an export-performance tuning knob that can continue to move as hardware data and long-export logs accumulate
  - Rule: keep transport-level tuning local while it only shapes export stability/performance at the ffmpeg rawvideo boundary
- `src/tools/video_export/VideoExportDialog.cpp`
  - Owns: export-dialog UI sizing and preview control constants
  - Rule: local UI constants usually stay local unless reused across dialogs
- `src/app/mainwindow/MainWindow.cpp`
  - Owns: embedded/fullscreen preview panel spacing plus fullscreen overlay timing, opacity, and reveal geometry constants
  - Examples:
    - fullscreen `Esc` hint top inset
    - fullscreen control-bar side/bottom margins, max width, and bottom hot-zone height
    - fullscreen control-bar hide offset, reveal animation duration, and opacity fade duration
    - fullscreen control-bar auto-hide delay
  - Rule: keep local while they only shape the main-window preview UX and do not need preview/export parity
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Owns: analysis idle scheduling debounce for low-priority validation/Muri work
  - Current tuning note: `kTimelineAnalysisIdleDelayMs` is `180 ms`, used to coalesce rapid edits before dispatching the combined validation+Muri analysis worker once preview snapshot publication has already completed
  - Rule: keep local while it only expresses main-window preview-vs-analysis priority; promote it if the same debounce becomes shared across dialogs, subprocess workers, or user-facing settings
- `src/common/PreviewInteractionConfig.h`
  - Owns: shared preview-slider keyboard and wheel seek defaults for the main window plus export dialog
  - Current tuning note: discrete seek steps use a `120 FPS` frame (`1 / 120 s`), held seeking ramps linearly at `+1.0x / s` up to `3.0x`, and the held-seek timer currently ticks every `16 ms`
  - Rule: keep shared here because the main preview and export-dialog preview must feel identical
- `src/timeline/TimelineView.cpp`
  - Owns: timeline zoom preset bounds, coarse button stops, and the initial `pixelsPerSecond_` scale derived from the default zoom
  - Current tuning note: the fixed zoom presets are now `25/50/75/100/150/200`; keyboard `Left` / `Right` keep viewport-scroll semantics and the existing single-step behavior, while held-scroll speed caps at `2 * zoomScale()` (for example `25% -> 0.5x`, `50% -> 1.0x`, `100% -> 2.0x`); Timeline minor beat lines currently use `1.4 px`; dense comma grids now render at most `32` subdivisions by collapsing to the largest divisor of the source `{beats}` value that does not exceed that cap
  - Rule: keep local while these values only shape timeline widget UX and do not need cross-subsystem parity

## 3. Promotion Rules

Promote a constant out of a `.cpp` file when any of the following become true:

- more than one subsystem depends on it
- preview and export must agree on it
- tests, scripts, or docs need to refer to it by name
- designers or maintainers are expected to tune it consciously over time

## 4. Keep-Local Rules

It is acceptable to keep a constant local when:

- it is purely an implementation detail of one render pass or widget
- moving it would not reduce duplication
- exposing it would make ownership less clear instead of more clear

## 5. Document These Changes Immediately

- moving a constant between files
- changing units or semantics without changing the name
- turning a magic number into a named constant
- deleting a constant that outside docs or tools previously referenced

## 6. Current High-Attention Areas

- Preview effect tuning in `src/preview/scene/*.cpp`
- Latency detection scan parameters
- Export encoder and bitrate heuristics
- Parser geometry/timing assumptions
- Any duplicated filename or asset-lookup literals outside `src/common/`
