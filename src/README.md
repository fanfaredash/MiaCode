# `src` Directory Layout

> **Route note:** `.codex/skills/miacode-dev-guide/` is the authoritative
> architecture map. This file is the local `src/` landing page and records
> which implementation path is the default when multiple versions coexist.

This repository is organised by module responsibility. Except for entry files,
keep sources in their second-level folders.

```text
src/
  app/                       # App entry, MainWindow, QuickShell glue
    main.cpp
    AppVersion.h.in
    mainwindow/
    ui/
    quick_shell/

  audio/                     # BASS / miniaudio backends, SFX runtime
    PreviewAudioBackend.h
    BassPreviewAudioBackend.*
    MiniaudioPreviewAudioBackend.*
    PreviewAudioSettings.*
    QtPreviewSfxRuntime.*
    QtPreviewSfxRuntime.*.cpp

  common/                    # Cross-module utilities, debug logging,
                             # shared config structs

  core/
    chart/                   # simai parsing + transforms
      document/
        SimaiDocument.*
        SimaiTimingMetadata.*
      parser/
        SimaiNativeParser.*
        SimaiNativeParser.*.cpp
        SimaiNativeDump.cpp
      transform/
        ChartBatchTransform.*
        ChartNormalization.*
    scene/                   # Pure frame-state math + per-layer descriptors
      PreviewFrameState.h
      Preview*LayerState.*
      PreviewOpacityCurves.*
      PreviewScene*.*
      PreviewSkinSelectors.*
      PreviewTrackShared.*
    video/                   # Shared preview/video render settings
      PreviewRenderSettings.h

  editor/                    # In-app text editor for chart files
    PlainCodeEditor.*
    BracketScopeHighlighter.*

  preview/                   # Current QSG preview/runtime implementation
    quick_scene/             # QSG-based chart layer renderers
    runtime/                 # PreviewRuntime and stage-media integration

  render/                    # Retained D3D11 / DirectComposition renderer
    PreviewDCompRenderer.*   # Render thread for opt-in DComp paths
    backend_d3d11/
      PreviewDCompCore.*
      PreviewDCompSpritePipeline.*
      PreviewDCompTextureCache.*
      PreviewDCompFrameStateSnapshot.h
      PreviewDCompSurface.*
      TimelineRenderView.*

  sources/                   # Source descriptors consumed by the DComp path

  timeline/                  # Editor timeline strip
    TimelineView.*
    TimelineView.*.cpp
    TimelineSceneState*
    TimelineNoteAssets.*
    quick/                   # Quick/QSG timeline surface used by QuickShell
      TimelineQuickItem.*
      TimelineQuick*Layer.*

  tools/                     # Standalone helpers + spec/probe targets
    latency/
    muri/
    probe/
    timeline/
    video_export/

  wrapper/                   # Windows launcher wrapper
```

## Active And Retained Implementations

When a component has several implementations, document the default startup path
and the retained path together. Do not assume a directory name such as `legacy`,
`quick`, `dcomp`, or `widget` is enough to communicate what runs by default.

Current defaults:

- GUI shell: `QuickShellBootstrap` is the normal GUI startup path from
  `src/app/main.cpp`. The older widget shell is retained as native surfaces
  hosted inside QuickShell and should not receive new top-level feature work
  unless that is the explicit target.
- Preview chart rendering: the default is Qt Quick/QSG through
  `preview/runtime/PreviewRuntime` and `preview/quick_scene/*`.
  `render/backend_d3d11/PreviewDCompSurface` plus `sources/*` are retained
  diagnostic implementations, enabled by `MIACODE_PREVIEW_USE_DCOMP=1` or
  `--quick-shell-beta`.
- Timeline rendering: the default QuickShell timeline is
  `timeline/quick/TimelineQuickItem` and its QSG layers. `TimelineView` remains
  the widget reference/helper surface, and
  `render/backend_d3d11/TimelineRenderView` is retained for DComp diagnostics
  behind `MIACODE_TIMELINE_USE_DCOMP=1` after DComp preview is enabled.
- Preview audio: `audio/QtPreviewSfxRuntime` selects
  `BassPreviewAudioBackend` on Windows and `MiniaudioPreviewAudioBackend` on
  non-Windows. On Windows, `MIACODE_BASS_BGM_RATE_MODE=rate_transpose` is an
  A/B diagnostic mode; the default BGM rate path is pitch-preserving tempo.
- Export audio: `tools/video_export/VideoExportPipeline.cpp` selects
  `BassExportAudioBackend` on Windows and `LegacyExportAudioBackend` on
  non-Windows.

It is acceptable to update a feature only on the default implementation path
when the retained implementation is diagnostic, compatibility-only, or not
user-facing for that feature. In that case, the commit message or PR summary
must explicitly say which retained path was not updated and why. If the
retained path is still a supported user-facing route for the changed behavior,
update both paths or leave a clear follow-up task in the same change.

## Conventions

- `app/` is for app entry and window orchestration only.
- `core/scene/` owns pure frame-state math and per-layer descriptor building.
  No QSG / D3D11 dependencies.
- `core/chart/` owns simai parsing, transforms, and normalization. No scene or
  runtime dependencies.
- `core/video/` owns shared render settings for the video pipeline. Actual
  playback lives in `preview/runtime/PreviewStageMediaHost`.
- `audio/` owns audio backends; nothing else may link BASS or miniaudio
  directly.
- `preview/` owns the current QSG preview/runtime path. New preview-rendering
  work should prefer shared `core/scene` state plus dedicated
  `preview/quick_scene` layers unless the task explicitly targets DComp.
- `render/` and `sources/` own the retained D3D11 / DirectComposition path.
  Keep them in sync only when the feature explicitly affects that diagnostic
  path or when a shared state contract changes.
- Prefer existing second-level folders instead of creating parallel aliases.
