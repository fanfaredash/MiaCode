# Debug Index

This document is the current user-facing index for MiaCode debug mode, log files, and preview/export diagnostics after the Qt Quick migration.

## Debug Entry Points

- Preferred CLI switch: `--debug`
- Windows release helper:
  - `Start_MiaCode_Debug.bat`
- Windows quick-shell-beta debug helper:
  - `Start_MiaCode_QuickShell_Debug.bat`

Inside debug mode, runtime, audio, export, startup-timing, and preview-profile outputs are enabled unless they are individually disabled.

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

## Category Gates

These only matter while debug mode is active:

- `MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT`
- `MIACODE_DISABLE_AUDIO_DEBUG_OUTPUT`
- `MIACODE_DISABLE_EXPORT_DEBUG_OUTPUT`
- `MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT`
- `MIACODE_DISABLE_STARTUP_TIMING`
- `MIACODE_DISABLE_GL_DEBUG_MESSAGES`

Fatal logging is intentionally not gated by debug mode.

## Current Preview / Export Backend Notes

- Realtime preview and export both use Qt Quick scene graph.
- The desktop app currently forces Qt Quick to the OpenGL backend.
- Export PBO diagnostics now describe the headless Quick export session, not a removed legacy renderer.

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

## Preview-Side Notes

Still active:

- `MIACODE_PREVIEW_SESSION_SCRIPT`
- `MIACODE_PREVIEW_SFX_DIR`
- `MIACODE_TRACK_PATH`
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

Retired with the old preview renderer and not recommended anymore:

- `MIACODE_ENABLE_PYGAME_PREVIEW`
- `MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY`
- `MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY`

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
  - `external_stage_media.video_frame_*` rows now include aggregate external-video frame rate, interval, and stall counts
- runtime log:
  - `preview/stage_media` now also emits low-noise `action=video_frame_stall_begin` / `action=video_frame_stall_end` transitions when external video stops delivering frames for longer than expected
  - `window/focus` records app-level focus transitions, activation edges, watched editor focus events, and text-focus restore attempts for focus-regression diagnosis

## Useful Workflows

Launch the default Quick Shell app in debug mode:

- `MiaCode.exe --debug`

Launch the legacy Qt native widget shell in debug mode:

- `MiaCode.exe --qt-native --debug`

Launch the Qt Quick hybrid host explicitly in debug mode:

- `MiaCode.exe --quick-shell-beta --debug`

Force export logging into a local directory:

- `set MIACODE_LOG_DIR=<folder>`
- `MiaCode.exe --debug`

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

For architecture details, see `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`.
For the native-window investigation and rollout plan, see `docs/PREVIEW_NATIVE_WINDOW_DEVELOPMENT_PLAN_SPEC.md`.
