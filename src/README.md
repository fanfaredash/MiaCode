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

  editor/                    # Chart text editing, widget-free. The editor itself
                             # is QML (app/qml_ui/editor); what lives here is the
                             # text policy and syntax it shares.
    SimaiTextEditPolicy.*
    SimaiCompletionCatalog.*
    BookmarkCommentSyntax.*
    TouchPadAuthoringEdit.*
    BracketScopeHighlighter.*

  extensions/                # Archive-only extension manifest/schema contract
    ExtensionManifest.*

  preview/                   # Current QSG preview/runtime implementation
    quick_scene/             # QSG-based chart layer renderers
    runtime/                 # PreviewRuntime and stage-media integration

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
`quick`, or `widget` is enough to communicate what runs by default.

Current defaults:

- GUI shell: `QmlUiBootstrap` (`src/app/qml_ui/`) is the single GUI startup
  path from `src/app/main.cpp`. There is no alternate shell and no
  native-surface re-hosting; the v1 QuickShell shell and its style bridge
  were removed on 2026-08-25.
- Preview chart rendering: Qt Quick/QSG through `preview/runtime/PreviewRuntime`
  and `preview/quick_scene/*`. This is the only preview renderer. The D3D11 /
  DirectComposition backend that used to live in `render/` and `sources/` was
  deleted on 2026-08-07, together with its six `MIACODE_*_DCOMP*` flags;
  `--quick-shell-beta` is now an inert argument.
- Timeline rendering: the default QuickShell timeline is
  `timeline/quick/TimelineQuickItem` and its QSG layers. `TimelineView` remains
  the widget reference/helper surface.
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

## Extension archive notes

The v1 extension manifest, permission enum, schema, SDK declarations, bundled
example, and API registry remain in the repository as an archive/specification
and offline validation surface. The product no longer creates an extension
host, embedded JavaScript runtime, Open Bridge, extension watcher, extension
preferences page, or bundled extension deployment directory. Consequently,
`ExtensionManifest.*` is the only extension implementation compiled into the
product; it must stay aligned with the archived schema and documentation via
`tools/extensions/check-extension-consistency.mjs`.

Do not add product runtime calls to the archive-only API registry. A future
extension host would need a separately approved ownership and dependency
decision before restoring any of those surfaces.

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
- `preview/` owns the QSG preview/runtime path. New preview-rendering work
  uses shared `core/scene` state plus dedicated `preview/quick_scene` layers.
  Do not introduce a second native render backend.
- Prefer existing second-level folders instead of creating parallel aliases.
