# Preview Audio Device Reanchor Design

## Goal

When the default audio output changes during preview playback, or the BGM audio
clock differs from the wall-clock chart position by at least 50 ms, silently
re-anchor the running BASS transport to the wall-clock chart second. The
preview UI, visual clock, and stage-media playback remain uninterrupted.

## Evidence and boundary

`logs 5` proves that a device event leaves the BGM stream in a new, persistent
position while the wall clock keeps advancing. The existing SFX audio-clock
guard switches to the wall clock at 254.850 ms and 252.427 ms, protecting SFX
only; it does not reposition BGM. Manual pause/replay restores the baseline,
which proves that a transport re-anchor, rather than BASS engine recreation,
is the appropriate recovery operation.

`logs 4` is explicitly excluded from recovery: its large ramp occurs after
the BGM reaches end of media. The existing audio-clock availability check
already rejects an ended track, and normal tail SFX continue on the wall clock.

This change is limited to the BASS preview path on Windows and macOS. Linux's
miniaudio backend retains the existing wall-clock behaviour.

## Design

### 1. Default-output signal

`QtPreviewSfxRuntime` owns a `QMediaDevices` observer. It snapshots
`QMediaDevices::defaultAudioOutput().id()` at construction and emits an
`audioOutputDevicesChanged(bool defaultOutputChanged)` signal on every
`audioOutputsChanged` notification. The boolean is true only when the default
device identity differs from the prior snapshot. The runtime writes one
debug-only `preview_audio_device` line for the observed transition.

`MainWindow` connects that signal to `TimelineSection`. A non-default device
enumeration is logged but does not seek. A default-output change requests
immediate recovery while playback is active.

### 2. One recovery decision policy

`PreviewAudioRecoveryPolicy.h` is a small pure helper shared by the timeline
integration and its CTest specification. Its inputs are playback activity,
default-output change, audio-clock availability, and absolute clock delta.
It returns exactly one of:

- no action;
- `default_output_changed`;
- `drift_exceeded` when `abs(wall - BGM) >= 0.050` seconds.

The runtime keeps a single queued-recovery flag. It coalesces a device signal
and a same-tick drift observation into one GUI-loop callback; it does not add
retry loops, cooldown state, or engine recreation.

### 3. BASS transport re-anchor

`PreviewAudioBackend` gains an optional
`reanchorPlayingTransportAtChartSecond(second, reason)` operation. Its default
is a no-op returning false, preserving Linux behaviour. The BASS override
requires a running BGM, then reuses its existing retained-seek transaction at
the supplied wall-clock chart second with `continuePlaying=true`.

That transaction already pauses the affected samples, positions BGM using
`raw = chart + &first`, resets the SFX cursor without replaying elapsed notes,
restores active touchhold state, and resumes BGM. No `BASS_Free`,
`BASS_Init`, GUI pause, timer stop, stage-media pause, or visual-clock change
is involved.

The timeline logs a debug-only `preview/audio_reanchor` record before and
after the attempt, including reason, wall second, BGM second, pre-recovery
delta, and result. A successful re-anchor returns to the existing audio-clock
SFX path on the following tick.

## Data flow

1. `QMediaDevices` observes an output-device list change.
2. A changed default output queues recovery when preview playback is active.
3. Independently, `sfxDrainSecond()` sees a BGM/wall delta of at least 50 ms;
   it keeps SFX on the wall clock for that tick and queues the same recovery.
4. The queued callback samples the wall-clock chart second and calls the
   backend re-anchor operation.
5. BASS positions BGM and SFX scheduling state to that second and resumes;
   visuals and video continue on their existing wall-clock path.

## Tests and acceptance criteria

- A pure policy test locks: inactive playback never recovers; default-output
  changes recover during playback; a 49 ms delta does not; a 50 ms delta does.
- The existing retained-transport spec continues to lock the seek/resume
  action used by BASS recovery.
- A build verifies the Qt Multimedia observer and the added facade/backend API.
- In a Windows/macOS capture, an output change produces
  `preview_audio_device`, then one `preview/audio_reanchor`; the following
  `bass_status` is near the normal +/-10 ms band. No recovery occurs at BGM
  end-of-media.
