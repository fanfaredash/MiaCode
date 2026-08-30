# Export Range Interaction Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make v2 export ranges non-empty, give endpoint and body drags distinct stable semantics, and remove the range-label layout jump.

**Architecture:** `QmlExportSession` becomes the sole owner of atomic start/end writes and its five-second minimum-duration policy. `ExportRangeSelector` only classifies pointer hit targets and renders them from one coordinate model; it drives the existing preview scrub API without an independent time source.

**Tech Stack:** Qt 6/C++, QML/Qt Quick, QtTest component specifications, CMake Release build, CTest.

---

### Task 1: Make the export-session range write atomic

**Files:**
- Modify: `src/app/qml_ui/export/QmlExportSession.h`
- Modify: `src/app/qml_ui/export/QmlExportSession.cpp`
- Test: `src/tools/qml_ui/QmlExportVideoPageSpec.cpp`

- [ ] **Step 1: Write failing range-contract checks**

Exercise an end-point update through the real QML harness and assert its opposite end stays fixed; assert a request for a zero/short duration normalizes to `min(5s, chart duration)`.

- [ ] **Step 2: Run the focused spec and observe the regression**

Run: `cmake --build build-macos-spec --target qml_export_video_page_spec --parallel 4 && ctest --test-dir build-macos-spec -R '^qml_export_video_page_spec$' --output-on-failure`

Expected: FAIL because the existing start setter retains the task duration and moves the effective end.

- [ ] **Step 3: Add the atomic range operation**

Implement `setExportRangeSeconds(start, end)`, use it from pointer-facing setters/text parsing, and enforce `min(5, chart duration)` without changing snapshot serialization.

- [ ] **Step 4: Re-run the focused spec**

Expected: PASS.

### Task 2: Separate grips, selected-body drag, and timestamp overlay

**Files:**
- Modify: `src/app/qml_ui/export/ExportRangeSelector.qml`
- Modify: `src/tools/qml_ui/QmlExportVideoPageSpec.cpp`

- [ ] **Step 1: Write failing real-component interaction checks**

Cover: endpoint drags move only their endpoint; dragging the selected middle shifts both values by one delta; a short legal range has two selectable endpoint grips plus a body target; hover changes no implicit height.

- [ ] **Step 2: Run the focused spec and observe the regression**

Run the Task 1 command. Expected: FAIL because the selector treats the lane as endpoint-only, has overlapping grips, and dynamically changes height for labels.

- [ ] **Step 3: Implement one coordinate and hit-target model**

Use canonical display centres for grips and the selected body, with a minimum visual span derived from grip geometry. Keep endpoint and body targets disjoint, call the atomic session method, and use the existing scrub lifecycle. Replace moving labels with one non-layout timestamp overlay.

- [ ] **Step 4: Re-run the focused spec**

Expected: PASS.

### Task 3: Record the contract and verify the application

**Files:**
- Modify: `.codex/skills/miacode-dev-guide/references/feature-index.md`
- Modify: `.codex/skills/miacode-dev-guide/references/design-ledger.md`
- Modify: `.codex/skills/miacode-dev-guide/references/hardcode-registry.md` (only if the duration or visual-span constant belongs there)
- Modify: `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`

- [ ] **Step 1: Update ownership and native acceptance records**

Record the atomic range setter, five-second floor, body-versus-endpoint hit rules, and hover-only timestamp behavior. Retain the native acceptance checkbox.

- [ ] **Step 2: Build and test**

Precheck active build processes, then run `cmake --build build-macos-spec --parallel 4` and `ctest --test-dir build-macos-spec --output-on-failure`.

- [ ] **Step 3: Inspect and commit**

Run `git diff --check` and `git diff --cached --check`, inspect the staged range changes, then commit with `fix(qml): clarify export range dragging`.
