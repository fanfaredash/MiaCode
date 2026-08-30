# Tab Order and Export Range Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let metadata participate in session-local editor-tab drag swaps, then restore the v1-style v2 export-range selector with direct handle dragging and preview synchronization.

**Architecture:** `ViewState` will expose a generic session-tab swap operation consumed by `EditorTabBar`; it never alters document content. A new QML range-selector component will bind existing `QmlExportSession` range values to `QmlPreviewModel`'s canonical preview time and scrub lifecycle, hosted by `ExportVideoPage` and supplied from `MainSplitView`.

**Tech Stack:** Qt 6, QML/Qt Quick Controls, C++ QML component specs, CMake Release build, CTest.

---

### Task 1: Let metadata join the session tab drag exchange

**Files:**
- Modify: `src/app/qml_ui/ViewState.qml:99-116`
- Modify: `src/app/qml_ui/editor/EditorTabBar.qml:87-190`
- Modify: `src/tools/qml_ui/QmlEditorControllerSpec.cpp:902-1018`
- Modify: `.codex/skills/miacode-dev-guide/references/feature-index.md:22-25`
- Modify: `.codex/skills/miacode-dev-guide/references/design-ledger.md:34-36`

- [ ] **Step 1: Write the failing real-component drag regression**

  Create an open metadata tab plus two difficulty tabs in the existing
  `QmlEditorControllerSpec` window harness. Drag metadata over a difficulty
  with `QTest::mousePress/mouseMove/mouseRelease`; expect their order to
  exchange while the active editor remains unchanged.

- [ ] **Step 2: Run the focused test to verify red**

  Run: `cmake --build build-macos-spec --target qml_editor_controller_spec --parallel 4 && ctest --test-dir build-macos-spec -R '^qml_editor_controller_spec$' --output-on-failure`

  Expected: failure because metadata's drag handler is disabled or the swap
  API rejects its key.

- [ ] **Step 3: Implement the minimal generic exchange**

  Rename the difficulty-only ViewState operation and the tab-bar hit-test/drag
  state to editor-tab terminology. Permit all existing open editor keys as a
  source and destination; preserve current active editor and MRU history.

- [ ] **Step 4: Re-run the focused test**

  Run the Task 1 command. Expected: PASS.

- [ ] **Step 5: Update ownership/product contracts and commit**

  Update the repository guide to say metadata participates and this remains
  session-only; commit with `feat(qml): allow metadata tab reordering`.

### Task 2: Build the visual export-range selector

**Files:**
- Create: `src/app/qml_ui/export/ExportRangeSelector.qml`
- Modify: `CMakeLists.txt:682,1741-1748`
- Modify: `src/app/qml_ui/export/ExportVideoPage.qml:666-725`
- Modify: `src/app/qml_ui/layout/MainSplitView.qml:196-200`
- Modify: `src/tools/qml_ui/QmlExportVideoPageSpec.cpp:20-160, 210-500`
- Modify: `.codex/skills/miacode-dev-guide/references/feature-index.md`
- Modify: `.codex/skills/miacode-dev-guide/references/design-ledger.md`
- Modify: `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md:419-425`

- [ ] **Step 1: Write the failing export-page interaction regression**

  Add the selector to `MIACODE_UI_QML_FILES` and link the spec with `Qt6::Test`.
  Give the existing real `ExportVideoPage` harness a fake preview session with
  a mutable `positionSeconds` and counters for `beginScrub`, `updateScrub`,
  and `endScrub`. Open the Range tab, drag the real start handle, and assert
  range value, preview time, scrub order, and retained end constraint. Add a
  text-field assertion that the value remains the same source of truth.

- [ ] **Step 2: Run the focused test to verify red**

  Run: `cmake --build build-macos-spec --target qml_export_video_page_spec --parallel 4 && ctest --test-dir build-macos-spec -R '^qml_export_video_page_spec$' --output-on-failure`

  Expected: failure because the selector and its handles do not exist.

- [ ] **Step 3: Implement `ExportRangeSelector.qml`**

  Draw the full-duration lane, blue selected segment/handles, read-only gold
  playhead, boundary labels, and endpoint clamping from its two injected
  sessions. The lane body is inert. Each handle calls the existing preview
  scrub lifecycle and uses the same coordinate conversion for paint and input.

- [ ] **Step 4: Host the selector and retain numeric input**

  Pass `previewSession` from `MainSplitView` into `ExportVideoPage`; insert
  the selector above the existing start/end text fields and remove only the
  duplicate “set to current” buttons. Keep text fields keyboard-focusable.

- [ ] **Step 5: Re-run the focused test**

  Run the Task 2 command. Expected: PASS.

- [ ] **Step 6: Update product docs and commit**

  Record the preview-time source and direct range-drag interaction in the
  guide and replace the v2 export-range feature-gap todo item. Commit with
  `feat(qml): restore visual export range selection`.

### Task 3: Verify the combined change

**Files:**
- Verify only.

- [ ] **Step 1: Inspect diffs and staged content**

  Run: `git diff --check`, `git diff --cached --check`, and inspect both
  feature diffs for duplicated timeline state or accidental document mutation.

- [ ] **Step 2: Build the full Release graph**

  Precheck for an existing MiaCode build, then run:
  `cmake --build build-macos-spec --parallel 4`

- [ ] **Step 3: Run full CTest**

  Run: `ctest --test-dir build-macos-spec --output-on-failure`

  Expected: all relevant tests pass; report the existing
  `qtavplayer_platform_spec` baseline failure if it remains the only red test.

- [ ] **Step 4: Report commits and native acceptance**

  Keep the native-desktop acceptance checklist entries for metadata tab drag
  and export-range drag until a person verifies pointer feel in the real app.
