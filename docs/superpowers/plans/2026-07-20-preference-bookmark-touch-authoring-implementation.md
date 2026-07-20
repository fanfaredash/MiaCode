# Preference, Bookmark, and Touch Authoring Implementation Plan

> Execute in the current MiaCode workspace with regression tests written before each production change. Build and test Release targets with the repository concurrent-build policy.

**Goal:** Make the confirmed application preferences round-trip correctly, size bookmark underlines to their line-number text, and implement Ctrl-held caret-token touch-area authoring with pressed feedback and synchronized preview seeking.

**Architecture:** Keep persistence fixes at their existing ownership boundaries, extract token/geometry/gesture decisions into small testable helpers, and make `PreviewRuntime` the state coordinator while `PreviewQuickSceneRoot` exclusively owns pointer phases. MainWindow remains the only owner of editor mutation and preview seeking.

**Tech stack:** C++20, Qt 6 Core/Gui/Widgets/Quick, CMake/CTest.

---

## Task 1: Lock preference normalization and extension theme behavior

**Files:**

- Modify: `src/app/ui/UiText.h`
- Modify: `src/app/ui/UiText.cpp`
- Modify: `src/extensions/ExtensionManager.cpp`
- Create: `src/tools/ui_text/UiTextPreferencesSpec.cpp`
- Modify: `CMakeLists.txt`

1. Add failing normalization cases for an unknown extension key, `extensionShortcuts`, top-level `theme` migration, and canonical `ui.theme` precedence.
2. Add failing tests for shared canonical-theme read/write helpers, including the value returned to extensions.
3. Expose side-effect-free normalization and canonical-theme helpers for the spec while retaining the existing file load/save API.
4. Start normalization from the raw object, remove the enumerated migrated built-in legacy keys, and overwrite canonical containers.
5. Change `theme/getCurrent`, `theme/setCurrent`, and `app/getInfo` to call the tested canonical-theme helpers rather than indexing JSON independently.
6. Build and run `ui_text_preferences_spec`; keep `ui_text_locale_spec` green.

## Task 2: Separate the BreakSlide cheer application preference from the local mix preset

**Files:**

- Modify: `src/audio/PreviewAudioSettings.h`
- Modify: `src/audio/PreviewAudioSettings.cpp`
- Modify: `src/tools/preview/PreviewAudioSettingsSpec.cpp`
- Modify: `src/app/mainwindow/MainWindowMemberStorage.inc`
- Modify: `src/app/mainwindow/sections/editor/MainWindow.EditorDisplay.cpp`
- Modify: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.AudioSettings.cpp`

1. Add failing tests for canonical sibling-key precedence, legacy fallback, and copying/applying a local preset while preserving the canonical cheer value.
2. Add pure helpers that resolve the application preference from preview JSON and copy/overlay a `PreviewAudioSettings` preset while forcing the canonical cheer value.
3. Add a dedicated MainWindow boolean for the canonical application preference.
4. On load, resolve the sibling key with compatibility fallback and overlay it on live and preset settings.
5. On save, write `app.preview.break_slide_tail_cheer_muted` and keep the audio-object field as a compatibility mirror.
6. Update checkbox, save-preset, and apply-preset paths so the canonical value always wins.
7. Run `preview_audio_settings_spec`.

## Task 3: Persist cover composition on panel teardown

**Files:**

- Modify: `src/tools/cover_export/CoverCompositionState.h`
- Modify: `src/tools/cover_export/CoverCompositionState.cpp`
- Create: `src/tools/cover_export/CoverCompositionPersistenceGuard.h`
- Create: `src/tools/cover_export/CoverCompositionPersistenceGuard.cpp`
- Modify: `src/tools/cover_export/CoverStudioPanel.h`
- Modify: `src/tools/cover_export/CoverStudioPanel.cpp`
- Modify: `src/tools/cover_export/CoverLayoutModelSpec.cpp`

1. Enable `QStandardPaths::setTestModeEnabled(true)`, assign a unique application name, and clean the isolated test preference path before and after the spec so no real user preference is read or written.
2. Add failing tests for a small `CoverCompositionPersistenceGuard`: destroying the guard must persist its latest supplied composition; an explicit export-time persist followed by destruction without edits must not duplicate the write; and explicit persist of A followed by a supplier update to B must persist B on destruction.
3. Add a failing preference-save result/round-trip assertion around `CoverCompositionState` in the isolated path.
4. Return the actual save result from `CoverCompositionState::savePreferences()`.
5. Make the guard a `CoverStudioPanel` member with a supplier bound to `exportCompositionJson()`. Call its content-idempotent `persistNow()` from export and from the panel destructor before renderer detachment; it skips only payloads equal to the last successful save, so edits after export are saved at close. Its own destructor is the safety-net lifecycle seam covered by the spec.
6. Log a failed destructor-time save without blocking teardown.
7. Run the cover layout/composition spec target.

## Task 4: Size bookmark underlines from rendered text bounds

**Files:**

- Modify: `src/editor/PlainCodeEditor.h`
- Modify: `src/editor/PlainCodeEditor.Layout.cpp`
- Modify: `src/tools/editor/PlainCodeEditorSpec.cpp`

1. Add failing geometry tests for one-, two-, three-, and five-digit values with a proportional font.
2. Introduce a pure underline-bounds helper whose right edge matches the existing right-aligned text rectangle.
3. Use the helper in `lineNumberAreaPaintEvent()` without changing row fill, font, or gutter sizing.
4. Run `plain_code_editor_spec`.

## Task 5: Implement caret-token insertion as a pure editor helper

**Files:**

- Create: `src/editor/TouchPadAuthoringEdit.h`
- Create: `src/editor/TouchPadAuthoringEdit.cpp`
- Modify: `src/tools/editor/PlainCodeEditorSpec.cpp`
- Modify: `CMakeLists.txt`

1. Add failing cases for empty/whitespace tokens, non-empty `/` insertion, Shift backtick insertion, trailing whitespace, consecutive commas, document boundaries, comma-side semantics, and active selections.
2. Return token start, insertion position, and insertion text from a pure planning helper based on the active cursor position.
3. Add a QTextDocument application helper that collapses selection and performs one edit block/undo step.
4. Run `plain_code_editor_spec`.

## Task 6: Expose successful token-time resolution

**Files:**

- Modify: `src/timeline/TimelineQuickModel.h`
- Modify: `src/timeline/TimelineQuickModel.cpp`
- Modify: `src/tools/timeline/TimelineModelSpec.cpp`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.TimelineSection.h`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- Modify: `src/app/mainwindow/MainWindow.h`
- Modify: `src/app/mainwindow/MainWindowPrivateMethodsA.inc`

1. Add failing tests proving valid zero seconds can be distinguished from no cursor anchor.
2. Expose a boolean `resolveTimelineSecondForCursor` wrapper around the existing private resolver.
3. Thread the boolean API through TimelineSection and MainWindow for the authoring command.
4. Run `timeline_model_spec`.

## Task 7: Centralize Ctrl touch authoring gesture and pressed visuals

**Files:**

- Modify: `src/core/scene/PreviewFrameState.h`
- Create: `src/core/scene/TouchPadAuthoringState.h`
- Modify: `src/preview/runtime/PreviewRuntime.h`
- Modify: `src/preview/runtime/PreviewRuntime.cpp`
- Modify: `src/preview/quick_scene/PreviewQuickSceneRoot.h`
- Modify: `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`
- Modify: `src/preview/quick_scene/PreviewQuickTouchHoverLayer.cpp`
- Modify: `src/app/mainwindow/MainWindowMemberStorage.inc`
- Modify: `src/app/mainwindow/MainWindow.h`
- Modify: `src/app/mainwindow/sections/preview/MainWindow.PreviewSection.h`
- Modify: `src/app/mainwindow/sections/preview/MainWindow.PreviewWarmupAndSettings.cpp`
- Modify: `src/app/mainwindow/sections/window/MainWindow.WindowSection.h`
- Modify: `src/app/mainwindow/sections/window/MainWindow.WindowInteraction.cpp`
- Modify: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
- Create: `src/tools/preview/TouchPadAuthoringStateSpec.cpp`
- Modify: `CMakeLists.txt`

1. Add failing state-machine tests for hover, press, move-away cancellation, same-pad finish, duplicate finish, Ctrl release, and Shift capture; add style assertions for darker pressed colors.
2. Add pressed pad state and idempotent coordinator operations to PreviewRuntime; clear hover/press when disabled.
3. Make `PreviewQuickSceneRoot` the exclusive authoring mouse owner with press/release/ungrab forwarding and normal Qt Quick grab behavior.
4. Remove direct text insertion and authoring mouse consumption from the native event filter. Track fixed Ctrl state there and cancel on lifecycle transitions.
5. Give Ctrl authoring priority over a bare-Ctrl pause-display binding only inside the defined editable context.
6. Drive runtime enablement from Ctrl + preference + editable context, independent of Alt and outline selection.
7. Render the pressed circle with darker fill/stroke while preserving canonical hit geometry.
8. Replace the existing direct-insert signal connection with the central command: create the token edit, resolve pre-edit token-start time, apply one undoable edit, and call `seekPreviewDiscreteToSecond(max(0, second - 1/60), true)` only on successful mapping.
9. Run `touch_pad_authoring_state_spec`, `plain_code_editor_spec`, and `timeline_model_spec`.

## Task 8: Audit documentation and full verification

**Files:**

- Modify: `.agents/skills/miacode-dev-guide/references/feature-index.md` or the routed preview/editor reference identified by the guide
- Modify: `.agents/skills/miacode-dev-guide/references/build-and-tools.md` if new spec targets are added
- Modify: relevant user-facing feature docs that still say cover preferences save only on export or authoring uses Alt

1. Update the repository guide for the cross-module touch-authoring chain, canonical BreakSlide preference, cover close-time persistence, and new test targets.
2. Run `git diff --check` and inspect the scoped diff for accidental user-file changes.
3. Configure/build affected Release targets with parallelism, then run their CTest entries.
4. Build the main MiaCode Release target to catch cross-module linkage and MOC/QML integration issues.
5. Run the broader relevant CTest subset and report any pre-existing/unrelated failures separately.
