---
name: miacode-dev-guide
description: Repository guide for MiaCode (a Qt6/C++/QML maimai chart editor + video exporter). Use when working anywhere in this repo to locate a feature's owning files/classes/functions; understand the module layout (core/app/preview/audio/timeline/tools) and dependency rules; follow cross-module sync pairs (parser ↔ timeline ↔ preview ↔ export ↔ Muri); find debug flags and the --debug logging system; follow build/target/CTest/packaging conventions; or check the committed render-architecture decision (in-process QSG is the only render path; the DComp/D3D11 backend and the out-of-process worker have both been removed). Also update this skill in the same change whenever repo structure, cross-module contracts, debug flags, or build conventions change.
---

# MiaCode Dev Guide

The repo memory layer for MiaCode. Start from the user-facing feature, map it to the
owning entry point, then open only the reference file you need. **Code is the source of
truth.** When code and any reference disagree, trust the code and fix the reference in the
same change.

## Route the task

- Module layout, dependency rules, the render-architecture decision, must-keep design
  contracts, and god-file watch-list → `references/architecture-and-layout.md`
- Map a feature to its files / classes / functions → `references/feature-index.md`
- Behavior mirrored across parser / timeline / preview / audio / export / Muri →
  `references/cross-chain-linkage.md`
- Constants, thresholds, magic-value ownership, promote-vs-keep-local → `references/hardcode-registry.md`
- Debug switches, the `--debug` logging system, log channels, env-var index → `references/debug-and-logging.md`
- Build configs, targets, the `MIACODE_BUILD_DEV_TOOLS` spec convention + CTest, scripts,
  assets, packaging, helper binaries → `references/build-and-tools.md`
- UI layout bugs — 边界被吞 / clipped borders, tab 页被裁剪, dark-mode 失效, 1px 缝隙/接缝,
  层叠关系错误 / stacking, hit-area mismatch → use the **`qt-ui-layout-pitfalls`** skill
  (symptom routing table + proven recipes) BEFORE editing any dialog/QML layout code.

## Repo at a glance (current paths — verified 2026-05-29)

- App boot + CLI export + export-worker entry: `src/app/main.cpp`
- Main window orchestration: `src/app/mainwindow/` (+ `sections/<feature>/`)
- Default UI (**v2**): `src/app/qml_ui/` (`QmlUiBootstrap`, phase-1 checklist:
  `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`). Export uses `QmlExportSession` +
  `ExportVideoPage.qml`; cover export opens `export/QmlCoverExportWindow` + `CoverExportWindow.qml`.
  Each window owns its QML engine, `QmlCoverExportSession`, UI requests, image provider and render
  resources; closing destroys them after any active capture finishes. `CoverExportPage.qml` is
  its content and `tools/cover_export/CoverCompositeRenderer` owns final composition.
  `QmlEditorPageHost` emits the window request while retaining the main page.
  `QuickShellController` is deleted. `UiText.qml` is QML's sole visible-string
  entry and must be used instead of `qsTr`. `QmlDocumentModel` submits body, metadata, difficulty and file operations
  to the v2 workspace; QML validation/Muri reads the workspace analysis service. Windows title
  bar: `QmlUiWindowChrome`.
- QML popup ownership: `AppComboBox` retains ComboBox selection/model behavior;
  `AppMenu` retains Menu actions/submenus. Menu widths use complete row implicit widths (mnemonic label,
  shortcut, indicators and padding); separators follow the menu width. Combo popups
  measure the longest option on opening. Both cap width to the window Overlay.
  Both own their transitions and interrupted-close state directly, keeping lifecycle
  callbacks within the popup object. `AppStickyPopup` inherits
  `AppDropdownPanel`; floating backgrounds and row states use `FloatingCard` and
  `HoverChrome`. `AppDialog` owns window-overlay placement, viewport-bounded preferred
  sizing, permanent centering, direct transition lifecycle, a scrollable `body`, and frosted chrome.
  Dialogs are stationary. Settings panels use the shared 560px preferred height; compact
  dialogs use 280px; `ChoiceDialog` notices fit their natural content height. The window
  bounds cap all dialogs. `fillBody` is enabled for settings
  with stretch content and virtualized queues; ordinary forms retain natural body height.
  `DialogFooter` owns wrapping action rows for settings, choices and progress. All app
  notices use `ChoiceDialog`; file/folder pickers retain platform behavior.
  Form pages expose natural content height to the dialog scroll owner; shortcut/queue
  ListViews keep their virtualized viewports. Panel tabs use the higher-contrast popup
  state palette. Cards and idle fields are borderless; field hover/focus outlines
  remain interactive in both wallpaper states.
  Push buttons and dropdown triggers use `buttonState`; emphasized push buttons use
  the accent base and `accentState`. Shared chrome prioritizes pressed, hover, then
  selected/focus fills, so selected controls retain hover feedback. Disabled dropdown
  labels and arrows use the disabled text role.
  Keep lifecycle objects out of Popup's default `contentData`.
  Menus and dropdown panels use `Popup.Item` and pass their popup to `FloatingCard`.
  Its visible-lifetime `BackdropBlur` samples the separate `Main.backdropSource` scene
  and the anchor's owning overlay item (for dialog dropdowns), then blurs a padded,
  half-resolution local region. Menu frost/tint works with wallpaper on or off;
  dialogs reuse the effect with a denser 0.94 tint and 96px blur (menus: 0.82 / 64px);
  tooltips retain the wallpaper-aware fill policy.
  Shared popup references use `QtQuick.Templates.Popup`, the common C++ base of styled
  Popup and Menu; the styled `QtQuick.Controls.Popup` type excludes the Menu style branch.
  Floating cards use tint, blur and shadow for separation, with borderless chrome.
  Menu, dropdown and dialog cards share `popupRadius` (12px) with their blur masks;
  small controls and tooltips retain `controlRadius` (6px).
- QML background ownership: `Theme.backgroundActive` gates both the wallpaper and surface
  alpha on `enabled && imageReadable`. `Theme.surfaceColor(baseColor)` combines shared
  `surfaceOpacity` (persisted panel alpha) with the base/panel brightness ratio in a single
  fill, preserving dark-region contrast over wallpaper. `Theme.overlayColor` applies
  shared fill alpha to controls/states (0.72) and popups (0.96), preserving the source alpha;
  wallpaper-off returns the original color. Wallpaper does not add decorative borders.
  Text, icons and transition opacity stay independent.
  Structural children inherit their region background; preview transport and statistics share
  `PreviewPane`. The workspace `PreviewSurface.backgroundColor` stays transparent so stage media
  remains visible; `PreviewSurface.backdropColor` applies `surfaceColor(background.previewCanvas)`
  inside the actual preview bounds, below its image/video layer, retaining wallpaper visibility
  through the theme-specific persisted panel alpha.
  Letterbox space remains part of the application surface. Media and export retain their own
  backgrounds. Timeline header/sidebar/base fills are disjoint and use the same surface-color
  function through the viewport bottom. Viewport-fit lanes use all height below the header;
  its content clips stay separate.
- Workspace outer corner and sidebar resize: `MainSplitView.qml` owns the boundary next to
  the activity bar. `CornerMask.qml` restores the actual wallpaper plus `surfaceColor(activityBar)`
  outside its 10px arc using a corner-sized texture and `shaders/corner_mask.frag`; sidebar
  pages have square fills. The same stationary mask covers whichever editor/preview pane
  reaches the boundary after collapse or swapping. Sidebar dragging uses the shared minimum
  content width's midpoint as the collapse/reopen threshold and persists on release. A collapsed
  divider has zero layout width, retaining the shared hover/drag hit area and active stroke.
- Staged v2 application layer: `src/app/v2/` (`ChartWorkspace` is UIv2's Widgets-free sole document,
  revision and complete-document save-point owner; `ChartWorkspaceFileService` owns BOM/system-
  encoding file I/O plus atomic saves; `AnalysisService` drives production QML validation/
  shifted-marker/Muri projection with one revision-stamped pending/available snapshot;
  `EditorSyncController` owns queued player/timeline/editor follow, navigation, caret and authoring
  synchronization). `MainWindow` hosts preview, timeline, export, latency, and autosave.
  QML writes `ChartWorkspace`; the window follows `ChartWorkspace::changed`.
- QuickShell compatibility: `src/app/quick_shell/QuickShellController.*` remains temporarily for
  v2; the v1 shell, `--ui=v1`, and `MIACODE_UI_SKIN` were deleted in stage 0a.
- Document model: `src/core/chart/document/` (`SimaiDocument`, `SimaiTimingMetadata`)
- Parser + validation: `src/core/chart/parser/` (`SimaiNativeParser*` — include-split TU)
- Chart transforms / normalization: `src/core/chart/transform/`
- Backend-neutral frame-state + per-layer scene math (NO GPU deps): `src/core/scene/`
  (`Preview*LayerState`, `PreviewFrameState`, `PreviewOpacityCurves`, `PreviewMarkerDrawOrder`)
- **Active** QSG render layers: `src/preview/quick_scene/` (`PreviewQuick*Layer`, `PreviewQuickSceneRoot`)
- Preview runtime host, headless export session, stage media: `src/preview/runtime/`
  (`PreviewRuntime`, `PreviewQuickExportSession`, `PreviewStageMediaHost`, `PreviewSceneAsset*`)
- Audio backends + SFX runtime: `src/audio/` (`BassPreviewAudioBackend`,
  `MiniaudioPreviewAudioBackend`, `QtPreviewSfxRuntime*`, `PreviewAudioSettings`)
- Timeline value model + sole QSG timeline surface: `src/timeline/` (+ `quick/`)
- Tools: `src/tools/{latency,muri,video_export,chart_transform,...}`
- Shared config headers + logging + oplog: `src/common/`

### Preview-audio device-cutoff contract (Windows, updated 2026-08-09)

- `PreviewAudioDeviceWatcher` uses IMM endpoint callbacks as the sole Windows source when
  native registration succeeds; it creates `QMediaDevices` only as the Windows-registration
  fallback and on non-Windows. Its direct-cutoff handler may run on Core Audio's MTA, so it calls only
  `QtPreviewSfxRuntime::requestDeviceChangeCutoff()`. That method closes the playback
  generation and synchronously invokes `PreviewBassEmergencyPause` on the previously
  bound concrete BASS output; GUI freezing is delivered later by `deviceCutoffRequested`
  to `TimelineSection`.
- `QtPreviewSfxRuntime` captures the cutoff chart-second from its monotonic playback
  anchor and posts one `DeviceChangePause` worker barrier. The GUI must use that
  captured second and identity, never sample a second clock or post a duplicate pause.
- If the same notification arrives while the clock is not armed, the runtime still
  posts a route-invalidation-only barrier. It must release the concrete endpoint and
  retained stream without emitting a GUI pause or clock sample; the next explicit Play
  must cold-Prepare. The paused PV/BG/outline policy is applied synchronously with the
  GUI playing-state flip, never through a deferred UI-tail callback.
- Windows BASS must set `BASS_CONFIG_DEV_DEFAULT=FALSE` exactly once before its first
  enumeration/init, then bind `BASS_Init` to the resolved Core Audio endpoint ID. BASS
  does not reopen that configuration window after `BASS_Free`, so rebuilds must reuse
  the first configuration result rather than call `BASS_SetConfig` again. On a cutoff
  the backend destroys its streams/device lease; only the next explicit Play may rebuild
  assets on the current endpoint. Do not reintroduce `BASS_Init(-1)` on this path.

> Note: `src/simai/`, `src/preview/scene/`, `src/preview/audio/`, `src/preview/video/` are
> OLD paths from before the "first-unification" reorg. They no longer exist. `src/render/`
> and `src/sources/` are also gone (DComp removal, 2026-08-07). If a doc or comment points
> there, it is stale → use the paths above.

## Render-architecture decision (2026-05-29, updated 2026-08-07) — read before touching preview/render

This is a committed product decision; treat it as a contract.

1. **In-process QSG is the MAIN path — keep it.** `PreviewRuntime` → `PreviewQuickSceneRoot`,
   fed by `core/scene/*LayerState`, rendered by `preview/quick_scene/*`. Both realtime preview
   and video export run through this stack (export via `PreviewQuickExportSession`).
2. **DComp/D3D11 — DELETED (2026-08-07).** `src/render/*` and `src/sources/*` are gone, along
   with all six `MIACODE_*_DCOMP*` flags and the `dcomp` link library.
   Windows still links `d3d11` + `dxgi`. MinGW also links `d3dcompiler` because QtAVPlayer
   calls `D3DCompile` (MSVC uses `#pragma comment`). Do not drop `d3dcompiler` on MinGW.
   Rationale: the backend had been default-off since beta34, so it carried ~11k lines and a
   parallel branch in every preview/timeline paint path while shipping to nobody; the
   decoupling work in decision 2's old form was strictly more expensive than deletion.
   `--quick-shell-beta` survives as an inert argument (it only ever set
   `MIACODE_PREVIEW_USE_DCOMP=1`). Do not reintroduce a second render backend.
3. **Out-of-process worker — DELETED (2026-06-02).** `src/preview/ipc/*`, `PreviewWorkerSession`
   (`src/preview/runtime/`), and the `MIACODE_PREVIEW_OUT_OF_PROCESS` / `MIACODE_PREVIEW_WORKER_*`
   flags are gone. Do not reintroduce this "v2" direction; in-process QSG is the keeper.
4. **`src/README.md` is superseded on this point.** It framed `sources/`+`compositor`+`render`
   as the v2 future and `preview/` (QSG) as legacy-to-be-deleted. Those trees no longer exist;
   QSG is the keeper.

## Work the repo in this order

1. Identify the user-facing capability and its entry point (`references/feature-index.md`).
2. Follow downstream consumers before editing. Many behaviors are mirrored across preview-time
   and export-time code — treat them as a **sync pair** unless `references/cross-chain-linkage.md`
   says otherwise.
3. Prefer code over docs when they disagree; fix the disproven reference in the same change.
4. Build / test / verify in **Release** only. Debug behavior is a runtime `--debug` flag, not a
   separate Debug CMake build (`references/build-and-tools.md`).

## Structural guardrails (from the 2026-05-29 audit)

- **No new god files.** Standing offenders not to grow: `VideoExportController.cpp` (~5000),
  `MuriAnalyzer.cpp` (~1300, decomposed into `miacode::muri::detail` stage TUs — now shared
  primitives + a thin `analyze()` orchestrator; don't regrow),
  `MainWindow` (176-method partial-class across ~30 section files).
  Add a new focused unit instead. Soft target: ~800 lines / one clear responsibility per file.
- **MainWindow is orchestration only** (a must-keep contract). New window features land in
  `sections/<feature>/` as cooperating objects, not as more `friend …Section` partials on the
  one giant `MainWindow.h`.
- **All logging goes through `miacode::debug_log` channels, gated by `--debug`.** Don't add raw
  `qDebug` / `std::cout` / `printf` / `OutputDebugString`. See `references/debug-and-logging.md`.
- **Don't add env flags casually.** ~80 `MIACODE_*` vars already exist. Prefer reusing/removing.
  If you must add one, give it an entry in `references/debug-and-logging.md` in the same change.
- **New scene/render work uses the QSG path:** add a `core/scene/` state builder + a
  `preview/quick_scene/` layer. Do NOT reintroduce a painter/OpenGL fallback or a second
  native render backend.
- **Never commit build artifacts/logs** (`*.obj`, `*.log`, `build/` output, `experimental/`
  binaries). `.gitignore` covers most — keep it that way.

## Maintenance rules (keep this skill true)

- After renames / moves / splits → update paths, class names, function names in
  `feature-index.md` and `architecture-and-layout.md`.
- After changing cross-module behavior → update `cross-chain-linkage.md`.
- After adding / moving / re-scoping a constant → update `hardcode-registry.md`.
- After adding / removing / re-gating a debug flag or log channel → update `debug-and-logging.md`.
- After changing build targets, specs, scripts, packaging, or asset conventions → update
  `build-and-tools.md`.
- When removing a feature, delete its stale breadcrumbs instead of leaving dangling references.
- English is the maintained source of truth for this skill; do not create a translated mirror.
