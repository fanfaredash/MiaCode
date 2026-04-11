# `src` Directory Layout (Current)

This repository is organized by module responsibility.
Except for entry files, keep sources in second-level folders.

```text
src/
  app/
    main.cpp
    AppVersion.h.in
    mainwindow/
    ui/

  common/

  editor/
    PlainCodeEditor.*
    BracketScopeHighlighter.*

  preview/
    audio/
      PreviewAudioSettings.*
      QtPreviewSfxRuntime.*
      QtPreviewSfxRuntime.*.cpp
    video/
      PreviewRenderSettings.h
    scene/
      PreviewFrameState.h
      Preview*LayerState.*
      PreviewOpacityCurves.*
      PreviewScene*.*
      PreviewSkinSelectors.*
      PreviewTrackShared.*
    quick_scene/
      PreviewQuick*Layer.*
      PreviewQuick*Nodes.*
      PreviewQuickSceneRoot.*
      PreviewTextureRepository.*
    runtime/
      PreviewRuntime.*
      PreviewQuickExportSession.*
      PreviewSceneAssetLoader.*
      PreviewSceneAssetRepository.*

  simai/
    document/
      SimaiDocument.*
    parser/
      SimaiNativeParser.*
      SimaiNativeParser.*.cpp
      SimaiNativeDump.cpp

  timeline/
    TimelineView.*
    TimelineView.*.cpp

  tools/
    latency/
      LatencyDetectorDialog.*
      LatencyDetectorDialog.*.cpp
    video_export/
      VideoExportDialog.*
      VideoExportController.*
    probe/
      SoundtouchProbe.cpp
```

## Conventions
- `app/` is for app entry and window orchestration only.
- `preview/scene` owns pure frame-state math and per-layer descriptor building.
- `preview/quick_scene` owns QSG/QQuick rendering only.
- `preview/runtime` owns live/runtime sessions, asset ownership, and headless Quick export session wiring.
- `preview/video` is now limited to media transport and shared preview render settings.
- `simai/parser` stores parser entry, parser internal fragments, and native dump tooling.
- Prefer existing second-level folders instead of creating parallel aliases.
