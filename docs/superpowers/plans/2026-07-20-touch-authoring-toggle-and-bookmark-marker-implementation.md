# Touch Authoring Toggle and Bookmark Marker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Ctrl+left/right touch authoring insert with slash/backtick or remove the first existing ordinary area Touch, and remove bookmark line-number underlines while retaining other marker styling.

**Architecture:** Keep comma-token parsing and document mutation in `TouchPadAuthoringEdit`; extend its plan to describe one replacement range so insertion and deletion remain a single undo step. Keep pointer phases in `PreviewQuickSceneRoot`, with small pure mouse-button helpers in `TouchPadAuthoringState`. Remove underline painting at its existing gutter owner.

**Tech Stack:** C++20, Qt 6 Core/Gui/Widgets/Quick, CMake/CTest.

---

### Task 1: Add failing token-toggle and button-routing specifications

**Files:**
- Modify: `src/tools/editor/PlainCodeEditorSpec.cpp`
- Modify: `src/tools/preview/TouchPadAuthoringStateSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] Add editor-plan cases for slash/backtick insertion and removal of first, middle, sole, mixed-separator, and first-of-duplicate `A1` items.
- [ ] Add `(120){4}A1` and `(120){4}A1/B2` preservation cases plus non-matches for `A10` and `A1h[4:1]`.
- [ ] Add deletion with trailing whitespace and confirm the untouched whitespace remains in place.
- [ ] Add document-application/undo coverage for deletion ranges.
- [ ] Add pure mouse-button cases: left and right supported, middle rejected, right alone selects backtick.
- [ ] Inject `MIACODE_SOURCE_ROOT` into `touch_pad_authoring_state_spec`; add source-contract assertions that the scene accepts left+right, consumes supported press/release, derives backtick only from right, and contains no `ShiftModifier` routing.
- [ ] Build both specs with `--parallel 2` and run them, confirming failures are caused by missing toggle/button behavior.

### Task 2: Implement replacement planning and right-button routing

**Files:**
- Modify: `src/editor/TouchPadAuthoringEdit.h`
- Modify: `src/editor/TouchPadAuthoringEdit.cpp`
- Modify: `src/core/scene/TouchPadAuthoringState.h`
- Modify: `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`
- Modify: `src/preview/runtime/PreviewRuntime.h`
- Modify: `src/preview/runtime/PreviewRuntime.cpp`
- Modify: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`

- [ ] Replace insertion-only plan fields with edit start, removal length, replacement text, token start, and valid/toggle metadata.
- [ ] Scan meaningful token text by `/` and `` ` ``; for the first item, peel complete leading `(…)` and `{…}` controls before exact pad comparison.
- [ ] Generate the confirmed adjacent-separator deletion range for only the first match; otherwise generate the existing insertion range/text.
- [ ] Apply the plan by selecting its removal range and inserting its replacement inside one edit block.
- [ ] Add pure helpers mapping supported Qt mouse buttons and right-button backtick choice.
- [ ] Make the scene accept/consume left and right press/release, derive separator from the released button, and remove Shift inspection.
- [ ] Rename boolean parameter/signal wording from Shift/backtick ambiguity to mouse-selected backtick semantics without changing MainWindow ownership or pre-edit seek timing.
- [ ] Rebuild and run the two focused specs; expect all new cases to pass.

### Task 3: Remove bookmark underline and update repository memory

**Files:**
- Modify: `src/editor/PlainCodeEditor.h`
- Modify: `src/editor/PlainCodeEditor.Layout.cpp`
- Modify: `src/tools/editor/PlainCodeEditorSpec.cpp`
- Modify: `src/tools/preview/TouchPadAuthoringStateSpec.cpp`
- Modify: `.agents/skills/miacode-dev-guide/references/feature-index.md`
- Modify: `docs/superpowers/specs/2026-07-20-touch-authoring-toggle-and-bookmark-marker-design.md` only if implementation reveals a contract correction

- [ ] Remove underline geometry assertions, helper declaration/definition, underline pen setup, and `drawLine`; retain bookmark/drop background fill and accent text color.
- [ ] Extend the source-contract spec to require the bookmark `fillRect` and accent text-color branch while rejecting `lineNumberUnderlineHorizontalBounds` and gutter `drawLine`.
- [ ] Update the guide from Ctrl+Shift wording to Ctrl+right and document first-occurrence toggle deletion plus no-underline bookmark styling.
- [ ] Build `plain_code_editor_spec`; confirm all new bookmark/token assertions pass while recording the known macOS completion-popup result separately.

### Task 4: Verify and commit

**Files:**
- Verify all modified files and generated targets.

- [ ] Run `git diff --check` and inspect the scoped diff for accidental macOS/native-window regressions.
- [ ] Build `MiaCode`, `plain_code_editor_spec`, `touch_pad_authoring_state_spec`, and `timeline_model_spec` in Release with `--parallel 2`.
- [ ] Run deterministic relevant CTest entries and separately record the known Cocoa completion-popup test if it persists.
- [ ] Commit the implementation on `dev-macos` without adding build outputs or `.DS_Store` files.
