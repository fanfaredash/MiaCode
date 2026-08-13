# Preview Audio Worker, Device Auto-Pause, And Log Pruning Design

- Date: 2026-08-08
- Branch: `codex/windows-idle-freeze-diagnostics`
- Supersedes: `2026-08-06-preview-audio-device-autopause-design.md`
- Background: `logs 18` and `docs/audit/BRANCH_AUDIT_WINDOWS_IDLE_FREEZE_DIAGNOSTICS_ZH.md`

## Goal

Move preview audio backend ownership and all backend calls out of the GUI
thread, so an output-device switch cannot stop GUI event processing. When an
output is plugged, unplugged, or the default output changes during preview,
the UI behaves like a normal player: it pauses immediately and remains paused
until the user presses Play. The change also removes diagnostic records that
are provably no-ops or duplicate observations while retaining enough event,
latency, error, and underrun evidence to diagnose the next incident.

## Evidence and boundary

The six `logs 18` GUI stall episodes are between 1.837 s and 3.998 s. In each
episode the last GUI-side audio boundary is `device_change_pause_begin`, and
the corresponding `bass_transport`/`bass_sample` pause work runs on the same
GUI thread. The wall clock continues while that synchronous call is blocked,
which explains the progress jump at the end of the stall. Moving only the
health sampler was insufficient because it changed diagnostic queries, not the
transport call that owns the device switch.

The existing device-change policy and its snapshot semantics remain the
trigger contract. This design changes the execution boundary of the pause and
the rest of the preview audio API; it does not attempt another silent
re-anchor, audio-device recreation, or watchdog stack-walk strategy.

The worker boundary applies to the realtime preview runtime on every platform
where `QtPreviewSfxRuntime` is built. The device watcher keeps the current
evidence boundary (BASS preview on Windows/macOS) until a separate miniaudio
device-change acceptance run proves equivalent behavior; moving miniaudio
backend calls off the GUI is still in scope. CLI/probe users of the runtime are
adapted explicitly rather than silently relying on GUI event processing.
The guaranteed auto-pause acceptance target for this change is therefore the
affected Windows/macOS BASS route; Linux receives the worker isolation but not
a newly enabled watcher policy until that separate run.

## Options considered

### A. Dedicated worker owning the complete backend (selected)

`QtPreviewSfxRuntime` becomes a non-blocking GUI facade. A dedicated worker
thread creates, uses, and destroys the selected BASS or miniaudio backend.
Commands cross the boundary through a priority/coalescing queue, and GUI
getters read a published snapshot. This removes every routine backend call
from the GUI and gives device pause priority over stale timeline ticks.

### B. Asynchronous device pause only

Only the device-change pause would be queued. Existing reload, seek, rate,
stop, and health calls would remain synchronous. This would reduce the first
diff but leaves several known BASS blocking points on the GUI and does not
meet the requirement to move audio processing out of the GUI.

### C. Separate audio process

An IPC audio service would isolate a wedged native library from the editor.
It requires a new protocol, packaging, process recovery, and export/audition
integration. That is a larger product boundary than the observed defect
requires and is deferred.

## Architecture

### 1. Ownership and thread boundary

Add `PreviewAudioWorker` and a small protocol layer under `src/audio/`.

- The worker owns the `PreviewAudioBackend` unique pointer and constructs it
  only after its thread starts. Backend initialization, asset loading,
  timeline preparation, transport, SFX, BGM, `BASS_ErrorGetCode`, and backend
  destruction all happen on the worker thread.
- The BASS mixer callback remains a native audio callback. It may update the
  worker-owned scheduler state and publish a snapshot, but it never invokes a
  GUI object, writes a GUI-owned field, or performs file logging while holding
  the scheduler lock.
- The current BASS health sampler must stop issuing independent BASS calls
  from a second owner thread. Health samples are collected by the audio worker
  (or from callback-published counters) and exposed through the same snapshot.
- Waveform decoding and video/export audio remain separate consumers. They
  must not borrow the preview backend pointer or call preview transport APIs.
  Any process-wide BASS device lifetime shared with waveform code is guarded
  by the `PreviewBassDeviceLease` described below; the implementation must not
  introduce a second uncoordinated `BASS_Init` owner.

`QtPreviewSfxRuntime` remains a `QObject` on the GUI thread. It owns the
worker handle, command queue, immutable command data, and a mutex/atomic
published snapshot; it does not own a backend and never calls a backend
virtual directly.

The worker is a non-`QObject` loop on one `std::thread`; this avoids giving a
backend `QObject` a GUI parent or depending on a second Qt event loop. The
facade constructs the queue, callback receiver, and thread in that order. The
thread creates the backend inside `workerLoop()`, publishes `Constructing` then
`Ready` (or `Degraded`), and destroys the backend immediately before the loop
returns. Completions use `QMetaObject::invokeMethod(facade, functor,
Qt::QueuedConnection)`, with the facade as the receiver, so delivery is on the
GUI event loop. The facade destructor first marks callback delivery disabled,
disables command producers, requests shutdown, and joins; only after the join
does it release the receiver and queue. Qt discards any queued functors whose
receiver has been destroyed, so a completion cannot dereference a dead
facade. No worker callback retains a raw `MainWindow` or `TimelineSection`.

### 2. Command protocol

Create a typed command protocol with a monotonic `generation`, a caller-owned
`transactionId`, and a runtime-owned `deviceSequence`. The rules are:

- `generation` starts at zero and increments on every playback boundary:
  start request, seek request, manual pause, stop, and active device pause.
  Asset/settings commands do not change it.
- `transactionId` is the existing MainWindow playback transaction. A command
  that is not part of a playback transaction carries zero.
- `deviceSequence` increments for every real watcher `Change`, including an
  ignored notification while paused. The GUI increments it before the
  `playing` guard so every `device_change_ignored` row has a sequence. An
  active pause also captures a separate immutable `pauseToken` (the sequence
  at the first active notification); this token, not the mutable latest device
  sequence, identifies the in-flight pause completion.
- A device pause advances `generation` before it is enqueued. It calls
  `invalidateBefore(newGeneration)` on queued playback commands. An already
  executing native call cannot be canceled; its completion is marked stale and
  cannot mutate the published playback state.
- The worker records `pauseBarrierGeneration` when it begins a device pause and
  drops queued start/commit/resume work with a lower generation. A later user
  Play receives a strictly higher generation and is allowed to run after the
  barrier. Duplicate device events while a pause is pending are coalesced into
  the existing request and never create a second resume path. A duplicate may
  advance `deviceSequence` and be logged as ignored, but it cannot replace the
  pending `pauseToken` or wall-clock anchor.

The GUI accepts a completion only when its `generation` equals the current
runtime generation and its nonzero `transactionId` equals the active
transaction. For a device pause, it additionally requires that
`completion.pauseToken` equals the current pending pause token. It does not
compare a pause completion to the latest mutable `deviceSequence`; this lets
duplicate notifications be coalesced without making the first valid
completion stale. Asset completions may use the current generation but are
also rejected when their asset generation is older than the latest
chart/settings generation.
Rejected completions are logged as stale and cannot update UI state.

Commands are classified as follows:

| Class | Examples | Queue rule |
| --- | --- | --- |
| High priority | shutdown, device-change pause, manual pause, stop-all | FIFO among control commands; placed ahead of pending ticks |
| Ordered control | reload assets, configure/clear timeline, prepare/commit/cancel, start/seek, rate/level changes | FIFO; a newer generation invalidates stale playback commands |
| Latest-only tick | `syncBackgroundTrack`, `drainEvents` | one replaceable slot per generation; old ticks are discarded |
| Audition | one-shot audition and dialog preview | ordered after the latest control barrier; returns completion, never blocks GUI |

The queue has a bounded wake-up mechanism: posting a command wakes the worker,
but repeated ticks do not enqueue an unbounded number of wake events. It has
three explicit capacities: 16 high-priority commands, 128 ordered controls,
and 32 auditions, plus one replaceable slot for each latest-only tick kind.
The high-priority deque reserves one slot each for shutdown, device pause, and
manual pause. Duplicate device pauses and latest ticks replace/coalesce; an
ordered or audition command that would exceed its capacity is rejected
immediately and gets a `QueueFull` completion (never silently dropped). A
manual/device pause is accepted whenever shutdown has not begun, even when the
ordinary high-priority deque is full, by using its reserved safety slot. If the
worker is already inside an uninterruptible native call, the safety command
runs immediately after that call; the GUI remains responsive and the delay is
reported. Enqueuing after shutdown is rejected and logged once.

The state transitions for device events and completions are:

| Event | `deviceSequence` | `pauseToken` | `pause transaction` | `generation` | Completion rule |
| --- | ---: | ---: | ---: | ---: | --- |
| change while paused | increment | none | none | unchanged | no pause completion |
| first change while playing | increment | set to that value | capture active txn | increment | accept matching token/txn/generation |
| duplicate while pause pending | increment | unchanged | unchanged | unchanged | coalesce; first token remains valid |
| user Play after pause | unchanged | cleared after completion or superseded | cleared/superseded | increment | accept new transaction/generation |
| late old completion | unchanged | unchanged | unchanged | unchanged | log stale; never mutate state |

### 3. Facade API and snapshot

Keep public method names where that minimizes caller churn, but change their
semantics explicitly:

- `void` mutators enqueue and return immediately.
- `audioEngineInitialized`, `hasBackgroundTrack`,
  `isBackgroundTrackRunning`, retained-state getters, and playback-second
  getters read the last published `PreviewAudioSnapshot`. The snapshot carries
  a sequence, backend-ready/error state, current transaction/generation,
  retained mode/BGM state, prepared second, authoritative second, running
  state, and the last command error. Reads are lock-free for scalar fields or
  protected by one short mutex for the aggregate; they never call BASS.
- Operations that previously returned a backend-computed value
  (`preparePreviewPlaybackTransaction`, retained resume/seek, pause result,
  and audition readiness) publish a completion signal carrying the command
  sequence and result. Callers must ignore a completion whose generation or
  transaction no longer matches. The concrete GUI signals are
  `backendReadyChanged(BackendReadyResult)`,
  `previewPrepared(PreviewPreparedResult)`,
  `previewPlaybackStarted(PreviewStartedResult)`,
  `previewPlaybackPaused(PreviewPausedResult)`,
  `retainedPlaybackCompleted(RetainedPlaybackResult)`, and
  `auditionCompleted(AuditionResult)`. Each payload contains the command
  sequence, generation, transaction id, device sequence, success/error code,
  and the operation-specific value. The immediate facade fallbacks are:
  `prepare...` and `start...` return the clamped requested second;
  retained resume/seek return the requested target; pause returns a result
  whose `pauseSecond` is the supplied wall-clock second and whose retained/BGM
  flags come from the last snapshot; `audition` returns true only when the
  command was accepted while the snapshot is ready (true means queued, not
  audible yet), and false otherwise. `audioEngineInitialized()` stays false
  until the worker publishes `Ready`; retained getters stay at their last
  snapshot values until a matching completion. These fallbacks are explicitly
  not claims that the backend has finished.
- `prepareForShutdown()` is the only GUI-facing operation allowed to wait. The
  explicitly named `waitForReadyForNonGui`/
  `waitForCompletionForNonGui` barriers are restricted to CLI/test callers and
  assert that the caller is not the facade's owning thread. `prepareForShutdown()`
  then
  rejects new work, sends a high-priority shutdown command, joins the worker,
  then destroys the backend on the worker thread. The join duration and any
  forced error are recorded in the runtime channel.

The protocol types are value types containing Qt implicitly shared data or
plain scalars. No command captures `MainWindow`, `TimelineSection`, raw
`QObject*`, or references to GUI-owned containers.

### 4. Device-change state flow

The watcher continues to emit only a real `Change` from the snapshot policy.
The GUI consumer follows this sequence:

1. Increment the device-change sequence first. If preview is not playing, record
   an ignored change and return.
2. If a device pause is already pending, record the new change as ignored and
   coalesced; keep the existing `pauseToken` and wall-clock anchor.
3. Capture the authoritative wall second before dispatching any backend command.
4. Cancel pending startup, pause the stage-media route, stop preview timers,
   set `qtPreviewPlaying_` false, update the pause button and timeline state,
   and publish the normal `preview.playback.changed(state=paused)` event.
   These operations contain no audio backend call.
5. Submit a reserved high-priority `DeviceChangePause` command with the captured second,
   transaction, and change kind. The worker stops BGM/touch-hold, disarms the
   SFX scheduler, and stops one-shot voices. It then publishes a completion
   with elapsed milliseconds and the backend result.
6. The GUI completion handler accepts the result only when
   `completion.generation == currentGeneration`,
   `completion.pauseToken == pendingPauseToken`, and
   `completion.transactionId == pendingPauseTransactionId`; it then clears the
   pending pause fields and refreshes cheap UI state. It never resumes
   playback. A late completion from an old generation/token is
   diagnostic-only.

Manual pause uses the same non-blocking UI transition and reserved high-priority
worker pause command; all pause variants are high priority and are never
ordinary ordered controls. Its wall-clock pause second remains authoritative;
a backend-reported position is retained only as a diagnostic comparison.
Repeated device events
while the pause is pending are coalesced and cannot restart playback.

Before step 5 the GUI copies the current playback transaction into
`pendingPauseTransactionId`. It may clear
`activePreviewPlaybackTransactionId_` as part of freezing the UI; the pending
copy is the only transaction id used to accept the eventual pause completion.
If the user presses Play before that completion arrives, the new Play clears
the pending pause token/transaction and advances `generation`, making the old
completion diagnostic-only. This explicitly permits a responsive user action
without allowing an old pause to overwrite the new play state.

### 5. Startup, seek, and resume migration

The existing startup handshake is retained but split at the backend boundary:

- GUI submits warmup/reload/configuration and marks the runtime as pending.
- Worker publishes `assetsReady`/`prepared` with a transaction id and effective
  second. GUI commits only if the transaction is still active and the preview
  has not been paused by a newer device sequence.
- Retained resume and seek submit commands and optimistically update the visual
  requested second. The worker's completion is the authoritative confirmation;
  stale completions are dropped.
- A device pause invalidates any pending prepare/commit and clears the startup
  state on the GUI immediately, so a blocked backend cannot later commit an
  obsolete play request.

The stage-media/video route remains a separate owner. Its GUI-facing calls are
not converted into audio commands; the device path pauses it before submitting
the audio command, preserving existing A/V ownership and avoiding a new
cross-module dependency.

### 6. Non-main-window consumers

`src/tools/probe/SoundtouchProbe.cpp` is a command-line consumer. The facade
therefore exposes `waitForReadyForNonGui(timeout)` and
`waitForCompletionForNonGui(commandSequence, timeout)`, implemented with the
worker snapshot's `std::condition_variable`; they do not require a Qt event
loop and are legal only for callers that are not the facade's GUI thread. The
probe waits for reload/start completion before its polling loop and uses the
same barrier for shutdown. `LatencySandboxController` and the audio-settings
dialog remain GUI consumers and use the same queued completion signals as
`MainWindow`; they must not add a blocking wait to make their old synchronous
assumptions work. Export audio backends and their out-of-process worker
protocol do not use `QtPreviewSfxRuntime` and remain unchanged.

The synchronous callers to migrate are enumerated here so the plan cannot
miss a hidden result dependency: `MainWindow.PreviewPlaybackState.cpp` (pause,
prepare/commit/cancel, retained state), `MainWindow.TimelinePlayback.cpp`
(retained resume/seek and effective start), `MainWindow.PreviewTimelineFlow.cpp`
(paused-state/configuration), `MainWindow.PreviewTick.cpp` (latest ticks and
drain), `MainWindow.PreviewWarmupAndSettings.cpp` (ready),
`MainWindow.Dialogs.AudioSettings.cpp` (audition),
`LatencySandboxController.cpp` (timeline/configuration), and
`SoundtouchProbe.cpp` (non-GUI condition-variable barrier).

## Error handling and shutdown

- The worker lifecycle is `Constructing -> Ready -> Loading -> Ready`, or
  `Constructing/Loading -> Degraded`; every path can then enter
  `ShuttingDown -> Stopped`. Backend construction or reload failure publishes
  `backendReady=false`, resolves all commands currently waiting on that load
  with `BackendUnavailable`, and leaves the GUI paused. `setWarmupResolvedPaths`
  and an explicit `reloadAssets` are the only retry path; a device event never
  retries automatically. A later user Play may issue the normal reload/start
  sequence, which is an explicit retry and can return to `Ready`.
- Every completion includes `generation`, `transactionId`, `deviceSequence`
  where relevant, `workerThreadId`, elapsed time, and a success/error code.
- The worker catches backend-level failures at the command boundary, records
  the native error code on the worker thread, and continues processing safe
  commands where possible. It never calls GUI methods from an exception path.
- Shutdown first disables the watcher and command producers, then drains the
  high-priority shutdown path and joins. Destruction of the backend, BASS
  sampler state, and native endpoint resources is ordered on the owning thread.
  A device callback arriving during teardown is ignored after the watcher
  owner is atomically cleared.

All in-process BASS users coordinate process-wide device lifetime through a
new `PreviewBassDeviceLease` (`src/audio/PreviewBassDeviceLease.{h,cpp}`). It
owns the mutex and guarded reference count currently represented by
`gBassDeviceRefCount`; preview worker instances, the audio-settings runtime,
waveform's `ScopedBassWaveformDevice`, and any in-process export backend take
the lease around `BASS_Init`, `BASS_GetDevice`, `BASS_Free`, and refcount
updates. Channel/stream operations remain on their owning worker, but no two
threads can change the process-wide current device or free it concurrently.
The lease is not held across ordinary decoding or logging. A lease acquisition
can delay a worker, never the GUI. Separate export processes have a separate
lease instance by process and retain their existing protocol.

## Logging policy

### Retain

Keep device-list/default-output detection and native-listener registration,
`device_change_ignored/begin/complete`, wall/audio pause-stall measurements,
worker command latency/completion/error lines, BASS errors and underrun/stall
edges, approximately 1 Hz `bass_status`, actual `bass_sfx_mixer_trigger`
events, startup identity/timing, preview play/pause boundaries, and
`timeline/update_paint_node_stats`. These are needed to prove causality and
correlate GUI heartbeat gaps with worker progress.

### Deduplicate or suppress

- `preview/stage_media action=playback_rate`: emit on first state and on
  `(rate, media-kind)` changes. The dedup key is reset on chart/media path
  change and runtime construction; suppress repeated unchanged `rate=1.000`
  rows.
  Deferred, flushed, and error transitions remain visible.
- `bass_sfx_scheduler action=disarm`: suppress the 24 known no-op rows where
  `was_active=0 had_sync=0 group_idx=-1`; retain real disarms and state changes.
- `quick_shell/arrow_dispatch action=mouse_press_arm`: coalesce the repeated
  watched-object rows for one click. The filter assigns a monotonically
  increasing `mousePressSequence` at native mouse-press entry; the dedup key
  is `(mousePressSequence, global, hot_rect, prev_armed, new_armed, button)`.
  Retain the first complete signature and any abnormal/mismatching row.
- `timeline/quick_scene scene_state_rebuild_begin/end`: replace ordinary
  sub-2-ms begin/end pairs with a one-second summary flushed on the next
  summary interval, a slow/error rebuild, and item destruction. The summary
  fields are `window_start_ms, rebuild_count, total_elapsed_ms,
  max_elapsed_ms, last_lines`; retain every slow (>=2 ms) or error row with
  count, elapsed, and line-count evidence.
- `ui/hang_watchdog action=report_gate`: suppress repeated
  `will_report=0` polling rows within an episode. The first suppression starts
  an episode counter; subsequent suppressed polls increment it in memory. Flush
  `suppressed_count` with the next `will_report=1` or the existing episode-end
  row, retaining the episode id and trigger.
- `AppBackgroundPainter set_settings_unchanged` and unchanged-size
  QuickShell workspace/sidebar syncs: edge-trigger them by the existing
  settings signature or `(handle, size)` key, reset on a real change or
  lifecycle boundary, and retain actual changes and lifecycle boundaries.

Historical log directories are evidence and are never rewritten or deleted.
Only future emission behavior changes.

## Files and ownership

Create:

- `src/audio/PreviewAudioWorkerProtocol.h` — command, completion, and snapshot
  value types plus queue policy helpers.
- `src/audio/PreviewAudioCommandQueue.{h,cpp}` — bounded priority queue,
  latest-tick coalescing, generation invalidation, and shutdown gate.
- `src/audio/PreviewAudioWorker.{h,cpp}` — worker loop, backend ownership,
  command execution, health publication, and completion dispatch.
- `src/audio/PreviewAudioWorkerFactory.{h,cpp}` (or the existing backend
  factory seam) — injectable backend construction for protocol/thread-affinity
  tests without loading BASS.
- `src/audio/PreviewBassDeviceLease.{h,cpp}` — process-wide BASS init/free
  serialization shared by preview, waveform, and in-process export callers.
- `src/tools/preview/PreviewAudioCommandQueueSpec.cpp` and
  `src/tools/preview/PreviewAudioWorkerProtocolSpec.cpp` — pure/fake tests.

Modify:

- `src/audio/QtPreviewSfxRuntime.{h,cpp}` and split implementation units —
  replace direct backend calls with the facade/worker bridge.
- `src/tools/probe/SoundtouchProbe.cpp`, latency sandbox sources, and the audio
  settings dialog — consume readiness/completion instead of assuming a backend
  call completed synchronously.
- `src/audio/PreviewAudioBackend.h` and BASS/miniaudio implementations —
  expose worker-side health polling and remove the independent GUI-visible
  sampler ownership.
- `MainWindow` preview playback/startup/tick files and member storage —
  consume snapshot/completion signals and perform the immediate UI pause.
- `PreviewAudioDeviceWatcher`/bootstrap — keep one real-change path and disable
  it before worker shutdown.
- `src/common/WaveformCache.cpp` and
  `src/tools/video_export/BassExportAudioBackend.cpp` — use the shared BASS
  lease for process-wide init/free only; decode/render ownership remains
  unchanged.
- log emitters listed in the pruning table above.
- `CMakeLists.txt`, affected CTest registrations, `docs/ops/DEBUG_INDEX.md`,
  and the MiaCode dev-guide reference files (`feature-index`,
  `cross-chain-linkage`, `debug-flags`, `design-ledger`).

No export audio behavior or protocol changes in this design. If
`BassExportAudioBackend.cpp` is in-process for a target, its only permitted
source change is mechanical use of `PreviewBassDeviceLease` around
process-wide init/free; historical log files are never changed.

## Testing strategy

Implementation uses red-green-refactor cycles:

1. Queue policy tests prove priority ordering, latest-tick replacement,
   generation invalidation, shutdown rejection, capacity/backpressure, and
   device-pause coalescing.
2. Protocol tests prove snapshot publication is monotonic and stale
   completions cannot overwrite a newer transaction.
3. A fake backend/worker test records the executing thread and proves facade
   calls never execute backend methods on the caller (GUI) thread. It also
   covers ready/reload failure, explicit recovery, shutdown races, and stale
   completion rejection. A queue-full test fills ordinary controls, then
   verifies manual and device pause still obtain their reserved safety slots.
4. Device-change policy tests retain the existing reorder/add/remove/default/
   active-state coverage.
5. Log-pruning helper tests lock each dedup key, reset boundary, summary flush,
   and retained edge described above.
6. A non-GUI probe/barrier integration test runs without calling
   `QCoreApplication::processEvents()` and still observes ready/completion.
7. A Release build and affected CTest targets run before the full available
   suite. `git diff --check` is required.

Windows runtime acceptance (with `--debug`) requires repeated default-output
switches and physical plug/unplug during BGM playback:

- GUI heartbeat and QSG/render callbacks continue during the native pause;
- the pause button/timeline enter paused state before worker completion;
- one worker completion reports the pause duration and no stale completion
  changes the position;
- no one-shot tail is heard after pause;
- playback does not resume until the user presses Play, and resumed audio is
  anchored without a progress jump;
- shutdown remains clean during an in-flight device change.

The existing main-workspace modification in `src/wrapper/MiaCodeLauncher.cpp`
is outside this change and must remain untouched.
