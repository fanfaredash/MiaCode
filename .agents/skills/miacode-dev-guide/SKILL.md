---
name: miacode-dev-guide
description: Repository guide for MiaCode, a Qt 6/C++/QML chart editor and video exporter. Use for any work in this repo that needs feature ownership, module boundaries, cross-path behavior, build/test conventions, diagnostics, assets, or architectural constraints.
---

# MiaCode Dev Guide

Use this skill as the repository memory index. Start from the user-facing behavior and read
only the reference that owns it. Code and project-native build/test results are authoritative;
when a reference disagrees, correct the reference in the same change.

## Route the task

- Architecture, module boundaries, active implementations, and structural constraints:
  [references/architecture-and-layout.md](references/architecture-and-layout.md)
- Feature ownership down to files, classes, and functions:
  [references/feature-index.md](references/feature-index.md)
- Parser, timeline, preview, audio, export, latency, and Muri synchronization:
  [references/cross-chain-linkage.md](references/cross-chain-linkage.md)
- Constants, thresholds, and configuration ownership:
  [references/hardcode-registry.md](references/hardcode-registry.md)
- Runtime `--debug`, log channels, environment flags, and diagnostics:
  [references/debug-and-logging.md](references/debug-and-logging.md)
- Release builds, targets, CTest, scripts, assets, and packaging:
  [references/build-and-tools.md](references/build-and-tools.md)
- Dialog/QML clipping, seams, stacking, dark-mode, overflow, or hit-area defects: use
  `qt-ui-layout-pitfalls` before editing layout code.

## Repository contracts

- `QuickShellBootstrap` is the normal GUI entry. `MainWindow` and QWidget surfaces are hosted
  implementation surfaces; keep new top-level orchestration in QuickShell/controller sections.
- In-process Qt Quick/QSG is the only preview and export render path. Keep scene math GPU-free
  in `src/core/scene/`, render layers in `src/preview/quick_scene/`, and runtime hosting in
  `src/preview/runtime/`. Do not recreate the removed DComp or preview-worker architectures.
- Treat preview/export and other mirrored consumers as synchronization pairs unless
  `cross-chain-linkage.md` explicitly marks a path as compatibility-only or diagnostic.
- Keep `MainWindow` orchestration thin; add focused units under
  `src/app/mainwindow/sections/<feature>/` and register new translation units in CMake.
- Route logs through `miacode::debug_log`; do not add ad-hoc console/debug-output channels.
- Routine verification is Release-only and uses `build-devtools/`. Enable
  `MIACODE_BUILD_DEV_TOOLS` when specs are required and run CTest with `-C Release`.

## Working sequence

1. Locate the primary owner in `feature-index.md`.
2. Read the relevant linkage/architecture reference and inspect all named consumers.
3. Make the smallest coherent change, preserving active-path and serialization boundaries.
4. Validate proportionally in Release; before commit or push, review the full diff and run the
   necessary build/tests as required by the workspace rules.

## Keep the guide current

- Moves, renames, ownership, or module changes: update `feature-index.md` and, when structural,
  `architecture-and-layout.md`.
- Cross-module behavior or serialized boundaries: update `cross-chain-linkage.md`.
- Constants/config ownership: update `hardcode-registry.md`.
- Flags, channels, or diagnostics: update `debug-and-logging.md`.
- Targets, scripts, assets, packaging, or verification conventions: update
  `build-and-tools.md`.
- Remove stale breadcrumbs when code is removed. Maintain one English source of truth here;
  `.codex/skills/miacode-dev-guide/SKILL.md` is only a compatibility entrypoint.
