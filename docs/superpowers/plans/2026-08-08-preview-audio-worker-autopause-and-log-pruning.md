# Preview Audio Worker, Device Auto-Pause, And Log Pruning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every realtime preview backend call off the GUI thread, pause visibly and permanently on a real output-device change, and suppress only the proven no-op/duplicate diagnostic rows.

**Architecture:** `QtPreviewSfxRuntime` remains the GUI-owned `QObject` facade, but a bounded priority queue and a non-`QObject` `std::thread` worker own all backend construction, calls, health sampling, and destruction. The GUI reads immutable snapshots and accepts typed completions only through centralized generation/transaction/pause-token checks; device notifications freeze GUI/video state before submitting a reserved high-priority audio pause. Independent pure logging policies gate future emissions without altering historical logs or the retained diagnostic edges.

**Tech Stack:** C++17, Qt 6 Core/Widgets/QML, `std::thread`/`std::condition_variable`, BASS/BASSmix/BASS_FX, miniaudio/SoundTouch, CMake dev-tool specs, CTest, `miacode::debug_log`.

## Task Status (2026-08-09)

- [x] Task 1: Typed Protocol And Bounded Command Queue
- [x] Task 2: Worker Ownership, Snapshot Publication, And Failure Recovery
- [x] Task 3: Non-GUI Completion Barrier
- [x] Task 4: Make Miniaudio A Real Backend Implementation
- [x] Task 5: Serialize Process-Wide BASS Device Lifetime
- [x] Task 6: Convert QtPreviewSfxRuntime Into A Non-Blocking Facade
- [x] Task 7: Migrate Startup, Retained Resume, And Seek State Machines
- [x] Task 8: Immediate Manual And Device-Change Pause
- [x] Task 9: Migrate Ticks, Warmup, Dialog, Latency, Probe, And Shutdown
- [x] Task 10: Pure Log-Pruning Policies
- [x] Task 11: Wire Audio, Media, Watchdog, And Scene Log Gates
- [x] Task 12: Wire QuickShell And Background Edge Logging
- [ ] Task 13: Documentation And Complete Verification (implementation complete; acceptance pending)

Implementation commits are recorded in the branch history; the final integration commits are
`97daa12e` and `137d2032`.

Verification status (2026-08-09):

- The affected Release targets build successfully with `--parallel 1`; focused CTest passes 14/14.
- Full CTest passes 53/56. The three stable failures are pre-existing and outside this change:
  `oplog_self_test`, `plain_code_editor_spec`, and `preview_firework_lifecycle_spec`.
- Windows BASS hardware acceptance for repeated output-device changes and shutdown during an
  in-flight change remains outstanding because this worktree is running on macOS.

---

## Working Rules

- Work only in `/Users/caoyusen/.codex/worktrees/MiaCode/windows-idle-freeze-diagnostics` on `dev-preview-audio-worker-autopause-log-pruning`.
- Preserve the unrelated main-workspace edit in `src/wrapper/MiaCodeLauncher.cpp`; never stage it from `/Users/caoyusen/Desktop/MiaCode`.
- Follow `@superpowers:test-driven-development`: add one behavioral spec, run it and observe the expected failure, then add only the implementation needed to pass.
- Follow `@miacode-dev-guide`: `src/audio` remains the only module that links native audio libraries; MainWindow files orchestrate but do not own worker mechanics.
- Follow `@miacode-concurrent-build`: Release only and one MiaCode build at a time. Per the 2026-08-08 user override after an over-parallelized build restarted the device, every agent must use exactly `--parallel 1`; never run multiple builds concurrently. Do not clean/rebuild/delete build artifacts without fresh user approval.
- Add every new source explicitly to `MiaCode`, the relevant spec target, and `soundtouch_probe` where that target shares the runtime.
- Run `git status --short` after every task commit; only intentional later-task changes may remain, so an omitted source cannot silently drift across commit boundaries.
- Do not edit, delete, or rewrite any `logs*` directory. This plan changes only future emission behavior.

## File Map

New audio units:

- `src/audio/PreviewAudioWorkerProtocol.h`: command/completion/snapshot value types and pure acceptance/coalescing predicates.
- `src/audio/PreviewAudioCommandQueue.{h,cpp}`: bounded high/ordered/audition queues, reserved pause/shutdown slots, latest-only ticks, invalidation, and shutdown gate.
- `src/audio/PreviewAudioWorkerFactory.{h,cpp}`: injectable backend construction; production selection stays platform-aware.
- `src/audio/PreviewAudioWorker.{h,cpp}`: worker loop, backend ownership, command execution, snapshot publication, completion dispatch, health sampling, and non-GUI barriers.
- `src/audio/PreviewBassDeviceLease.{h,cpp}`: process-wide serialization/refcount for `BASS_Init`, `BASS_GetDevice`, and `BASS_Free` only.

New specs:

- `src/tools/preview/PreviewAudioCommandQueueSpec.cpp`
- `src/tools/preview/PreviewAudioWorkerProtocolSpec.cpp`
- `src/tools/preview/PreviewAudioWorkerSpec.cpp`
- `src/tools/preview/PreviewAudioNonGuiBarrierSpec.cpp`
- `src/tools/debug_index/LogPruningPolicySpec.cpp`

Existing owners to modify:

- Runtime/backend: `src/audio/QtPreviewSfxRuntime.{h,cpp}`, all `QtPreviewSfxRuntime.*.cpp` splits, `PreviewAudioBackend.h`, `MiniaudioPreviewAudioBackend.{h,cpp}`, and the `BassPreviewAudioBackend*` family.
- BASS lifetime consumers: `src/common/WaveformCache.cpp`, `src/tools/video_export/BassExportAudioBackend.{h,cpp}`.
- Playback orchestration: `src/app/mainwindow/MainWindowMemberStorage.inc`, timeline playback/startup/tick files, warmup/settings, frame bootstrap, and window shutdown.
- Other consumers: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.AudioSettings.cpp`, `src/tools/latency/LatencySandboxController.{h,cpp}`, `src/tools/probe/SoundtouchProbe.cpp`.
- Log policy/emissions: `src/common/LogEmissionPolicy.h`, `UiHangWatchdogPolicy.h`, `UiHangWatchdog.cpp`, `BassPreviewSfxSchedulerPolicy.h`, BASS event drain, stage media, QuickShell bootstrap/controller, timeline Quick item, and app background painter.
- Registration/docs: `CMakeLists.txt`, `docs/ops/DEBUG_INDEX.md`, and the five relevant MiaCode dev-guide references.

### Task 1: Typed Protocol And Bounded Command Queue

**Files:**
- Create: `src/audio/PreviewAudioWorkerProtocol.h`
- Create: `src/audio/PreviewAudioCommandQueue.h`
- Create: `src/audio/PreviewAudioCommandQueue.cpp`
- Create: `src/tools/preview/PreviewAudioCommandQueueSpec.cpp`
- Create: `src/tools/preview/PreviewAudioWorkerProtocolSpec.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Register and write the failing queue/protocol specs**

Cover these independent cases with the repository's standalone `main()` assertion style:

```cpp
expect(queue.enqueue(makeOrdered(CommandKind::Start, 4)).accepted, "start accepted");
expect(queue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 1.0)).accepted, "tick accepted");
expect(queue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 2.0)).replaced, "tick replaced");
expect(queue.enqueue(makeHigh(CommandKind::DeviceChangePause, 5, 17)).accepted, "reserved pause accepted");
expect(queue.takeNext()->kind == CommandKind::DeviceChangePause, "pause precedes stale tick");
queue.invalidateBefore(5);
expect(!queue.containsPlaybackGeneration(4), "old playback invalidated");
```

Also prove FIFO within each class, capacities `16/128/32`, dedicated shutdown/device/manual-pause safety slots, device-pause coalescing, `QueueFull`, post-shutdown rejection, snapshot sequence monotonicity, and the centralized completion predicate:

```cpp
expect(acceptsPlaybackCompletion(currentGeneration, activeTxn, completion), "matching completion");
expect(!acceptsPlaybackCompletion(currentGeneration + 1, activeTxn, completion), "old generation rejected");
expect(acceptsDevicePauseCompletion(generation, pendingTxn, firstPauseToken, completion),
       "duplicate device sequence does not invalidate first pause token");
expect(!acceptsAssetCompletion(latestAssetGeneration, olderReloadCompletion),
       "old chart/settings reload cannot publish ready");
```

- [x] **Step 2: Run RED**

Run:

```bash
cmake --build build --config Release --target preview_audio_command_queue_spec preview_audio_worker_protocol_spec --parallel 1
```

Expected: FAIL because the new protocol/queue types and methods do not exist.

- [x] **Step 3: Implement the minimum protocol and queue**

Define explicit value types with no `QObject*`, references, or GUI containers:

```cpp
enum class CommandKind { Shutdown, DeviceChangePause, ManualPause, StopAll,
    SetWarmupResolvedPaths, ReloadAssets, SetChartPath,
    SetBackgroundOffset, SetBackgroundRate, ApplyRateAtSecond, ApplyLevels,
    ConfigureTimeline, ClearTimeline, ApplyPausedState,
    Prepare, Commit, Cancel, Start, ResumeRetained, SeekRetained,
    ResetRetained, ClearRetained, ResetCursor, PauseTouchhold,
    RestoreTouchhold, StartBackground, SeekBackground, PauseBackground,
    StopSfxVoices, SyncBackgroundTrack, DrainEvents, Audition };
enum class CommandError { None, QueueFull, ShuttingDown, BackendUnavailable,
    BackendFailure, Stale };
enum class WorkerLifecycle { Constructing, Ready, Loading, Degraded, ShuttingDown, Stopped };

struct CommandIdentity {
    quint64 sequence = 0;
    quint64 generation = 0;
    quint64 assetGeneration = 0;
    quint64 transactionId = 0;
    quint64 deviceSequence = 0;
    quint64 pauseToken = 0;
};
```

Use owned/implicitly-shared payloads (`QString`, `QVector`, settings structs), three deques, and two optional latest-tick slots. `invalidateBefore()` removes only playback commands with older generations. `beginShutdown()` rejects producers and preserves the one shutdown command. Keep the completion acceptance functions in this header so MainWindow and specs use the same predicate.

`assetGeneration` is independent from playback `generation`. Increment it for
every chart path, warmup path, or audio-settings state that supersedes an
in-flight asset load. Carry it through commands, snapshots, ready/reload
completions, and reject any asset completion below the latest value even when
its playback generation still matches.

Pin every public facade operation to one protocol domain:

| Facade operations | Command/result | Generation domain |
| --- | --- | --- |
| warmup paths, chart path, reload | matching `Set*`/`ReloadAssets`; ready completion | `assetGeneration` |
| offset/rate/levels/timeline/paused state | ordered matching command; diagnostic completion | current asset + playback generation |
| prepare/commit/cancel/start | matching playback command; prepared/started completion | playback generation + transaction |
| retained resume/seek/reset/clear | matching retained command; retained completion where result-bearing | playback generation + transaction |
| manual/device pause, stop-all | reserved high command; paused/stop completion | playback generation; device pause also token + pending transaction |
| cursor/touch-hold/background/SFX controls | ordered command; diagnostic failure completion | current playback generation |
| BGM sync/event drain | one latest-only slot each | current playback generation |
| audition | bounded audition command; audition completion | current asset generation |
| scalar getters | no command; read `PreviewAudioSnapshot` | snapshot sequence |

- [x] **Step 4: Run GREEN**

Run the two targets above, then:

```bash
ctest --test-dir build -C Release --output-on-failure -R "(preview_audio_command_queue_spec|preview_audio_worker_protocol_spec)"
```

Expected: both specs PASS with no warnings/errors.

- [x] **Step 5: Commit**

```bash
git add CMakeLists.txt src/audio/PreviewAudioWorkerProtocol.h src/audio/PreviewAudioCommandQueue.h src/audio/PreviewAudioCommandQueue.cpp src/tools/preview/PreviewAudioCommandQueueSpec.cpp src/tools/preview/PreviewAudioWorkerProtocolSpec.cpp
git commit -m "feat(audio): define preview worker command protocol"
```

### Task 2: Worker Ownership, Snapshot Publication, And Failure Recovery

**Files:**
- Create: `src/audio/PreviewAudioWorkerFactory.h`
- Create: `src/audio/PreviewAudioWorkerFactory.cpp`
- Create: `src/audio/PreviewAudioWorker.h`
- Create: `src/audio/PreviewAudioWorker.cpp`
- Create: `src/tools/preview/PreviewAudioWorkerSpec.cpp`
- Modify: `src/audio/PreviewAudioBackend.h`
- Modify: `src/audio/PreviewAudioWorkerProtocol.h`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write the failing fake-backend worker spec**

Create a fake implementing `PreviewAudioBackend` that records constructor, every method, and destructor thread IDs. Inject it through a factory lambda. Test:

- backend construction/use/destruction all occur on one worker ID different from the caller ID;
- lifecycle publishes `Constructing -> Ready` and monotonic snapshot sequences;
- reload failure publishes `Degraded`, resolves dependent work with `BackendUnavailable`, and an explicit later warmup/reload returns to `Ready`;
- an old reload completion with asset generation `A` cannot publish Ready after chart/settings generation `A+1` exists;
- backend exceptions become typed failures and do not kill the worker loop;
- a blocked ordinary fake call delays native pause execution but never blocks `enqueue()`;
- shutdown disables callbacks, resolves/rejects pending work, destroys backend, then joins.

- [x] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_audio_worker_spec --parallel 1
```

Expected: FAIL because `PreviewAudioWorker` and injectable factory do not exist.

- [x] **Step 3: Implement the worker loop and explicit failure drain**

The worker owns the backend exclusively. Construction and shutdown are both
exception-safe; a missing/failed backend is a valid `Degraded` state, not a
process-terminating exception:

```cpp
void PreviewAudioWorker::run()
{
    publishLifecycle(WorkerLifecycle::Constructing);
    try {
        backend_ = factory_();
    } catch (const std::exception& error) {
        publishDegraded(CommandError::BackendUnavailable, error.what());
    } catch (...) {
        publishDegraded(CommandError::BackendUnavailable, "unknown factory failure");
    }
    publishReadyOrDegraded();
    while (auto command = queue_.takeNextBlocking()) {
        if (command->kind == CommandKind::Shutdown) {
            rejectQueuedCommands(CommandError::ShuttingDown);
            break;
        }
        execute(*command); // catch at this boundary
    }
    publishLifecycle(WorkerLifecycle::ShuttingDown);
    if (backend_ != nullptr) {
        try {
            backend_->prepareForShutdown();
        } catch (...) {
            publishShutdownFailure();
        }
    }
    backend_.reset();
    rejectQueuedCommands(CommandError::ShuttingDown);
    publishLifecycle(WorkerLifecycle::Stopped);
}
```

`execute()` catches backend exceptions at the command boundary and emits a
typed completion. `rejectQueuedCommands()` emits one `ShuttingDown` completion
per queued command (including latest-only slots), wakes all non-GUI barrier
waiters, and is idempotent. `SetWarmupResolvedPaths` is accepted while degraded
and stores owned pending paths without claiming backend readiness. An explicit
`ReloadAssets` is the only operation that retries the factory; after successful
construction it applies the pending warmup paths before reloading assets,
publishes `Ready`, and resolves the reload completion. All unrelated commands
while degraded return `BackendUnavailable`. Before publishing an asset snapshot
or ready completion, compare its `assetGeneration` to the worker's latest
observed asset generation; stale loads receive `Stale` and cannot replace the
newer snapshot. Completions carry identity, error/native
code, operation-specific result, worker thread ID, queue delay, and execution
duration. Snapshot writes use an aggregate protected by one short mutex or
atomic shared-pointer publication compatible with supported macOS libc++; no
BASS call may occur while holding the snapshot mutex. Production factory
selection moves out of `QtPreviewSfxRuntime::createBackend()` but is not wired
to the facade yet. Add the factory injection constructor/overload now so the
facade and specs can supply a fake without loading BASS; the production
constructor delegates to the same interface. Register the worker protocol,
queue, factory, and worker sources in the `MiaCode` `target_sources` list and
in the `soundtouch_probe` `SOURCES` list (the first target uses the BASS block
on Windows/macOS; the probe keeps its existing platform guards). The two spec
targets use the existing `miacode_add_dev_tool(... TEST ...)` helper with
`Qt6::Core` and `INCLUDES src src/audio src/common`.

- [x] **Step 4: Run GREEN and regress the pure specs**

```bash
cmake --build build --config Release --target preview_audio_worker_spec preview_audio_command_queue_spec preview_audio_worker_protocol_spec --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "preview_audio_(worker_spec|command_queue_spec|worker_protocol_spec)"
```

Expected: all selected specs PASS.

- [x] **Step 5: Commit**

```bash
git add CMakeLists.txt src/audio/PreviewAudioBackend.h src/audio/PreviewAudioWorkerProtocol.h src/audio/PreviewAudioWorkerFactory.h src/audio/PreviewAudioWorkerFactory.cpp src/audio/PreviewAudioWorker.h src/audio/PreviewAudioWorker.cpp src/tools/preview/PreviewAudioWorkerSpec.cpp
git commit -m "feat(audio): add backend-owning preview worker"
```

### Task 3: Non-GUI Completion Barrier

**Files:**
- Create: `src/tools/preview/PreviewAudioNonGuiBarrierSpec.cpp`
- Modify: `src/audio/PreviewAudioWorkerProtocol.h`
- Modify: `src/audio/PreviewAudioWorker.h`
- Modify: `src/audio/PreviewAudioWorker.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write a failing barrier spec without a Qt event loop pump**

Start the fake worker, enqueue reload/start, and call `waitForReadyForNonGui(timeout)` / `waitForCompletionForNonGui(sequence, timeout)` from a plain `std::thread`. Do not construct a GUI, call `QCoreApplication::processEvents()`, or rely on queued Qt signals. Verify success, timeout, shutdown wakeup, and a guard failure when invoked from the facade-owning thread ID.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_audio_non_gui_barrier_spec --parallel 1
```

Expected: FAIL because completion storage/barrier methods are missing.

- [ ] **Step 3: Add bounded completion storage and condition-variable waits**

Store completion records by command sequence until observed or retired, notify after snapshot/completion publication, and make shutdown wake every waiter. The API returns a typed timeout/shutdown result, not a default success. Assertions/documentation must prohibit use on the facade owner thread.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build --config Release --target preview_audio_non_gui_barrier_spec preview_audio_worker_spec --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "preview_audio_(non_gui_barrier|worker)_spec"
```

Expected: both specs PASS without event-loop pumping.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/audio/PreviewAudioWorkerProtocol.h src/audio/PreviewAudioWorker.h src/audio/PreviewAudioWorker.cpp src/tools/preview/PreviewAudioNonGuiBarrierSpec.cpp
git commit -m "feat(audio): add non-gui worker completion barrier"
```

### Task 4: Make Miniaudio A Real Backend Implementation

**Files:**
- Modify: `src/audio/MiniaudioPreviewAudioBackend.h`
- Modify: `src/audio/MiniaudioPreviewAudioBackend.cpp`
- Create: `src/audio/MiniaudioPreviewAudioBackend.Timeline.cpp`
- Create: `src/audio/MiniaudioPreviewAudioBackend.Background.cpp`
- Create: `src/audio/MiniaudioPreviewAudioBackend.Assets.cpp`
- Create: `src/audio/MiniaudioPreviewAudioBackend.Engine.cpp`
- Create: `src/audio/MiniaudioPreviewAudioBackend.Voices.cpp`
- Modify: `src/audio/PreviewAudioWorkerFactory.cpp`
- Modify: `src/tools/preview/PreviewAudioWorkerSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add a failing production-factory/miniaudio worker case**

Build the factory without `MIACODE_HAS_BASS_AUDIO`, construct the miniaudio backend on the worker, verify `backendId()=="miniaudio"`, and destroy it on that same worker. Add a source drift assertion that the implementation no longer contains `#define QtPreviewSfxRuntime MiniaudioPreviewAudioBackend`.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_audio_worker_spec soundtouch_probe --parallel 1
```

Expected: the new drift assertion fails while the macro alias remains.

- [ ] **Step 3: Split the included implementation fragments before renaming**

The current miniaudio TU includes `QtPreviewSfxRuntime.Timeline.cpp`,
`Background.cpp`, `Assets.cpp`, `Engine.cpp`, and `Voices.cpp` under a macro.
Create same-responsibility backend fragments
`MiniaudioPreviewAudioBackend.{Timeline,Background,Assets,Engine,Voices}.cpp`
by mechanical copies, change their definitions/includes to
`MiniaudioPreviewAudioBackend::`, and make
`MiniaudioPreviewAudioBackend.cpp` include only those new fragments. Keep the
old `QtPreviewSfxRuntime.*.cpp` fragments exclusively for the facade until
Task 6; do not edit them for miniaudio after this step. Remove the macro and
the `QObject` parent relationship only after the new backend fragments compile.
Because these are included implementation fragments, register only
`MiniaudioPreviewAudioBackend.cpp` (plus its header) in each target; do not add
the included fragments as separate translation units and create duplicate
symbols. Update both the `MiaCode` audio source list and the
`soundtouch_probe` source list explicitly.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build --config Release --target preview_audio_worker_spec soundtouch_probe --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "preview_audio_worker_spec"
```

Expected: spec and probe target build PASS on the non-BASS route.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/audio/MiniaudioPreviewAudioBackend.h src/audio/MiniaudioPreviewAudioBackend.cpp src/audio/MiniaudioPreviewAudioBackend.Timeline.cpp src/audio/MiniaudioPreviewAudioBackend.Background.cpp src/audio/MiniaudioPreviewAudioBackend.Assets.cpp src/audio/MiniaudioPreviewAudioBackend.Engine.cpp src/audio/MiniaudioPreviewAudioBackend.Voices.cpp src/audio/PreviewAudioWorkerFactory.cpp src/tools/preview/PreviewAudioWorkerSpec.cpp
git commit -m "refactor(audio): make miniaudio backend independent"
```

### Task 5: Serialize Process-Wide BASS Device Lifetime

**Files:**
- Create: `src/audio/PreviewBassDeviceLease.h`
- Create: `src/audio/PreviewBassDeviceLease.cpp`
- Create: `src/tools/preview/PreviewBassDeviceLeaseSpec.cpp`
- Modify: `src/audio/BassPreviewAudioBackendImpl.h`
- Modify: `src/audio/BassPreviewAudioBackend.cpp`
- Modify: `src/audio/BassPreviewAudioBackend_EngineInit.cpp`
- Modify: `src/common/WaveformCache.cpp`
- Modify: `src/tools/video_export/BassExportAudioBackend.h`
- Modify: `src/tools/video_export/BassExportAudioBackend.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing lease policy/spec**

Inject `getDevice/init/free` function objects so the spec needs no real output device. Prove concurrent acquire/release is serialized, one initialized device has a guarded refcount, init failure does not increment, last release calls free exactly once, and a borrowed existing device is not freed. Add a scope-duration assertion showing the lease lock is released before simulated decode work.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_bass_device_lease_spec --parallel 1
```

Expected: FAIL because `PreviewBassDeviceLease` is missing.

- [ ] **Step 3: Implement and migrate all in-process BASS owners**

Move `gBassDeviceRefCount` and its mutex into the lease implementation. The lease surrounds only `BASS_GetDevice`, `BASS_Init`, refcount mutation, and final `BASS_Free`; never hold it during stream decode, mixer calls, backend logs, or worker waits. Replace the preview engine-init/teardown blocks, waveform `ScopedBassWaveformDevice`, and export `initializeBass()/shutdownBass()` with the shared lease. Export behavior/protocol remains otherwise unchanged.

- [ ] **Step 4: Run GREEN and BASS regressions**

```bash
cmake --build build --config Release --target preview_bass_device_lease_spec bass_preview_retained_state_spec video_export_audio_render_plan_spec MiaCode soundtouch_probe --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "(preview_bass_device_lease|bass_preview_retained_state|video_export_audio_render_plan)_spec"
```

Expected: all selected specs PASS, and both real targets compile the migrated
preview engine-init, waveform, and export BASS owners.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/audio/PreviewBassDeviceLease.h src/audio/PreviewBassDeviceLease.cpp src/tools/preview/PreviewBassDeviceLeaseSpec.cpp src/audio/BassPreviewAudioBackendImpl.h src/audio/BassPreviewAudioBackend.cpp src/audio/BassPreviewAudioBackend_EngineInit.cpp src/common/WaveformCache.cpp src/tools/video_export/BassExportAudioBackend.h src/tools/video_export/BassExportAudioBackend.cpp
git commit -m "fix(audio): serialize shared BASS device lifetime"
```

### Task 6: Convert QtPreviewSfxRuntime Into A Non-Blocking Facade

**Files:**
- Modify: `src/audio/QtPreviewSfxRuntime.h`
- Modify: `src/audio/QtPreviewSfxRuntime.cpp`
- Modify: `src/audio/QtPreviewSfxRuntime.Assets.cpp`
- Modify: `src/audio/QtPreviewSfxRuntime.Background.cpp`
- Modify: `src/audio/QtPreviewSfxRuntime.Engine.cpp`
- Modify: `src/audio/QtPreviewSfxRuntime.Timeline.cpp`
- Modify: `src/audio/QtPreviewSfxRuntime.Voices.cpp`
- Modify: `src/audio/PreviewAudioBackend.h`
- Modify: `src/audio/BassPreviewAudioBackend.h`
- Modify: `src/audio/BassPreviewAudioBackend_PlaybackClock.cpp`
- Modify: `src/audio/PreviewAudioWorker.{h,cpp}`
- Modify: `src/audio/PreviewAudioWorkerFactory.{h,cpp}`
- Modify: `src/tools/preview/PreviewAudioHealthSpec.cpp` only if the new sample payload needs a field assertion
- Modify: `src/tools/preview/PreviewAudioWorkerSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Extend the failing facade/thread-affinity tests**

Construct a facade with an injected fake factory. Call every public mutator and getter from the owner thread. Assert mutators return within a short fake-backend block window, every backend call records the worker ID, getters use the last snapshot, and completion signals contain command sequence/generation/transaction/device fields. Cover queued callback destruction: destroy the facade with callbacks pending and verify no receiver dereference.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_audio_worker_spec --parallel 1
```

Expected: FAIL because the facade still directly owns/calls `backend_`.

- [ ] **Step 3: Replace direct calls with command submission and snapshots**

The facade owns queue/worker/callback receiver state, never `PreviewAudioBackend`. Keep public names where useful, but return the documented immediate fallbacks for formerly synchronous calls and return the command sequence for operations whose completion must be tracked. Add signals `backendReadyChanged`, `previewPrepared`, `previewPlaybackStarted`, `previewPlaybackPaused`, `retainedPlaybackCompleted`, and `auditionCompleted`. Centralize generation advancement for start/seek/manual pause/stop/device pause.

Before destruction: disable delivery, disable producers, enqueue reserved shutdown, join, then release receiver/queue. `prepareForShutdown()` is the only GUI-facing wait. Use `QMetaObject::invokeMethod(this, functor, Qt::QueuedConnection)` so Qt drops callbacks after receiver destruction.

- [ ] **Step 4: Move health sampling into the owning worker**

Delete `audioHealthSamplerThread_` and its start/stop loop. Add the concrete
worker-only virtual `PreviewAudioHealthSample sampleHealth()` to
`PreviewAudioBackend`; it is called by `PreviewAudioWorker::run()` when a
`steady_clock` deadline reaches the approximately 1 Hz interval, using the
same condition-variable wait as command wakeups. `PreviewAudioWorker` publishes
the sample into `PreviewAudioSnapshot`, emits the existing `bass_status` row
outside all scheduler/snapshot locks, and preserves underrun/stall edge
transitions. The fake backend records the sampling thread and a deterministic
clock/deadline case in `PreviewAudioWorkerSpec`; `PreviewAudioHealthSpec`
continues to cover payload math. The native mixer callback only increments
worker-owned counters and never logs or touches GUI-owned state while holding
its scheduler lock.

- [ ] **Step 5: Run GREEN and scan the boundary**

```bash
cmake --build build --config Release --target preview_audio_worker_spec preview_audio_health_spec soundtouch_probe --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "(preview_audio_worker|preview_audio_health)_spec"
rg -n "backend_->|audioHealthSamplerThread_|BASS_(Get|Channel|Mixer|Error|Init|Free)" src/audio/QtPreviewSfxRuntime* src/app/mainwindow
```

Expected: specs PASS; the scan finds no facade/MainWindow backend pointer or BASS call and no independent sampler thread.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/audio/QtPreviewSfxRuntime* src/audio/PreviewAudioBackend.h src/audio/PreviewAudioWorker.h src/audio/PreviewAudioWorker.cpp src/audio/PreviewAudioWorkerFactory.h src/audio/PreviewAudioWorkerFactory.cpp src/audio/BassPreviewAudioBackend.h src/audio/BassPreviewAudioBackend_PlaybackClock.cpp src/tools/preview/PreviewAudioHealthSpec.cpp src/tools/preview/PreviewAudioWorkerSpec.cpp
git commit -m "refactor(audio): isolate preview backend on worker"
```

### Task 7: Migrate Startup, Retained Resume, And Seek State Machines

**Files:**
- Modify: `src/app/mainwindow/MainWindowMemberStorage.inc`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.Internal.h`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackState.cpp`
- Modify: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
- Create: `src/tools/preview/PreviewAudioPlaybackFlowPolicySpec.cpp`
- Create: `src/audio/PreviewAudioPlaybackFlowPolicy.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing pure flow-policy tests**

Model only acceptance/state decisions, not widgets. Prove prepare/retained completions commit only for the current generation and active transaction; a newer seek/start rejects older completions; the requested visual second may update immediately but worker effective second becomes authoritative only on an accepted completion; failed/degraded prepare leaves the UI paused.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_audio_playback_flow_policy_spec --parallel 1
```

Expected: FAIL because the playback flow policy/acceptance helpers are absent.

- [ ] **Step 3: Wire completion signals once during bootstrap**

Connect runtime signals to focused `TimelineSection` handlers. Store current audio generation and pending command sequences in `MainWindowMemberStorage.inc`; do not capture raw MainWindow pointers in worker commands. Keep stage-media startup callbacks on the GUI thread and retain the existing strong-group handshake.

- [ ] **Step 4: Convert cold prepare and retained resume/seek**

`startQtPreviewPlayback()` submits audio work tagged with the newly allocated playback transaction and returns after optimistic visual setup. `previewPrepared` or `retainedPlaybackCompleted` marks `previewStartupAudioPrepared_` only after the centralized acceptance predicate succeeds, then calls the existing `tryCommitPreviewStartupSync()`. Cancel and commit are ordered commands. A stale completion logs diagnostic context and performs no state/UI mutation.

- [ ] **Step 5: Run GREEN**

```bash
cmake --build build --config Release --target preview_audio_playback_flow_policy_spec MiaCode --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "preview_audio_(playback_flow_policy|worker_protocol)_spec"
```

Expected: policy specs PASS and `MiaCode` links with no synchronous-result call sites in the two playback files.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/audio/PreviewAudioPlaybackFlowPolicy.h src/tools/preview/PreviewAudioPlaybackFlowPolicySpec.cpp src/app/mainwindow/MainWindowMemberStorage.inc src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.Internal.h src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackState.cpp src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp
git commit -m "refactor(preview): await audio startup completions"
```

### Task 8: Immediate Manual And Device-Change Pause

**Files:**
- Modify: `src/audio/PreviewAudioPlaybackFlowPolicy.h`
- Modify: `src/audio/PreviewAudioWorker.{h,cpp}`
- Modify: `src/audio/PreviewAudioCommandQueue.{h,cpp}`
- Modify: `src/audio/QtPreviewSfxRuntime.{h,cpp}`
- Modify: `src/tools/preview/PreviewAudioWorkerSpec.cpp`
- Modify: `src/tools/preview/PreviewAudioCommandQueueSpec.cpp`
- Modify: `src/tools/preview/PreviewAudioPlaybackFlowPolicySpec.cpp`
- Modify: `src/tools/preview/PreviewAudioDeviceChangePolicySpec.cpp`
- Modify: `src/app/mainwindow/MainWindowMemberStorage.inc`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackState.cpp`
- Modify: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
- Modify: `src/audio/PreviewAudioDeviceWatcher.{h,cpp}` if teardown guarding is not already sufficient

- [ ] **Step 1: Write failing pause-token, generation-barrier, and UI-transition tests**

Prove this sequence:

```text
real change -> increment deviceSequence
if playing and no pending pause -> capture wall second/txn, set immutable pauseToken,
advance generation, freeze GUI state, enqueue DeviceChangePause
duplicate while pending -> increment deviceSequence, keep token/txn/wall second, coalesce
Play before completion -> clear/supersede pending pause, advance generation
late pause completion -> diagnostic only
matching completion -> clear pending pause, never resume
```

Also retain reorder/add/remove/default-output policy coverage and prove a paused notification is logged/ignored without generation change.

In `PreviewAudioCommandQueueSpec.cpp` and `PreviewAudioWorkerSpec.cpp`, add a
queue/worker case that enqueues old-generation `Start`, `Commit`, and retained
`Resume/Seek`, then begins a `DeviceChangePause` at generation `N`.
The worker records `pauseBarrierGeneration=N` and drops every queued playback
command with a lower generation; a later user Play at `N+1` is accepted. This
test must fail until both queue invalidation and worker-side barrier filtering
exist.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_audio_worker_spec preview_audio_command_queue_spec preview_audio_playback_flow_policy_spec preview_audio_device_change_policy_spec --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "(preview_audio_worker|preview_audio_command_queue|preview_audio_playback_flow_policy|preview_audio_device_change_policy)_spec"
```

Expected: new token/superseded-Play assertions FAIL.

- [ ] **Step 3: Make manual pause non-blocking**

Capture the wall-clock second, stop timers and stage media, update
`qtPreviewPlaying_`, pause controls/timeline, and publish
`preview.playback.changed(state=paused)` before submitting the reserved
`ManualPause`. Store a separate pending manual-pause generation/transaction and
retain the immediate wall-second only as the visual fallback. The matching
`previewPlaybackPaused` completion is the only path that writes retained mode,
retained BGM state, and the authoritative retained second; its backend position
is logged as a diagnostic delta against the captured wall second. A completion
that fails, is stale, or is superseded leaves the newer UI/retained state alone.
Never resume automatically and never overwrite the captured wall second with an
unaccepted backend result.

- [ ] **Step 4: Implement device pause with a separate pending transaction and worker barrier**

In `pausePreviewForAudioDeviceChange`, increment sequence before the playing
guard. On the first active event, copy `activePreviewPlaybackTransactionId_`
into `pendingPauseTransactionId_`, set `pendingPauseToken_`, and invalidate
startup. After the immediate GUI/video freeze, call a dedicated facade method
such as `requestDeviceChangePause(pauseIdentity, wallSecond)`. Inside that one
facade operation, atomically advance the runtime generation, call
`queue.invalidateBefore(newGeneration)`, and enqueue the reserved command; it
returns the generated identity/acceptance metadata without exposing the queue
to MainWindow. The worker records
`pauseBarrierGeneration` when it starts `DeviceChangePause`, drops queued
`Start`/`Commit`/retained `Resume/Seek` commands below that barrier, and reports
each dropped command as stale. Perform the same immediate GUI/video pause, then
submit `DeviceChangePause`, which stops BGM/touch-hold/one-shots and disarms the
scheduler. Accept completion only with generation + pending transaction +
immutable token. Do not compare against latest `deviceSequence`; do not auto-
resume. A user Play clears/supersedes the pending token and uses a strictly
higher generation, so it can run after the barrier.

- [ ] **Step 5: Run GREEN**

```bash
cmake --build build --config Release --target preview_audio_worker_spec preview_audio_command_queue_spec preview_audio_playback_flow_policy_spec preview_audio_device_change_policy_spec MiaCode --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "(preview_audio_worker|preview_audio_command_queue|preview_audio_playback_flow_policy|preview_audio_device_change_policy)_spec"
```

Expected: specs PASS and app builds.

- [ ] **Step 6: Add matching manual-pause completion handling**

Route `previewPlaybackPaused` through one GUI handler that distinguishes the
pending manual token from the pending device token. For a current manual pause,
copy the completion's retained mode/BGM state and accepted pause second into
the GUI retained-state members before allowing a later Play to choose exact
resume versus retained seek. For a current device pause, clear only the pending
device fields and refresh cheap diagnostics; never resume or overwrite a newer
Play. For either kind, a stale/superseded completion is diagnostic-only. Add
these two acceptance cases to `PreviewAudioPlaybackFlowPolicySpec` before the
commit.

- [ ] **Step 7: Commit**

```bash
git add src/audio/PreviewAudioPlaybackFlowPolicy.h src/audio/PreviewAudioWorker.h src/audio/PreviewAudioWorker.cpp src/audio/PreviewAudioCommandQueue.h src/audio/PreviewAudioCommandQueue.cpp src/audio/QtPreviewSfxRuntime.h src/audio/QtPreviewSfxRuntime.cpp src/tools/preview/PreviewAudioWorkerSpec.cpp src/tools/preview/PreviewAudioCommandQueueSpec.cpp src/tools/preview/PreviewAudioPlaybackFlowPolicySpec.cpp src/tools/preview/PreviewAudioDeviceChangePolicySpec.cpp src/app/mainwindow/MainWindowMemberStorage.inc src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackState.cpp src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp src/audio/PreviewAudioDeviceWatcher.h src/audio/PreviewAudioDeviceWatcher.cpp
git commit -m "fix(audio): pause preview immediately on device change"
```

### Task 9: Migrate Ticks, Warmup, Dialog, Latency, Probe, And Shutdown

**Files:**
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- Modify: `src/app/mainwindow/sections/preview/MainWindow.PreviewWarmupAndSettings.cpp`
- Modify: `src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.AudioSettings.cpp`
- Modify: `src/tools/latency/LatencySandboxController.h`
- Modify: `src/tools/latency/LatencySandboxController.cpp`
- Modify: `src/tools/probe/SoundtouchProbe.cpp`
- Modify: `src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp`
- Modify: other fire-and-forget runtime call sites found by the final scan
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing worker integration cases for coalesced ticks and GUI consumers**

Extend worker/facade specs so hundreds of `syncBackgroundTrack`/`drainEvents`
calls leave at most one latest slot per kind/generation; audio readiness changes
only from a ready completion whose `assetGeneration` matches the latest chart
and settings state; an older reload completion cannot set
`previewSfxRuntimePrepared_`; audition returns `true` only when accepted while
the snapshot is ready for the same asset generation and reports actual success
through completion. Verify no GUI consumer calls either non-GUI wait method.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target preview_audio_worker_spec preview_audio_non_gui_barrier_spec --parallel 1
```

Expected: at least the newly specified readiness/audition behavior fails before consumer migration.

- [ ] **Step 3: Migrate GUI consumers to queue/signal semantics**

- `PreviewTick`: submit latest-only BGM sync/event drain, including terminal flush.
- `PreviewTimelineFlow`: submit ordered paused-state/config work.
- `PreviewWarmupAndSettings`: increment asset generation on superseding chart/warmup/settings state and set `previewSfxRuntimePrepared_` only from a matching ready completion; reload/apply remain ordered.
- Audio settings dialog and `LatencySandboxController`: use queued readiness/audition completions; never wait or call `processEvents()`.
- Every remaining fire-and-forget caller submits immutable data with current generation and tolerates `ShuttingDown`/`QueueFull` completion.

- [ ] **Step 4: Adapt the command-line probe and teardown**

`SoundtouchProbe` keeps constructing/enqueueing through the facade on its main
thread, then starts a dedicated `std::thread` waiter that calls the condition-
variable barriers for the reload/start command sequences. The main thread joins
that waiter before destroying the facade; the waiter never owns the facade and
neither thread pumps Qt events. This satisfies the Task 3 owner-thread guard
instead of weakening it. MainWindow shutdown stops/disconnects the device
watcher and producers before calling `prepareForShutdown()`; late native
callbacks see the cleared owner and return. Add worker sources to the probe
target as well as `MiaCode`.

- [ ] **Step 5: Run GREEN and the forbidden-call scan**

```bash
cmake --build build --config Release --target preview_audio_worker_spec preview_audio_non_gui_barrier_spec soundtouch_probe MiaCode --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "preview_audio_(worker|non_gui_barrier)_spec"
rg -n "waitFor(Ready|Completion)ForNonGui|processEvents\(|backend_->" src/app src/tools/latency src/tools/probe src/audio/QtPreviewSfxRuntime*
```

Expected: tests/build PASS; waits occur only in the probe/spec, no GUI `processEvents()` workaround, and no facade direct backend call.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp src/app/mainwindow/sections/preview/MainWindow.PreviewWarmupAndSettings.cpp src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.AudioSettings.cpp src/tools/latency/LatencySandboxController.h src/tools/latency/LatencySandboxController.cpp src/tools/probe/SoundtouchProbe.cpp src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp src/app/mainwindow
git commit -m "refactor(audio): migrate preview runtime consumers"
```

### Task 10: Pure Log-Pruning Policies

**Files:**
- Create: `src/common/LogEmissionPolicy.h`
- Create: `src/tools/debug_index/LogPruningPolicySpec.cpp`
- Modify: `src/audio/BassPreviewSfxSchedulerPolicy.h`
- Modify: `src/tools/preview/BassPreviewSfxSchedulerPolicySpec.cpp`
- Modify: `src/common/UiHangWatchdogPolicy.h`
- Modify: `src/tools/debug_index/UiHangWatchdogPolicySpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing pure policy specs**

Cover:

- `EdgeLogGate<Key>`: first value, changed value, unchanged suppression, explicit reset;
- stage `(rate, mediaKind)` key and per-chart reset;
- mouse signature: first complete signature emits, identical watched-object repeats suppress, mismatches remain visible;
- rebuild window: `<2 ms` aggregation, one-second flush, slow/error pre-flush and individual emission, destruction flush fields;
- background/surface signature lifecycle reset and slow workspace call bypass;
- BASS `shouldLogDisarm`: only `(false,false,-1)` suppresses;
- watchdog suppression episode: first false starts count, later false increments, next true or episode end flushes count/id/trigger.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target log_pruning_policy_spec bass_preview_sfx_scheduler_policy_spec ui_hang_watchdog_policy_spec --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "(log_pruning_policy|bass_preview_sfx_scheduler_policy|ui_hang_watchdog_policy)_spec"
```

Expected: new assertions FAIL because the policies are absent.

- [ ] **Step 3: Implement I/O-free state machines**

`LogEmissionPolicy.h` contains only templates/value state and injected timestamps; it must not include `DebugLog.h` or perform I/O. Keep BASS and watchdog rules in their existing domain policy headers. Return decisions/summary payloads to emitters rather than accepting logging callbacks.

- [ ] **Step 4: Run GREEN**

Run the commands from Step 2.

Expected: all three policy specs PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/common/LogEmissionPolicy.h src/tools/debug_index/LogPruningPolicySpec.cpp src/audio/BassPreviewSfxSchedulerPolicy.h src/tools/preview/BassPreviewSfxSchedulerPolicySpec.cpp src/common/UiHangWatchdogPolicy.h src/tools/debug_index/UiHangWatchdogPolicySpec.cpp
git commit -m "test(logging): define diagnostic pruning policies"
```

### Task 11: Wire Audio, Media, Watchdog, And Scene Log Gates

**Files:**
- Modify: `src/audio/BassPreviewAudioBackend_EventDrain.cpp`
- Modify: `src/preview/runtime/PreviewStageMediaHost.h`
- Modify: `src/preview/runtime/PreviewStageMediaHost_Backend.cpp`
- Modify: `src/preview/runtime/PreviewStageMediaHost_Media.cpp`
- Modify: `src/common/UiHangWatchdog.cpp`
- Modify: `src/timeline/quick/TimelineQuickItem.h`
- Modify: `src/timeline/quick/TimelineQuickItem.cpp`

- [ ] **Step 1: Add emitter-facing failing assertions where practical**

Extend the pure specs with exact payload checks for the fields the emitters consume: rebuild summary `window_start_ms/rebuild_count/total_elapsed_ms/max_elapsed_ms/last_lines` and watchdog `suppressed_count/episode_id/trigger`. Confirm deferred/flushed/error playback-rate events bypass the ordinary rate gate.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target log_pruning_policy_spec bass_preview_sfx_scheduler_policy_spec ui_hang_watchdog_policy_spec --parallel 1
```

Expected: exact payload/bypass assertions FAIL until policy results are complete.

- [ ] **Step 3: Wire each emitter without changing retained records**

- Suppress BASS scheduler disarm only for `was_active=0 had_sync=0 group_idx=-1`.
- Gate only ordinary stage `playback_rate`; reset on actual chart/media path change. Preserve `playback_rate_deferred`, `playback_rate_flushed`, and errors.
- Collapse watchdog `will_report=0` polls within an episode and attach the summary to the next report or episode-end record.
- Aggregate ordinary `<2 ms` scene rebuild pairs for one second; flush on interval, slow/error rebuild, and `TimelineQuickItem` destruction. Preserve every `>=2 ms` rebuild and `update_paint_node_stats`.

- [ ] **Step 4: Run GREEN and retained-log scan**

```bash
cmake --build build --config Release --target log_pruning_policy_spec bass_preview_sfx_scheduler_policy_spec ui_hang_watchdog_policy_spec MiaCode --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "(log_pruning_policy|bass_preview_sfx_scheduler_policy|ui_hang_watchdog_policy)_spec"
rg -n "playback_rate_(deferred|flushed)|update_paint_node_stats|device_change_(ignored|begin|complete)|bass_status|underrun|stall" src
```

Expected: specs/build PASS and every retained diagnostic family still has an emitter.

- [ ] **Step 5: Commit**

```bash
git add src/audio/BassPreviewAudioBackend_EventDrain.cpp src/preview/runtime/PreviewStageMediaHost.h src/preview/runtime/PreviewStageMediaHost_Backend.cpp src/preview/runtime/PreviewStageMediaHost_Media.cpp src/common/UiHangWatchdog.cpp src/timeline/quick/TimelineQuickItem.h src/timeline/quick/TimelineQuickItem.cpp
git commit -m "refactor(logging): suppress no-op audio and scene diagnostics"
```

### Task 12: Wire QuickShell And Background Edge Logging

**Files:**
- Modify: `src/app/quick_shell/QuickShellBootstrap.h`
- Modify: `src/app/quick_shell/QuickShellBootstrap.cpp`
- Modify: `src/app/quick_shell/QuickShellController.h`
- Modify: `src/app/quick_shell/QuickShellController.cpp`
- Modify: `src/app/ui/AppBackgroundPainter.h`
- Modify: `src/app/ui/AppBackgroundPainter.cpp`
- Modify: `src/tools/debug_index/LogPruningPolicySpec.cpp`

- [ ] **Step 1: Add failing lifecycle/abnormal-edge cases**

Prove mouse `mousePressSequence` increments at the native press entry, identical signatures within that sequence suppress, and a mismatching watched-object row remains. Prove unchanged background settings emit once per lifecycle/signature and real settings changes always emit. Prove sidebar/workspace `(handle,size)` repeats suppress independently, lifecycle/host replacement resets, and `elapsed_ms >= 50` workspace warnings always emit.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --config Release --target log_pruning_policy_spec --parallel 1
```

Expected: new sequence/lifecycle/slow-call assertions FAIL.

- [ ] **Step 3: Add per-owner policy members and wire emission decisions**

Keep state in the owning long-lived object, not static globals. Reset on construction/destruction, surface-host replacement, or actual settings/media lifecycle changes. Do not change hit-testing, mouse routing, surface sizing, or visual settings behavior.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build --config Release --target log_pruning_policy_spec MiaCode --parallel 1
ctest --test-dir build -C Release --output-on-failure -R "log_pruning_policy_spec"
```

Expected: spec PASS and app builds.

- [ ] **Step 5: Commit**

```bash
git add src/app/quick_shell/QuickShellBootstrap.h src/app/quick_shell/QuickShellBootstrap.cpp src/app/quick_shell/QuickShellController.h src/app/quick_shell/QuickShellController.cpp src/app/ui/AppBackgroundPainter.h src/app/ui/AppBackgroundPainter.cpp src/tools/debug_index/LogPruningPolicySpec.cpp
git commit -m "refactor(logging): edge-trigger repeated GUI diagnostics"
```

### Task 13: Documentation And Complete Verification

**Files:**
- Modify: `docs/ops/DEBUG_INDEX.md`
- Modify: `.agents/skills/miacode-dev-guide/references/architecture-and-layout.md`
- Modify: `.agents/skills/miacode-dev-guide/references/feature-index.md`
- Modify: `.agents/skills/miacode-dev-guide/references/cross-chain-linkage.md`
- Modify: `.agents/skills/miacode-dev-guide/references/debug-and-logging.md`
- Modify: `.agents/skills/miacode-dev-guide/references/build-and-tools.md`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Update maintained source-of-truth docs**

Document the in-process audio thread boundary (distinct from the deleted preview render process), worker-owned backend/health sampling, generation/transaction/pause-token completion contract, device auto-pause/no-auto-resume behavior, BASS device lease ownership, new specs, and exact retained/suppressed log families. Remove stale claims that GUI code calls the backend or that an independent sampler owns BASS queries.

- [ ] **Step 2: Run source and whitespace audits**

```bash
git diff --check
rg -n "backend_->|audioHealthSamplerThread_|#define QtPreviewSfxRuntime MiniaudioPreviewAudioBackend" src/audio/QtPreviewSfxRuntime* src/app src/audio/MiniaudioPreviewAudioBackend.cpp
rg -n "BASS_(Init|Free|GetDevice)" src --glob '!src/audio/PreviewBassDeviceLease.cpp'
rg -n "waitFor(Ready|Completion)ForNonGui" src
git status --short
```

Expected: no whitespace errors; no direct facade backend/macro/sampler ownership; any remaining process-wide BASS calls are explicitly justified and lease-covered; non-GUI waits occur only in probe/spec code; no logs or build artifacts are staged.

- [ ] **Step 3: Build the affected Release targets concurrently**

Before building, perform the one-build-at-a-time process check required by `miacode-concurrent-build`. Then run:

```bash
cmake --build build --config Release --target MiaCode soundtouch_probe preview_audio_command_queue_spec preview_audio_worker_protocol_spec preview_audio_worker_spec preview_audio_non_gui_barrier_spec preview_bass_device_lease_spec preview_audio_playback_flow_policy_spec log_pruning_policy_spec bass_preview_sfx_scheduler_policy_spec preview_audio_device_change_policy_spec preview_audio_health_spec ui_hang_watchdog_policy_spec --parallel 1
```

Expected: every target builds successfully in Release.

- [ ] **Step 4: Run focused and full available CTest**

```bash
ctest --test-dir build -C Release --output-on-failure -R "(preview_audio|preview_bass_device_lease|bass_preview|log_pruning|ui_hang_watchdog)"
ctest --test-dir build -C Release --output-on-failure
```

Expected: focused tests and the complete configured suite PASS.

- [ ] **Step 5: Perform Windows BASS manual acceptance**

Run the fresh `build-devtools/Release/MiaCode.exe --debug` through the verified desktop shortcut. During BGM playback, repeat default-output switches and physical plug/unplug, then test shutdown during an in-flight change. Confirm:

- UI heartbeat/render callbacks continue while worker pause takes 2-5 seconds;
- pause button/timeline freeze immediately at the captured wall second;
- audio/one-shot/touch-hold stop when the worker reaches the command;
- exactly one matching completion is accepted and stale completions do not move the playhead;
- playback stays paused until Play, and resume has no post-stall progress jump;
- debug logs retain listener/change/begin/complete, worker latency/error, status/edge, watchdog stall, and paint-node evidence while the specified no-op families are reduced;
- shutdown completes cleanly during a pending device change.

If this macOS worktree cannot perform the Windows hardware run, record it explicitly as an outstanding manual acceptance item; do not claim it passed.

- [ ] **Step 6: Commit docs/final integration fixes**

```bash
git add CMakeLists.txt docs/ops/DEBUG_INDEX.md .agents/skills/miacode-dev-guide/references/architecture-and-layout.md .agents/skills/miacode-dev-guide/references/feature-index.md .agents/skills/miacode-dev-guide/references/cross-chain-linkage.md .agents/skills/miacode-dev-guide/references/debug-and-logging.md .agents/skills/miacode-dev-guide/references/build-and-tools.md
git commit -m "docs: record preview audio worker diagnostics"
```

## Final Review Gate

After Task 13, request one full spec-compliance review against
`docs/superpowers/specs/2026-08-08-preview-audio-worker-autopause-and-log-pruning-design.md`, then one code-quality/concurrency review over the complete diff. Resolve and re-review all blocking findings before declaring completion. In particular, reviewers must trace every public `QtPreviewSfxRuntime` method to prove no backend/native call remains on the GUI thread, and must inspect shutdown/callback lifetime, reserved pause capacity, immutable pause-token matching, and BASS lease lock scope.
