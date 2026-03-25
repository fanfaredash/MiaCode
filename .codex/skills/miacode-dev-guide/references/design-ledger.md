# Design Ledger

Use this file to separate hard contracts from adjustable implementation choices.

## 1. Source Of Truth Rule

- Code is the source of truth.
- `DEVELOPMENT_PLAN.md`, `MURI_INTEGRATION_PLAN.md`, and other notes are guidance and memory aids.
- When docs and code diverge, trust code first, then update the docs.

## 2. Must-Keep Contracts

- `MainWindow` is orchestration, not the long-term home for every feature body.
  - New window features should normally land in `src/app/mainwindow/sections/<feature>/`.
- `SimaiDocument` is the editable storage model for metadata and difficulty text.
- Parser output is the shared intermediate representation for:
  - timeline
  - preview
  - Muri analysis
  - export reconstruction
- Runtime SFX and export SFX must use the same note-to-sound semantics.
- `&first` is stored as raw document data; timing semantics are applied through getters and marker shifting instead of ad-hoc inversion scattered around the codebase.
- Export uses a snapshot/worker boundary rather than mutating UI state from the worker process.
- Asset lookup is file-based and convention-driven, not database-driven.

## 3. Current Defaults That Are Adjustable

- Preview canvas default aspect ratio is square unless temporarily changed by export UI.
- Preview note flow speed defaults to the value in `PreviewGameplayConfig.h`.
- Export auto-encoder selection currently favors conservative H.264-oriented paths in automatic mode before falling back.
- Background media naming is currently limited to `bg.*` or `pv.mp4` style conventions.
- Preview panel layout is currently card-based, with preview, controls, and stats as separate blocks.
- Validation UI currently emphasizes surfaced issues in the bottom tabs and summary chips in the editor header.

These are adjustable, but if changed they should be documented here and in the relevant index/linkage files.

## 4. Areas That Are Intentionally Flexible

- UI polish details such as spacing, card proportions, and control arrangement
- tuning values for visual effects, so long as linked render/export assumptions remain coherent
- packaging behavior for dev-only helper executables
- exact wording and severity presentation of validation and diagnostics UI
- export heuristics such as bitrate, encoder preference order, and progress reporting, as long as the worker contract remains intact

## 5. Areas That Need Guardrails

- Path resolution rules are still duplicated in a few places, especially for media and track files.
- A large amount of visual tuning still lives in implementation-local constants in `PreviewCanvas.cpp`.
- Latency detection carries algorithm-specific constants that are meaningful but still mostly local to the tool.
- `DEVELOPMENT_PLAN.md` contains useful design notes, but some path references and structure descriptions already drifted from the current tree.

Treat these areas as change-sensitive even when the underlying behavior is adjustable.

## 6. Explicitly Open Or Risky Areas

- Whether duplicated path-resolution logic should be centralized further
- Whether more preview/export configuration should move from implementation-local constants into shared config headers
- Whether some current chart-directory filename conventions should become configurable
- Whether the repository should gain scripts to auto-refresh code maps and hardcode registries instead of manual upkeep

## 7. Decision Logging Rule

When a previously flexible area becomes a hard contract, add it to section 2.

When a hard contract is intentionally relaxed, remove or rewrite the old rule instead of layering contradictory text on top of it.

When a design choice is still under discussion but the current code depends on it, write it here as "current default" or "open/risky", not as a permanent rule.
