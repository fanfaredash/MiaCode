# Preview Audio Device Reanchor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Silently re-anchor BASS preview BGM to the wall-clock chart position on default-output changes or 50 ms audio-clock divergence.

**Architecture:** Keep visuals, stage media, and the Linux miniaudio route unchanged. A pure policy selects a single recovery reason; the Qt Multimedia observer and the existing SFX drift check both queue one MainWindow callback. That callback invokes a narrow BASS-only retained-seek transaction rather than rebuilding the audio device.

**Tech Stack:** C++20, Qt 6 Core/Multimedia, BASS/BASS_FX, CMake/CTest.

---

## File map

- Create: `src/audio/PreviewAudioRecoveryPolicy.h` — pure 50 ms recovery decision.
- Create: `src/tools/preview/PreviewAudioRecoveryPolicySpec.cpp` — policy regression test.
- Modify: `src/audio/PreviewAudioBackend.h` — optional re-anchor operation.
- Modify: `src/audio/BassPreviewAudioBackend.h` and `src/audio/BassPreviewAudioBackend_PlaybackClock.cpp` — BASS implementation built from existing retained seek.
- Modify: `src/audio/QtPreviewSfxRuntime.h/.cpp` — `QMediaDevices` observer, facade, and signal.
- Modify: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp` — connect device change to the timeline section.
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.TimelineSection.h`, `MainWindow.PreviewTick.cpp`, and `MainWindowMemberStorage.inc` — queue and execute one recovery.
- Modify: `CMakeLists.txt` — register the policy spec.
- Modify: `docs/ops/DEBUG_INDEX.md` and `.codex/skills/miacode-dev-guide/references/{cross-chain-linkage,hardcode-registry,debug-flags}.md` — record the contract, threshold, and logs.

### Task 1: Add the recovery decision policy (TDD)

**Files:**
- Create: `src/tools/preview/PreviewAudioRecoveryPolicySpec.cpp`
- Create: `src/audio/PreviewAudioRecoveryPolicy.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing policy spec**

Require a not-yet-existing `decidePreviewAudioRecovery(...)` API to return no
action for inactive playback and 49 ms drift, `DefaultOutputChanged` for an
active changed default, and `DriftExceeded` for active available audio at
exactly 50 ms.

- [ ] **Step 2: Run RED**

Run: `cmake --build build --target preview_audio_recovery_policy_spec --config Release --parallel`

Expected: compilation fails because the policy header/API is absent.

- [ ] **Step 3: Implement the minimal header-only policy**

Define the reason enum, the `0.050` second threshold, and one decision
function. Do not add cooldowns, retries, or device-specific logic.

- [ ] **Step 4: Run GREEN**

Run: `cmake --build build --target preview_audio_recovery_policy_spec --config Release --parallel && ctest --test-dir build -R '^preview_audio_recovery_policy_spec$' --output-on-failure`

Expected: 1/1 test passes.

### Task 2: Expose a narrow BASS re-anchor transaction (TDD)

**Files:**
- Modify: `src/audio/PreviewAudioBackend.h`
- Modify: `src/audio/BassPreviewAudioBackend.h`
- Modify: `src/audio/BassPreviewAudioBackend_PlaybackClock.cpp`
- Modify: `src/audio/QtPreviewSfxRuntime.h`
- Modify: `src/audio/QtPreviewSfxRuntime.cpp`
- Modify: `src/tools/preview/BassPreviewRetainedStateSpec.cpp`

- [ ] **Step 1: Extend the retained-state spec first**

Add an assertion that an active/non-retained transaction takes the existing
`AnchorAndResume` action, documenting the exact transaction the new BASS
method delegates to.

- [ ] **Step 2: Run the retained-state spec before the new API**

Run: `cmake --build build --target bass_preview_retained_state_spec --config Release --parallel && ctest --test-dir build -R '^bass_preview_retained_state_spec$' --output-on-failure`

Expected: existing policy assertions pass; the new API integration is not yet
compiled by this pure spec.

- [ ] **Step 3: Add the optional facade/backend method**

Add a false-returning default method to `PreviewAudioBackend`. The BASS
override validates that BGM is running, delegates to
`seekRetainedPreviewPlaybackTransaction(chartSecond, true)`, and logs the
reason. The Qt facade only forwards it. Do not call `BASS_Free`, `BASS_Init`,
or any MainWindow pause method.

- [ ] **Step 4: Build the real audio route**

Run: `cmake --build build --target MiaCode --config Release --parallel`

Expected: the app compiles with the new virtual/facade API.

### Task 3: Observe default output and trigger one silent recovery (TDD)

**Files:**
- Modify: `src/audio/QtPreviewSfxRuntime.h`
- Modify: `src/audio/QtPreviewSfxRuntime.cpp`
- Modify: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
- Modify: `src/app/mainwindow/MainWindowMemberStorage.inc`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.TimelineSection.h`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp`

- [ ] **Step 1: Add policy-spec coverage for event versus drift precedence**

Add the active-default-change and drift-boundary assertions before wiring Qt
signals, proving both sources resolve to the single queueable reason.

- [ ] **Step 2: Run RED for the new assertions**

Run: `cmake --build build --target preview_audio_recovery_policy_spec --config Release --parallel && ctest --test-dir build -R '^preview_audio_recovery_policy_spec$' --output-on-failure`

Expected: failure until the policy covers the requested precedence/boundary.

- [ ] **Step 3: Wire the observer and queue**

Create `QMediaDevices` as a child of `QtPreviewSfxRuntime`, snapshot the
default output id, and emit a signal when `audioOutputsChanged` changes that
id. Connect it during frame bootstrap. In `TimelineSection`, use one pending
boolean plus a queued callback; on execution, sample the wall-clock second
and call the new backend re-anchor operation only while playback remains
active.

At 50 ms in `sfxDrainSecond`, preserve the existing wall-clock SFX fallback
for that tick and request the same callback. No retry/cooldown mechanism is
introduced.

- [ ] **Step 4: Build and run affected specs**

Run: `cmake --build build --target MiaCode preview_audio_recovery_policy_spec bass_preview_retained_state_spec --config Release --parallel && ctest --test-dir build -R '^(preview_audio_recovery_policy_spec|bass_preview_retained_state_spec|preview_audio_health_spec)$' --output-on-failure`

Expected: app builds and all three specs pass.

### Task 4: Document the recovery contract and verify the whole change

**Files:**
- Modify: `docs/ops/DEBUG_INDEX.md`
- Modify: `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`
- Modify: `.codex/skills/miacode-dev-guide/references/hardcode-registry.md`
- Modify: `.codex/skills/miacode-dev-guide/references/debug-flags.md`

- [ ] **Step 1: Document the new behaviour and logs**

Record the BASS-only device-change path, `50 ms` threshold, and the
`preview_audio_device` / `preview/audio_reanchor` debug records. State that
ended BGM and Linux do not recover through this route.

- [ ] **Step 2: Run final verification**

Run: `cmake --build build --target MiaCode preview_audio_recovery_policy_spec bass_preview_retained_state_spec preview_audio_health_spec --config Release --parallel && ctest --test-dir build -R '^(preview_audio_recovery_policy_spec|bass_preview_retained_state_spec|preview_audio_health_spec)$' --output-on-failure && git diff --check`

Expected: build exit code 0, 3/3 tests pass, and no whitespace errors.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt src/audio src/app/mainwindow docs/ops .codex/skills/miacode-dev-guide/references
git commit -m "fix(preview-audio): reanchor after device clock drift"
```
