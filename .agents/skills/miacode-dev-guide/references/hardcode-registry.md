# Hardcode Registry

Where important constants live, what they mean, and whether they should stay local or move into a
shared config header. Ported with paths corrected (2026-05-29); verify against code.

## 1. Shared config headers (`src/common/` unless noted)

- `AssetPaths.h` — asset root discovery + `assetPath(...)`. Promote new shared path rules here.
- `PreviewGameplayConfig.h` — logical canvas size, lane geometry, flow-speed normalization, tap
  lifecycle (`1 + 240 / speed` frames per half-lifecycle at 120 FPS), slide pretrace,
  slide-head rotation calibration (`5.20 + 194.36 / slide image count`
  frames per 72 degrees at the 120 FPS / 0.5 s reference), judge-effect durations, default slide-stacking toggle
  (`kPreviewSlideEarlierSecondAndTextOnTop`).
- `PreviewSkinConfig.h` — tap-head scale, hold-width / hold-cap-slice / slide-track ratios (preview
  ↔ timeline skin parity).
- `PreviewVideoGeometryConfig.h` — background brightness defaults, layout square scale (default
  `0.95`), dimming geometry, smooth brightness.
- `LayoutRingConfig.h` — fixed outline-to-playfield diameter ratio (preview ↔ export dim mask).
- `PreviewSfxAssets.h` — SFX kind→filename mapping + SFX dir resolution.
- `ChartClockCount.h` — shared `&clock_count=` parsing + BPM fallback order; a missing value is materialized as `4` by `SimaiDocument::ensureDefaultClockCount`; consumed by chart metadata, the latency settings UI, and export count-in.
- `PreviewTimingSettings.h` — persisted preview timing offset layers (`audioOffset`,
  `displayOffset`, `judgeOffset`, `answerOffset`).
- `PreviewAudioMixConfig.h` — shared offline mix format (`48 kHz`, stereo).
- `PreviewSfxTiming.h` — shared runtime/export SFX timing formulas + `1/60 s` pre-trigger.
- `src/audio/PreviewAudioSettings.h` — SFX aggregation policy + bucket-to-kind gain map. Defaults:
  Global `0.30`, Track `1.0`, Answer `0.80`, others `0.30`.
- `VideoExportConfig.h` — export lead-in (full-range `2.0s`) / partial preload (`1.0s`).
- `MuriConfig.h` — static tap-on-slide threshold min/max/default + Muri timing cutoffs
  (slide-head no-tap warning `50 ms`, late-warning `150 ms`, runtime available window default `24 h`)
  + hand-footprint radii (`kHandRadiusNormal/Wifi/Max`) + the simultaneous-touch model switch
  `kSimultaneousTouchEnclosingCircleEnabled` (default `0`, not UI-exposed): `0` = each touch in an
  each-group keeps its own pad-sized footprint; `1` = collapse the group into one smallest-enclosing
  circle (oversized → two-hand press). Read in `MuriAnalyzer.cpp` `buildRuntimeHandActions`, so it
  feeds BOTH the slide/wifi judge and the multi-touch diagnostics.
- `TimelineThemeConfig.h` — timeline-scene theme colors for the Quick timeline path
  (e.g. editor-cursor header marker `QColor(239,68,68,230)`); also the **tiered grid-line-height
  feature**: toggle `kTimelineTieredGridLineHeightsEnabled` (default `1`; `0` = legacy full-height)
  + per-tier fractions `kTimelineGridHeightFraction{Measure 9/9, Subdivision 8/9, Comma 7/9}` +
  resolver `timelineGridLineHeightFraction()`. Lines anchor at the top of the content area and
  extend down by `fraction * timelineHeight`. Consumed by `TimelineSceneStateBuilder.cpp`
  `addGridLine` (QSG via pre-baked `state.gridLines`) AND `TimelineView.Paint.cpp` (widget
  path — bar + comma tiers only; that path has no separate quarter-note subdivision lines).
- `VideoExportRuntimePolicy.{h,cpp}` (`src/tools/video_export/`) — export PBO env precedence + worker
  crash-retry policy (`kVideoExportWorkerMaxCrashRetries = 1`) and file-size preset policy
  (bitrate coefficient/min/max, peak-rate/buffer multipliers, GOP seconds, audio cap, relative
  x264 CRF adjustment, and whether export suppresses video backgrounds). The relative adjustment
  preserves the Fast/High Quality gap: Compact adds 1 and the ultra-compact modes add 3. Both
  ultra-compact modes use a fixed 4000 kbps video target and peak-rate ceiling; non-standard x264
  exports apply the bitrate/VBV limits alongside CRF so the size policy covers software encoders.

## 2. Implementation-local hotspots (keep local unless promotion rule triggers)

- `src/editor/SimaiCompletionCatalog.cpp` — bracket-completion suggestion lists. Fixed,
  product-decided order (do NOT sort): `[` durations `{8:1] 4:1] 16:3] 384:1]}`, `{`
  subdivisions `{16} 24} 32}}`. `(` BPM list is dynamic (scanned `(<n>)` markers +
  `&wholebpm`), not a constant. Keep local; the `SimaiCompletionCatalogSpec` pins these.
- `src/core/scene/*.cpp` — large volume of render tuning (lane angle base/step, sprite scaling,
  touch/touchhold close curves, judge-effect timing/geometry, firework tuning, descriptor sizing).
  Note: `PreviewJudgeFireworkLayerState.cpp` pins firework hole ratios to the legacy
  `PreviewCanvas` material bounds while its
  0.71666664 s spoke/color-ball curves and the shader's two 12-star fixed-position pulse/ring batches
  follow the supplied 30 fps firework reference. Inner stars sit inside the colour-glow ring and outer
  stars are size-inset from the judgment boundary; visible star half-extents vary deterministically
  from 0.040 to 0.050 of the judgment radius and use 0.95 peak opacity. Star height is compressed to match the unchanged horizontal
  span. Their 0.365 s pulse (0.115 s in + 0.25 s out) moves outward by
  0.02 judgment-radius while appearing and another 0.04 while shrinking/fading, emitted in two
  shuffled batches starting at 0.00/0.08 s. Angles directly sample the full circle, so clusters and empty arcs are allowed,
  with a fresh per-trigger/replay QSG-node seed held stable for the effect lifetime. Within each batch,
  inner stars precede outer stars by 0.010 s and per-ring
  jitter is limited to 0.004 s. Fade-in uses full geometry size; shrink starts only on fade-out. Timeline culling
  shares that duration through
  `PreviewGameplayConfig.h`; keep both consumers synchronized through that single constant.
- `src/core/scene/PreviewAnimatedSpriteHelpers.cpp` — animated-sprite wave timing
  (`kMaterialAnimationTimeScale/PhaseScale`), overlay cache quantization (`4096.0`) / cap (`128`).
- `src/preview/quick_scene/PreviewQuickSpriteNodes.cpp` — shader-side `BreakAnimate`/`HoldShine`
  coefficients, sprite-material uniform layout, "merge adjacent compatible sprites, never reorder"
  batch rule (affects runtime AND export — shared QSG path).
- `src/preview/runtime/PreviewStageMediaHost.cpp` — stage-video watchdog/recovery (fires after
  `600 ms`, ≤2 soft recoveries). **QMediaPlayer path only** (non-Windows / `!MIACODE_USE_QTAVPLAYER`);
  the Windows QtAVPlayer backend doesn't need it (no silent-fallback / converter-rebuild failure
  modes) and compiles these as no-op stubs. Paused-seek ack tolerance `kPausedSeekAckToleranceMs`
  (`80 ms`) is shared by both backends.
- `src/tools/latency/` — detection windows, hop sizes, BPM scan range, offset penalties, snap
  thresholds.
- `src/core/chart/transform/ChartNormalization.cpp` — whole-chart format snap constants
  (`384`-grid, `16th-note` denominator floor).
- `src/core/chart/parser/SimaiNativeParser.cpp` — parser default geometry/timing assumptions
  (parser-level constants have repo-wide consequences — treat changes as cross-chain).
- `src/tools/video_export/VideoExportEncoder.cpp` — encoder probe timeouts, render-quality preset
  mapping (`Fast`/`High Quality`), application of the runtime size policy, and ffmpeg fallback.
- `src/tools/video_export/RawVideoPipeTransport.cpp` — pipe queue depth / buffer sizing
  (`maxBufferedFrames` derived from frame size, ×2; `requestedBufferBytes` `2 * max(frameBytes,1MiB)`).
- `src/app/mainwindow/MainWindow.cpp` + `sections/window/*.cpp` — preview panel spacing, fullscreen
  overlay timing/opacity/reveal geometry, bottom-tab resize bounds (hot zone `8 px`, content scale
  `kBottomTabsContentScaleMin/Max = 0.5..4.0` in `MainWindow.WindowShell.cpp`), fixed `30 Hz`
  timeline UI cadence. The `4.0` max is a SAFETY bound only — the real ceiling above 100% is
  `kBottomTabsMaxWindowHeightFraction = 2/3` of the whole window height. **Two-tier scale above
  100%:** only the note GRID height grows (raw scale); the header ("顶部变换"), note 素材/markers and
  lane-label fonts CAP at 100%. The 语法/无理 issue-list fonts are a fixed 90% of base, uniform /
  height-independent (`kBottomTabsIssueListFontScale = 0.9`). The `4.0` max is a
  SYNC-PAIR mirrored as `kMaxContentScale` in `TimelineSceneStateBuilder.cpp` and the literal `4.0`
  in `TimelineView.cpp`/`TimelineView.Core.cpp`/`TimelineQuickStateBridge.cpp` `setContentScale`
  clamps — change all together. See `cross-chain-linkage.md`.
- `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp` — toolbox media-prepend ffmpeg
  defaults (`1920x1080@30`, x264 `CRF 18 veryfast`; silence stereo `44100 Hz` libmp3lame `-q:a 2`).
- `src/app/mainwindow/sections/validation/MainWindow.ValidationListUi.cpp` — issue-row padding /
  min height / ignored-row opacity.
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp` —
  `kTimelineAnalysisIdleDelayMs` (`180 ms`).
- `src/common/PreviewInteractionConfig.h` — shared preview-slider seek (discrete `1/120 s`,
  held ramp `+1.0x/s` to `3.0x`, timer `16 ms`).
- `src/timeline/TimelineView.cpp` — zoom presets `25/50/75/100/150/200`, header label sizing, minor
  beat line `1.4 px`, dense comma cap `32` subdivisions.

## 3. Promote a constant out of a `.cpp` when

- more than one subsystem depends on it; preview and export must agree on it; tests/scripts/docs
  refer to it by name; or maintainers are expected to tune it consciously over time.

## 4. Keep local when

- it is purely an implementation detail of one render pass/widget; moving it would not reduce
  duplication; or exposing it would muddy ownership.

## 5. Document immediately when

- moving a constant between files; changing units/semantics without changing the name; turning a
  magic number into a named constant; deleting a constant other docs/tools referenced.

## 6. Current high-attention areas

Preview effect tuning in `src/core/scene/*.cpp`; latency scan parameters; export encoder/bitrate
heuristics; parser geometry/timing assumptions; duplicated filename/asset literals outside
`src/common/`.
