# Debug Flags

Use this file to locate repo-wide debug switches, timing logs, preview overrides, and export-only diagnostic environment variables.

## 1. Generic Debug Controls

- Shared helper:
  - File: `src/common/DebugOptions.h`
  - Owns: truthy env parsing, runtime-debug CLI aliases, generic debug log paths
- Runtime debug output:
  - CLI aliases: `--debug-runtime`, `--miacode-debug`, `--enable-debug-output`
  - Entry: `MainWindow::configureRuntimeDebugOutput`
  - Effect: sets `MIACODE_ENABLE_RUNTIME_DEBUG_OUTPUT=1`, clears `%TEMP%/miacode_runtime_debug.log`
  - Current consumers:
    - `MainWindow::appendOutput`
    - `QtPreviewSfxRuntime::appendAudioDebugLog`
- Startup timing:
  - Env: `MIACODE_ENABLE_STARTUP_TIMING`
  - Legacy alias: `MAIMURI_ENABLE_STARTUP_TIMING`
  - Log path: `%TEMP%/miacode_startup_timing.log`
  - Current producers:
    - `src/app/main.cpp`
    - `src/app/mainwindow/MainWindow.cpp`
    - `src/preview/video/PreviewCanvas.cpp`

Prefer putting new repo-wide debug toggles in `src/common/DebugOptions.h` before adding one-off parsers.

## 2. Preview And Runtime Overrides

- Legacy preview backend toggle:
  - Env: `MIACODE_ENABLE_PYGAME_PREVIEW`
  - Legacy alias: `MAIMURI_ENABLE_PYGAME_PREVIEW`
  - Owner: `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
- Legacy preview session script override:
  - Env: `MIACODE_PREVIEW_SESSION_SCRIPT`
  - Legacy alias: `MAIMURI_PREVIEW_SESSION_SCRIPT`
  - Owner: `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- Preview SFX directory override:
  - Env: `MIACODE_PREVIEW_SFX_DIR`
  - Legacy alias: `MAIMURI_PREVIEW_SFX_DIR`
  - Owner: `src/common/PreviewSfxAssets.h`
- Preview track-path override:
  - Env: `MIACODE_TRACK_PATH`
  - Legacy alias: `MAIMURI_TRACK_PATH`
  - Owner: `src/app/mainwindow/MainWindow.cpp`

These are debug-friendly overrides, but they are feature-specific and should stay near the owning workflow unless they become generic controls.

## 3. Editor Runtime Performance Logging

- Flag source:
  - Same runtime-debug CLI aliases from section 1
- Log path:
  - `%TEMP%/miacode_runtime_debug.log`
- Current edit-performance tags:
  - `edit/metadata_perf`
  - `edit/muri_perf`
  - `edit/validation_perf`
  - `edit/validation_apply_perf`
- Main owner:
  - `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - `src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`

This path is intended for segmentation and regression hunting during editor typing work. Keep tags stable when possible so old logs stay comparable.

## 4. Export-Only Diagnostics And Tuning

These knobs are intentionally not merged into the generic helper because they belong to the video-export pipeline only.

- Export logging:
  - `MIACODE_EXPORT_LOG_PATH`
- Repeated-frame and compare diagnostics:
  - `MIACODE_EXPORT_DIAG_REPEAT`
  - `MIACODE_EXPORT_DIAG_LOG_ALL_REPEATS`
  - `MIACODE_EXPORT_DIAG_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_COMPARE_RENDER_PATHS`
  - `MIACODE_EXPORT_DIAG_COMPARE_RADIUS`
  - `MIACODE_EXPORT_DIAG_COMPARE_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_COMPARE_LOG_THRESHOLD`
  - `MIACODE_EXPORT_DIAG_CROP_BOTTOM`
- Object and pipe tracing:
  - `MIACODE_EXPORT_DIAG_OBJECT_HASH`
  - `MIACODE_EXPORT_DIAG_OBJECT_DIFF_THRESHOLD`
  - `MIACODE_EXPORT_DIAG_OBJECT_TRACE`
  - `MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_PIPE_HASH`
  - `MIACODE_EXPORT_DIAG_PIPE_HASH_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_RAW_DUMP_PATH`
- Export render-path forcing:
  - `MIACODE_EXPORT_ENABLE_GPU_RENDER`
  - `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
  - `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`
- Encoder probing and tuning:
  - `MIACODE_EXPORT_SKIP_ENCODER_RUNTIME_PROBE`
  - `MIACODE_EXPORT_FORCE_ENCODER`
  - `MIACODE_EXPORT_ENCODER_MODE`
  - `MIACODE_EXPORT_ENCODER_THREADS`
  - `MIACODE_EXPORT_FILTER_THREADS`
  - `MIACODE_EXPORT_X264_PRESET`
  - `MIACODE_EXPORT_X264_CRF`
  - `MIACODE_EXPORT_X264_BFRAMES`
  - `MIACODE_EXPORT_X265_POOLS`
  - `MIACODE_EXPORT_X265_FRAME_THREADS`
  - `MIACODE_EXPORT_X265_LOOKAHEAD`
  - `MIACODE_EXPORT_X265_LOOKAHEAD_SLICES`
  - `MIACODE_EXPORT_X265_BFRAMES`
  - `MIACODE_EXPORT_X265_WPP`
  - `MIACODE_EXPORT_X265_PMODE`
  - `MIACODE_EXPORT_X265_PME`
- Main owner:
  - `src/tools/video_export/VideoExportController.cpp`
- Script that preloads a common diagnostic bundle:
  - `scripts/export_and_analyze_duplicates.py`

## 5. Helper Binaries And Scripts For Debug Work

- Helper executables:
  - `miacode_muri_dump`
  - `simai_native_dump`
  - `soundtouch_probe`
- Common analysis scripts:
  - `scripts/analyze_ffmpeg_chain_variants.py`
  - `scripts/analyze_video_duplicate_frames.py`
  - `scripts/compare_log_vs_video_trajectory.py`
  - `scripts/export_and_analyze_duplicates.py`

Update this file when a new debug switch is added, a log path changes, or a feature-specific diagnostic family grows large enough that engineers need an index instead of grep.
