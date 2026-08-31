# Architecture & Layout

Module layout, dependency direction, the render-architecture decision, must-keep design
contracts, and the god-file watch-list. Pair with `feature-index.md` (feature → file map).

## 1. Module layout (current, post "first-unification" reorg)

```text
src/
  app/            App entry + window orchestration ONLY
    main.cpp        GUI boot, CLI export, export-worker entry, startup timing
    mainwindow/     MainWindow + sections/<feature>/ (partial-class slices).
                    Shrinking: it BORROWS the v2 services, never owns them.
                    Deleted entirely in stage 4 — do not add new state here.
    v2/             ApplicationServices — the non-Widget owner of the document
                    domain and the shared UI boundaries: ChartWorkspace,
                    ChartWorkspaceFileService, AnalysisService,
                    EditorSyncController, ChartDropImportService,
                    UiRequestService, JobProgressService,
                    PreviewAppearanceState. Constructed BEFORE MainWindow and
                    destroyed after it. Nothing else in src/app may construct
                    one of these — application_services_spec scans the tree and
                    fails if it does.
                    ExportEngine (export page) and EditorPageRouter (page
                    switching) are contracts the QML layer names instead of
                    MainWindow; the assembly holds only their SLOTS because both
                    implementations are still the window. The window installs itself and
                    withdraws at the top of ~MainWindow, and consumers bind to
                    exportEngineSlot() rather than snapshotting the pointer.
                    PreviewAppearanceState holds the eight values that decide
                    how BOTH the live preview and the exported video are drawn
                    (skin dir/variant, judge effect, outline, slide-earlier, tap
                    judge distance, center display, intro sound). It owns values
                    only — applying them to the live surfaces and persisting
                    them is MainWindow reacting to skinChanged /
                    judgeEffectStyleChanged / introSoundChanged. MainWindow
                    binds its same-named members to values() by reference;
                    writes through values() are deliberately silent so restore
                    paths do not look like user edits.
    qml_ui/         The default (v2) shell: QmlUiBootstrap, QmlApplicationContext,
                    the Qml*Model façades and the MiaCode.UI QML module.
    quick_shell/    Preview-surface / popup-position / keyboard-activation policy
                    headers only. QuickShellController and QuickShellContracts
                    were deleted 2026-08-30; the v1 QML shell, native-surface
                    re-hosting and style bridge went 2026-08-25, and `--ui=v1`
                    no longer exists. What remains is preview-composition
                    plumbing that leaves with MainWindow in stage 4.
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
- `app/v2/` must stay **Qt Widgets-free** and must not include `mainwindow/`. The direction is
  one-way: `mainwindow/` and `qml_ui/` reach into `app/v2/`, never the reverse. This is enforced
  by linkage, not review — `application_services_spec`, `ui_request_service_spec` and the other
  v2 specs link `Qt6::Core` (+`Gui`/`Test`) only, so a QtWidgets include fails the build.
- `audio/` is the only place allowed to link BASS / miniaudio.
- `tools/*` are standalone (each spec/dump/probe builds against a minimal source subset).
- **The QML layer's reach into `MainWindow` is a tracked, monotonic number.**
  `docs/specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md` enumerates every name
  `src/app/qml_ui/` uses on the hidden window, and `qml_ui_backend_surface_spec` holds code and
  doc to set equality — new coupling fails, and migrating something without deleting its row
  fails too. Update the doc in the same commit as the work. Prefer removing a `friend` grant or a
  private-member read over shaving a method: a friend grant is unbounded access, so trading it
  for named public calls is progress even when the method count rises. When the thing the QML
  layer wants is a whole capability rather than a value (the export engine, page switching),
  declare an interface in `src/app/v2/` and let MainWindow implement it — publishing those
  entry points would formalize widget-era mechanics that are supposed to disappear. As of
  2026-09-01 no QML type is a `friend` of MainWindow.
- **Every library `MiaCode` links is on an allowlist**: `docs/ops/DEPENDENCY_ALLOWLIST.md`, guarded
  by `dependency_allowlist_spec`. Adding a dependency without a doc row fails the suite, and so
  does leaving a stale row behind. `src/tools/net` is deliberately NOT in the product target — it
  compiles only under `net_client_spec`, which is what keeps `Qt6::Network` out of MiaCode.

## 3. Render-architecture decision (2026-05-29, updated 2026-08-07)

See `SKILL.md` for the canonical statement. Summary:

- **KEEP (main):** in-process QSG — `PreviewRuntime` → `PreviewQuickSceneRoot`, `core/scene/*`,
  `preview/quick_scene/*`. Realtime preview + export share it.
- **DELETED (2026-08-07):** DComp/D3D11. `render/*` and `sources/*` are gone, along with all
  six `MIACODE_*_DCOMP*` flags and the `dcomp` link library. Windows still links `d3d11` +
  `dxgi`; MinGW also links `d3dcompiler` for QtAVPlayer `D3DCompile`. It had been
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
- Realtime preview BGM timing is backend-owned: Windows uses BASS/BASS_FX for all rates;
  non-Windows uses the stretched SoundTouch path with an engine-time anchor clock.
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
| `core/chart/parser/SimaiNativeParser.cpp` | — | `#include "*.cpp"` unity split (`:1584`) → 真正 multi-TU, or rename includes to `.inc`/`.ipp` |
| `core/chart/transform/ChartNormalization.cpp`, `preview/runtime/PreviewRuntime.cpp`, `timeline/TimelineSceneStateBuilder.cpp`, … | 1.0–1.7k (~30 files) | never deep-audited (the blind-spot tier — doc §6B); audit before splitting |

## Update this file when

- A second-level folder is added/renamed, or a file moves between modules.
- A dependency rule changes, or the render-architecture decision changes.
- A flexible default hardens into a contract (move it into §4), or a contract is relaxed.
