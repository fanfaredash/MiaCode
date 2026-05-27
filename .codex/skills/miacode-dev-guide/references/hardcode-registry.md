# Hardcode Registry

Use this file to track where important constants live, what they mean, and whether they should stay local or move into shared config headers.

## 1. Shared Config Headers

- `src/common/AssetPaths.h`
  - Owns: asset root discovery and `assetPath(...)`
  - Scope: shared resource lookup
  - Promote new shared path rules here before duplicating them elsewhere
- `src/common/PreviewGameplayConfig.h`
  - Owns: logical canvas size, lane-distance geometry, preview flow-speed normalization shared by Tap/Touch settings, tap lifecycle timing, slide pretrace timing, judge-effect durations, and the default value for the shared preview/export slide stacking toggle (`kPreviewSlideEarlierSecondAndTextOnTop`)
  - Scope: preview and export timing assumptions
- `src/common/PreviewSkinConfig.h`
  - Owns: shared tap-head scale plus hold-width, hold-cap-slice, and slide-track sizing ratios used to keep preview and timeline skin geometry aligned
  - Scope: preview and timeline skin-asset visual parity
- `src/common/PreviewVideoGeometryConfig.h`
  - Owns: background brightness defaults, layout square scaling (current default `0.95`), dimming geometry, smooth brightness blending
  - Scope: preview and export visual geometry
- `src/common/LayoutRingConfig.h`
  - Owns: fixed outline-to-playfield diameter ratio shared by realtime preview and export dim-mask geometry
  - Scope: preview and export outline/dimming alignment
- `src/common/PreviewSfxAssets.h`
  - Owns: SFX kind-to-filename mapping and SFX directory resolution
  - Scope: sound asset conventions
- `src/common/ChartClockCount.h`
  - Owns: export-only `&clock_count=` parsing plus its BPM fallback order (`&wholebpm=`, first inline `(BPM)`, then `120 BPM`)
  - Scope: chart metadata to full-range export count-in scheduling
- `src/common/PreviewTimingSettings.h`
  - Owns: persisted preview timing offset layers (`audioOffset`, `displayOffset`, `judgeOffset`, `answerOffset`) plus the internal frame-vs-second conversion helper for future callers
  - Scope: realtime preview, persistence, and export snapshot/task timing parity
- `src/common/PreviewAudioMixConfig.h`
  - Owns: shared preview/export offline mix format constants (`48 kHz`, stereo)
  - Scope: preview BASS runtime, export audio render-plan consumers, and Windows BASS offline mixing
- `src/common/PreviewSfxTiming.h`
  - Owns: shared runtime/export SFX timing formulas, including the current offset layering (`audioOffset` as whole-SFX chart shift, positive `displayOffset` advancing only answer/judge families, family-specific `judge` / `answer` offsets) plus the chart-domain `1/60 s` pre-trigger shared by answer and judge-family SFX
  - Scope: realtime preview and export SFX timing parity
- `src/preview/audio/PreviewAudioSettings.h`
  - Owns: shared preview/export SFX aggregation policy plus the Majdata-View-style bucket-to-kind gain mapping
  - Current defaults: Global `0.30`, Track `1.0`, Answer `0.80`, Tap `0.30`, EX `0.30`, Break `0.30`, Slide `0.30`, Touch `0.30`, Firework `0.30`
  - Scope: realtime preview and export audio balance
- `src/common/VideoExportConfig.h`
  - Owns: export lead-in constants for zero-start exports and non-zero partial-export preload
  - Scope: export timeline alignment
- `src/common/MuriConfig.h`
  - Owns: static tap-on-slide threshold min/max/default plus shared Muri timing cutoffs such as tap-on-slide warning, the slide-head no-startup-tap warning cutoff (`50 ms`), the slide-head late-warning cutoff (`150 ms`), and the slide runtime available window (current default `24 h`)
  - Scope: static and runtime Muri collision interpretation across preview, timeline refresh, dump tooling, and export
- `src/common/TimelineThemeConfig.h`
  - Owns: timeline-scene theme colors shared by Quick and DComp render paths, including the red editor-cursor header marker color (`QColor(239, 68, 68, 230)`)
  - Scope: timeline renderer visual parity

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
  - Current tuning note: `src/preview/scene/PreviewJudgeFireworkLayerState.cpp` keeps the firework color-ball hole ratios local, but they are intentionally pinned to the legacy pre-Qt Quick material bounds (`_InnerLB/_InnerUB/_OuterLB/_OuterUB`) so the center cutout and the 15 colored sector spokes both stay visually aligned with the old `PreviewCanvas` hole-mask fade. The color-ball scale/alpha curves are intentionally aligned to the true `v0.3.7-dev5` `PreviewCanvas.cpp` values rather than the later `0d6dd1d` zero-start quick-restore ramps, because that later ramp made the center ball read too small before the spokes came in.
  - Rule: keep local only when the values are render-internal and not consumed elsewhere
- `src/preview/scene/PreviewAnimatedSpriteHelpers.cpp`
  - Owns: continuous animated-sprite wave timing (`kMaterialAnimationTimeScale`, `kMaterialAnimationPhaseScale`) plus helper-side overlay cache quantization/cap for EX-style CPU composites
  - Current tuning note: overlay cache quantization stays local at `4096.0`, and the helper-side overlay cache is currently capped at `128` entries because it is a Quick-preview reuse detail rather than shared product behavior
  - Rule: keep local while the preview/export pipelines only need shared animation timing and overlay reuse policy, but document changes if another subsystem must reason about the same wave scaling or cache budget
- `src/preview/quick_scene/PreviewQuickSpriteNodes.cpp`
  - Owns: shader-side `BreakAnimate` / `HoldShine` brightness and contrast coefficients, custom sprite-material uniform layout, and the layer-local contiguous batch policy keyed by texture only while per-sprite effect selection stays in vertex data
  - Current tuning note: this file is now the owner for runtime sprite effect math and for the “merge only adjacent compatible sprites, never reorder” rule; changes here affect both runtime preview and export preview because both share the Quick scene graph path
  - Rule: keep local while the values are renderer-internal, but document any changes that alter visual parity or layer-order guarantees
- `src/preview/runtime/PreviewStageMediaHost.cpp`
  - Owns: QtMultimedia stage-video watchdog and recovery thresholds for realtime background video
  - Current tuning note: playback watchdog fires after `600 ms` without a fresh frame / with a stale frame / when not playing, then tries at most `2` soft recoveries before giving up for that playback stretch: first a pause + seek-flush + play, then a video-output rebind + seek-flush + play. Full backend rebuild remains reserved for explicit media errors, invalid media, and existing seek/prepare timeout recovery paths.
  - Rule: keep local while these thresholds only protect runtime video preview; promote if export preview, worker preview, or user-facing recovery preferences need the same policy
- `src/tools/latency/LatencyDetectorDialog.cpp`
  - Owns: detection windows, hop sizes, BPM scan range, offset penalties, snap thresholds
  - Rule: keep local when the values are intrinsic to the latency tool, but document any user-visible range changes
- `src/simai/transform/ChartNormalization.cpp`
  - Owns: whole-chart formatting snap constants for note-grid minimization and duration-signature rewriting
  - Current tuning note: `384`-snap formatting keeps rendered `{beats}` selection independent from hold/slide duration syntax, still rewrites no-`#` duration signatures against a fixed `384` grid, and keeps rendered duration denominators at or above a `16th-note` floor
  - Rule: keep local while only the chart formatter consumes these thresholds, but document any user-visible formatting changes immediately
- `src/simai/parser/SimaiNativeParser.cpp`
  - Owns: parser-default geometry and timing assumptions used to derive marker behavior
  - Rule: parser-level constants can have repo-wide consequences; treat changes as cross-chain changes
- `src/tools/video_export/VideoExportController.cpp`
  - Owns: encoder probe timeouts, bitrate heuristics, frame diagnostics thresholds, ffmpeg fallback behavior
  - Current tuning note: export preset mapping stays local here. `Fast` keeps the historical baseline, while `High Quality` and `High Compression` retune x264 CRF/preset/B-frames plus per-encoder bitrate or quality flags for NVENC/QSV/AMF/MF/libopenh264/mpeg4 without changing the codec-facing UI surface.
  - Rule: export heuristics may stay local, but document behavior changes that affect output compatibility or packaging assumptions
- `src/tools/video_export/RawVideoPipeTransport.cpp`
  - Owns: raw-video pipe queue depth, pipe buffer sizing, connect timeout, writer chunk size, and bounded producer blocking behavior
  - Current tuning note: `RawVideoPipePlan::maxBufferedFrames` is derived from frame size using `32 * (1920*1080) / (width*height)`, clamped to `8..128`, and then scaled by `2` so the effective queued-frame budget is `16..256`; `RawVideoPipePlan::requestedBufferBytes` now targets `2 * max(frameBytes, 1 MiB)` so the OS-side raw pipe buffer is also doubled
  - Rule: keep transport-level tuning local while it only shapes export stability/performance at the ffmpeg rawvideo boundary
- `src/tools/video_export/VideoExportDialog.cpp`
  - Owns: export-dialog UI sizing and preview control constants
  - Rule: local UI constants usually stay local unless reused across dialogs
- `src/app/mainwindow/MainWindow.cpp` and `src/app/mainwindow/sections/window/*.cpp`
  - Owns: embedded/fullscreen preview panel spacing, fullscreen overlay timing/opacity/reveal geometry constants, bottom-tab content resize bounds/hot-zone behavior, and the fixed Timeline-side preview UI cadence used by scrubbing/timeline refresh
  - Examples:
    - fullscreen `Esc` hint top inset
    - fullscreen control-bar side/bottom margins, max width, and bottom hot-zone height
    - fullscreen control-bar hide offset, reveal animation duration, and opacity fade duration
    - fullscreen control-bar auto-hide delay
    - bottom-tab invisible top-edge resize hot zone (`8 px`), content-scale clamp (`50%..100%`), derived header/list-font scale (`75%..100%`), and Timeline host height calculation from scaled header plus scaled lanes
    - fixed `30 Hz` Timeline UI cadence (`33 ms` timer interval / `1/30 s` seek-throttle threshold)
  - Rule: keep local while they only shape the main-window preview UX and do not need preview/export parity
- `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp`
  - Owns: toolbox media-prepend ffmpeg defaults and local file conventions
  - Current tuning note: prepended background-video black frames are encoded as `1920x1080` at `30 FPS`, the original video is letterboxed into that canvas, output uses x264 `CRF 18` / `veryfast`, and the generated background video is video-only. Prepended `track.mp3` silence uses stereo `44100 Hz` anullsrc and libmp3lame `-q:a 2`. Backups are fixed at `track_bak.mp3` for audio and `<video-stem>_bak.mp4` for video. Blank duration defaults detect beat count from `&clock_count=` / `&clockcount=` before falling back to `4`, and BPM from `&wholebpm=` before the first half-width chart BPM token before falling back to `120`.
  - Rule: keep local while this remains a single toolbox operation; promote if export, preview, or batch tooling starts sharing the same media-mutation policy
- `src/app/mainwindow/sections/validation/MainWindow.ValidationListUi.cpp`
  - Owns: wrapped Validation/Muri issue-row padding, minimum row height, and ignored-row opacity used by the shared rich-text list delegate
  - Rule: keep local while these values only shape diagnostics-list rendering in the main window and are not reused by other widgets or the Quick frontend
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Owns: analysis idle scheduling debounce for low-priority validation/Muri work
  - Current tuning note: `kTimelineAnalysisIdleDelayMs` is `180 ms`, used to coalesce rapid edits before dispatching the combined validation+Muri analysis worker once preview snapshot publication has already completed
  - Rule: keep local while it only expresses main-window preview-vs-analysis priority; promote it if the same debounce becomes shared across dialogs, subprocess workers, or user-facing settings
- `src/common/PreviewInteractionConfig.h`
  - Owns: shared preview-slider keyboard and wheel seek defaults for the main window plus export dialog
  - Current tuning note: discrete seek steps use a `120 FPS` frame (`1 / 120 s`), held seeking ramps linearly at `+1.0x / s` up to `3.0x`, and the held-seek timer currently ticks every `16 ms`
  - Rule: keep shared here because the main preview and export-dialog preview must feel identical
- `src/timeline/TimelineView.cpp`
  - Owns: timeline zoom preset bounds, coarse button stops, and the initial `pixelsPerSecond_` scale derived from the fallback default zoom when no stored app preference exists
  - Current tuning note: the fixed zoom presets are now `25/50/75/100/150/200`; keyboard `Left` / `Right` keep viewport-scroll semantics and the existing single-step behavior, while held-scroll speed caps at `2 * zoomScale()` (for example `25% -> 0.5x`, `50% -> 1.0x`, `100% -> 2.0x`); Timeline header line numbers now anchor to per-line `startSecond` values and keep a local minimum spacing of `22 px`, with same-anchor collisions favoring the later source line; header labels size by digit count instead of visible-neighbor width, using a local `0.9x` scale for 1-digit labels, a `0.8x` base for 2+ digits, and additional per-digit-count width fitting against the same `22 px` spacing budget with a `2 px` side gap while keeping bottoms aligned; each displayed line-start anchor also draws a local inverted triangle marker whose tip offset and side angle reuse the playback-entry triangle, while the marker height keeps the earlier header-local `0.85 * digitWidth * 0.7` cap; Timeline minor beat lines currently use `1.4 px`; dense comma grids now render at most `32` subdivisions by collapsing to the largest divisor of the source `{beats}` value that does not exceed that cap
  - Current tuning note: the fixed zoom presets are now `25/50/75/100/150/200`; keyboard `Left` / `Right` keep viewport-scroll semantics and the existing single-step behavior, while held-scroll speed caps at `2 * zoomScale()` (for example `25% -> 0.5x`, `50% -> 1.0x`, `100% -> 2.0x`); Timeline header line numbers now anchor to per-line `startSecond` values and keep a local minimum spacing of `22 px`, with same-anchor collisions favoring the later source line; header labels size by digit count instead of visible-neighbor width, using a local `0.9x` scale for 1-digit labels, a `0.8x` base for 2+ digits, and additional per-digit-count width fitting against the same `22 px` spacing budget with a `2 px` side gap while keeping bottoms aligned; each displayed line-start anchor also draws a local inverted triangle marker whose tip offset and side angle reuse the playback-entry triangle, while the marker height keeps the earlier header-local `0.85 * digitWidth * 0.7` cap; Timeline minor beat lines currently use `1.4 px`; dense comma grids now render at most `32` subdivisions by collapsing to the largest divisor of the source `{beats}` value that does not exceed that cap; the classic QWidget path mirrors the Quick/DComp red editor-cursor header marker color locally
  - Rule: keep local while these values only shape timeline widget UX and do not need cross-subsystem parity
- `src/tools/video_export/VideoExportRuntimePolicy.h`
  - Owns: export PBO env precedence and worker crash-retry policy shared by the controller, main-window worker launcher, and runtime policy spec
  - Current tuning note: offscreen PBO readback now defaults to on unless `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` wins, and `kVideoExportWorkerMaxCrashRetries` is fixed at `1`
  - Rule: keep shared here because export runtime config parsing, worker retry decisions, docs, and specs must stay aligned

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
