# Stage 4 MediaTools Ownership Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all six `MediaToolsEngine` implementations out of the hidden `MainWindow` into a non-Widget `MediaToolsService`, while preserving QML behavior, progress/notice semantics, preview file-lock coordination, and the QML-owned PV batch queue.

**Architecture:** `QmlUiBootstrap` owns `MediaToolsService` and installs it in the existing `ApplicationServices::mediaToolsEngineSlot()` before constructing the hidden `MainWindow`. The service reads `ChartWorkspace`, uses `UiRequestService` and `JobProgressService`, and resolves `PreviewSurface` through its slot for a begin/end file-operation transaction; `MainWindow` retains only a thin QAction forwarding path and the PreviewSurface implementation. The QML model and its PV batch worker remain unchanged.

**Tech Stack:** C++20, Qt 6.8, Qt Core/Gui/QML/Quick, CMake, CTest, Release developer specs, ffmpeg subprocesses.

---

## File map and invariants

### Files to create

- `src/app/v2/MediaToolsService.h` — non-Widget `QObject` + `MediaToolsEngine` implementation declaration, injected application services and private path/operation helpers.
- `src/app/v2/MediaToolsService.cpp` — moved ffmpeg/file/latency helpers and all six MediaTools entry implementations; must not include or name `MainWindow`, `QWidget`, or Qt Widgets.
- `src/tools/ui/Stage4MediaToolsOwnershipSpec.cpp` — source-contract guard for provider ownership, slot lifecycle, PreviewSurface transaction API, and absence of the old MainWindow media provider.
- `src/tools/v2/MediaToolsServiceSpec.cpp` — focused runtime spec with a fake PreviewSurface and real UI/progress services, covering file-operation pairing, refusal, cancellation, and late callbacks.

### Files to modify

- `CMakeLists.txt` — add the service to `MiaCode`, remove `MainWindow.Dialogs.MediaTools.cpp` from that target, and register the new developer spec.
- `src/app/v2/PreviewSurface.h` — add `beginMediaFileOperation()` and `endMediaFileOperation(bool)` to the non-Widget preview contract.
- `src/app/qml_ui/QmlUiBootstrap.h/.cpp` — own/create/shutdown the service and register/clear its engine slot in the required order.
- `src/app/mainwindow/MainWindow.h` — remove the `MediaToolsEngine` base/include/overrides; add the two PreviewSurface overrides if not already declared by the existing surface section; keep private QAction slots as thin dispatchers.
- `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp` — stop registering `MainWindow` as the MediaTools provider.
- `src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp` — stop clearing the MediaTools slot from `MainWindow` destruction; leave the other seven slot withdrawals unchanged.
- `src/app/mainwindow/sections/dialogs/MainWindow.DialogsSection.h/.cpp` — remove the six old provider wrappers and media implementation declarations; route the two legacy QAction slots to the ApplicationServices slot; retain file-operation helpers needed by TrackMetadata.
- `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp` — adapt the retained preview release/reload helpers to implement the PreviewSurface begin/end contract; remove the now-unused completion-dialog helper and includes.
- `src/app/mainwindow/sections/preview/MainWindow.PreviewStageMediaRoute.cpp` and its section header when required — expose the existing PreviewSection release/route behavior through the new MainWindow PreviewSurface overrides without duplicating reload logic.
- `.agents/skills/miacode-dev-guide/references/feature-index.md` — record the new MediaTools owner and PreviewSurface coordination boundary.
- `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md` — mark only the MediaTools provider migration as complete; do not mark `MainWindow` deletion or the final QGuiApplication host complete.
- `docs/ops/DEPENDENCY_ALLOWLIST.md` only if the moved implementation changes the documented media ownership/path; retain the existing Qt/ffmpeg media rationale.

### Files to delete

- `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.MediaTools.cpp` after every helper and six interface method has moved and the old source is removed from CMake.

### Must not change in this batch

- `src/app/qml_ui/media/QmlMediaToolsModel.{h,cpp}` public API, slot binding, or PV batch worker/queue ownership.
- `src/app/v2/ApplicationServices.{h,cpp}` MediaTools slot API.
- `src/app/main.cpp`, `UiComponents`, native shell/menu cleanup, or unrelated existing dirty changes.

The governing invariant is: the object installed in `MediaToolsEngine` owns the implementation and is the only object that can clear that slot; the slot is cleared before service destruction; a successful preview begin is the only permission to mutate a media file; every successful begin has exactly one end, including error and cancellation paths.

Because this worktree already contains user changes in several of the files above, every implementation checkpoint must inspect `git diff` and stage only this plan's hunks. Do not reset, clean, or overwrite the pre-existing changes.

## Task 1: Add the failing ownership contract before production migration

**Files:**

- Create: `src/tools/ui/Stage4MediaToolsOwnershipSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing source-contract spec.**

  Scan `src/app` and fail if any production source still contains the six `MainWindow` MediaTools overrides, `MainWindow::DialogsSection` MediaTools implementation names, or `setMediaToolsEngine(this)`. Assert that the target eventually lists `src/app/v2/MediaToolsService.{h,cpp}`, that the old `MainWindow.Dialogs.MediaTools.cpp` is removed from CMake, and that `MainWindow.h` no longer inherits or includes `MediaToolsEngine`.

  Also assert the required `PreviewSurface` begin/end declarations, QML bootstrap service construction and slot clear-before-reset ordering, the six service methods, and the absence of `MainWindow`/`QtWidgets`/`QWidget` text in the new service. Skip the spec file itself when recursively scanning production sources. Assert that `QmlMediaToolsModel` still contains the slot-backed single-file forwarding and PV batch worker symbols.

- [ ] **Step 2: Register the spec under `MIACODE_BUILD_DEV_TOOLS`.**

  Add `miacode_add_dev_tool(stage4_media_tools_ownership_spec TEST ...)` with only `Qt6::Core` and `MIACODE_SOURCE_ROOT`, matching the existing `stage4_widget_residue_spec` pattern. Do not add production implementation sources to this source scanner target.

- [ ] **Step 3: Run the red test.**

  Run:

  ```bash
  cmake -S . -B build-macos -DMIACODE_BUILD_DEV_TOOLS=ON
  cmake --build build-macos --target stage4_media_tools_ownership_spec --config Release --parallel 4
  ctest --test-dir build-macos -C Release -R stage4_media_tools_ownership_spec --output-on-failure
  ```

  Expected: the spec builds and fails because the current provider is still `MainWindow`, the new service and PreviewSurface methods do not exist, and the old source is still listed. This failure is the TDD guard; do not modify production code before this step has failed.

- [ ] **Step 4: Check only the new spec is staged if making a checkpoint commit.**

  Use `git diff --cached --name-only` before any commit. If a commit is made, stage only the new spec and its CMake registration; do not stage unrelated or pre-existing hunks from `CMakeLists.txt`.

## Task 2: Add the PreviewSurface file-operation transaction

**Files:**

- Modify: `src/app/v2/PreviewSurface.h`
- Modify: `src/app/mainwindow/MainWindow.h`
- Modify: `src/app/mainwindow/sections/dialogs/MainWindow.DialogsSection.h`
- Modify: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp`
- Modify: `src/app/mainwindow/sections/preview/MainWindow.PreviewStageMediaRoute.cpp` and the PreviewSection header only if the forwarding declarations require it

- [ ] **Step 1: Define the exact contract.**

  Add:

  ```cpp
  virtual bool beginMediaFileOperation() = 0;
  virtual bool endMediaFileOperation(bool reloadTrack) = 0;
  ```

  Document that `beginMediaFileOperation()` returns `true` only after stop, route clear, decoder release, and the existing event-drain barrier have completed. A `false` result means the caller must not mutate a file. `endMediaFileOperation()` is valid only after a successful begin, performs the sole route/runtime reload, and returns whether restoration succeeded; callers must not retry file writes when it returns `false`.

- [ ] **Step 2: Make the existing MainWindow helper the single implementation.**

  Change the retained `DialogsSection::releasePreviewMediaForFileOperation()` and `reloadPreviewMediaAfterFileOperation(bool)` helpers to return a success value while preserving their existing TrackMetadata behavior. Add the `MainWindow` PreviewSurface overrides as thin calls into those helpers, so the stop/clear/release and SFX/waveform/route reload code remains in one place. Return `false` when the section or required current-file state is unavailable; do not add a second copy of the reload algorithm to the new service.

- [ ] **Step 3: Build the affected target to expose all other pure-virtual implementations.**

  Run:

  ```bash
  cmake --build build-macos --target MiaCode --config Release --parallel 4
  ```

  Expected: until the MainWindow overrides are added, compilation fails at the intended PreviewSurface contract break. Fix only the affected MainWindow implementation/declarations before proceeding.

- [ ] **Step 4: Re-run the red ownership spec and inspect the diff.**

  The spec should still fail on the provider ownership assertions, but the PreviewSurface method assertions should now pass. Run `git diff --check` and verify the existing TrackMetadata calls and behavior were not removed.

## Task 3: Create the non-Widget MediaToolsService shell and move pure helpers

**Files:**

- Create: `src/app/v2/MediaToolsService.h`
- Create: `src/app/v2/MediaToolsService.cpp`
- Create: `src/tools/v2/MediaToolsServiceSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add the service declaration.**

  Declare `MediaToolsService final : public QObject, public MediaToolsEngine` with a constructor receiving references to `ChartWorkspace`, `UiRequestService`, `JobProgressService`, and the `PreviewSurface*&` slot. Store the slot reference, never a cached PreviewSurface pointer. The service may be parentless because `QmlUiBootstrap` owns it with `std::unique_ptr`; its destructor must invalidate callback/lifetime state.

  Keep the six exact `override` signatures from `MediaToolsEngine.h`. Add private value helpers for the current workspace snapshot, active chart text, media blank paths, ffmpeg resolution, notices, and the begin/end transaction. Do not add `DocumentBridge` or `MainWindow` as a dependency unless the dependency audit proves a value cannot be obtained from `ChartWorkspace`.

- [ ] **Step 2: Move the anonymous file/media helpers without changing algorithms.**

  Move the ffmpeg path resolver, Windows lock diagnostics, retrying remove/rename/copy/restore, duration probing, progress-pumping ffmpeg runner, compression, 44100Hz conversion, track silence prepend, and PV black prepend helpers from `MainWindow.Dialogs.MediaTools.cpp`. Preserve operation-log markers, progress token checks, cancellation messages, backup names, replacement fallbacks, and platform guards. Use only Qt Core/Gui and domain/media headers; the new source must not include `MainWindow.DialogsSection.h`, `MainWindowShared.h`, `QtWidgets`, or any QWidget type.

- [ ] **Step 3: Register the service in `MiaCode`.**

  Add the two service files to the main target source list. Keep the old MediaTools source listed temporarily until Task 6 removes its definitions, so the intermediate compile errors identify any missed dependencies; do not leave both implementations in the final target.

- [ ] **Step 4: Compile the service shell.**

  Run:

  ```bash
  cmake --build build-macos --target MiaCode --config Release --parallel 4
  ```

  Expected: the shell and moved helper code compile only after all required non-Widget includes and source dependencies are present. No runtime behavior is changed yet, and the ownership spec remains red.

## Task 4: Implement all six service methods with workspace snapshots and safe callbacks

**Files:**

- Modify: `src/app/v2/MediaToolsService.h/.cpp`
- Modify: `src/tools/ui/Stage4MediaToolsOwnershipSpec.cpp` if an assertion needs to distinguish request-time validation from execution-time validation
- Create: `src/tools/v2/MediaToolsServiceSpec.cpp`

- [ ] **Step 1: Implement workspace-derived paths and context.**

  Resolve the chart directory and media paths from `ChartWorkspace::snapshot().filePath` and `ChartWorkspace::document().videoPath`. Derive the active chart text from the snapshot's active difficulty and the document, and read `document().extraFields` for clock count/whole BPM. Preserve the existing `QVariantMap` keys (`available`, `title`, `isTrack`, `inputName`, `backupName`, `hasBackup`, `beats`, `bpm`) and warning/error notice keys/messages.

- [ ] **Step 2: Add the focused runtime spec before filling in the operation bodies.**

  In `MediaToolsServiceSpec.cpp`, define a fake `PreviewSurface` with counters and controllable `beginMediaFileOperation()`/`endMediaFileOperation(bool)` results. Use real `UiRequestService` and `JobProgressService` instances with `QSignalSpy`, and a `QTemporaryDir` workspace containing a chart path plus track/backup fixtures. Cover: successful backup restore mutates the expected file and calls begin/end once; a failed begin leaves bytes unchanged and never calls end; a post-begin restore failure still calls end once; an ffmpeg cancellation path ends once and never retries the write; and destroying the service before answering a queued confirmation makes the late callback a no-op. On Unix create a tiny deterministic ffmpeg stub in the temporary directory; on Windows use the equivalent command-file stub or mark only the subprocess cancellation case as a platform skip while retaining all transaction assertions.

  Register `media_tools_service_spec` in CMake with the new service, the existing workspace/UI/progress/latency sources, `${_miacode_chart_core}`, `${_miacode_log_core}`, `Qt6::Core`, `Qt6::Gui`, and `Qt6::Test`. The test must not link Qt Widgets. Run the target now and expect compile/link or assertion failures because the service operation bodies are not implemented yet.

- [ ] **Step 3: Implement `mediaBlankContext()` and `detectMediaBlankTiming()`.**

  Keep the old availability checks and latency-analysis behavior, but replace `metadataExtraEdit_`, `owner_.currentFilePath_`, `owner_.document_`, and `owner_.activeChartText()` reads with workspace values. These two methods must not release preview media or write files.

- [ ] **Step 4: Implement confirmation-based conversion and compression.**

  Validate the workspace snapshot, target path, file existence/size gate, and ffmpeg path before issuing the same confirmation. Capture the request-time file identity and use a `QPointer<MediaToolsService>` or equivalent lifetime token in every asynchronous callback. On acceptance, re-check that the service is alive and the current workspace still names the captured file/media before beginning the transaction.

  Run the helper only after `beginMediaFileOperation()` returns `true`. On success, failure, or cancellation, call `endMediaFileOperation(reloadTrack)` exactly once. Preserve the existing information/error severity and cancellation text. Successful output must use `UiRequestService::requestNoticeAction()` with the existing “open folder” label and a `QDesktopServices::openUrl()` callback for the produced file directory.

- [ ] **Step 5: Implement restore and prepend operations.**

  For restore, validate the input/backup paths before begin, restore under the transaction, report the same success/error notice, and end with the target-specific reload flag. For apply, reject invalid positive `beats`/`bpm`, resolve ffmpeg and paths before begin, run the target-specific prepend helper, preserve cancellation/error text, end exactly once, and show the same completion action on success. A null PreviewSurface slot, failed begin, or shutdown race posts an error and performs no file mutation.

- [ ] **Step 6: Make shutdown safe.**

  Add a service shutdown/invalidation flag or lifetime token. `QmlUiBootstrap` will invalidate it before tearing down the QML context; the service destructor also invalidates it. Pending confirmation callbacks must become no-ops after invalidation and must never call a destroyed service, begin a new transaction, or post a completion from a destroyed provider.

- [ ] **Step 7: Run the focused runtime spec and existing contract tests.**

  Run:

  ```bash
  cmake --build build-macos --target MiaCode media_tools_service_spec job_progress_service_spec ui_request_service_spec --config Release --parallel 4
  ctest --test-dir build-macos -C Release -R 'media_tools_service_spec|job_progress_service_spec|ui_request_service_spec' --output-on-failure
  ```

  Expected: the moved provider compiles, and the existing progress/notice contracts remain green. Do not remove or modify the QML model's PV batch tests/behavior.

## Task 5: Install and destroy the service in QmlUiBootstrap

**Files:**

- Modify: `src/app/qml_ui/QmlUiBootstrap.h`
- Modify: `src/app/qml_ui/QmlUiBootstrap.cpp`
- Modify: `src/tools/ui/Stage4MediaToolsOwnershipSpec.cpp`

- [ ] **Step 1: Add ownership in declaration order.**

  Declare `std::unique_ptr<miacode::v2::MediaToolsService> mediaToolsService_` immediately after `applicationServices_` and before `backend_`, with comments documenting that the service is created before the hidden backend and destroyed after the QML context but before the backend.

- [ ] **Step 2: Create and register before MainWindow.**

  In `start()`, construct `ApplicationServices`, construct `MediaToolsService` with the assembly's workspace/UI/progress services and `previewSurfaceSlot()`, register it via `setMediaToolsEngine(mediaToolsService_.get())`, then construct `MainWindow`. The service must tolerate a null PreviewSurface slot during construction; it resolves the slot only at operation time.

- [ ] **Step 3: Centralize failure/shutdown cleanup.**

  Ensure every root-load failure path and the accepted-close path follows this sequence: invalidate service callbacks, release QML/root resources, destroy QML engine/context, clear `MediaToolsEngine` slot, reset `mediaToolsService_`, reset `backend_`, then reset `applicationServices_`. Clearing the slot must precede service reset; service reset must precede backend reset. Keep the other seven MainWindow slots cleared by MainWindow destruction.

- [ ] **Step 4: Add lifecycle assertions to the source spec.**

  Assert construction order (`ApplicationServices` → service registration → backend), and source-order shutdown assertions for invalidation/slot clear/service reset/backend reset. Also assert that `MainWindow` no longer clears the MediaTools slot and that the QML model still binds to `mediaToolsEngineSlot()`.

- [ ] **Step 5: Run the lifecycle and ownership checks.**

  Run:

  ```bash
  cmake --build build-macos --target stage4_media_tools_ownership_spec media_tools_service_spec qml_ui_bootstrap_lifecycle_spec --config Release --parallel 4
  ctest --test-dir build-macos -C Release -R 'stage4_media_tools_ownership_spec|media_tools_service_spec|qml_ui_bootstrap_lifecycle_spec' --output-on-failure
  ```

  Expected: the lifecycle spec is green; the ownership spec remains red only for the still-present MainWindow provider until Task 6.

## Task 6: Remove MainWindow ownership and unify legacy QAction dispatch

**Files:**

- Modify: `src/app/mainwindow/MainWindow.h`
- Modify: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
- Modify: `src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp`
- Modify: `src/app/mainwindow/sections/dialogs/MainWindow.DialogsSection.h/.cpp`
- Modify: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp`
- Modify: `CMakeLists.txt`
- Delete: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.MediaTools.cpp`

- [ ] **Step 1: Remove the provider base and six overrides.**

  Remove `MediaToolsEngine.h` from `MainWindow.h`, remove the `MediaToolsEngine` base, and remove the six public override declarations and six `MainWindow` forwarding definitions. Remove the old `MediaBlankTarget` and media implementation declarations from `DialogsSection` once no call site remains.

- [ ] **Step 2: Keep QAction behavior as one thin route.**

  Retain the private `onCompressBackgroundVideo()` and `onConvertTrackTo44100Hz()` slots only as legacy QAction adapters. Each must read `applicationServices_.mediaToolsEngine()` and invoke the corresponding virtual method; it must not call `dialogsSection_->onCompressBackgroundVideo()` or `dialogsSection_->onConvertTrackTo44100Hz()`. Keep the menu action labels/connections and the QML `mediaToolsRequested` page opener unchanged.

- [ ] **Step 3: Remove the old implementation source.**

  Delete `MainWindow.Dialogs.MediaTools.cpp` only after its pure helpers and all six methods are present in `MediaToolsService.cpp`. Remove it from the `MiaCode` source list. Remove `showMediaOperationCompleteDialog()` and now-unused includes from `MainWindow.Dialogs.cpp`, but preserve `resolveCurrentChartDirectory()`, preview release/reload helpers, and every TrackMetadata caller.

- [ ] **Step 4: Remove MainWindow slot registration/clear.**

  Delete only `setMediaToolsEngine(this)` from `MainWindow.FrameBootstrap.cpp` and only `setMediaToolsEngine(nullptr)` from `MainWindow.WindowRuntime.cpp`. Leave export, router, latency, timeline, preview, preferences, document bridge, and export-page-session lifecycle unchanged.

- [ ] **Step 5: Build the ownership transition.**

  Run:

  ```bash
  cmake --build build-macos --target MiaCode stage4_media_tools_ownership_spec --config Release --parallel 4
  ctest --test-dir build-macos -C Release -R stage4_media_tools_ownership_spec --output-on-failure
  ```

  Expected: `MiaCode` compiles and the ownership spec passes. If compilation finds an unremoved old media reference, fix the exact reference rather than restoring a MainWindow adapter.

## Task 7: Record the new ownership boundary

**Files:**

- Modify: `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`
- Modify: `.agents/skills/miacode-dev-guide/references/feature-index.md`
- Modify: `docs/ops/DEPENDENCY_ALLOWLIST.md` only if required by the final source ownership wording

- [ ] **Step 1: Update the Todolist precisely.**

  Add a dated Stage 4 entry stating that all six `MediaToolsEngine` methods are now implemented by `src/app/v2/MediaToolsService.{h,cpp}`, registered by `QmlUiBootstrap`, and coordinated through `PreviewSurface`; state that `QmlMediaToolsModel` still owns the PV batch queue. Do not check off the full `src/app/mainwindow/` removal or final `QGuiApplication + QQmlApplicationEngine` item.

- [ ] **Step 2: Update the feature index and dependency record.**

  Move current ownership descriptions from hidden MainWindow dialogs to `MediaToolsService`, record the workspace/UiRequest/JobProgress/PreviewSurface dependencies and the slot lifecycle, and keep historical migration notes intact. If the allowlist mentions the old file as current, replace it with the new service path without changing unrelated Qt Widgets or QtAVPlayer rows.

- [ ] **Step 3: Re-run the source contract.**

  Run `ctest --test-dir build-macos -C Release -R stage4_media_tools_ownership_spec --output-on-failure` and confirm the docs do not claim that MainWindow or Qt Widgets have already disappeared.

## Task 8: Release verification and handoff

**Files:**

- No intended source changes; only fix issues found by verification, and keep fixes in the owning task's files.

- [ ] **Step 1: Reconfigure and build the focused Release targets.**

  Run:

  ```bash
  cmake -S . -B build-macos -DMIACODE_BUILD_DEV_TOOLS=ON
  cmake --build build-macos --target MiaCode stage4_widget_residue_spec stage4_media_tools_ownership_spec media_tools_service_spec --config Release --parallel 4
  ```

- [ ] **Step 2: Run focused regression tests.**

  Run:

  ```bash
  ctest --test-dir build-macos -C Release -R 'stage4_widget_residue_spec|stage4_media_tools_ownership_spec|media_tools_service_spec|qml_ui_bootstrap_lifecycle_spec|job_progress_service_spec|ui_request_service_spec|application_services_spec|qml_export_font_contract_spec|qml_export_video_page_spec|dependency_allowlist_spec' --output-on-failure
  ```

  Expected: all tests in the selected set pass. If a pre-existing unrelated test fails, record its exact test name and output separately; do not weaken this batch's guards.

- [ ] **Step 3: Run the Release build and broader CTest set appropriate to the existing build.**

  On macOS's Unix Makefiles, run `cmake --build build-macos --config Release --parallel 4`; do not assume an `ALL_BUILD` target. Run the full `ctest --test-dir build-macos -C Release --output-on-failure` when practical and record any known baseline failure.

- [ ] **Step 4: Inspect ownership, whitespace, and dirty-worktree boundaries.**

  Run:

  ```bash
  git diff --check
  git status --short
  git diff --stat
  git diff -- src/app/v2/MediaToolsService.h src/app/v2/MediaToolsService.cpp src/app/v2/PreviewSurface.h src/app/qml_ui/QmlUiBootstrap.h src/app/qml_ui/QmlUiBootstrap.cpp
  ```

  Confirm there is one MediaTools implementation, no new Widget/MainWindow dependency in the service, exactly one begin/end pair per successful operation, no PV queue movement, and no pre-existing user change was reverted or accidentally included in a checkpoint commit.

- [ ] **Step 5: Report evidence.**

  Final handoff must list the service/slot/lifecycle changes, the focused and Release commands actually run, pass/fail results, any baseline failure, and the preserved pre-existing dirty files. Do not claim full MainWindow or Qt Widgets removal in this increment.
