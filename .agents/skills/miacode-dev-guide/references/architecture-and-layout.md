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
  or Qt Widgets. `core/scene/` must stay GPU-free (no QSG / D3D11 includes).
- `app/` (UI) depends on core; core never depends on UI.
- `audio/` is the only place allowed to link BASS / miniaudio.
- `tools/*` are standalone (each spec/dump/probe builds against a minimal source subset).

## 3. Render-architecture decision (2026-05-29, updated 2026-08-07)

See `SKILL.md` for the canonical statement. Summary:

- **KEEP (main):** in-process QSG — `PreviewRuntime` → `PreviewQuickSceneRoot`, `core/scene/*`,
  `preview/quick_scene/*`. Realtime preview + export share it.
- **DELETED (2026-08-07):** DComp/D3D11. `render/*` and `sources/*` are gone, along with all
  six `MIACODE_*_DCOMP*` flags and the `dcomp` / `d3dcompiler` link libraries. It had been
  default-off since beta34, so it cost ~11k lines and a parallel branch in every preview and
  timeline paint path while shipping to nobody. Do not reintroduce a second render backend.
- **DELETED (2026-06-02):** the out-of-process worker (`preview/ipc/*`,
  `PreviewWorkerSession`/`Supervisor`, `MIACODE_PREVIEW_OUT_OF_PROCESS`, `MIACODE_PREVIEW_WORKER_*`)
  is gone. Do not reintroduce.
- `src/README.md` predates this and framed DComp/sources as the future — superseded.

There is now exactly one scene stack: `core/scene/*LayerState` →
`preview/quick_scene/PreviewQuick*Layer`. Older docs and commit messages describe a second
`sources/*Source` → `render/compositor` → `render/backend_d3d11/*` path; that is history.

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
- Realtime preview BGM timing is backend-owned: Windows/macOS BASS builds use BASS/BASS_FX for
  all rates; builds without BASS use the stretched SoundTouch path with an engine-time anchor clock.
- `QtPreviewSfxRuntime` is a GUI-thread facade only. Every native preview-audio construction,
  backend call, health sample, and destruction belongs to the in-process `PreviewAudioWorker`
  `std::thread`; GUI code submits owned value commands and reads immutable snapshots.
- Worker completions cross one typed acceptance boundary: playback results require the current
  generation plus transaction, asset-ready results require the current asset generation, and a
  device pause also requires its immutable pause token. Do not add one-off completion checks in
  MainWindow call sites.
- A real output-device change freezes GUI/video progress before its reserved worker pause is
  submitted. It is an explicit, permanent pause: only a later user Play action may resume it.
  Never turn device recovery into automatic playback recovery.
- `PreviewBassDeviceLease` serializes the process-wide `BASS_GetDevice`, `BASS_Init`, and final
  `BASS_Free` lifetime edges only. It must not wrap ordinary channel/mixer work or extend the
  worker lock scope around native callbacks.
- Asset lookup is file-based and convention-driven, not database-driven.
- **UI localization has ONE inline entry point: `UiText::localized(en, zh, ja = {})`**
  (`src/app/ui/UiText.h`). Simplified Chinese is the reference language. Do NOT reintroduce the
  scattered patterns the 2026-07-07 audit removed (`isChineseUi() ? zh : en` ternaries, per-file
  `l10n`/`trText`/`localizedText` helpers) — those only ever did zh/en and silently fell back to
  English for every other language. Rules:
  - Key-value strings (menus, common dialogs): `UiText::text(key)` with matching entries in BOTH
    `zhMap` and `jaMap` (`UiText.cpp`). The key sets must stay identical.
  - Inline strings in feature code: `UiText::localized(en, zh)`. Japanese is filled from the
    central zh-keyed dictionary `src/app/ui/UiTextJaDictionary.cpp` (translate from the Chinese).
    Pass an explicit third `ja` arg only for a one-off that shouldn't live in the dictionary.
  - Parser validation messages: `SimaiNativeValidationLocale` (English/Chinese/Japanese); derive
    it from `UiText::resolvedLanguage()` via `miacode::mainwindow::shared::uiValidationLocale()`.
    The zh/ja maps live in `SimaiNativeParser.Driver.cpp`.
  - QML: the palette bridge exports `uiLanguage` ("en"/"zh"/"ja"); QML uses a local
    `localized(en, zh, ja)` helper (see `BottomTabsQuickHost.qml`). No `qsTr` (no `.ts` shipped).
  - CJK UI font candidates per language live in `MainWindowShared.cpp` (`uiFont`/`uiAccentFont`)
    and `main.cpp`; add families when adding a language.
  - `ui_text_locale_spec` (CTest) enforces zhMap/jaMap key parity AND that every inline
    `localized(en, zh)` zh string has a dictionary entry. Full rationale:
    `docs/audit/I18N_AND_UI_COMPONENT_AUDIT_ZH.md`.

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
| `audio/BassPreviewAudioBackend.cpp` | 2487 | → core + `_EngineInit/_Assets/_Transport/_PlaybackClock/_EventDrain` + `…Impl.h` + `…Sample.h` (nested `Sample` shared across 6 TUs incl. the dtor); `PreviewBassDeviceLease` owns the process-wide BASS device lifetime outside the split TUs |
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
| `core/chart/parser/SimaiNativeParser.cpp` | — | `#include "*.cpp"` unity split (`:1584`) → 真正 multi-TU, or rename includes to `.inc`/`.ipp` |
| `core/chart/transform/ChartNormalization.cpp`, `preview/runtime/PreviewRuntime.cpp`, `timeline/TimelineSceneStateBuilder.cpp`, … | 1.0–1.7k (~30 files) | never deep-audited (the blind-spot tier — doc §6B); audit before splitting |

## Update this file when

- A second-level folder is added/renamed, or a file moves between modules.
- A dependency rule changes, or the render-architecture decision changes.
- A flexible default hardens into a contract (move it into §4), or a contract is relaxed.
