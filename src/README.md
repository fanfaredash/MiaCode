# `src` Directory Layout (v2-refactor target)

This repository is organised by module responsibility, modelled after
the OBS source/compositor split. Except for entry files, keep sources
in their second-level folders.

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
    chart/                   # simai parsing + transforms (was src/simai/)
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
    scene/                   # Pure frame-state math + per-layer
                             # descriptor building (was preview/scene/)
      PreviewFrameState.h
      Preview*LayerState.*
      PreviewOpacityCurves.*
      PreviewScene*.*
      PreviewSkinSelectors.*
      PreviewTrackShared.*
    video/                   # libmpv probe (Phase 0); MpvVideoSource
                             # (Phase 4); shared render settings.
      MpvProbe.*
      PreviewRenderSettings.h

  editor/                    # In-app text editor for chart files
    PlainCodeEditor.*
    BracketScopeHighlighter.*

  preview/                   # Legacy preview surfaces — Phase 2/3 will
                             # replace these with the source/compositor
                             # path. Retained for now so the existing
                             # QSG render path keeps working.
    quick_scene/             # QSG-based chart layer renderers
    runtime/                 # PreviewRuntime, snapshot host (timing
                             # extraction → core/timing/ in Phase 2/3)

  render/                    # OBS-style rendering (was preview/dcomp/)
    PreviewDCompRenderer.*   # Render thread; renamed → RenderThread in Phase 3
    backend_d3d11/
      PreviewDCompCore.*
      PreviewDCompSpritePipeline.*
      PreviewDCompTextureCache.*
      PreviewDCompFrameStateSnapshot.h
      PreviewDCompSurface.*  # Becomes RenderView in Phase 3
      PreviewDCompPhase0Smoke.cpp

  timeline/                  # Editor timeline strip — option-A scope
                             # expansion: Phase 2/3 will add timeline
                             # IPreviewSources + a second RenderView.
    TimelineView.*
    TimelineView.*.cpp
    TimelineSceneState*
    TimelineNoteAssets.*
    quick/                   # legacy QSG layer items
      TimelineQuickItem.*
      TimelineQuick*Layer.*

  tools/                     # Standalone helpers + spec/probe targets
    latency/
    muri/
    probe/
    timeline/
    video_export/
```

## Conventions
- `app/` is for app entry and window orchestration only.
- `core/scene/` owns pure frame-state math and per-layer descriptor
  building. No QSG / D3D11 dependencies.
- `core/chart/` owns simai parsing, transforms, normalisation. No
  scene / runtime dependencies.
- `core/video/` owns libmpv-related code and shared render settings.
- `audio/` owns audio backends; nothing else may link BASS or
  miniaudio directly.
- `render/` owns the D3D11 / DirectComposition rendering pipeline.
  Phase 2 introduces `IPreviewSource` + `Compositor` here; Phase 3
  introduces `RenderView`.
- `preview/` is **legacy** — Phase 2/3 deletes most of it as sources/
  takes over. Don't add new code here.
- Prefer existing second-level folders instead of creating parallel
  aliases.
