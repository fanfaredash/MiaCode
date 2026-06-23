# Architecture & Layout

Module layout, dependency direction, the render-architecture decision, must-keep design
contracts, and the god-file watch-list. Pair with `feature-index.md` (feature → file map).

## 1. Module layout (current, post "first-unification" reorg)

```text
src/
  app/            App entry + window orchestration ONLY
    main.cpp        GUI boot, CLI export, export-worker entry, startup timing
    mainwindow/     MainWindow + sections/<feature>/ (partial-class slices)
    quick_shell/    --quick-shell-beta QML shell + controller/style bridges
    ui/             UiText, UiTheme, ShortcutRegistry, WindowParityMetrics
  audio/          Audio backends + SFX runtime. Nothing else links BASS/miniaudio.
  common/         Cross-module utilities, debug logging, shared config headers
  core/
    chart/        simai parsing + transforms (was src/simai/). No scene/runtime deps.
      document/     SimaiDocument, SimaiTimingMetadata
      parser/       SimaiNativeParser* (include-split), SimaiNativeDump
      transform/    ChartBatchTransform, ChartNormalization, Non384SnapTable
    scene/        Pure frame-state math + per-layer descriptors (was preview/scene/).
                  NO QSG / D3D11 deps. Preview*LayerState, PreviewFrameState, etc.
    video/        Shared render settings only (PreviewRenderSettings.h). Tiny.
  editor/         In-app chart text editor (PlainCodeEditor, BracketScopeHighlighter)
  preview/
    quick_scene/  ACTIVE QSG chart layer renderers (PreviewQuick*Layer, *SceneRoot)
    runtime/      PreviewRuntime, PreviewQuickExportSession, PreviewStageMediaHost,
                  PreviewSceneAsset*
  render/         DComp/D3D11 rendering (DEFAULT OFF — being decoupled)
    PreviewDCompRenderer.*
    backend_d3d11/  PreviewDCompCore/Surface/SpritePipeline/TextureCache, TimelineRenderView
  sources/        OBS-style IPreviewSource feed for the DComp compositor (DEFAULT OFF)
    chart/          *Source (StageBackground, Track, Head, Slide, Judge, ...)
    timeline/       Timeline*Source
  timeline/       Editor timeline strip: data + QSG surface
    quick/          TimelineQuick*Layer / TimelineQuickItem
  tools/          Standalone helpers + spec/probe targets
    latency/ muri/ video_export/ chart_transform/ probe/ editor/ preview/ ...
  wrapper/        MiaCodeLauncher.cpp (Windows dist launcher)
```

Path-translation table for stale docs/comments:

| Old path (do not use) | Current path |
|---|---|
| `src/simai/document/` | `src/core/chart/document/` |
| `src/simai/parser/` | `src/core/chart/parser/` |
| `src/simai/transform/` | `src/core/chart/transform/` |
| `src/preview/scene/` | `src/core/scene/` |
| `src/preview/audio/` | `src/audio/` |
| `src/preview/video/PreviewMediaController` | **removed** — widget-shell bg media now via `PreviewRuntime` stage-background + `PreviewStageMediaHost` |

## 2. Dependency direction

- `core/chart/` and `core/scene/` are the domain core: no dependency on `app/`, `preview/`,
  `render/`, or Qt Widgets. `core/scene/` must stay GPU-free (no QSG / D3D11 includes).
- `app/` (UI) depends on core; core never depends on UI.
- `audio/` is the only place allowed to link BASS / miniaudio.
- `preview/quick_scene/` (QSG) and `render/` (DComp) are two parallel renderers that both
  consume frame state. Keep them independent; do not let one include the other's headers.
- `tools/*` are standalone (each spec/dump/probe builds against a minimal source subset).

## 3. Render-architecture decision (2026-05-29)

See `SKILL.md` for the canonical statement. Summary:

- **KEEP (main):** in-process QSG — `PreviewRuntime` → `PreviewQuickSceneRoot`, `core/scene/*`,
  `preview/quick_scene/*`. Realtime preview + export share it.
- **KEEP but OFF + decouple:** DComp/D3D11 (`render/*` + `sources/*`). `previewUseDCompEnabled()`
  defaults `false` (`src/common/DebugOptions.h:194`). End state: zero coupling to the QSG build.
- **DELETED (2026-06-02):** the out-of-process worker (`preview/ipc/*`,
  `PreviewWorkerSession`/`Supervisor`, `MIACODE_PREVIEW_OUT_OF_PROCESS`, `MIACODE_PREVIEW_WORKER_*`)
  is gone. Do not reintroduce.
- `src/README.md` predates this and frames DComp/sources as the future — superseded.

Why two scene stacks exist (so you don't "fix" the wrong one): the QSG path uses
`core/scene/*LayerState` → `preview/quick_scene/PreviewQuick*Layer`. The DComp path uses
`sources/*Source` → `render/compositor` → `render/backend_d3d11/*`. They do not share layer
code. Mainline features go in the QSG path.

## 4. Must-keep design contracts

(Ported from the prior design ledger; verified against current code where noted.)

- `MainWindow` is orchestration, not the home for every feature body. New window features land
  in `src/app/mainwindow/sections/<feature>/`.
- `SimaiDocument` is the editable storage model for metadata + difficulty text.
- Parser output is the shared intermediate representation for timeline, preview, Muri analysis,
  and export reconstruction.
- Runtime SFX and export SFX must use the same note-to-sound semantics (see
  `cross-chain-linkage.md` §4).
- `&first` is stored as raw document data; timing is applied through getters + marker shifting,
  not ad-hoc inversion scattered around the code.
- Export uses a snapshot/worker boundary (`VideoExportSnapshot::toJson/fromJson`) rather than
  mutating UI state from the export process. (This is the *export* worker — distinct from the
  deprecated *preview* worker.)
- New preview/export rendering work adds a `core/scene/` state builder or a
  `preview/quick_scene/` layer; do not reintroduce a painter/OpenGL fallback path.
- Realtime preview BGM timing is backend-owned: Windows uses BASS/BASS_FX for all rates;
  non-Windows uses the stretched SoundTouch path with an engine-time anchor clock.
- Asset lookup is file-based and convention-driven, not database-driven.

## 5. God-file / structure watch-list

**2026-06-19 god-file split (branch `refactor/god-file-split`)** — 13 product-code god-files
decomposed into focused translation units, byte-faithful (sed moves, zero logic change), each
gated on build + CTest 25/25. The full methodology and rollback notes are kept as maintainer-only
local archives.

Conventions established by that pass — reuse, don't reinvent:
- A single class's method bodies split across several `.cpp` (the `MainWindow.<Section>.cpp`
  partial pattern) is the default; the class's public `.h` does not change.
- Stateless file-local helpers/constants shared by >1 split TU → a per-file `<Base>.Internal.h`
  in a named `…_detail` namespace (`inline`/`inline constexpr`); each TU `#include`s it +
  `using namespace …_detail;`. (Was an anonymous namespace = internal linkage = link error if moved.)
- Mutable shared file-static state → `extern` in the internal header, defined in exactly ONE TU.
- A file-local **type** (nested struct / inner class) used by >1 TU → move its definition to the
  shared header (e.g. `PlainCodeEditor::LineNumberArea`, `BassPreviewAudioBackend::Sample`).
- Free-function god-files → named `detail` namespace + prototypes in the internal header, defs
  split across group TUs; **default arguments live in the header prototype only** (ODR).
- CMake source lists are explicit (no glob): register every new `.cpp` in `target_sources(MiaCode …)`
  AND every dev-tool `miacode_add_dev_tool(… SOURCES …)` that also lists the original.

Done — do NOT regrow these (add a new focused unit instead):

| File (now) | was | result |
|---|---|---|
| `tools/video_export/VideoExportController.cpp` | 5144 | → 94 (`exportFullPreview`) + `VideoExportControllerInternal.h` + `VideoExportEncoder/Diagnostics/FrameRender/Pipeline/PreparedTask.cpp`. The 2017-line `exportPreparedTask` + `ExportTempDirRegistry` live in `…PreparedTask.cpp` — that TU stays large until the *method itself* is decomposed (a design change, not a TU split). |
| `tools/video_export/VideoExportDialog.cpp` | 3105 | → core (ctor + file-local `TimestampSpinBox`/`ExportRangeTrack` + range/preview cluster) + `.SettingsPersistence/.IntroControls/.ExportFlow` + `Internal.h` |
| `preview/runtime/PreviewStageMediaHost.cpp` | 3335 | → core (ctor/dtor) + `_Backend/_Media/_Playback/_Diagnostics/_Timeout` + `Internal.h` (the `isIntegratedRenderAdapter` static cache stays single-TU; dual `MIACODE_USE_QTAVPLAYER`/`HAVE_QT_MULTIMEDIA` `#ifdef` paths preserved) |
| `audio/BassPreviewAudioBackend.cpp` | 2487 | → core + `_EngineInit/_Assets/_Transport/_PlaybackClock/_EventDrain` + `…Impl.h` (`extern gBassDeviceRefCount`) + `…Sample.h` (nested `Sample` shared across 6 TUs incl. the dtor) |
| `timeline/TimelineQuickModel.cpp` | 2614 | → core + `…Parser/…Snapshot/…Indexing` + `TimelineQuickModelPrivate.h` (also fed `timeline_model_spec`) |
| `core/chart/transform/ChartBatchTransform.cpp` | 2302 | → `.Parsers/.Subdivision/.Selection/.Transform` + `…Internal.h` (`MC_OP` via `OperationLog.h`; also fed `chart_batch_transform_spec`) |
| `editor/PlainCodeEditor.cpp` | 2019 | → `.Layout/.HighlightAndCaret/.BracketCompletion/.Input/.Bookmarks` + `.Internal.h` (`LineNumberArea` moved to header; also fed `plain_code_editor_spec`) |
| `app/main.cpp` | 1864 | → core (`main`) + `startup_diagnostics_win32/graphics_backend/cli_shared/cli_video_export/cli_video_export_worker.cpp` + `MainEntrypoints.h` (`miacode::app::entry`) |
| `app/mainwindow/sections/{dialogs,document,timeline}/` — the 5 biggest section files | 1.8–3.5k each | each sub-split into more `MainWindow.<Section>.cpp` partial slices (Dialogs→AudioSettings/MediaTools/TrackMetadata/ExportSettings; TimelinePlayback→PreviewSeek/PreviewPlaybackState/PreviewIntroRegion/PreviewTick; DocumentFlow→FileFlow/DesignerFlow/AutosaveFlow; PreviewTimelineFlow→3; TimelineLayout→4) + per-file `Internal.h` where helpers are shared |
| `tools/muri/MuriAnalyzer.cpp` | ~1300 (was 4600) | (earlier) `analyze()` thin orchestrator over `miacode::muri::detail` stage TUs — don't regrow. History: `.claude/MURI_DECOMPOSITION_HANDOFF.md` |

Still standing / out of scope of the 2026-06-19 pass:

| File | ~lines | Note / direction |
|---|---|---|
| `app/mainwindow/MainWindow.h` + remaining `sections/*` | 176 methods; ~11 sections still 1.0–1.7k | god class sliced by friend partials; further reduction = promote sections to state-owning cooperators (design change) |
| `app/quick_shell/qml/QuickShellMain.qml` | 1582 | **SUGGEST-only**: extract a C++ surface-routing controller + layout engine first, then split QML — 38 root props + timers + layout call-chain are too coupled to move mechanically |
| `render/backend_d3d11/PreviewDComp{Surface,SpritePipeline}.cpp` | ~1.8k each | DEFAULT-OFF, being decoupled — split deferred (low value, may be deleted) |
| `core/chart/parser/SimaiNativeParser.cpp` | — | `#include "*.cpp"` unity split (`:1584`) → 真正 multi-TU, or rename includes to `.inc`/`.ipp` |
| `core/chart/transform/ChartNormalization.cpp`, `preview/runtime/PreviewRuntime.cpp`, `timeline/TimelineSceneStateBuilder.cpp`, … | 1.0–1.7k (~30 files) | never deep-audited (the blind-spot tier — doc §6B); audit before splitting |

## Update this file when

- A second-level folder is added/renamed, or a file moves between modules.
- A dependency rule changes, or the render-architecture decision changes.
- A flexible default hardens into a contract (move it into §4), or a contract is relaxed.
