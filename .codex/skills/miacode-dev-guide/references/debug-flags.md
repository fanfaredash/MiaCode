# Debug Flags

Use this file to locate repo-wide debug switches, log channels, preview overrides, and export-only diagnostic environment variables.

The user-facing canonical doc lives at `docs/DEBUG_INDEX.md`. This file stays shorter and points to the owning code.

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
  - quickshell external-media sessions now also write `external_stage_media.*` summary rows for separate-surface state, media kind, aggregate video-frame counts, estimated video FPS, video-frame intervals, and stall counts without emitting per-frame runtime logs
  - current summary also includes `stage_bg.*` sub-metrics for stage-background media conversion, media texture work, dim-uniform updates, node-update time, and media/dim frame counters
  - layer summary rows now also include `candidate_count_*` and `active_count_*` so prepared-window efficiency can be compared against sprite/batch counts

The Windows release package also ships:

- `Start_MiaCode_Debug.bat`
  - sets debug mode, creates a local `logs/` directory, and launches `MiaCode.exe --debug`
- `Start_MiaCode_QuickShell_Debug.bat`
  - sets debug mode, creates a dedicated `logs/quick-shell-beta/` directory, and launches `MiaCode.exe --quick-shell-beta --debug`

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
- `preview/host_window_event`
- `preview/embedded_refresh`
- `preview/quick_runtime`
- `preview/quick_scene`

The `preview/quick_runtime` stream now also emits `action=frame_stall` when the embedded Quick window stays visible/exposed but stops presenting for an extended interval.
The `preview/embedded_refresh` stream now also marks resize-throttling transitions with `action=resize_degrade_begin` / `action=resize_degrade_end`.
The `startup/qt_config` runtime tag logs the active Qt graphics/render-loop experiment flags at process start, including whether the default native-sibling workaround was opted out.
The `window/focus` runtime tag now traces app-level `focusChanged`, activation edges, watched editor/preview `FocusIn`/`FocusOut` events, pending text-focus snapshots, and restore attempts for Alt-Tab regression debugging.
The `preview/playback` audio tag now traces realtime preview start transactions. Strong-sync startup should log `action=start_request`, `action=audio_prepared`, `action=canvas_presented`, and `action=commit` under one `txn`, while weak-video startup adds `action=weak_video_prepare_started`, `action=weak_video_ready_before_commit`, or `action=late_video_start_after_commit`.
The `preview/stage_media` audio tag is now switch-level only for quickshell media changes, presentation-mode flips, `VideoOutput` binding transitions, weak-sync video prepare / commit transitions (`action=prepare_playback_*`, `action=commit_prepared_playback*`), and low-noise external-video stall transitions (`action=video_frame_stall_begin` / `action=video_frame_stall_end`); the old per-frame quickshell video arrival line was retired.
The audio log emits `stretched_clock_drift` for low-noise stretched-BGM drift sampling; current fields are `fallback`, `bg`, `delta_ms`, `rate`, `engine_now_frame`, `start_engine_frame`, and `tick_bg_gap_ms` (`fallback - backgroundTrackLastTimelineSecond`) so engine-time anchor drift can be separated from tick-to-tick catch-up.
The preview audio facade now also emits `preview_audio_backend` selection lines so backend routing and BASS fallback decisions are visible in the audio log.
The BASS preview backend now also emits low-noise `bass_schedule_arm` and `bass_status` lines. `bass_schedule_arm` shows which collapsed SFX group has been armed as the next mixer sync, while `bass_status` samples the current authoritative audio second, mixer-relative second, BGM transport second, fallback drift in milliseconds, the next armed SFX group, and the last triggered SFX group at roughly a `1s` interval.
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
  - these now drive `VideoExportQuickRenderBackend` plus `PreviewQuickExportSession`, not the removed legacy offscreen renderer
  - default export path keeps GPU offscreen render enabled and requests PBO readback unless `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1`
  - `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` is the standard safety override and the forced env used by worker crash retry
  - `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO=1` now only makes the default explicit
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
- offscreen PBO readback defaults to enabled unless explicitly disabled.
- export log tags `pbo_capability_probe`, `pbo_cleanup_deferred`, and `export_context_not_current_on_teardown` are the primary probe / teardown breadcrumbs for PBO stability issues.
- export worker `CrashExit` retries exactly once, only when the crashing attempt still requested PBO, and the retry injects `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1`.
- encoder auto mode defaults to `balanced`.

## 6. Helper Binaries And Scripts For Debug Work

- Helper executables:
  - `miacode_muri_dump`
  - `simai_native_dump`
  - `soundtouch_probe`
- Common debug scripts:
  - `scripts/Start_MiaCode_Debug.bat`
  - `scripts/Start_MiaCode_QuickShell_Debug.bat`
  - `scripts/Start_MiaCode_Debug_CompareDump.bat`
  - `scripts/analyze_ffmpeg_chain_variants.py`
  - `scripts/analyze_video_duplicate_frames.py`
  - `scripts/compare_log_vs_video_trajectory.py`
  - `scripts/export_and_analyze_duplicates.py`

Update this file when a debug switch, log path, log channel, release debug workflow, or export diagnostic family changes.
