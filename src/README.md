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
      PreviewCanvas.*
      PreviewCanvas.*.cpp
      PreviewGLRenderer.*
      PreviewMediaController.*
    layout/
      PreviewIntegration.*

  simai/
    document/
      SimaiDocument.*
    parser/
      SimaiNativeParser.*
    native/
      SimaiNativeParser.*.cpp
      SimaiNativeDump.cpp

  timeline/
    TimelineView.*
    TimelineView.*.cpp

  tools/
    latency/
      LatencyDetectorDialog.*
      LatencyDetectorDialog.*.cpp
    probe/
      SoundtouchProbe.cpp
```

## Conventions
- `app/` is for app entry and window orchestration only.
- `preview/` keeps exactly three categories: `audio`, `video`, `layout`.
- `simai/native` stores parser internal fragments and native dump tooling.
- Prefer existing second-level folders instead of creating parallel aliases.
