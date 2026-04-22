# Timeline / Preview Latency Recovery Plan

## Summary

- Current evidence points to two separate problems:
  - preview interactions (`play`, `pause`, `stop`, `ctrl+click`) still spend noticeable time on the main thread outside audio transport work
  - the first transform-based quick timeline scroll attempt changed drawing semantics and still did not stop `scene_state_rebuild` storms
- The recovery plan therefore starts with a correctness reset, then reintroduces performance work in smaller, measurable steps.
- The primary hot paths are owned by:
  - `src/timeline/quick/TimelineQuickStateBridge.*`
  - `src/timeline/quick/TimelineQuickItem.*`
  - `src/app/mainwindow/sections/timeline/MainWindow.*`

## Phase 1 - Correctness Reset And Observability Baseline

### Changes

- Restore quick timeline scroll and redraw behavior to the last known-correct rebuild-driven baseline.
- Remove incomplete transform-only experimental code and stray test scaffolding that assumes the abandoned API shape.
- Keep the new latency instrumentation that correlates user actions with preview, timeline, and audio work:
  - `preview/interaction`
  - `timeline/interaction`
  - `timeline/bridge`
  - `timeline/quick_scene`
  - `timeline/cursor_map`
- Document the new runtime/audio tags so later phases can use one stable log vocabulary.

### Code Analysis

- The failed transform attempt proved that the current quick timeline layers are not yet separated into static geometry and dynamic viewport transforms.
- `TimelineQuickStateBridge::setHorizontalScrollValue()` and `TimelineQuickItem::currentSceneState()` still define the authoritative semantics for clamp, scroll, and overlay placement.
- The immediate need is to recover visual correctness before changing scene graph ownership again.

### Expected Effect

- Timeline drawing returns to the pre-transform semantics, so missing or shifted note/waveform content is eliminated.
- Logs remain rich enough to answer:
  - whether wheel drag maps 1:1 to `set_horizontal_scroll_value`
  - whether a given interaction still triggers `scene_state_rebuild`
  - whether `ctrl+click` causes extra scroll/center side effects

### Validation

- `Release` build succeeds.
- Manual checks:
  - timeline wheel drag renders all notes, waveform, header, and overlay correctly
  - `ctrl+click` still seeks accurately
  - preview `play`, `pause`, `stop`, and seek interactions still emit the new interaction logs
- Log checks:
  - `timeline/bridge action=set_horizontal_scroll_value` appears during wheel drag
  - `timeline/quick_scene action=scene_state_rebuild_*` remains available as the rebuild baseline

## Phase 2 - Horizontal Scroll Transform Architecture

### Changes

- Rework quick timeline scrolling into a cached-scene plus transform model.
- Split static scene content from dynamic viewport state:
  - static: grid, waveform, notes, header geometry
  - dynamic: horizontal scroll offset, playhead, cursor, transient focus hints
- Make horizontal scroll update node transforms instead of rebuilding scene state for every wheel/drag step.

### Code Analysis

- The current rebuild-driven scroll path recomputes `TimelineSceneState` for clamp and again for paint, then bumps layer revisions broadly.
- That design is correct but too expensive for high-frequency input.
- A proper transform phase needs stable cached metrics first, then per-layer transform ownership that matches existing visual semantics.

### Expected Effect

- Wheel drag and drag-scroll no longer trigger rebuild storms in steady state.
- Frame pacing improves because scroll updates stop invalidating the entire quick timeline scene.

### Validation

- During 2-3 seconds of wheel drag, `scene_state_rebuild` count stays flat or changes only for explicit non-scroll invalidations.
- New transform/update logs dominate the timeline path instead of rebuild logs.
- Visual parity holds for grid, waveform, notes, markers, and overlay alignment at multiple zoom levels.

## Phase 3 - Single-Rebuild Ctrl+Click Seek

### Changes

- Keep `ctrl+click` synchronous, but constrain the timeline side to at most one rebuild per seek.
- Separate cursor mirroring from scroll centering so `setCursorSeconds(..., center)` does not trigger extra scene work after seek.
- Ensure preview seek owns the one allowed rebuild; follow-up cursor/playhead updates must be dynamic-only.

### Code Analysis

- Current logs show `ctrl+click` is better than before, but still mixes seek, cursor centering, and scroll side effects.
- This makes it impossible to reason about one clean preview-seek cost.

### Expected Effect

- `ctrl_click_release -> ctrl_click_seek_complete` becomes a single, attributable timeline update sequence.
- Timeline and preview logs cleanly distinguish seek work from cursor mirror work.

### Validation

- Each paused `ctrl+click` produces at most one `scene_state_rebuild_begin/end` pair.
- No extra `set_horizontal_scroll_value` is emitted after the one seek-owned update unless the click genuinely requests centering.

## Phase 4 - Preview Interaction Critical Path Reduction

### Changes

- Shorten the critical path of `play`, `pause`, `stop`, and paused seek by deferring non-essential UI and media follow-up.
- Audit `MainWindow` preview orchestration so `*_complete` means audio state and primary playhead are stable, not that every follow-up consumer has finished.
- Continue trimming BASS-side steady-state reposition cost where logs still show measurable transport overhead.

### Code Analysis

- Current logs show audio transport is only one part of end-to-end latency.
- Main-window state sync, timeline updates, and stage media follow-up still dominate the perceived delay budget.

### Expected Effect

- `play`, `pause`, `stop`, and paused seek become visibly more responsive even before deeper backend changes.
- Logs clearly expose the remaining cost center if latency is still above target.

### Validation

- Compare `preview/interaction` durations before and after the phase:
  - `pause_request -> pause_complete`
  - `stop_request -> stop_complete`
  - `play_request -> play_complete`
  - `ctrl_click_release -> ctrl_click_seek_complete`
- Confirm that steady-state interactions no longer reopen initialization-only audio paths.

## Defaults And Assumptions

- Windows preview path is the current target; export path and non-Windows backends are out of scope for this plan.
- Phase 1 prioritizes correctness and observability over raw latency wins.
- Later phases should only ship after their logs demonstrate that the new behavior matches the intended user action semantics.
