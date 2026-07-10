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
  - Owns: shared `&clock_count=` parsing plus its BPM fallback order (`&wholebpm=`, first inline `(BPM)`, then `120 BPM`)
  - Current default: missing `&clock_count=` is materialized as `4` by `SimaiDocument::ensureDefaultClockCount(...)`; latency BPM auto-detection maps detected meter ids such as `3/4`, `4/4`, or `6/8` to the numerator when updating the field
  - Scope: chart metadata, latency settings UI, and full-range export count-in scheduling
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
  - Owns: shared preview/export SFX aggregation policy plus the Net-View-style bucket-to-kind gain mapping
  - Current defaults: Global `0.30`, Track `1.0`, Answer `0.80`, Tap `0.30`, EX `0.30`, Break `0.30`, Slide `0.30`, Touch `0.30`, Firework `0.30`
  - Scope: realtime preview and export audio balance
- `src/common/VideoExportConfig.h`
  - Owns: export lead-in constants for zero-start exports and non-zero partial-export preload
  - Scope: export timeline alignment
- `src/common/MuriConfig.h`
  - Owns: static tap-on-slide threshold min/max/default plus shared Muri timing cutoffs such as tap-on-slide warning, the slide-head no-startup-tap warning cutoff (`50 ms`), the slide-head late-warning cutoff (`150 ms`), and the slide runtime available window (current default `24 h`)
  - Scope: static and runtime Muri collision interpretation across preview, timeline refresh, dump tooling, and export
- `src/common/TimelineThemeConfig.h`
  - Owns: timeline-scene theme colors shared by Quick, DComp, and QWidget render paths; the waveform brightness clamp/default (`0.2..2.0`, default `0.5`); the grid-line brightness clamp/default (`0.2..2.0`, default `1.0`); and the red editor-cursor header marker color (`QColor(239, 68, 68, 230)`)
  - Current tuning note: waveform brightness scales fill alpha so the hue stays stable in both themes, while grid-line brightness fades/darkens/lightens the meter-driven bar-line color (`gridMajor`), within-measure beat/subdivision color (`gridSubdivision`), and comma/note-position tick color (`gridMinor`)
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
- `src/preview/runtime/PreviewSceneAssetLoader.cpp`
  - Owns: preview asset-loader image preprocessing for custom paused judge-area composites
  - Current tuning note: `kPausedJudgeAreaLabelBrightness` is `0.25`, matching `scripts/build_outline_area_labeled.py`, so labels in the runtime `custom outline + outline_area.png + region_labels_overlay_transparent_v3.png` composite match the maintained built-in labeled outline style
  - Rule: keep local while only the asset loader and the maintainer script use this label-overlay treatment; promote if another runtime/export path needs to tune the same value directly
- `src/preview/runtime/PreviewStageMediaHost.cpp`
  - Owns: QtMultimedia stage-video watchdog and recovery thresholds for realtime background video
  - Current tuning note: playback watchdog fires after `600 ms` without a fresh frame / with a stale frame / when not playing, then tries at most `2` soft recoveries before giving up for that playback stretch: first a pause + seek-flush + play, then a video-output rebind + seek-flush + play. Full backend rebuild remains reserved for explicit media errors, invalid media, and existing seek/prepare timeout recovery paths.
  - Rule: keep local while these thresholds only protect runtime video preview; promote if export preview, worker preview, or user-facing recovery preferences need the same policy
- `src/tools/latency/LatencyDetectorDialog.cpp`
  - Owns: detection windows, hop sizes, BPM scan range, offset penalties, snap thresholds
  - Rule: keep local when the values are intrinsic to the latency tool, but document any user-visible range changes
- `src/audio/BassPreviewAudioBackendImpl.h`
  - Owns: Windows preview BGM BASS/BASS_FX rate-mode defaults and BASS_FX tempo-window presets for pitch-preserving A/B tests
  - Current tuning note: Windows BGM defaults to pitch-preserving tempo mode with the `compact40` (`40/15/8`) window preset. `MIACODE_BASS_BGM_RATE_MODE=rate_transpose` switches to source-time-priority `BASS_ATTRIB_FREQ` mode. `MIACODE_BASS_BGM_TEMPO_PRESET` is active only in tempo mode; presets are unset/`compact40` (`40/15/8`), `stock` (plugin default), `auto` (`0/0/8`), `tight20` (`20/8/4`), `balanced30` (`30/10/6`), `smooth60` (`60/20/8`), and `wide82` (`82/28/8`). `MIACODE_BASS_BGM_TEMPO_PARAMS` overrides those presets with custom `sequence_ms,seek_ms,overlap_ms`.
  - Rule: keep local while this is a Windows preview-only diagnostic path and export does not share live BASS_FX tempo playback
- `src/core/chart/transform/ChartNormalization.cpp`, `src/core/chart/transform/ChartNormalizationSegmentPolicy.cpp`
  - Owns: whole-chart formatting snap constants for note-grid minimization, segment-length preservation, selection carry restoration, and duration-signature rewriting
  - Current tuning note: `384`-snap formatting keeps rendered `{beats}` selection independent from hold/slide duration syntax, still rewrites no-`#` duration signatures against a fixed `384` grid, keeps rendered duration denominators at or above a `16th-note` floor, requires reduce=true segment output to exactly express each snapped 384-grid segment length, and owns the optional blank-line sectioning after every 4 emitted measure lines
  - Rule: keep local while only the chart formatter consumes these thresholds, but document any user-visible formatting changes immediately
- `src/simai/parser/SimaiNativeParser.cpp`
  - Owns: parser-default geometry and timing assumptions used to derive marker behavior
  - Rule: parser-level constants can have repo-wide consequences; treat changes as cross-chain changes
- `src/tools/video_export/VideoExportController.cpp`
  - Owns: encoder probe timeouts, bitrate heuristics, frame diagnostics thresholds, ffmpeg fallback behavior
  - Current tuning note: export preset mapping stays local here. `Fast` keeps the historical baseline, while `High Quality` and `High Compression` retune x264 CRF/preset/B-frames plus per-encoder bitrate or quality flags for NVENC/QSV/AMF/MF/libopenh264/mpeg4 without changing the codec-facing UI surface. The Quality preset does not affect the D3D11 export backend path: D3D11 uses synchronous staging-map readback regardless of preset, while the OpenGL rollback backend remains the only path where the old PBO-oriented Fast behavior can matter.
  - Rule: export heuristics may stay local, but document behavior changes that affect output compatibility or packaging assumptions
- `src/tools/video_export/RawVideoPipeTransport.cpp`
  - Owns: raw-video pipe queue depth, pipe buffer sizing, connect timeout, writer chunk size, and bounded producer blocking behavior
  - Current tuning note: `RawVideoPipePlan::maxBufferedFrames` is derived from frame size using `32 * (1920*1080) / (width*height)`, clamped to `8..128`, and then scaled by `2` so the effective queued-frame budget is `16..256`; `RawVideoPipePlan::requestedBufferBytes` now targets `2 * max(frameBytes, 1 MiB)` so the OS-side raw pipe buffer is also doubled
  - Rule: keep transport-level tuning local while it only shapes export stability/performance at the ffmpeg rawvideo boundary
- `src/tools/video_export/VideoExportDialog.cpp`
  - Owns: export-dialog UI sizing and preview control constants
  - Rule: local UI constants usually stay local unless reused across dialogs
- `src/tools/cover_export/CoverStudioPanel.cpp`
  - Owns: Cover Studio editor-only preview sizing and visual zoom constants
  - Current tuning note: the live cover preview uses a `560 px` fallback fit box when no viewport size is available, and visual canvas zoom is clamped to `50%..200%` in `10%` steps. This zoom only resizes the embedded editor preview and does not affect `.miacover` layout geometry or export resolution.
  - Rule: keep local while these values only shape Cover Studio inspection UX; promote only if another editor/viewer needs shared canvas-zoom behavior
- `src/tools/cover_export/CoverLayerListPanel.cpp`
  - Owns: Cover Studio layer-list delegate row geometry, inline eye/lock hitboxes, and thumbnail box sizing
  - Current tuning note: layer rows are `58 px` tall, inline controls are `22 px`, and thumbnails are `38 px`. These constants only shape the local layer-list delegate and do not affect composition geometry or export output.
  - Rule: keep local while the layer list is the only consumer; promote only if another panel starts sharing the same delegate contract
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
- `src/app/ui/UiTheme.cpp`
  - Owns: app-wide workspace background scrim alpha values for `EditorShell`, metadata/welcome/export pages, preview panel, and bottom-tab panes, plus the stronger card/input backgrounds that preserve readability above a user-selected image
  - Current tuning note: these alpha values are intentionally lower than ordinary opaque theme surfaces because the image is already composited by `AppBackgroundLayer` using the user's background opacity preference. Raising the scrims back near opaque can make a successfully loaded background appear absent.
  - Rule: keep local while these values only shape Qt widget workspace readability; promote only if QuickShell or another frontend must share the exact same app-background scrim contract
- `src/editor/BracketScopeHighlighter.cpp`
  - Owns: editor bracket-scope and comment colors for the chart/metadata text highlighter
  - Current tuning note: dark theme keeps the existing warm/square/comment colors; light theme uses `#A23B2A` for `()` and `{}`, `#1D4ED8` for `[]` duration scopes, and `#15803D` for `||` comments so duration brackets stand apart from normal `#203040` text
  - Rule: keep local while this is only editor syntax color polish; promote if another syntax renderer needs the same palette
- `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp`
  - Owns: toolbox media-prepend ffmpeg defaults and local file conventions
  - Current tuning note: prepended background-video black frames are encoded as `1920x1080` at `30 FPS`, the original video is letterboxed into that canvas, output uses x264 `CRF 18` / `veryfast`, and the generated background video is video-only. Prepended `track.mp3` silence uses stereo `44100 Hz` anullsrc and libmp3lame `-q:a 2`. Backups are fixed at `track_bak.mp3` for audio and `<video-stem>_bak.mp4` for video. Blank duration defaults detect beat count from `&clock_count=` / `&clockcount=` before falling back to `4`, and BPM from `&wholebpm=` before the first half-width chart BPM token before falling back to `120`.
  - Rule: keep local while this remains a single toolbox operation; promote if export, preview, or batch tooling starts sharing the same media-mutation policy
- `src/tools/net/NetClient.cpp`, `src/tools/net/NetBatchDownloadWorker.cpp`, and `src/tools/net/NetBatchDownloadDialog.cpp`
  - Owns: Net public-resource download defaults and local chart-folder conventions
  - Current tuning note: network requests use a local `60 s` timeout. User-ID queries prefer one `uploader:<ID>` list request and only try limited case-variant fallback queries if fuzzy matching is enabled and the exact uploader request returns no rows; tag-only queries may use additional case/plain-text variants when fuzzy matching is enabled. Resource downloads run on a background worker thread, retry up to `3` attempts with an `800 ms` retry wait, chart-to-chart queue pacing is `250 ms`, and successful folder downloads use the fixed file set `track.mp3`, `bg.jpg`, and `maidata.txt`. Optional zip packaging writes those same three entries. Diagnostics report per-resource `KiB/s` / `MiB/s`, per-chart slowest resource, and queue-average speed.
  - Rule: keep local while this remains a single Net toolbox operation; promote if other importers/downloaders start sharing the same remote-resource policy
- `src/app/mainwindow/sections/validation/MainWindow.ValidationListUi.cpp`
  - Owns: wrapped Validation/Muri issue-row padding, minimum row height, and ignored-row opacity used by the shared rich-text list delegate
  - Rule: keep local while these values only shape diagnostics-list rendering in the main window and are not reused by other widgets or the Quick frontend
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Owns: analysis idle scheduling debounce for low-priority validation/Muri work
  - Current tuning note: `kTimelineAnalysisIdleDelayMs` is `180 ms`, used to coalesce rapid edits before dispatching the combined validation+Muri analysis worker once preview snapshot publication has already completed. Waveform alignment diagnostics additionally log per-waveform onset probes at amplitudes `0.02`, `0.05`, and `0.10` from `src/common/WaveformCache.cpp`; those thresholds are diagnostic-only and exist to compare quiet intro, medium onset, and loud transient timing in user logs.
  - Rule: keep local while it only expresses main-window preview-vs-analysis priority; promote it if the same debounce becomes shared across dialogs, subprocess workers, or user-facing settings
- `src/app/quick_shell/qml/TimelineTabSurface.qml`
  - Owns: QuickShell Timeline header-control layout constants
  - Current tuning note: the left header keeps only the zoom control, and the restored right header settings button uses a transparent `28 * headerScale` by `22 * headerScale` QML hit target normally `8 * headerScale` from the right edge, clamped past the zoom control on narrow widths. Header labels and line-start markers avoid that right-side button while zoom remains pinned left. The transparent QML hit zones forward hover/press state to the native Quick/DComp header renderer so colour feedback remains visible above the composition overlay.
  - Rule: keep local while this only shapes QuickShell Timeline header ergonomics; document changes that affect marker/control overlap
- `src/app/quick_shell/QuickShellController.cpp`
  - Owns: QuickShell Timeline native menu presentation constants for the brightness sliders
  - Current tuning note: the menu sliders expose the shared timeline brightness clamps as `20%..200%` with `5%` single/page/tick steps, using `QMenu + QWidgetAction` so dragging a slider keeps the menu open and writes immediately to `TimelineQuickStateBridge`
  - Rule: keep local while these values only format the QuickShell menu surface; promote if another UI needs the same percent-step presentation
- `src/timeline/TimelineSceneStateBuilder.cpp`
  - Owns: native Quick/DComp Timeline header zoom-stepper triangle geometry and the right-side brightness/settings glyph emitted for invisible QML header hit zones
  - Current tuning note: the zoom-stepper triangles are centered as a compact pair within the `22 * headerControlScale` control height, using a local inner gap of `max(2 px, 4 * headerControlScale)` while preserving the separately tuned triangle width and height. Zoom body/stepper and settings controls draw hover backgrounds, accent borders, pressed fills, and pressed glyph offsets natively. The right-side settings button mirrors the QML hit target (`28 * headerControlScale` wide, normally `8 * headerControlScale` from the right edge and clamped past zoom) and uses three local slider strokes plus knob rects so it remains visible under the native Quick/DComp overlay path.
  - Rule: keep local while this only shapes native Timeline header-control visuals; promote only if another control needs the same paired-triangle or slider-glyph geometry
- `src/common/DebugOptions.h`
  - Owns: preview diagnostic env parsing defaults such as `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS`
  - Current tuning note: waveform-alignment focused BASS status sampling defaults to `250 ms`, intentionally lower than the normal ~`1 s` `bass_status` cadence so short 1x offset reports can be captured without making high-frequency logging the default.
  - Rule: keep local while this remains a debug-only sampling cadence; promote only if multiple diagnostics share the same sampling policy
- `src/common/PreviewInteractionConfig.h`
  - Owns: shared preview-slider keyboard and wheel seek defaults for the main window plus export dialog
  - Current tuning note: discrete seek steps use a `120 FPS` frame (`1 / 120 s`), held seeking ramps linearly at `+1.0x / s` up to `3.0x`, and the held-seek timer currently ticks every `16 ms`
  - Rule: keep shared here because the main preview and export-dialog preview must feel identical
- `src/timeline/TimelineView.cpp`
  - Owns: timeline zoom preset bounds, coarse button stops, and the initial `pixelsPerSecond_` scale derived from the fallback default zoom when no stored app preference exists
  - Current tuning note: the fixed zoom presets are now `25/50/75/100/150/200`; keyboard `Left` / `Right` keep viewport-scroll semantics and the existing single-step behavior, while held-scroll speed caps at `2 * zoomScale()` (for example `25% -> 0.5x`, `50% -> 1.0x`, `100% -> 2.0x`); Timeline header line numbers now anchor to per-line `startSecond` values and keep a local minimum spacing of `22 px`, with same-anchor collisions favoring the later source line; header labels size by digit count instead of visible-neighbor width, using a local `0.9x` scale for 1-digit labels, a `0.8x` base for 2+ digits, and additional per-digit-count width fitting against the same `22 px` spacing budget with a `2 px` side gap while keeping bottoms aligned; each displayed line-start anchor also draws a local inverted triangle marker whose tip offset and side angle reuse the playback-entry triangle, while the marker height keeps the earlier header-local `0.85 * digitWidth * 0.7` cap; Quick/DComp header labels and line-start markers are emitted with one viewport of horizontal prefetch and rebuild only on scroll-bucket boundaries; Timeline minor beat lines currently use `1.4 px`; dense comma grids now render at most `32` subdivisions by collapsing to the largest divisor of the source `{beats}` value that does not exceed that cap; the classic QWidget path mirrors the Quick/DComp red editor-cursor header marker color locally; waveform rendering applies a one-preview-frame phase compensation based on the preview canvas frame interval because logs from 60 Hz and 120 Hz devices show the authoritative audio clock leading the timeline-rendered playhead by about one frame while the root cause remains unknown
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
