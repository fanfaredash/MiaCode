# Debug Index

This document is the current user-facing index for MiaCode debug mode, log files, and preview/export diagnostics after the Qt Quick migration.

> Reconciled against the code on 2026-05-29: 79 live `MIACODE_*` environment flags across ~27 files. When you add/remove a flag, update this index (and `.claude/skills/miacode-dev-guide/references/debug-and-logging.md`).

## Debug Entry Points

- Preferred CLI switch: `--debug`
- Windows release helper:
  - `Start_MiaCode_Debug.bat`
- Windows focused diagnostic helpers retained in the public repo:
  - `Start_MiaCode_SoftwareVideoDecode.bat`
  - `Start_MiaCode_QtPluginDiag.bat`

Inside debug mode, runtime, audio, export, startup-timing, and preview-profile outputs are enabled unless they are individually disabled.
Outside debug mode, the export log still keeps a concise stage/failure summary so users can report export issues without reproducing under `--debug`.

## Default Log Files

Default directory order:

1. channel-specific override path
2. `MIACODE_LOG_DIR`
3. project-local `.miacode/logs/` once a chart file is bound
4. app-local `logs/` next to `MiaCode.exe` while `--debug` is active
5. system temp directory

Export worker note:

- Background export workers now inherit the snapshot chart's project-local `.miacode/logs/` directory by default.
- This keeps worker-side export and fatal logs aligned with the chart being exported, including batch-export items, unless `MIACODE_LOG_DIR` or a per-channel path override is set.
- If a worker exits with `CrashExit` while the current attempt still requested offscreen PBO readback, the UI retries that export once with `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` and keeps the first-crash diagnostics for the final error report.

Default filenames:

- runtime: `miacode_runtime_debug.log`
- audio: `miacode_audio_debug.log`
- export: `miacode_video_export.log`
- startup timing: `miacode_startup_timing.log`
- fatal: `miacode_fatal.log`
- preview profile: `miacode_preview_profile_summary.txt`

Path overrides:

- `MIACODE_RUNTIME_LOG_PATH`
- `MIACODE_AUDIO_LOG_PATH`
- `MIACODE_EXPORT_LOG_PATH`
- `MIACODE_STARTUP_LOG_PATH`
- `MIACODE_FATAL_LOG_PATH`
- `MIACODE_PREVIEW_PROFILE_PATH`
- `MIACODE_OPERATION_LOG_PATH` (operation-breadcrumb log)
- `MIACODE_OPLOG_SHADOW_PATH` (operation-log shadow copy)

Other logging knobs:

- `MIACODE_SKIP_ASYNCLOG_FLUSH` — skip the async-log flush on shutdown (diagnostics only).

## Category Gates

These only matter while debug mode is active for the detailed channels. The always-on concise export summary remains available without `--debug`.

- `MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT`
- `MIACODE_DISABLE_AUDIO_DEBUG_OUTPUT`
- `MIACODE_DISABLE_EXPORT_DEBUG_OUTPUT`
- `MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT`
- `MIACODE_DISABLE_STARTUP_TIMING`

Fatal logging is intentionally not gated by debug mode. The `Operation` channel
(op-chain failures from `MC_OP` `~Scope`/`fail()`) is **also always-on** — it has no
per-category disable flag and ignores `--debug`, by design: it fires only on failure
paths (silent on the happy path) so the logical-call-chain is captured whenever
something goes wrong. The per-category gates above are snapshot into atomics at
`setDebugModeEnabled` (not re-read per log line).

## Current Preview / Export Backend Notes

- Realtime preview and export both use Qt Quick scene graph.
- The desktop app currently forces Qt Quick to the OpenGL backend.
- Export PBO diagnostics now describe the headless Quick export session, not a removed legacy renderer.
- Background **PV/BG video decoding** in realtime preview (and therefore the export preview dialog) uses **QtAVPlayer (FFmpeg)** on Windows instead of Qt Multimedia's `QMediaPlayer`. This is selected at **build time** by the `MIACODE_USE_QTAVPLAYER` compile macro — a CMake build-time macro, **not** an environment flag, so it cannot be toggled at runtime (CMake defines it on Windows when the FFmpeg dev SDK is present; other platforms keep the `QMediaPlayer` path). The FFmpeg dev SDK path is a separate CMake cache variable, provisioned by `scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1`. The export *encoder output* is unaffected (still the standalone `ffmpeg.exe` filtergraph). On `InvalidMedia` the preview backend retries once forcing FFmpeg software decode (`preview/stage_media action=video_software_fallback`)..
- `MIACODE_BUILD_DEV_TOOLS` is the **CMake configure option** (not an environment flag) that builds the developer spec/eval executables and registers them with CTest (`cmake -D MIACODE_BUILD_DEV_TOOLS=ON`; `scripts/build/build-win.ps1` turns it on for Debug configs). Some dev tools mention it in comments (e.g. `src/tools/latency/LatencyBatchTest.cpp`), which is why it appears in this index — it has no runtime effect in a shipped build.

Relevant export backend toggles:

- `MIACODE_EXPORT_ENABLE_GPU_RENDER`
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`
- Default export path keeps GPU offscreen render enabled and requests PBO readback unless `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1`.
- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` is the primary stability fallback switch.
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO=1` now only makes the default explicit and remains useful for cleaning inherited `ENABLE=0` env state.

Relevant export diagnostics:

- `MIACODE_EXPORT_DIAG_REPEAT`
- `MIACODE_EXPORT_DIAG_LOG_ALL_REPEATS`
- `MIACODE_EXPORT_DIAG_CROP_BOTTOM`
- `MIACODE_EXPORT_DIAG_MAX_LINES`
- `MIACODE_EXPORT_DIAG_OBJECT_HASH`
- `MIACODE_EXPORT_DIAG_OBJECT_TRACE`
- `MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES`
- `pbo_capability_probe` export-log entries record version / extension checks, smoke-probe result, and the final enable/disable reason.
- `pbo_cleanup_deferred` and `export_context_not_current_on_teardown` mark teardown paths that skipped explicit GL cleanup because the export context was not current.
- `render_backend` export-log entries now report `pboRequested=1` by default and only leave `pboEnabled=0` when capability probing or runtime fallback disables it.
- `audio_backend_select`, `audio_mix_ok`, and `audio_backend_render_complete` export-log entries describe StageB mixed-audio backend routing and offline WAV generation.
- `fail_audio_plan`, `fail_audio_backend_select`, and `fail_audio_mix` are the primary StageB breadcrumbs when export audio generation fails before ffmpeg starts.
- `miacode_video_export.log` now uses two tiers: concise major-stage/failure summaries always, detailed ffmpeg/render diagnostics only under `--debug`.
- detailed `frame_timing` sampling is now sparser and only records every `300` frames unless a frame stalls.
- `MIACODE_EXPORT_DIAG_OBJECT_DIFF_THRESHOLD`
- `MIACODE_EXPORT_DIAG_COMPARE_RENDER_PATHS`
- `MIACODE_EXPORT_DIAG_COMPARE_RADIUS`
- `MIACODE_EXPORT_DIAG_COMPARE_MAX_LINES`
- `MIACODE_EXPORT_DIAG_COMPARE_LOG_THRESHOLD`
- `MIACODE_EXPORT_DIAG_PIPE_HASH`
- `MIACODE_EXPORT_DIAG_PIPE_HASH_MAX_LINES`
- `MIACODE_EXPORT_DIAG_RAW_DUMP_PATH`

Encoder selection/tuning:

- `MIACODE_EXPORT_SKIP_ENCODER_RUNTIME_PROBE`
- `MIACODE_EXPORT_FORCE_ENCODER`
- `MIACODE_EXPORT_ENCODER_MODE`
- `MIACODE_EXPORT_ENCODER_THREADS`
- `MIACODE_EXPORT_FILTER_THREADS`
- `MIACODE_EXPORT_X264_PRESET`
- `MIACODE_EXPORT_X264_CRF`
- `MIACODE_EXPORT_X264_BFRAMES`

FFmpeg binary + extra readback toggle:

- `MIACODE_FFMPEG` / `MIACODE_FFMPEG_PATH` — override the ffmpeg executable path used by export (and the dialog's ffmpeg probe).
- `MIACODE_EXPORT_DISABLE_PBO_READBACK=1` — extra PBO-readback opt-out, narrower than `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`.

## Preview-Side Notes

Still active:

- `MIACODE_PREVIEW_SFX_DIR`
- `MIACODE_TRACK_PATH`
- `MIACODE_BASS_BGM_RATE_MODE` (Windows BASS preview BGM only; unset defaults to pitch-preserving BASS_FX `tempo`, while `rate_transpose` / `transpose` / `source_time` / `accurate` switches to source-time-priority rate transpose for A/B listening)
- `MIACODE_BASS_BGM_TEMPO_PRESET` (Windows BASS preview BGM only, only when tempo mode is active; BASS_FX window presets: unset = `compact40`, `stock` = plugin default, `auto` = `0/0/8`, `tight20` = `20/8/4`, `balanced30` = `30/10/6`, `compact40` = `40/15/8`, `smooth60` = `60/20/8`, `wide82` = `82/28/8`)
- `MIACODE_BASS_BGM_TEMPO_PARAMS` (Windows BASS preview BGM only, only when tempo mode is active; overrides preset with custom `sequence_ms,seek_ms,overlap_ms`, accepting comma, slash, semicolon, pipe, `x`, or spaces as separators)
- `MIACODE_PREVIEW_FRAME_PACING_DIAG`
- `MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS`
- `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG` (requires `--debug`; adds focused waveform/BGM alignment evidence in the audio log, raises BASS `bass_status` cadence for short-lived 1x offset repros, and emits runtime `timeline/render_map` rows from the final Quick timeline scene state)
- `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS` (default `250`; BASS `bass_status` interval while waveform-alignment diagnostics are enabled)
- `MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES`
- `MIACODE_PREVIEW_REJECT_NEGATIVE_HS` (default off; opt-OUT escape hatch — negative HS `<HS*-N>` is ON by default. `1` restores the strict stance where the parser rejects `hs <= 0` (Q7). Read once at app boot into `SimaiNativeParser::setAllowNegativeHsEnabled`. By default negative HS parses and the preview/export renderer flies tap / star / each-line notes inward from OUTSIDE the judgement ring (hold / touch / slide take the HS magnitude, never reverse); zero is always rejected. Inherited by the CLI export-worker subprocess so preview and export agree. Off-canvas reverse spawn relies on the playfield clip..)
- `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO` (`1` forces QtAVPlayer to decode preview video in software instead of D3D11VA — diagnostic + workaround for the green NV12-padding artifact on videos whose dimensions aren't 16-aligned, e.g. 300×300; hardware textures are allocated at the macroblock-aligned coded size and the uninitialized padding samples as pure green)
- `MIACODE_PREVIEW_SINGLE_D3D11_DEVICE` (default off; `1` enables H2 single-device preview decode — share ONE video-capable, multithread-protected `ID3D11Device` between the QtAVPlayer/FFmpeg D3D11VA decoder and the preview `QQuickView`'s QRhi, eliminating the per-frame cross-device keyed-mutex texture bridge and its render-thread `AcquireSync(INFINITE)` freeze that garbles/stalls preview on Intel/Arc iGPUs. Experimental, needs GUI acceptance on an affected iGPU; every step falls back to the legacy two-device bridge on failure, so the worst case is "no change". Confirm via the `media_backend … single_device=1` log line.)
- `MIACODE_PREVIEW_DUMP_HWFRAMES` (default `0`=off; requires `--debug`. `N` = read back + stat-classify the first N D3D11VA decoder NV12 surfaces per "arm" (an arm = playback start; re-armed on each seek) to localize the hardware-decode green/garble bug. Render-thread bounded readback: Maps a STAGING copy of the decoder DPB slot (single-device path) or the bridge output (two-device path), computes NV12-domain stats (`y_mean`, `y_rowdelta` garbage proxy, `uv_zero`/`uv_neutral`, interior-vs-edge zeroed-chroma = green) + coded-vs-display size, and emits ONE `preview/hwframe` line per readback with a verdict `hint`. **No image files are written** — the stat line is the decisive output. Zero cost when `0` (single relaxed-atomic load+compare per frame, no env read on the hot path). ⚠ Repro precondition: integrated GPUs default to software decode, so to exercise the buggy hardware path launch with `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=0` [+ `MIACODE_PREVIEW_SINGLE_D3D11_DEVICE=1` for the H2 path].. The decode-path summary lands in `preview/hwdecode_summary`, seek catch-up in `preview/seek_landing`.)
- `MIACODE_PREVIEW_D3D11_DEBUG_LAYER` (default off; requires `--debug`. `1` adds `D3D11_CREATE_DEVICE_DEBUG` to the H2 shared device (`PreviewSharedD3D11Device::createSharedDevice`) so the D3D11 debug layer's typed-SRV / NV12 PlaneSlice warnings (the decisive H-FMT signal) and resource-not-ready / device-removed errors (H-DEC) are pumped into `preview/hwframe_d3d11dbg` on the low-frequency summary cadence. Falls back to no-debug creation if the SDK debug layer isn't installed. Only the *imported* H2 device needs this; for the legacy two-device path set the Qt env `QSG_RHI_DEBUG_LAYER=1` instead, since that device is created by Qt's QRhi.)
- `MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT` (default **off** — RESERVED / experimental; **not** `--debug`-gated. Was the §10 "completion-order" fix attempt: before the copy reads the DPB slot, `qavhwdevice_d3d11.cpp` (`copyToShared` two-device + `copyTextureSameDevice` single-device) issues an `ID3D11Query(EVENT)` + `Flush` + bounded ~100ms spin to force the decode GPU work to complete. ⚠ It did **NOT** resolve the post-seek green on the user's Intel Arc 130T (A/B verified), so it is now **off by default** — the user-facing fix is the 硬件渲染/软件渲染 preference (default hardware; switch to software on an affected iGPU, hot-switchable, no restart). The mechanism is kept as env-gated reserved code (a legitimate D3D11 sync that may help on other hardware): set `MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT=1` to re-enable. Active state shows in `preview/hwdecode_summary … completion_waits=` / `preview/hwframe … completion_wait=`. Published via `qavSetPreviewHwDecodeFixConfig`.)
- `MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT` (default off; **not** `--debug`-gated. **Safety net** (secondary) for the same green/garble bug: when on, the D3D11VA copy path drops any decoded frame FFmpeg flagged corrupt/decode-error (`AV_FRAME_FLAG_CORRUPT` or `AVFrame::decode_error_flags != 0`) instead of sampling it, so the RHI holds the previous good frame (a brief freeze beats a green flash). Free — reads existing AVFrame flags, no GPU readback. Default off because it only helps if the driver/decoder actually reports those flags for the green frames (unverified) and because dropping on benign concealment flags could over-discard. First confirm via the `preview/hwframe … decode_err= corrupt=` dump fields and the `preview/hwdecode_summary frames_decode_error=` counter that FFmpeg flags the green frames, then set `MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT=1`. Published via `qavSetPreviewHwDecodeFixConfig`; drops counted in `preview/hwdecode_summary corrupt_dropped=`.)
- `MIACODE_PREVIEW_VISUAL_SMOOTHING` (default on; `0` to disable scene-playhead smoothing)
- `MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS` (default `1.0`; biases visual playhead forward by N display intervals to compensate for render→present pipeline latency, `0` disables, range `[0, 4]`)
- `MIACODE_PREVIEW_QSG_RENDER_TIMING` (`1` to capture Qt scene-graph timings into the runtime log under `preview/qsg_timing` — diagnoses stutter that lives outside the offscreen renderer)
- `MIACODE_TIMELINE_HOTPATH_DIAG`
- `MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS`
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_FRAMES`
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES`
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR`

Startup default:

- The embedded realtime preview now enables `Qt::AA_DontCreateNativeWidgetSiblings` by default before `QApplication` construction.
- This is the current workaround for Windows black-screen regressions around `QQuickView` hosted through `QWidget::createWindowContainer()` when native dialogs or sibling native windows appear.
- Use `MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS=1` only for regression A/B.
- The old debug-only enable snippet `MIACODE_PREVIEW_DONT_CREATE_NATIVE_WIDGET_SIBLINGS=1` is now redundant because the workaround is already on by default.

Retired from the main app:

- The old native-dialog WinEvent hook and related `window/native*` investigation traces are no longer built into `MiaCode.exe`.
- If Windows dialog investigation needs to return, it must live in a separate dev-only tool and must not ship in the release `dist/` package.

Retired with the old preview renderer and not recommended anymore (no longer read by any code):

- `MIACODE_ENABLE_PYGAME_PREVIEW`
- `MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY`
- `MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY`
- `MIACODE_PREVIEW_SESSION_SCRIPT` (was a preview session-script hook; gone)
- `MIACODE_DISABLE_GL_DEBUG_MESSAGES` (GL debug-message gate; gone)

Preview diagnostics now split these timing sources instead of reporting a single ambiguous FPS number:

- preview HUD:
  - `Present` is the rolling `frameSwapped` / presented-frame rate
  - `Tick` is the logical preview-tick rate
  - `Req` is the `requestUpdate()` cadence seen by the runtime
  - `Pacing` shows whether the preview is using fixed 60/120 pacing or display-refresh pacing, plus the current screen refresh rate
- preview profile summary:
  - `present_avg_ms` / `present_max_ms` are now true session-accumulated present intervals
  - `present_window_*` keeps the last rolling present window for comparison with the on-screen FPS readout
  - `tick_*`, `update_request_*`, and `ratio.*` rows help distinguish logic pacing from actual present pacing
  - `fixed_timer.soft_late_total`, `fixed_timer.hard_resync_total`, and `fixed_timer.present_missed_slots_total` describe fixed-FPS presentation scheduler drift without implying skipped note logic
  - `fixed_timer.skipped_intervals_total` is retained as a retired compatibility row and should remain `0`; use `fixed_timer.present_missed_slots_total` for display-slot misses and `logic.skipped_ticks_total` for logic skips
  - `fixed_timer.high_res_resolution_enabled`, `fixed_timer.high_res_requested`, `fixed_timer.high_res_begin_ok`, and `fixed_timer.high_res_active_at_stop` show whether the Windows timer-resolution A/B path was requested, successfully entered, and later released
  - `display_refresh.*` rows show display-refresh frame requests, matched presents, watchdog fallback ticks, and present wait time
  - `tick.speed_ratio_max` and `tick.large_step_count` flag logical playhead jumps that would look like catch-up acceleration
  - `audio_clock.delta_max_ms`, `audio_clock.large_step_total`, `audio_clock.audio_vs_fallback_avg_ms`, and `audio_clock.audio_vs_fallback_max_abs_ms` compare the authoritative audio clock against the elapsed fallback clock
  - `visual_time.large_step_total` counts visual playhead jumps after the selected time authority has been applied
  - `external_stage_media.video_frame_*` rows now include aggregate external-video frame rate, interval, and stall counts
- runtime log:
  - `preview/frame_pacing` is off by default and turns on with `MIACODE_PREVIEW_FRAME_PACING_DIAG=1`; normal request/tick/present samples are capped by `MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS` while watchdog, fixed timer present-miss/hard-resync, and large-step events log immediately
  - fixed timer high-resolution status events `fixed_timer_high_res_requested`, `fixed_timer_high_res_enabled`, `fixed_timer_high_res_failed`, and `fixed_timer_high_res_disabled` are written as sparse status lines so env propagation can be verified even when per-frame pacing diagnostics are off
  - `tick_sample` / `tick_large_step` include `fallback_second`, `audio_second`, `fallback_delta_ms`, `audio_delta_ms`, `visual_delta_ms`, `audio_minus_fallback_ms`, and `time_authority`
  - `preview/stage_media` now also emits low-noise `action=video_frame_stall_begin` / `action=video_frame_stall_end` transitions when external video stops delivering frames for longer than expected
  - `preview/waveform` records waveform cache/request/apply evidence, including source (`memory`, `disk`, `build`, `placeholder`), track id, file stamp, duration, top-level column count, milliseconds per column, first onset at `0.02` / `0.05` / `0.10`, and peak position; `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG=1` adds focused `preview/waveform_align` breadcrumbs, lowers `bass_status` interval via `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS`, and adds runtime `timeline/render_map` rows with visible range, scroll, primitive counts, waveform/grid/note first-visible coordinates, playhead/cursor/entry second-to-X roundtrips, the waveform column under the playhead, and the strongest waveform column near the playhead
  - `window/focus` records app-level focus transitions, activation edges, watched editor focus events, and text-focus restore attempts for focus-regression diagnosis
  - BASS `bass_status` sampling is now reduced to about once per second in debug mode, and includes `bgm_delta_ms` (`auth - bgm_chart`) so waveform/playhead complaints can be separated from generic fallback-clock drift; waveform-alignment diagnostics can temporarily lower that interval
  - BASS BGM rate-mode decisions log as low-frequency `bgm_speed_mode` / `bgm_rate_mode` rows, not per tick; BASS_FX tempo-window experiments additionally log `bgm_tempo_window` with requested and read-back `sequence_ms`, `seek_ms`, and `overlap_ms`
  - `preview/interaction` correlates user-facing preview actions such as `play`, `pause`, `stop`, and `ctrl+click` seek
  - `timeline/interaction` records quick timeline drag, wheel-scroll, and held-key horizontal scroll inputs
  - `timeline/bridge` records quick timeline hot-path state pushes such as `action=set_horizontal_scroll_value` only when `MIACODE_TIMELINE_HOTPATH_DIAG=1`
  - `timeline/quick_scene` records full `scene_state_rebuild_*` boundaries in debug mode; scroll-only `action=content_transform_update` paints require `MIACODE_TIMELINE_HOTPATH_DIAG=1`
  - `timeline/cursor_map` profiles `timelineSecondForCursor()` so editor cursor-to-second mapping can be separated from redraw cost
  - `timeline/render_map` is emitted only with `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG=1` and samples final Quick scene-state mappings so waveform/playhead complaints can be checked against actual rendered world-X and viewport-X positions

## Render Path Toggles (diagnostics / A·B only)

The in-process Qt Quick (QSG) path is the default and the only one needed for normal use; both realtime preview and export run through it. The toggles below select alternate render topologies for diagnostics and are **off by default**.

DirectComposition / D3D11 preview (`src/common/DebugOptions.h`; default OFF, being decoupled from the QSG path):

- `MIACODE_PREVIEW_USE_DCOMP` — opt into the DComp-backed chart preview popup.
- `MIACODE_TIMELINE_USE_DCOMP` — DComp pipeline for the timeline pane.
- `MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND` — host DComp in an owned top-level HWND (override; default on when DComp is on).
- `MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA` — per-pixel-alpha NRB popup (override; default on when DComp is on).
- `MIACODE_PREVIEW_DCOMP_EXCLUSIVE` — let DComp be the sole chart renderer (auto-on with per-pixel alpha).
- `MIACODE_PREVIEW_DCOMP_QUIESCE_QSG` — skip QSG repaint subscriptions while DComp is active.
- `MIACODE_PREVIEW_QSG_FULL_DISABLE` — force QSG to the software/basic path (GPU-contention isolation test).
- `MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP` — force `QSG_RENDER_LOOP=basic` at startup.
- `MIACODE_SKIP_DIAG_D3D11` — skip the startup D3D11 diagnostic probe.

## Misc / Platform

- `MIACODE_LANG` — force UI language (overrides system locale; `app/ui/UiText.cpp`).
- `MIACODE_DISABLE_MMCSS` — opt out of MMCSS pro-audio thread scheduling (Windows; `common/Mmcss.cpp`).
- `MIACODE_DISPLAY_VERSION_STRING` — override the displayed version string.
- `MIACODE_SKIP_PREFLIGHT` — skip the launcher preflight checks (`wrapper/MiaCodeLauncher.cpp`).

## Useful Workflows

Launch the default Quick Shell app in debug mode:

- `MiaCode.exe --debug`

Launch the legacy Qt native widget shell in debug mode:

- `MiaCode.exe --qt-native --debug`

Launch the Qt Quick hybrid host explicitly in debug mode:

- `MiaCode.exe --quick-shell-beta --debug`

Force export logging into a local directory:

- `set MIACODE_LOG_DIR=<folder>`
- `MiaCode.exe`
- add `--debug` when you also need the detailed per-stage diagnostics

Force export GPU render off:

- `set MIACODE_EXPORT_ENABLE_GPU_RENDER=0`

Force export PBO off:

- `set MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1`
- This is the standard stability fallback switch and the same override used by the automatic worker crash retry path.

Force export PBO on:

- `set MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO=1`
- PBO readback is already on by default; this only forces the default back on after inherited env cleanup or an explicit `ENABLE=0`.

Opt out of the default embedded-preview native-sibling workaround for regression A/B:

- `set MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS=1`
- `MiaCode.exe --debug`

Enable the Windows fixed-FPS timer-resolution A/B path:

- `set MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES=1`
- `set MIACODE_PREVIEW_FRAME_PACING_DIAG=1`
- `MiaCode.exe --debug`

## Code Anchors

- debug options:
  - `src/common/DebugOptions.h`
- shared log routing:
  - `src/common/DebugLog.h`
  - `src/common/DebugLog.cpp`
- app boot debug entry:
  - `src/app/main.cpp`
- runtime/debug orchestration:
  - `src/app/mainwindow/MainWindow.cpp`
- export logging:
  - `src/tools/video_export/VideoExportController.cpp`
- Quick export session:
  - `src/preview/runtime/PreviewQuickExportSession.cpp`

For architecture details, see `docs/specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`.
