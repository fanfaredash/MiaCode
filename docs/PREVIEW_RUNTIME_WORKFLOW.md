# Preview Runtime Workflow

Status: Legacy archive.

This document is kept for historical reference only. It describes the pre-Qt-Quick runtime coordination model and some of the migration-era orchestration decisions around `MainWindow`, paused-preview snapshots, warmup, and media/SFX ownership.

Do not treat this file as the current source of truth for active rendering architecture.

Use these files instead when changing the live preview/export path:

- `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- `docs/DEBUG_INDEX.md`
- `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`

Legacy scope retained here:

- preview snapshot publication and paused-preview apply semantics
- warmup generation and latest-only apply rules
- playback session ownership in `MainWindow`
- media controller and SFX runtime coordination expectations

Current caveat:

- The old `PreviewCanvas` / painter / native-OpenGL path described by earlier revisions of this document is no longer the active renderer.
- The live renderer is now the Qt Quick scene-graph path described in `PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`.
