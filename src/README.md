# `src` Directory Responsibilities

This project is split by functional module. Keep new code in the closest module and avoid adding more logic back into `app/MainWindow.*` unless it is window-level orchestration.

## `app/`
- Entry and shell wiring.
- `main.cpp`: app/bootstrap (`QApplication`, style, surface format).
- `MainWindow.*`: UI orchestration, action routing, document lifecycle, cross-module coordination.

## `editor/`
- Text editor widgets and editing UX helpers.
- Example: `PlainCodeEditor.*`.

## `simai/`
- Simai format model, parse/serialize, native parser/dump tooling.
- No UI dependencies here.

## `timeline/`
- Timeline view/rendering, note marker visualization, timeline interaction.
- Should consume parsed note markers, not parse document text directly.

## `preview/`
- Preview rendering and media/audio runtime.
- `PreviewCanvas.*`: OpenGL rendering path and profiling.
- `PreviewMediaController.*`: media decode/position/frame delivery.
- `QtPreviewSfxRuntime.*`: SFX/BGM runtime and playback-rate audio path.
- `PreviewIntegration.*`: glue helpers between preview components.

## `tools/`
- Developer/diagnostic utilities and probes that are not part of the normal app path.

## Debug Output Policy
- Runtime debug output is **off by default**.
- Enable it only from command line:
  - `--miacode-debug`
  - `--debug-runtime`
  - `--enable-debug-output`
- This switch controls:
  - preview debug HUD default override
  - runtime debug text output
  - profiling/artifact file emission (e.g. preview profiling summary, audio debug log)

