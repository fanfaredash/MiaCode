# Video Export Subprocess Isolation

Status: Legacy archive.

This document name is preserved because earlier export iterations discussed worker-process boundaries, crash containment, and ffmpeg orchestration under this title.

It is not the active architecture spec for the current Qt Quick export path.

Use these files for current ownership and wiring:

- `../specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`

Legacy points that still conceptually apply:

- export worker boundaries are a contract
- snapshot serialization must stay aligned on both sides
- ffmpeg process ownership belongs to the export controller layer, not scene rendering
- renderer refactors should not collapse worker/process isolation guarantees by accident

Current caveat:

- The worker/export boundary still exists, but the frame renderer on the worker side is now the headless Qt Quick export session rather than the removed legacy preview renderer.
