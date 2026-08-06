# Preview Audio Device Auto-Pause Design

- Date: 2026-08-06
- Branch: `codex/windows-idle-freeze-diagnostics`
- Supersedes: `2026-08-05-preview-audio-device-reanchor-design.md`
- Background: `docs/audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md` §1 (问题 3)

## Goal

When the set of audio output devices changes, or the system default output
changes, while the preview is playing, pause the preview. The user resumes
manually. Nothing attempts to correct the clock while playback continues.

## Why the approach changed

Three prior attempts tried to keep playback running and correct the anchor in
place:

| Commit | Attempt | Outcome |
|---|---|---|
| `188411d9` | Drain SFX on the BGM audio clock | Did not fix the reported desync |
| `6c0cd3e9` | `QMediaDevices` observer + drift-threshold silent re-anchor | Did not fix it |
| `073e31fa` | Schedule SFX on the BASS mixer via `BASS_SYNC_POS` | Did not fix it; also removed the device observer added by `6c0cd3e9` |

The audit's F3-2 already established the one recovery action that is known to
work: a pause/resume cycle, because `apply_paused_state` calls
`anchorTransportToSecond` and resume calls `startTransportFromCurrentAnchor`,
which is a forced re-anchor. Users have been performing that correction by
hand.

This design stops trying to re-anchor without pausing and instead performs the
known-good action automatically. Two consequences follow:

- The trigger becomes a deterministic device event instead of a threshold, so
  there is no `50 ms` / `250 ms` constant left to tune.
- The recovery is the existing, well-tested user-pause path rather than a new
  transport operation.

## Scope

Compiled only where `MIACODE_HAS_BASS_AUDIO` is defined (Windows and macOS).
The audit fixes 问题 3 to the BASS path; Linux runs `MiniaudioPreviewAudioBackend`
and is a separate, unproven problem (问题 4). Auto-pausing on Linux would be an
unfounded interruption.

Not in scope: the other 问题 3 triggers listed by users (app backgrounding,
laptop power-saving). Those are not device-list events and are not addressed
here.

## Design

### 1. `src/audio/PreviewAudioDeviceChangePolicy.h` — pure policy

```cpp
namespace miacode::preview_audio::device_change {

struct OutputSnapshot {
    QStringList outputIds;      // sorted output device ids
    QString defaultOutputId;
};

enum class Change { None, OutputListChanged, DefaultOutputChanged, Both };

Change compareSnapshots(const OutputSnapshot& before, const OutputSnapshot& after);
bool shouldPausePreview(Change change, bool previewPlaying);
const char* changeName(Change change);

}  // namespace miacode::preview_audio::device_change
```

The two snapshot fields map onto the two user-visible events: a changed
`outputIds` list is a hotplug (插拔设备); a changed `defaultOutputId` is the
system switching output (调整设备).

`outputIds` is sorted before comparison. Qt does not guarantee a stable order
from `QMediaDevices::audioOutputs()`, so an unordered comparison would report
a pure reorder as a change.

No Qt Multimedia dependency, so the CTest spec links only `Qt6::Core` and runs
on a machine with no sound device.

### 2. `src/audio/PreviewAudioDeviceWatcher.{h,cpp}` — Qt signal translation

A `QObject` that owns a `QMediaDevices` and takes the first snapshot at
construction. On each `QMediaDevices::audioOutputsChanged` it re-snapshots and
calls `compareSnapshots`. Only when the result is not `Change::None` does it
store the new snapshot and emit `outputConfigurationChanged(Change)`.

Under `--debug` it writes one line to the Audio channel under the
`preview/audio_device` tag, carrying the change kind and the before/after
default-output ids. The Audio channel is deliberate: `appendPreviewPlaybackLog`
already writes `preview/playback pause_exact` there, so the device event and
the pause it causes land adjacent in `miacode_audio_debug.log` alongside the
`bass_status` / `bgm_delta_ms` rows used to judge the desync.

The class holds no playback state and no policy; its whole job is turning a
noisy Qt notification into a "something really changed" signal.

### 3. `MainWindow::TimelineSection::pausePreviewForAudioDeviceChange` — sole consumer

```
if (!shouldPausePreview(change, state_.qtPreviewPlaying_)) return;
pauseQtPreviewPlaybackExact();
owner_.updatePauseButtonAppearance();
```

The decision goes through the policy rather than an inline `qtPreviewPlaying_`
test so the branch the spec locks is the branch that ships. That guard covers
three cases at once:

- The preview is not playing, so there is nothing to correct.
- A video export is running. `MainWindow.ExportFlow.cpp:534` already pauses the
  preview before opening the export dialog, so `qtPreviewPlaying_` is false for
  the duration of an export and a device event cannot disturb it.
- One physical hotplug makes Qt emit several notifications (device removed,
  then default reassigned). The first one pauses; every later one is a no-op.
  This is why no debounce timer is needed.

`pauseQtPreviewPlaybackExact()` is the same function the pause button calls, so
the pause second, timeline centring, and the `preview.playback.changed`
extension event are identical to a manual pause.

`updatePauseButtonAppearance()` is state correctness, not notification: without
it the button would read "playing" while playback is stopped. Per the agreed
behaviour there is no status-bar message and no dialog — the pause is silent.

The watcher is constructed and connected in `MainWindow.FrameBootstrap.cpp`,
immediately after `previewSfxRuntime_` is created.

The export intro lead-in (`exportIntroLeadInActive_`) is deliberately left
alone. It plays a single `track_start` audition over overlay frames, with no
continuous BGM, so it has no clock to desynchronise.

## Data flow

1. A device is plugged, unplugged, or the system default output changes.
2. `QMediaDevices::audioOutputsChanged` fires, possibly several times.
3. `PreviewAudioDeviceWatcher` re-snapshots and emits once per real change.
4. `TimelineSection` pauses if and only if the preview is currently playing.
5. The user presses play. The existing resume path re-anchors the transport,
   which is the recovery the audit already proved works.

## Removal batch

The following have no call sites. `073e31fa` set `hasAudioClock = false`
unconditionally in `onQtPreviewTick()` (`MainWindow.PreviewTick.cpp:159`),
which orphaned `sfxDrainSecond()` and everything reachable only from it.
Deleting them changes no runtime behaviour.

| File | Removed |
|---|---|
| `src/audio/PreviewAudioRecoveryPolicy.h` | whole file |
| `src/tools/preview/PreviewAudioRecoveryPolicySpec.cpp` | whole file |
| `CMakeLists.txt` | the `PreviewAudioRecoveryPolicySpec` target |
| `MainWindow.PreviewTick.cpp` | `sfxDrainSecond()`, `requestPreviewAudioReanchor()`, the policy include |
| `MainWindow.TimelineSection.h` | the two matching declarations |
| `MainWindowMemberStorage.inc` | `sfxAudioClockActive_`, `previewAudioReanchorPending_` |
| `QtPreviewSfxRuntime.{h,cpp}` | `audioClockChartSecond`, `reanchorPlayingTransportAtChartSecond` |
| `PreviewAudioBackend.h` | both virtuals |
| `BassPreviewAudioBackend.h`, `_Transport.cpp`, `_PlaybackClock.cpp` | both overrides and their implementations |

Documentation updated in the same change: `docs/ops/DEBUG_INDEX.md:266` drops
`preview/sfx_clock` and `preview/audio_reanchor` and gains
`preview/audio_device`; the `cross-chain-linkage.md` and `hardcode-registry.md`
entries for the recovery policy are replaced in both the `.claude/skills` and
`.codex/skills` copies of `miacode-dev-guide`.

## Tests and acceptance criteria

`src/tools/preview/PreviewAudioDeviceChangePolicySpec.cpp` locks:

- a pure reorder of `outputIds` is `Change::None`;
- adding or removing a device is `Change::OutputListChanged`;
- only the default id changing is `Change::DefaultOutputChanged`;
- both changing is `Change::Both`;
- `shouldPausePreview` is false for every `Change` when playback is inactive,
  and true for every non-`None` `Change` when it is active.

Build verification is Release only, capped at `-j4`.

On-device acceptance (Windows or macOS, `--debug`): play a chart with BGM, then
plug or unplug a headset. The log shows one `preview/audio_device` line
followed by one `preview/playback pause_exact` line, and the preview stops.
After pressing play, `bgm_delta_ms` returns to its baseline band. Repeating the
hotplug while paused produces a `preview/audio_device` line and no pause line.
