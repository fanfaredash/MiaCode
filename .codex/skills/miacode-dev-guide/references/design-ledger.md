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
- Native chart-review preview currently stacks an extra maimuri-style judge overlay above the legacy hex judge effects. `MuriRenderOptions::showChartReviewSlideJudgeOverlay` controls slide/wifi-family overlays and defaults to enabled; `MuriRenderOptions::showChartReviewSimpleJudgeOverlay` controls tap/hold/break text overlays plus slide-head text and defaults to disabled until UI toggles are added.
- In `RenderMode::MaimuriDxStyle`, wifi track erasure currently follows runtime lane progress rather than static area checkpoints: the shared track trims by the slowest of the three lanes, falls back to judged lane areas if the progress array is unavailable, and stays erased after runtime completion instead of replaying a full-track flash. When `MuriRenderOptions::wifiNeedC` is enabled, the last area stays visible until `C` is actually released.
- Export auto-encoder selection currently favors conservative H.264-oriented paths in automatic mode before falling back.
- Partial export currently keeps the full-range export lead-in behavior untouched, but when a request is not marked as full-range it prepends a 1.5-second preload and filters exported objects up front by `marker.second` within `[L, R]`; slides and their tracks stay coupled to the head timestamp because the whole `TimelineNoteMarker` is either kept or dropped as one unit.
- Export output naming currently auto-appends `.mp4` when missing and avoids collisions by choosing `name(1).mp4`, `name(2).mp4`, and so on before the worker starts.
- Background media naming is currently limited to `bg.*` or `pv.mp4` style conventions.
- Preview panel layout is currently card-based, with preview, controls, and stats as separate blocks.
- Preview fullscreen currently reuses the same transport controls as the embedded preview, adds a rounded translucent-black `Esc` hint bubble, and only reveals a near-full-width bottom overlay control bar when the cursor moves into the bottom hot zone, with fade-in/fade-out opacity animation before auto-hiding again.
- While the chart text editor has focus, `Ctrl+Enter` currently forwards to the existing preview play/pause transport instead of being left to the text widget.
- Timeline drag/scrub currently edits runtime `R`, not editor-anchor `C`: dragging or wheel panning first rebinds `R` to the viewport center when needed, header clicks and `Ctrl+click` on the timeline perform the spec's `R -> C` action by click second, and editor-side cursor updates only move `C`.
- Timeline zoom currently offers `25%..150%` presets in `5%` steps, starts at `50%`, and the header zoom button cycles the coarse stops `25/50/75/100/125/150`.
- Slide/wifi synchronous color state currently splits head-vs-trace semantics: `headEach` follows the synchronous note-head group, while `slideEach` only turns on when slide-like notes from that same each-group also share the same `slideTraceSecond`; an older slide does not inherit a yellow track just because a later each-group reaches the same shoot moment.
- Preview follow currently only affects active playback: when enabled it binds `C` to the latest comma at or before `R`, while paused follow toggles do not move `L / R / C`.
- Preview playback currently freezes a play-start snapshot for preview video/SFX/object stats until playback stops; live text edits still redraw the timeline and refresh validation/Muri inputs, but they do not rewrite the content that is already playing.
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
