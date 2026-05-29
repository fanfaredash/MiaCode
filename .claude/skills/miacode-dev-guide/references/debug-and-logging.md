# Debug & Logging

The `--debug` logging system, log channels, and the `MIACODE_*` env-var index. The legacy
canonical doc is `docs/DEBUG_INDEX.md`; this file is the code-owner-oriented summary.

## 1. Logging architecture (this is the ONE mechanism — use it)

- Core: `miacode::debug_log` in `src/common/DebugLog.{h,cpp}` — an **async, channelized log
  writer** (background thread + queue + overflow/drop stats + flush/shutdown).
- Channels (`debug_log::Channel`): `Runtime`, `Audio`, `Export`, `StartupTiming`, `Fatal`,
  `PreviewProfile`, `Operation`.
- API: `debug_log::appendLine(channel, scope, payload)` / `appendTimingLine(...)` /
  `appendFatalMessage(...)`. ~183 call sites go through this; there is essentially no raw
  `qDebug`/`std::cout` in mainline code — **keep it that way.**
- One process-level `qInstallMessageHandler` is installed in `src/app/main.cpp` (~`:2066`).
- Options live in `src/common/DebugOptions.h` (env reads via `envFlagEnabled` /
  `envOptionalFlagValue`).

**Rule:** new logging = a `debug_log` channel line gated by `--debug`. Do not add raw
`qDebug`/`qInfo`/`std::cout`/`printf`/`OutputDebugString`. The only existing raw sites are in the
deprecated worker and the off-by-default DComp path — do not copy that pattern into mainline.

### Gating
- Runtime/Audio/StartupTiming/PreviewProfile detail is gated by process debug mode (`--debug`).
- `Fatal` is intentionally **not** gated.
- `Export` keeps a concise stage/failure summary even without `--debug`; detail needs `--debug`.

### Log file locations
- Shared dir env: `MIACODE_LOG_DIR`. Default: project-local `.miacode/logs/` once a chart is
  bound, else app-local `logs/` next to the executable.
- Per-channel path overrides: `MIACODE_RUNTIME_LOG_PATH`, `MIACODE_AUDIO_LOG_PATH`,
  `MIACODE_EXPORT_LOG_PATH`, `MIACODE_STARTUP_LOG_PATH`, `MIACODE_FATAL_LOG_PATH`,
  `MIACODE_OPERATION_LOG_PATH`, `MIACODE_PREVIEW_PROFILE_PATH`.
- Default channel files: `miacode_runtime_debug.log`, `miacode_audio_debug.log`,
  `miacode_video_export.log`, `miacode_startup_timing.log`, `miacode_fatal.log`,
  `miacode_preview_profile_summary.txt`.

## 2. The env-flag situation (audit 2026-05-29)

The *writer* is unified and healthy. The *control surface* is fragmented: **79 distinct
`MIACODE_*` env vars** (reconciled 2026-05-29) read as string literals across the code, with no
single in-code registry. Fullest catalog: `docs/DEBUG_INDEX.md` (reconciled to match the code —
all 79 by category); this file is the agent-facing quick index. Guidance:

- **Prefer not to add a new flag.** If you must, add it to `DebugOptions.h` with a default and a
  one-line purpose, and list it in `docs/DEBUG_INDEX.md` in the same change — otherwise the
  `debug_flag_index_spec` CTest fails (it greps every `MIACODE_*` literal in `src/` and requires a
  doc entry, and flags doc entries with no code as stale). That guard is the current drift defense.
- **Intended direction (still pending):** a single table-driven registry in `DebugOptions.h`
  ({name, default, category, purpose}) that can *generate* `docs/DEBUG_INDEX.md` (today it is
  hand-written + `debug_flag_index_spec`-guarded), plus a clean split between user-facing `--debug`
  and developer-only diagnostic flags. Reduce the count as DComp/worker retire.
- **Already-dead flags** (removed from code; do not reintroduce): `MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY`,
  `MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY`, `MIACODE_PREVIEW_DONT_CREATE_NATIVE_WIDGET_SIBLINGS`,
  `MIACODE_PREVIEW_SESSION_SCRIPT`, `MIACODE_DISABLE_GL_DEBUG_MESSAGES`.
  `MIACODE_BUILD_DCOMP_SMOKE` is tied to the deleted `PreviewDCompPhase0Smoke.cpp`.

## 3. Env-var index by category

**Logging control:** `MIACODE_LOG_DIR`, the `*_LOG_PATH` set (above), `MIACODE_OPLOG_SHADOW_PATH`,
`MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT`, `MIACODE_DISABLE_AUDIO_DEBUG_OUTPUT`,
`MIACODE_DISABLE_EXPORT_DEBUG_OUTPUT`, `MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT`,
`MIACODE_DISABLE_STARTUP_TIMING`, `MIACODE_SKIP_ASYNCLOG_FLUSH`, `MIACODE_OPERATION_LOG_PATH`.

**Preview (active QSG path) tuning/diag:** `MIACODE_PREVIEW_VISUAL_SMOOTHING`,
`MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS`, `MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES`,
`MIACODE_PREVIEW_FRAME_PACING_DIAG`(`_SAMPLE_MS`), `MIACODE_PREVIEW_QSG_RENDER_TIMING`,
`MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP`, `MIACODE_TIMELINE_HOTPATH_DIAG`,
`MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS`,
`MIACODE_PREVIEW_SFX_DIR`, `MIACODE_TRACK_PATH`, `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_{FRAMES,DIR,MAX_SAMPLES}`.

**DComp/D3D11 (DEFAULT OFF — being decoupled):** `MIACODE_PREVIEW_USE_DCOMP` (default off,
`DebugOptions.h:194`), `MIACODE_TIMELINE_USE_DCOMP`, `MIACODE_PREVIEW_DCOMP_EXCLUSIVE`,
`MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA`, `MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND`,
`MIACODE_PREVIEW_DCOMP_QUIESCE_QSG`, `MIACODE_PREVIEW_QSG_FULL_DISABLE`, `MIACODE_POPUP_TRACKER`,
`MIACODE_SKIP_DIAG_D3D11`.

**Out-of-process worker (DEPRECATED — delete with the worker):**
`MIACODE_PREVIEW_OUT_OF_PROCESS`, `MIACODE_PREVIEW_WORKER_QSG_RENDER`,
`MIACODE_PREVIEW_WORKER_REAL_PUBLISHER`, `MIACODE_PREVIEW_WORKER_DISABLE_CRASH_LIMIT`,
`MIACODE_PREVIEW_WORKER_DISABLE_MURI_REPORT`, `MIACODE_PREVIEW_WORKER_INJECT_CRASH`(`_MODE`),
`MIACODE_PREVIEW_CRASH_TEST_CYCLES`.

**Export diagnostics/tuning** (owner: `src/tools/video_export/VideoExportController.cpp` +
`VideoExportRuntimePolicy.h`): `MIACODE_EXPORT_DIAG_*` (REPEAT, CROP_BOTTOM, MAX_LINES,
OBJECT_{HASH,TRACE,TRACE_MAX_LINES,DIFF_THRESHOLD}, COMPARE_{RENDER_PATHS,RADIUS,MAX_LINES,LOG_THRESHOLD},
PIPE_HASH(`_MAX_LINES`), RAW_DUMP_PATH, LOG_ALL_REPEATS); backend forcing
`MIACODE_EXPORT_{ENABLE_GPU_RENDER,ENABLE_OFFSCREEN_PBO,DISABLE_OFFSCREEN_PBO,DISABLE_PBO_READBACK}`;
encoder `MIACODE_EXPORT_{SKIP_ENCODER_RUNTIME_PROBE,FORCE_ENCODER,ENCODER_MODE,ENCODER_THREADS,
FILTER_THREADS,X264_PRESET,X264_CRF,X264_BFRAMES}`.

**Misc/runtime:** `MIACODE_FFMPEG`(`_PATH`), `MIACODE_LANG`, `MIACODE_DISPLAY_VERSION_STRING`,
`MIACODE_DISABLE_MMCSS`, `MIACODE_SKIP_PREFLIGHT`.

## 4. Runtime trace tags (Runtime channel `scope` values)

Stable tags include: `window/focus`, `app_shutdown`, `close_timing/*`, `preview/quick_runtime`,
`preview/quick_scene`, `preview/interaction`, `preview/frame_pacing`, `timeline/interaction`,
`timeline/bridge`, `timeline/quick_scene`, `timeline/cursor_map`, and `edit/*_perf` editor
performance tags. High-frequency tags (`timeline/bridge` scroll pushes, `timeline/quick_scene`
scroll-only paints) require `MIACODE_TIMELINE_HOTPATH_DIAG=1` even in debug mode.

## Update this file when

- A debug flag or log channel is added, removed, or re-gated (and update `DebugOptions.h`).
- A flag moves between the active / DComp-off / worker-deprecated categories.
- The logging mechanism or default log directory changes.
