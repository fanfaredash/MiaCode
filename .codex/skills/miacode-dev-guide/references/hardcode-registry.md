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
- `src/common/PreviewVideoGeometryConfig.h`
  - Owns: background brightness defaults, layout square scaling (current default `0.95`), dimming geometry, smooth brightness blending
  - Scope: preview and export visual geometry
- `src/common/PreviewSfxAssets.h`
  - Owns: SFX kind-to-filename mapping and SFX directory resolution
  - Scope: sound asset conventions
- `src/common/VideoExportConfig.h`
  - Owns: export lead-in constants for zero-start exports and non-zero partial-export preload
  - Scope: export timeline alignment
- `src/tools/muri/MuriStaticChecker.h`
  - Owns: static tap-on-slide threshold min/max/default
  - Scope: static Muri collision interpretation

## 2. Implementation-Local Hotspots

- `src/preview/video/PreviewCanvas.cpp`
  - Owns: large volume of render tuning constants
  - Examples:
    - lane angle base and step
    - sprite scaling ratios
    - touch/touchhold close curve parameters
    - judge-effect curve timing and geometry
    - firework visual tuning
    - cache limits and atlas packing numbers
  - Rule: keep local only when the values are render-internal and not consumed elsewhere
- `src/tools/latency/LatencyDetectorDialog.cpp`
  - Owns: detection windows, hop sizes, BPM scan range, offset penalties, snap thresholds
  - Rule: keep local when the values are intrinsic to the latency tool, but document any user-visible range changes
- `src/simai/parser/SimaiNativeParser.cpp`
  - Owns: parser-default geometry and timing assumptions used to derive marker behavior
  - Rule: parser-level constants can have repo-wide consequences; treat changes as cross-chain changes
- `src/tools/video_export/VideoExportController.cpp`
  - Owns: mix sample rate, encoder probe timeouts, bitrate heuristics, frame diagnostics thresholds, ffmpeg fallback behavior
  - Rule: export heuristics may stay local, but document behavior changes that affect output compatibility or packaging assumptions
- `src/tools/video_export/RawVideoPipeTransport.cpp`
  - Owns: raw-video pipe queue depth, pipe buffer sizing, connect timeout, writer chunk size, and bounded producer blocking behavior
  - Current tuning note: `RawVideoPipePlan::maxBufferedFrames` defaults to `32` frames and caps the app-side rawvideo backlog before producer-side blocking; treat it as an export-performance tuning knob that can continue to move as hardware data and long-export logs accumulate
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

- Preview effect tuning in `PreviewCanvas.cpp`
- Latency detection scan parameters
- Export encoder and bitrate heuristics
- Parser geometry/timing assumptions
- Any duplicated filename or asset-lookup literals outside `src/common/`
