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

  extensions/                # Local extension host, Open Bridge registry,
                             # manifest/permission loader, embedded JS runtime
    ExtensionManifest.*
    ExtensionOpenBridge.*
    ExtensionManager.*
    EmbeddedExtensionRuntime.*

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

## Extension Open Bridge Notes

The extension system has two public-facing layers that must stay aligned:

- Stable SDK objects in `EmbeddedExtensionRuntime.cpp`, such as
  `miacode.preview`, `miacode.timeline`, `miacode.document`, and
  `miacode.ui`.
- The Open Bridge registry in `extensions/ExtensionOpenBridge.cpp`, exposed as
  `miacode.open.list()`, `miacode.open.describe(...)`, and
  `miacode.open.call(...)`.

When adding extension-facing capability, prefer adding or updating an Open
Bridge facade descriptor first, then make any SDK convenience wrapper delegate
to that same host route. Do not create a second policy path with different
permissions or statuses.

Important marker semantics:

- `stability: "open"` means the object is a stable facade. It is still a
  controlled host surface, not a raw C++ pointer.
- `stability: "experimentalRaw"` means the object is intentionally exposed as
  raw/internal/unstable capability. It is a warning and compatibility marker,
  not a block.
- `experimentalRaw: true`, `rawAccess: true`, and
  `rawCppObjectsExposed: true` are metadata for callers and docs. They do not
  by themselves reject calls.
- `forbidden: false` on legacy raw-target descriptors means the old
  `forbiddenTargets()` discovery name is kept for compatibility, but the target
  is no longer treated as a hard-denied object.

The actual hard-block switches live in `ExtensionManager.cpp`:

- `isPermanentlyBlockedApiMethod(...)`
- `isBlockedPermission(...)`
- `extensionIsForbiddenOpenTarget(...)` in `ExtensionOpenBridge.cpp`

The current policy is "open with experimental raw metadata": these checks do
not reject raw targets by name. If that policy changes, update
`resources/extensions/README.md`, `docs/specs/extensions/EXTENSION_SYSTEM_V1.md`,
`packages/miacode-extension-api/index.d.ts`, and
`tools/extensions/check-extension-consistency.mjs` in the same change.

Experimental raw targets are discoverable and callable through Open Bridge.
`shell.execute` and `process.spawn` are also wired as implemented public
registry entries and start detached processes when the extension declares the
required raw permission. Raw namespaces that do not expose a concrete native
object yet still return an experimental accepted descriptor; treat that as
"the raw bridge is open and marked unstable", not as a permission block.

The extension registry currently has no `planned` entries. Event/provider/export
hook registration, sidebar-style views, preferences-style views, and
media/theme/backup/shortcut facades are implemented host routes. If a future
descriptor is added as `planned`, it must remain clearly non-callable until the
host route exists.

Event registration is not descriptor-only. The embedded runtime stores event
callbacks with the registering extension id, and `ExtensionManager` dispatches
document/save/timeline/preview events after successful public host calls that
change those states. Keep callback execution under the registering extension's
permission context.

`miacode.devtools` is the supported diagnostic facade. It reports API/Open
Bridge descriptors, diagnostics, registered event callback count, UI
contribution state, and the recent host-call ring buffer. It must remain an
inspection surface only; it must not expose raw QWidget/QML/QObject/renderer
pointers or bypass manifest permission checks.

The same inspection data is exposed in the app through **Preferences >
Extensions > DevTools Panel**. That window reads `ExtensionManager`'s host
snapshot directly and shows API Registry, Open Bridge, Recent Calls,
Extensions, UI Contributions, Diagnostics, and Raw JSON tabs.

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
