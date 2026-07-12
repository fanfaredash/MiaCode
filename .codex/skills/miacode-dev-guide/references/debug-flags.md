# Debug Flags

Use this file to locate repo-wide debug switches, log channels, preview overrides, and export-only diagnostic environment variables.

The user-facing canonical doc lives at `docs/ops/DEBUG_INDEX.md`. This file stays shorter and points to the owning code.

## 1. Generic Debug Controls

- Shared helpers:
  - `src/common/DebugOptions.h`
  - `src/common/DebugLog.h`
  - `src/common/DebugLog.cpp`
- Preferred CLI switch:
  - `--debug`
- Shared log directory env:
  - `MIACODE_LOG_DIR`
- Default debug-mode fallback:
  - project-local `.miacode/logs/` once a chart file is bound, otherwise app-local `logs/` next to the running executable when `MIACODE_LOG_DIR` and per-channel overrides are unset
  - export worker launches now pre-bind `MIACODE_LOG_DIR` to the snapshot chart's `.miacode/logs/` directory when no explicit shared log-dir override is present, and the worker also restores the same path after snapshot parse for direct CLI-worker runs
  - in debug mode, startup now trims retained runtime/audio/export/startup/fatal logs down to at most `100 KB` each by dropping the oldest lines first
  - editor startup beacon / op-chain shadow paths are captured before chart-local project logs are bound; when runtime debug output or `MIACODE_PREVIEW_HUD_PAINT_DIAG=1` is active, project-log binding writes a `runtime/logging/crash_breadcrumb_hint` signpost with PID, project log dir, and startup-beacon/op-chain path hints
- Channel-specific path overrides:
  - `MIACODE_RUNTIME_LOG_PATH`
  - `MIACODE_AUDIO_LOG_PATH`
  - `MIACODE_EXPORT_LOG_PATH`
  - `MIACODE_STARTUP_LOG_PATH`
  - `MIACODE_FATAL_LOG_PATH`
  - `MIACODE_PREVIEW_PROFILE_PATH`

Runtime/audio/startup/profile detailed outputs are gated by the process debug mode entered through `--debug`. Fatal logs are intentionally not gated. The export log now keeps a concise stage/failure summary even without `--debug`, while detailed export diagnostics still require debug mode.

Debug subcategories now default to on inside debug mode and are disabled with:

- `MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT`
- `MIACODE_DISABLE_AUDIO_DEBUG_OUTPUT`
- `MIACODE_DISABLE_EXPORT_DEBUG_OUTPUT`
- `MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT`
- `MIACODE_DISABLE_STARTUP_TIMING`
- `MIACODE_DISABLE_GL_DEBUG_MESSAGES`

## 2. Log Channels

- Runtime log:
  - default file: `miacode_runtime_debug.log`
  - main producer: `MainWindow::appendOutput`
- Extension support log:
  - default file: `extensions.log` in `ExtensionManager::extensionLogDirectory()` (`MIACODE_LOG_DIR` when set, otherwise app-local `logs/`)
  - producers: `ExtensionManager`, embedded extension runtime signals, extension `log`/`context.log()` calls, permission denials, manifest diagnostics, failed host API calls, and extension command failures
  - user-facing access: Preferences > Extensions > Open Logs Folder; DevTools Raw JSON also exposes `logPath`
- Audio log:
  - default file: `miacode_audio_debug.log`
  - producers: `QtPreviewSfxRuntime`, `MiniaudioPreviewAudioBackend`, `BassPreviewAudioBackend`, `PreviewStageMediaHost`, preview startup/playback transaction logs, `soundtouch_probe`
- Export log:
  - default file: `miacode_video_export.log`
  - producers: `VideoExportController`, export audio backends
  - normal mode writes concise major-stage/failure/result lines so user bug reports still have a shareable export log
  - debug mode adds the existing detailed ffmpeg/render/backend diagnostics on top of that same file
- Startup timing:
  - default file: `miacode_startup_timing.log`
  - extra disable gate: `MIACODE_DISABLE_STARTUP_TIMING`
  - producers: `src/app/main.cpp`, `MainWindow`, `PreviewSceneAssetRepository`
- Fatal log:
  - default file: `miacode_fatal.log`
  - used for critical preview/export failures and worker failures
- Preview profile summary:
  - default file: `miacode_preview_profile_summary.txt`
  - `present_avg_ms` / `present_max_ms` are session-accumulated present intervals, while `present_window_*` keeps the last rolling present window used by the on-screen present FPS readout
  - quickshell preview sessions now also write `tick_*`, `update_request_*`, `frame_pacing.*`, and `ratio.*` rows so logical tick cadence, update-request cadence, and actual present cadence can be compared directly
  - fixed-FPS pacing sessions now also write `fixed_timer.soft_late_total`, `fixed_timer.hard_resync_total`, `fixed_timer.present_missed_slots_total`, session-level `fixed_timer.high_res_resolution_enabled`, `fixed_timer.high_res_requested`, `fixed_timer.high_res_begin_ok`, `fixed_timer.high_res_active_at_stop`, and `logic.skipped_ticks_total`; the retired `fixed_timer.skipped_intervals_total` row is kept for compatibility and should remain `0`
  - display-refresh pacing sessions now also write `display_refresh.requests_total`, `display_refresh.presented_after_request_total`, `display_refresh.watchdog_timeouts_total`, `display_refresh.queued_ticks_total`, `display_refresh.timer_fallback_ticks_total`, `display_refresh.present_wait_*`, `tick.speed_ratio_max`, and `tick.large_step_count`
  - audio-clock comparison rows `audio_clock.delta_max_ms`, `audio_clock.large_step_total`, `audio_clock.audio_vs_fallback_avg_ms`, `audio_clock.audio_vs_fallback_max_abs_ms`, plus `visual_time.large_step_total`, separate BASS/elapsed drift from selected visual playhead jumps
  - quickshell external-media sessions now also write `external_stage_media.*` summary rows for separate-surface state, media kind, aggregate video-frame counts, estimated video FPS, video-frame intervals, and stall counts without emitting per-frame runtime logs
  - current summary also includes `stage_bg.*` sub-metrics for stage-background media conversion, media texture work, dim-uniform updates, node-update time, and media/dim frame counters
  - layer summary rows now also include `candidate_count_*` and `active_count_*` so prepared-window efficiency can be compared against sprite/batch counts

The Windows release package also ships:

- `Start_MiaCode_Debug.bat`
  - sets debug mode, creates a local `logs/` directory, and launches `MiaCode.exe --debug`

Dev-tool-only parser repro hook:

- `MIACODE_SIMAI_REPRO_CHART`
  - optional `simai_parser_spec` real-chart repro path; point it at a `maidata.txt` containing `&inote_5=` to add a local corpus parse check while keeping the public CTest run deterministic when unset
  - owner: `src/tools/simai_parser/SimaiParserSpec.cpp`

## 3. Preview And Runtime Overrides

- `MIACODE_PREVIEW_SESSION_SCRIPT`
  - preview session script override
  - owner: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `MIACODE_PREVIEW_SFX_DIR`
  - preview SFX directory override
  - owner: `src/common/PreviewSfxAssets.h`
- `MIACODE_TRACK_PATH`
  - preview track-path override
  - owner: `src/app/mainwindow/MainWindow.cpp`
- `MIACODE_EXTENSION_DEV_PATHS`
  - platform path-list separated development extension paths scanned in addition to the user extension directory and app-local `extensions-dev`; each entry may point directly at an extension root or at a parent directory containing extension roots
  - owner: `src/extensions/ExtensionManifest.cpp`
- `MIACODE_BASS_BGM_RATE_MODE`
  - Windows BASS preview BGM rate-mode override; unset defaults to pitch-preserving BASS_FX `tempo`, while `rate_transpose` / `transpose` / `source_time` / `accurate` switches to source-time-priority rate transpose for A/B listening
  - owner: `src/audio/BassPreviewAudioBackendImpl.h`, applied in `src/audio/BassPreviewAudioBackend_Assets.cpp`
- `MIACODE_BASS_BGM_TEMPO_PRESET`
  - Windows BASS preview BGM BASS_FX window preset override; only active when `MIACODE_BASS_BGM_RATE_MODE=tempo`
  - supported presets: unset = `compact40`, `stock` = plugin default, `auto` = `0/0/8`, `tight20` = `20/8/4`, `balanced30` = `30/10/6`, `compact40` = `40/15/8`, `smooth60` = `60/20/8`, `wide82` = `82/28/8`
  - owner: `src/audio/BassPreviewAudioBackendImpl.h`, applied in `src/audio/BassPreviewAudioBackendSample.h`
- `MIACODE_BASS_BGM_TEMPO_PARAMS`
  - Windows BASS preview BGM custom BASS_FX window override; only active when tempo mode is active; overrides the preset with `sequence_ms,seek_ms,overlap_ms`
  - owner: `src/audio/BassPreviewAudioBackendImpl.h`, applied in `src/audio/BassPreviewAudioBackendSample.h`
- `MIACODE_PREVIEW_FRAME_PACING_DIAG`
  - enables low-noise runtime `preview/frame_pacing` request/tick/present/watchdog diagnostics without requiring `--debug`
  - normal request/tick/present samples are rate-limited; watchdog timeout, fixed-timer present-miss/hard-resync, orphan-present, and large-step events log immediately
  - owners: `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`, `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrapFinalize.cpp`, `src/app/mainwindow/sections/timeline/MainWindow.TimelineLayout.cpp`, `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp`
- `MIACODE_PREVIEW_HUD_PAINT_DIAG`
  - enables focused runtime `preview/hud_state` and `preview/hud_paint` breadcrumbs for export-preview / HUD paint crashes without requiring `--debug`
  - force-writes GUI-thread HUD mutations, render-thread `PreviewQuickHudLayer::paint()` entry/skip/overlay stages, and a flushed `draw_text_before` line before each HUD `QPainter::drawText` call so access violations leave the exact HUD branch/text tag on disk
  - also lets project-log binding write `runtime/logging/crash_breadcrumb_hint` without global `--debug`, so users checking chart-local logs are pointed to `%TEMP%` / startup env crash breadcrumbs when the startup shadow path was captured earlier
  - owners: `src/common/DebugOptions.h`, `src/common/DebugLog.cpp`, `src/preview/runtime/PreviewRuntime.cpp`, `src/preview/quick_scene/PreviewQuickHudLayer.cpp`
- `MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS`
  - pacing diagnostic normal-sample interval in milliseconds, default `1000`
  - owner: `src/common/DebugOptions.h`
- `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG`
  - enables focused waveform/BGM/render-map alignment diagnostics inside debug mode; keeps the low-frequency waveform cache/apply lines, increases BASS `bass_status` sampling, and emits runtime `timeline/render_map` samples from the final Quick timeline scene state so short 1x offsets are less likely to be missed
  - owners: `src/common/DebugOptions.h`, `src/common/WaveformCache.cpp`, `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`, `src/audio/BassPreviewAudioBackend_PlaybackClock.cpp`, `src/timeline/quick/TimelineQuickItem.cpp`
- `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS`
  - focused BASS `bass_status` sample interval while waveform-alignment diagnostics are enabled, default `250`
  - owner: `src/common/DebugOptions.h`
- `MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES`
  - Windows-only A/B switch for fixed 60/120 preview pacing; while fixed-FPS realtime preview is playing it requests `timeBeginPeriod(1)` and releases it on stop, mode switch, or shutdown
  - default is off; DisplayRefresh pacing does not use it
  - owners: `src/common/DebugOptions.h`, `src/app/mainwindow/sections/timeline/MainWindow.TimelineLayout.cpp`
- `MIACODE_PREVIEW_VISUAL_SMOOTHING`
  - bounds the per-frame visual playhead delta so render-time variance doesn't propagate audio-time jumps straight into the rendered scene; large drift > 50ms triggers a snap (treated as seek)
  - default is on; set to `0` to pass audio time through unchanged
  - owners: `src/common/DebugOptions.h`, `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp` (`applyVisualClockSmoothing`)
- `MIACODE_PREVIEW_QSG_RENDER_TIMING`
  - sets `QSG_RENDER_TIMING=1` and `QT_LOGGING_RULES+=qt.scenegraph.time.*=true` before `QApplication` construction, then installs a `QtMessageHandler` that routes those messages to the runtime log under `runtime/preview/qsg_timing`
  - use to diagnose stutter that the offscreen-renderer perf instrumentation can't see — i.e. when `renderer_perf max_frame_total_ms` is well under the vsync budget but `fixed_gate_present wait_ms` and `fixed_gate_tick gate_wait_ms` still spike
  - default off; flag is read once at startup so it must be in the environment before the exe launches
  - owners: `src/common/DebugOptions.h`, `src/app/main.cpp`
- `MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS`
  - Tier 2A predictive playhead. Biases the rendered visual time forward by N display intervals (scaled by current playback rate) so the frame represents audio time at the moment it's actually visible — eliminates the perceived "audio leads video by one frame" lag from the GUI→render→composite→present pipeline
  - default `1.0` (≈16.7ms at 60Hz); set to `0` to disable, allowed range `[0, 4]`
  - applies to all return paths of `applyVisualClockSmoothing` (smoothed, snap, reverse-motion, disabled, not-initialized) so motion stays continuous across snap boundaries
  - the stored visual-clock state tracks un-biased smoothed audio time; lookahead is applied only to the per-frame return value, so smoothing math (drift, catchup, snap) stays coherent
  - owners: `src/common/DebugOptions.h`, `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp` (`applyVisualClockSmoothing`)
- `MIACODE_TIMELINE_HOTPATH_DIAG`
  - re-enables high-frequency quick timeline hot-path logs such as `timeline/bridge action=set_horizontal_scroll_value` and `timeline/quick_scene action=content_transform_update` inside runtime debug mode
  - owners: `src/timeline/quick/TimelineQuickStateBridge.cpp`, `src/timeline/quick/TimelineQuickItem.cpp`
- `MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY`
  - samples every Nth direct-upload preview video frame
  - compares GPU framebuffer readback against a CPU `QVideoFrame::toImage()` fallback render
  - writes `preview_gl/video_compare` lines into the runtime log
  - owner: legacy path removed; the variable is currently stale and should not be reintroduced without a new Quick-side owner
- `MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY`
  - samples every Nth preview `paintGL` present after the main frame is drawn
  - compares the final presented preview against a diagnostic reference render
  - writes `preview/present_compare` lines into the runtime log
  - owner: legacy path removed; the variable is currently stale and should not be reintroduced without a new Quick-side owner
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_FRAMES`
  - enables PNG dumps for preview compare samples
  - applies to both `preview_gl/video_compare` and `preview/present_compare`
  - owner: `src/common/DebugImageCompare.h`
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES`
  - caps dumped samples per compare stream; `0` means no cap
  - owner: `src/common/DebugImageCompare.h`
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR`
  - overrides the preview compare PNG dump root directory
  - owner: `src/common/DebugImageCompare.h`
- `MIACODE_DISABLE_GL_DEBUG_MESSAGES`
  - disables OpenGL driver message logging inside debug mode
  - current impact is limited because the active preview/export path is Qt Quick on an explicitly forced OpenGL backend, not the removed `PreviewCanvas` logger stack
- `MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP`
  - if `QSG_RENDER_LOOP` is otherwise unset, forces `basic` render loop before `QApplication` construction
  - intended for A/B diagnosis of embedded `QQuickView` present stalls around native dialogs
  - owner: `src/app/main.cpp`
- `MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS`
  - disables the default-on `Qt::AA_DontCreateNativeWidgetSiblings` startup workaround before `QApplication` construction
  - intended for regression A/B around `createWindowContainer` and native-dialog interaction on Windows
  - owner: `src/app/main.cpp`
- `MIACODE_PREVIEW_DONT_CREATE_NATIVE_WIDGET_SIBLINGS`
  - historical enable-only A/B flag used during investigation
  - no longer required because `Qt::AA_DontCreateNativeWidgetSiblings` is now enabled by default
  - owner: legacy debug launch snippets only; the active startup switch lives in `src/app/main.cpp`

Runtime black-screen / dialog tracing in the main app currently uses these tags:

- `window/focus`
- `app_shutdown`
- `close_timing/document`
- `close_timing/window`
- `close_timing/quick_shell`
- `close_timing/export_worker`
- `close_timing/export_temp_dirs`
- `ui/hang_watchdog`
- `layout/export_page`
- `layout/rehosted_widget`
- `export_page/embedded_video_panel`
- `video_export/embedded_layout`
- `quick_shell/layout`
- `preview/host_window_event`
- `preview/embedded_refresh`
- `logging/crash_breadcrumb_hint`
- `preview/quick_runtime`
- `preview/quick_scene`
- `preview/interaction`
- `preview/frame_pacing`
- `timeline/interaction`
- `timeline/bridge`
- `timeline/quick_scene`
- `timeline/cursor_map`
- `timeline/render_map`

The `preview/quick_runtime` stream now also emits `action=frame_stall` when the embedded Quick window stays visible/exposed but stops presenting for an extended interval.
The `preview/frame_pacing` stream is off by default and turns on with `MIACODE_PREVIEW_FRAME_PACING_DIAG=1`; it emits low-frequency `display_request`, `display_present`, and `tick_sample` lines plus immediate `display_watchdog_timeout`, `fixed_timer_present_miss`, `fixed_timer_hard_resync`, `display_orphan_present`, and `tick_large_step` events. Sparse fixed-timer high-resolution status lines (`fixed_timer_high_res_requested`, `fixed_timer_high_res_enabled`, `fixed_timer_high_res_failed`, `fixed_timer_high_res_disabled`) are written even without per-frame pacing diagnostics so env propagation can be verified.
The `preview/embedded_refresh` stream now also marks resize-throttling transitions with `action=resize_degrade_begin` / `action=resize_degrade_end`.
The `startup/qt_config` runtime tag logs the active Qt graphics/render-loop experiment flags at process start, including whether the default native-sibling workaround was opted out.
The `window/focus` runtime tag now traces app-level `focusChanged`, activation edges, watched editor/preview `FocusIn`/`FocusOut` events, pending text-focus snapshots, and restore attempts for Alt-Tab regression debugging.
The `preview/interaction` runtime tag now traces end-to-end preview action boundaries for `play`, `pause`, `stop`, and `ctrl+click` seek, keyed by one `op` id per interaction.
The `timeline/interaction` runtime tag now traces quick timeline drag, wheel-scroll, and held-key horizontal scroll inputs so user input can be matched to quick-scene work.
The `timeline/bridge` runtime tag now records high-frequency quick timeline bridge pushes such as `action=set_horizontal_scroll_value` only when `MIACODE_TIMELINE_HOTPATH_DIAG=1`.
The `timeline/quick_scene` runtime tag now distinguishes full `scene_state_rebuild_*` passes from `action=content_transform_update` scroll-only paints; the scroll-only hot-path paint log requires `MIACODE_TIMELINE_HOTPATH_DIAG=1`.
The `timeline/cursor_map` runtime tag now profiles cursor-to-second mapping in `timelineSecondForCursor()`.
The `ui/hang_watchdog` runtime tag is installed only when runtime debug output is active. It records the active GUI phase if a marked `MIACODE_HANG_PHASE` stays active for more than about `2s`, flushes the op-chain shadow, and includes async-log writer stats so UI hangs can be separated from logging backpressure. `MIACODE_HANG_JOIN` and `MIACODE_HANG_JOIN_IMPL` are helper macros for those scoped phase markers, not environment variables. Current marked phases focus on QuickShell native surface sizing, rehosted widget relayout, and embedded export video-panel creation/layout.
The `layout/export_page`, `layout/rehosted_widget`, `export_page/embedded_video_panel`, `video_export/embedded_layout`, and `quick_shell/layout` runtime tags are slow-path layout diagnostics. They emit only in runtime debug mode and log warnings when layout/rehost/embedded-panel steps cross their local slow thresholds.
The `app_shutdown` runtime tag now traces `lastWindowClosed`, periodic post-close heartbeats, `aboutToQuit`, `app.exec()` exit, and post-event-loop object teardown so "window disappeared but process still lives" tails can be separated from close-event time.
QuickShell accepted root-window closes now notify C++ from QML before the window hide tail, logging `root_close_accepted_notify` and `accepted_close_shutdown_*`; the immediate pass shuts down preview and closes the hidden backend `MainWindow`, then a queued pre-quit pass logs `accepted_close_destroy_*` while destroying the QML engine, native surfaces, controller, style bridge, and backend before explicitly requesting `quit()`.
The `close_timing/*` runtime tags now record close-path duration totals in milliseconds for document save-confirm work, QuickShell relay work, legacy window close hooks, export-worker teardown, and `aboutToQuit` export temp-dir cleanup so exit long tails can be triaged from one debug session.
The `preview/playback` audio tag now traces realtime preview start transactions. Strong-sync startup should log `action=start_request`, `action=audio_prepared`, `action=canvas_presented`, and `action=commit` under one `txn`, while weak-video startup adds `action=weak_video_prepare_started`, `action=weak_video_ready_before_commit`, or `action=late_video_start_after_commit`.
The `preview/stage_media` audio tag is now switch-level only for quickshell media changes, presentation-mode flips, `VideoOutput` binding transitions, weak-sync video prepare / commit transitions (`action=prepare_playback_*`, `action=commit_prepared_playback*`), and low-noise external-video stall transitions (`action=video_frame_stall_begin` / `action=video_frame_stall_end`); the old per-frame quickshell video arrival line was retired.
The `preview/waveform` audio tag records waveform cache/request/apply evidence: memory/disk/build/placeholder source, normalized track id, file stamp, duration, top-level column count, milliseconds per column, first-column onset at `0.02` / `0.05` / `0.10` amplitude, and peak-column position. `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG=1` additionally keeps focused placeholder breadcrumbs under `preview/waveform_align`, raises `bass_status` cadence to `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS` (default `250 ms`), and emits runtime `timeline/render_map` state/sample rows with visible range, scroll, primitive counts, waveform/grid/note first-visible coordinates, playhead/cursor/entry second-to-X roundtrips, the waveform column under the playhead, and the strongest waveform column in a small playhead-centered window.
The audio log emits `stretched_clock_drift` for low-noise stretched-BGM drift sampling; current fields are `fallback`, `bg`, `delta_ms`, `rate`, `engine_now_frame`, `start_engine_frame`, and `tick_bg_gap_ms` (`fallback - backgroundTrackLastTimelineSecond`) so engine-time anchor drift can be separated from tick-to-tick catch-up.
The preview audio facade now also emits `preview_audio_backend` selection lines so backend routing and BASS fallback decisions are visible in the audio log.
The BASS preview backend now also emits low-noise `bass_schedule_arm` and `bass_status` lines. `bass_schedule_arm` shows which collapsed SFX group has been armed as the next mixer sync, while `bass_status` samples the current authoritative audio second, mixer-relative second, BGM raw/chart transport second, `bgm_delta_ms` (`auth - bgm_chart`), fallback drift in milliseconds, the next armed SFX group, and the last triggered SFX group at roughly a `1s` interval unless waveform-alignment diagnostics lower it. Windows BGM rate mode is logged at load/rate-change boundaries via `bgm_speed_mode` and `bgm_rate_mode`; BASS_FX window experiments log `bgm_tempo_window` with requested and read-back `sequence_ms`, `seek_ms`, and `overlap_ms`. These are low-frequency A/B breadcrumbs and should not be emitted per tick outside focused diagnostics.
The export log now also emits `audio_backend_select`, `audio_mix_ok`, `audio_backend_render_complete`, and `fail_audio_*` lines so StageB backend routing and mixed-audio generation failures are visible without parsing ffmpeg arguments.
Detailed export `frame_timing` sampling now defaults to every `300` frames unless a frame/render/write stall forces an earlier line.
Normal document open/save now uses the direct native `QFileDialog::getOpenFileName` / `getSaveFileName` path. The old `window/dialog_event`, `window/native`, `window/native_hook`, and `window/native_related` probes were retired from the main app and should only return through a separate dev-only tool that is not packaged into release artifacts.

Primary owners:

- `src/app/mainwindow/MainWindow.cpp`
- `src/preview/runtime/PreviewQuickRuntimeSurface.cpp`
- `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`

## 4. Editor Runtime Performance Logging

- Output channel:
  - runtime log
- Stable tags:
  - `edit/metadata_perf`
  - `edit/quick_timeline_perf`
  - `edit/follow_sync_perf`
  - `edit/follow_sync_breakdown`
  - `edit/extra_selections_perf`
  - `edit/extra_selections_validation_perf`
  - `edit/extra_selections_apply_perf`
  - `edit/muri_perf`
  - `edit/validation_perf`
  - `edit/validation_apply_perf`
  - `timeline/ui_perf`
- Main owners:
  - `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - `src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`

High-noise workspace-layout and dialog polling logs were reduced; prefer summary-level runtime tags over per-iteration traces.

## 5. Export Diagnostics And Tuning

Owner: `src/tools/video_export/VideoExportController.cpp`

- Repeat diagnostics:
  - `MIACODE_EXPORT_DIAG_REPEAT`
  - `MIACODE_EXPORT_DIAG_LOG_ALL_REPEATS`
  - `MIACODE_EXPORT_DIAG_CROP_BOTTOM`
  - `MIACODE_EXPORT_DIAG_MAX_LINES`
- Object diagnostics:
  - `MIACODE_EXPORT_DIAG_OBJECT_HASH`
  - `MIACODE_EXPORT_DIAG_OBJECT_TRACE`
  - `MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_OBJECT_DIFF_THRESHOLD`
- Render-path comparison:
  - `MIACODE_EXPORT_DIAG_COMPARE_RENDER_PATHS`
  - `MIACODE_EXPORT_DIAG_COMPARE_RADIUS`
  - `MIACODE_EXPORT_DIAG_COMPARE_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_COMPARE_LOG_THRESHOLD`
- Pipe diagnostics:
  - `MIACODE_EXPORT_DIAG_PIPE_HASH`
  - `MIACODE_EXPORT_DIAG_PIPE_HASH_MAX_LINES`
- Raw dump:
  - `MIACODE_EXPORT_DIAG_RAW_DUMP_PATH`
- Backend forcing:
  - `MIACODE_EXPORT_ENABLE_GPU_RENDER`
  - `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
  - `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`
  - `MIACODE_EXPORT_RENDER_BACKEND`
  - these now drive `VideoExportQuickRenderBackend` plus `PreviewQuickExportSession`, not the removed legacy offscreen renderer
  - default export path keeps GPU offscreen render enabled and now uses D3D11 QRhi synchronous staging-map readback; set `MIACODE_EXPORT_RENDER_BACKEND=opengl` to use the OpenGL FBO/PBO rollback path, where `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` still disables PBO readback
  - `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` is the standard safety override and the forced env used by worker crash retry
  - `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO=1` now only makes the default explicit
  - `MIACODE_EXPORT_RENDER_BACKEND=d3d11_qrhi|opengl|auto` selects the offscreen chart-render session for CLI export and the export worker; default `d3d11_qrhi` uses `PreviewQuickD3D11ExportSession` on Windows with P3-policy adapter selection, `QQuickGraphicsDevice::fromDeviceAndContext`, `QQuickRenderTarget::fromD3D11Texture`, and synchronous CopyResource+Map readback, while `opengl` keeps the stable OpenGL QQuickRenderControl FBO/PBO rollback path
- Encoder selection and tuning:
  - `MIACODE_EXPORT_SKIP_ENCODER_RUNTIME_PROBE`
  - `MIACODE_EXPORT_FORCE_ENCODER`
  - `MIACODE_EXPORT_ENCODER_MODE`
  - `MIACODE_EXPORT_ENCODER_THREADS`
  - `MIACODE_EXPORT_FILTER_THREADS`
  - `MIACODE_EXPORT_X264_PRESET`
  - `MIACODE_EXPORT_X264_CRF`
  - `MIACODE_EXPORT_X264_BFRAMES`

Current normalization rules:

- `MIACODE_EXPORT_DIAG_MAX_LINES` is the shared default for compare and pipe-hash line caps.
- object-trace max-lines defaults to `max(diag_max_lines, 5000)` unless explicitly overridden.
- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO` wins over `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`.
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO` implies GPU render should be requested.
- D3D11 QRhi export does not use PBO; OpenGL rollback export keeps offscreen PBO readback enabled unless explicitly disabled.
- D3D11 QRhi export has no PBO path; the controller logs `pbo_readback_disabled_by_backend` and uses synchronous staging-map readback.
- export log tags `pbo_capability_probe`, `pbo_cleanup_deferred`, and `export_context_not_current_on_teardown` are the primary probe / teardown breadcrumbs for PBO stability issues.
- export worker `CrashExit` retries exactly once, only when the crashing attempt still requested PBO, and the retry injects `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1`.
- encoder auto mode defaults to `balanced`.

## 6. Helper Binaries And Scripts For Debug Work

- Helper executables:
  - `miacode_muri_dump`
  - `simai_native_dump`
  - `soundtouch_probe`
- Common debug scripts:
  - `scripts/debug/Start_MiaCode_Debug.bat`
  - `scripts/debug/Start_MiaCode_SoftwareVideoDecode.bat`
  - `scripts/debug/Start_MiaCode_QtPluginDiag.bat`

Update this file when a debug switch, log path, log channel, release debug workflow, or export diagnostic family changes.
