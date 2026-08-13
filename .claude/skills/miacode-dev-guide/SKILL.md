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
- Default UI (**v2**): `src/app/qml_ui/`
- QuickShell (**v1**, `--ui=v1` / `MIACODE_UI_SKIN=v1`): `src/app/quick_shell/`
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
- Timeline data + QSG timeline surface: `src/timeline/` (+ `quick/`)
- Tools: `src/tools/{latency,muri,video_export,chart_transform,...}`
- Shared config headers + logging + oplog: `src/common/`

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
