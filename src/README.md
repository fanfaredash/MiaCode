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
- `preview/` keeps exactly three categories: `audio`, `video`, `layout`.
- `simai/parser` stores parser entry, parser internal fragments, and native dump tooling.
- Prefer existing second-level folders instead of creating parallel aliases.
