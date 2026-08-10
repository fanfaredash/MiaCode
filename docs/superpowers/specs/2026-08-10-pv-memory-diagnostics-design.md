# PV Memory Diagnostics Design

## Goal

Provide low-overhead, internal-only diagnostics for a macOS realtime-PV memory-growth report. A normal `MiaCode --debug` run must create one dedicated log that lets an engineer rank the likely causes without UI automation or sampling `VTDecoderXPCService` or any other external process.

## Scope and Constraints

- The diagnostic is active only when normal runtime debug output is active.
- It writes a dedicated `miacode_pv_memory_debug.log` file, with an optional `MIACODE_PV_MEMORY_LOG_PATH` override. It must not add PV-memory records to the runtime or audio logs.
- It must not inspect, enumerate, or log external processes, including `VTDecoderXPCService`.
- It must not log decoded frame contents or create extra frame mappings/readbacks for diagnostics.
- Normal realtime preview may emit one periodic sample every five seconds while a PV is loaded. Lifecycle samples are allowed at defined boundaries.
- Existing runtime and audio logging behaviour must remain unchanged.

## Architecture

`DebugLog` owns a new `PvMemory` channel so rotation, asynchronous writing, directory selection, and channel-path override follow the existing logging contract. The channel is enabled by runtime debug mode. Debug-session startup resets the channel and writes one `action=session_start` record, so `MiaCode --debug` always creates the dedicated file even when no chart has a PV.

`ProcessDiagnostics` supplies a new structured current-process sample used only by this channel. Its exact fields are `process_resident_bytes`, `process_footprint_bytes`, `process_internal_bytes`, and `process_compressed_bytes`; every unavailable value is `-1`. On macOS it queries `task_info(mach_task_self(), TASK_VM_INFO, …)` and returns `-1` for all fields if the query fails. Other platforms use available native equivalents only when semantics match; otherwise they also report `-1`. The existing runtime resource-gauge payload remains backward compatible.

`PreviewStageMediaHost` owns a separate monotonic `pv_memory_source_id` and `pv_memory_clear_epoch`; neither reuses `videoSourceGeneration_`. A new source load instance receives a fresh source ID, even when it reloads the same PV path. Clearing snapshots that source's counters and increments the clear epoch before resetting media state. Delayed checkpoints capture both immutable snapshot and epoch; they are cancelled when the epoch no longer matches, never attributed to a newly loaded source. A late `NoMedia` is logged only against the still-pending clear snapshot, never the current source. The existing `recoverVideoBackend()` delete/rebuild path first finalizes the current source exactly once with `reason=recovery`, then begins a fresh source ID after rebuilding and loading its replacement player; no aggregate counters cross that recovery boundary. The host emits rate-limited periodic samples while a video PV is active, and immediate boundaries for source load, first frame, pause/play, VideoOutput attach/detach, clear, `NoMedia`, explicit player deletion/rebuild, and two delayed post-clear checkpoints. Lifecycle boundaries are emitted only for an active video source; inactive attach/detach/play/pause calls do not create PV-memory records. The host aggregates `QVideoFrame::toImage()` work already performed by the current macOS `QMediaPlayer` path; it must not cause any additional conversion.

## Log Contract

Every line uses the existing canonical timestamp/level/PID/TID grammar. All records use a `pv_memory` scope in the dedicated file.

### `action=sample`

Written every five seconds only while `media_kind=video` and the `pv_memory_source_id` is current. Required fields:

- `reason`, `pv_memory_source_id`, `media_visible`, `playback_state`, `media_status`, `position_ms`
- `frame_count`, `frame_rate_estimate`, `frame_width`, `frame_height`, `pixel_format`
- `video_output_attached`, `video_sink_attached`
- `to_image_attempts`, `to_image_successes`, `to_image_nulls`, `to_image_total_ms`, `to_image_max_ms`, `to_image_last_bytes`, `to_image_peak_bytes`, `to_image_output_bytes_estimate`
- `process_*` memory fields from `ProcessDiagnostics`.

`to_image_output_bytes_estimate` is explicitly an output-image-size accumulation, not a measurement of allocator ownership or a claim that all bytes remain live.

### `action=boundary`

Written at the following reasons: `source_load`, `first_frame`, `play`, `pause`, `end_of_media`, `output_attach`, `output_detach`, `clear_before`, `clear_after`, `no_media`, `player_destroy_before`, `player_destroy_after`, `post_clear_3s`, and `post_clear_15s`. `player_destroy_*` is emitted only for an existing explicit delete/rebuild path; host destruction writes its source summary and does not introduce a new teardown solely for instrumentation.

It contains the same fields as `action=sample`, plus no free-form source path. The identity is `pv_memory_source_id` only, so reports do not expose chart/PV filenames. `clear_after`, every delayed post-clear record, and a late `no_media` are old-source snapshot records: they include `clear_epoch` and `snapshot=1`, retain the old source ID and immutable media/frame/`toImage` counters, and sample only the process-memory fields at their actual event time.

### `action=summary`

Written exactly once when a source is replaced, cleared, recovered/rebuilt, or the host is destroyed. It reports the source ID's elapsed active time, frame count, `toImage` aggregates, first/last process-memory payloads, peak values, and whether `NoMedia` was observed after clear.

## Interpretation and Ranking

The log does not claim to measure VideoToolbox's separate XPC process. It supplies the evidence for this deterministic engineer-facing ranking workflow:

1. **Per-frame `toImage()` readback / image churn.** Highest priority when the source has sustained `to_image_successes` and current-process footprint rises during the same interval. This is the existing primary hypothesis.
2. **QMediaPlayer or output-sink lifetime.** Highest remaining priority when `toImage` activity is low or absent, but process memory keeps rising during video playback or does not settle by `post_clear_15s` after `NoMedia`.
3. **VideoOutput/scene-graph last-frame retention.** Investigate when attach/detach boundaries materially change memory while playback counters are otherwise stable.
4. **Persistent Quick graphics resources.** Investigate only when growth is associated with hiding/showing a preview surface or resource retention after it has stopped; it is not the leading explanation for monotonic growth during steady playback.

The diagnostics report raw evidence only; Codex or an engineer applies the ranking after inspecting a captured session. The diagnostics do not auto-disable `toImage`, rebuild a player, or otherwise alter rendering behaviour.

## Verification

1. Extend the process-diagnostics spec to prove that the dedicated channel is gated by debug mode, creates exactly the dedicated filename/path override, and is included in startup rotation/clear behaviour. Debug-off must create neither the channel timer nor the file.
2. Add focused unit/spec coverage for PV-memory payload formatting and counter reset/summary boundaries without decoding media. It must prove that instrumentation does not invoke `QVideoFrame::toImage()` beyond the pre-existing call.
3. Cover asynchronous lifetime cases: clear followed immediately by a new source must not emit or misattribute old 3/15-second checkpoints; late `NoMedia` must bind to its old clear snapshot; clear, recovery, and destruction must each write exactly one source summary.
4. Build the macOS Release target with the repository's maximum two concurrent build jobs.
5. Run the existing process-diagnostics and relevant preview/debug-index specs.
6. Launch the packaged debug launcher, play one PV normally, and verify that the dedicated log contains a session-start record, source-load boundary, first-frame boundary, periodic samples, and a clear/shutdown summary while the runtime/audio logs do not contain `pv_memory` records.

## Documentation

Update `docs/ops/DEBUG_INDEX.md` and the MiaCode developer-guide debug-flags reference with the filename, enable condition, path override, five-second cadence, privacy boundary, and interpretation order.
