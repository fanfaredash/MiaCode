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

## 5. God-file / structure watch-list (audit 2026-05-29)

Do not grow these; refactor or add new units instead.

| File | ~lines | Problem | Refactor direction |
|---|---|---|---|
| `src/tools/video_export/VideoExportController.cpp` | 5000 | 97-line header, monster procedural .cpp | split EncoderSelection / FfmpegPipeline / FrameRenderLoop / ExportDiagnostics |
| `src/tools/muri/MuriAnalyzer.cpp` | ~1300 (was 4600) | **decomposition ✅ done** — `analyze()` is now a ~187-line thin orchestrator over per-stage TUs in `miacode::muri::detail` (Geometry/Model/SlideReferenceData/RuntimeModelBuilder/OverlayBuilder/SlideWifiJudge/SimpleNoteJudge/DiagnosticLabels/DiagnosticCollector). What remains is cross-stage shared primitives (Internal.h-declared) + the orchestrator — don't regrow it. Optional further splits + history: `.claude/MURI_DECOMPOSITION_HANDOFF.md` |
| `src/app/mainwindow/MainWindow.h` + `sections/*` | 176 methods | god class sliced by `#include`/friend, not real components | promote sections to state-owning cooperators |
| `src/app/main.cpp` | 2400 | GUI + CLI + export + worker + startup all in one | split CLI/export/worker entry points |
| `src/core/chart/parser/SimaiNativeParser.cpp` | — | `#include "*.cpp"` unity split (`:1584`) |真正 multi-TU, or rename includes to `.inc`/`.ipp` |

## Update this file when

- A second-level folder is added/renamed, or a file moves between modules.
- A dependency rule changes, or the render-architecture decision changes.
- A flexible default hardens into a contract (move it into §4), or a contract is relaxed.
