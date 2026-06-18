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
off-by-default DComp path — do not copy that pattern into mainline.

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

The *writer* is unified and healthy. The *control surface* is fragmented: **72 distinct
`MIACODE_*` env vars** (reconciled 2026-06-02, after the out-of-process preview worker was
deleted) read as string literals across the code, with no single in-code registry. Fullest
catalog: `docs/DEBUG_INDEX.md` (reconciled to match the code — all by category); this file is
the agent-facing quick index. Guidance:

- **Prefer not to add a new flag.** If you must, add it to `DebugOptions.h` with a default and a
  one-line purpose, and list it in `docs/DEBUG_INDEX.md` in the same change — otherwise the
  `debug_flag_index_spec` CTest fails (it greps every `MIACODE_*` literal in `src/` and requires a
  doc entry, and flags doc entries with no code as stale). That guard is the current drift defense.
- **Intended direction (still pending):** a single table-driven registry in `DebugOptions.h`
  ({name, default, category, purpose}) that can *generate* `docs/DEBUG_INDEX.md` (today it is
  hand-written + `debug_flag_index_spec`-guarded), plus a clean split between user-facing `--debug`
  and developer-only diagnostic flags. Reduce the count further as DComp retires.
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
`MIACODE_PREVIEW_REJECT_NEGATIVE_HS` (default off; opt-OUT — negative-HS `<HS*-N>` reverse-flow is ON
by default, `=1` restores strict reject of hs<=0 per Q7; read once at boot into `SimaiNativeParser::setAllowNegativeHsEnabled` as `!reject`),
`MIACODE_PREVIEW_FRAME_PACING_DIAG`(`_SAMPLE_MS`), `MIACODE_PREVIEW_QSG_RENDER_TIMING`,
`MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP`, `MIACODE_TIMELINE_HOTPATH_DIAG`,
`MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS`,
`MIACODE_PREVIEW_SFX_DIR`, `MIACODE_TRACK_PATH`, `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_{FRAMES,DIR,MAX_SAMPLES}`,
`MIACODE_PREVIEW_DUMP_HWFRAMES`, `MIACODE_PREVIEW_D3D11_DEBUG_LAYER`,
`MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT` (default **off** — §10 fix attempt, did NOT fix Arc 130T; reserved), `MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT`.

**HW-decode green/garble/seek diagnostics (Windows D3D11VA, `DebugOptions.h` accessors
`previewDumpHwFrameBudget()` / `previewSharedD3D11DebugLayerEnabled()`, both `--debug`-gated):**
The decode/copy path lives in `third_party/QtAVPlayer/.../qavhwdevice_d3d11.cpp` (render thread)
and `qavplayer.cpp` `skipFrame` (decode thread). Per the drift-guard (`DebugFlagIndexSpec` scans
only `src/`), the env literals stay in `src/common/DebugOptions.h` and the resolved values are
PUBLISHED into the decoder via setters in `qavd3d11sharedcontext_p.h`
(`qavSetPreviewDiagLogSink` / `qavSetPreviewHwFrameDumpConfig` / `qavArmPreviewHwFrameDump` /
`qavGetPreviewDiagCounters` / `qavTakePreviewCatchupSkipCount`), wired once by
`miacode::preview::installPreviewDecodeDiagnostics()` (called from
`PreviewStageMediaHost::initializeBackendObjects`). Storm-safety: the per-frame copy path only
does relaxed-atomic counter increments + ONE atomic load+compare for the dump gate — no env read,
no string, no log, no I/O when `MIACODE_PREVIEW_DUMP_HWFRAMES=0` (default). `MIACODE_PREVIEW_DUMP_HWFRAMES=N`
bounds a STAGING NV12 readback to N frames/arm (re-armed on seek) that emits NV12-domain stats +
verdict `hint` as one log line per readback (no image files written). Log scopes (all `Channel::Runtime`): `preview/hwframe`
(per-dump stat line + path transitions, via the published sink), `preview/hwframe_d3d11dbg`
(drained `ID3D11InfoQueue` debug-layer messages), `preview/hwdecode_summary` (cumulative
copy/timeout/copy-fail/res-change counters + coded-vs-display, drained on the GUI thread at
seek/EoM), `preview/seek_landing` (seek→first-display latency + catch-up GOP-burst count). Spec:
`docs/PREVIEW_HWDECODE_GREEN_GARBLE_SEEK_DEBUG_PLAN_ZH.md` §9.

**HW-decode green/garble FIX ATTEMPT (§10, located on Arc 130T = decoder output, completion-order; NOT
`--debug`-gated):** `MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT` (default **OFF** as of the
render-mode-toggle wrap-up — it did NOT fix the Arc 130T green (A/B verified); kept as env-gated
RESERVED code, accessor `previewHwDecodeCompletionWaitEnabled()`) makes the copy paths force the
decode GPU work to complete (`ID3D11Query(EVENT)` + `Flush` + bounded ~100ms spin via
`waitForDecodeCompletion`) before `CopySubresourceRegion` reads the DPB slot. The shipped fix is now
the **硬件渲染/软件渲染 user preference** (default hardware; `PreviewStageMediaHost::setVideoDecodePreference`
hot-switches the live `QAVPlayer` in place — `setInputVideoCodec("")`⇄`"software"` + reload + seek,
no restart). `MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT`
(default off, `previewHwDecodeDropCorruptFramesEnabled()`) drops frames FFmpeg flagged
corrupt/`decode_error_flags` (free, from the AVFrame) so RHI holds the last good frame. Both are
published via `qavSetPreviewHwDecodeFixConfig`. New diag fields: `preview/hwframe` dump line gains
`since_seek_ms` / `decode_err` / `corrupt` / `completion_wait` / `codec`; `preview/hwdecode_summary`
gains `completion_waits` / `completion_wait_timeouts` / `frames_decode_error` / `corrupt_dropped` /
`codec`. Detail: `docs/PREVIEW_HWDECODE_GREEN_GARBLE_SEEK_DEBUG_PLAN_ZH.md` §10–§11. ⚠ Repro: iGPUs default to software
decode — launch with `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=0` (+ `MIACODE_PREVIEW_SINGLE_D3D11_DEVICE=1`
for H2). For the two-device path's RHI debug layer use the Qt env `QSG_RHI_DEBUG_LAYER=1` (Qt creates
that device; `MIACODE_PREVIEW_D3D11_DEBUG_LAYER` only reaches the imported H2 device).

**Preview video decode backend (Windows):** `MIACODE_USE_QTAVPLAYER` is a **build-time compile
macro** (CMake-defined on Windows, NOT an environment flag — it can't be toggled at runtime). It
switches `PreviewStageMediaHost` onto the FFmpeg/QtAVPlayer backend; other platforms keep the
`QMediaPlayer` path. It still appears in `docs/DEBUG_INDEX.md` because the `debug_flag_index_spec`
drift guard greps every `MIACODE_*` literal in `src/`. The FFmpeg dev SDK path is a CMake cache
variable (not in `src/`, so keep its literal out of `DEBUG_INDEX.md` or the guard flags it stale).
On hardware-decode `InvalidMedia` the host retries once forcing software decode — log line
`Channel::Audio` scope `preview/stage_media`, `action=video_software_fallback`. The QtAVPlayer path
drops the QMediaPlayer-only `recoverVideoBackend` / playback watchdog / soft-recovery / deferred-rate
scaffolding (no silent WMF fallback, no converter-rebuild crash to recover from).
`MIACODE_PREVIEW_SINGLE_D3D11_DEVICE` (default off; env flag, `DebugOptions.h`) enables **H2
single-device decode**: `PreviewSharedD3D11Device` creates one video-capable, multithread-protected
`ID3D11Device`, hands it to the preview `QQuickView` (`QuickShellPreviewCompositeSurface` →
`setGraphicsDevice`) AND publishes it to the decoder (`qavSetSharedRenderD3D11Device` →
`qavdemuxer.cpp` `setup_video_codec` → `av_hwdevice_ctx_alloc`+init). Then `qavhwdevice_d3d11.cpp`
`handle()` takes a same-device fast path (`copyTextureSameDevice`, no shared handle/keyed-mutex/
`AcquireSync(INFINITE)`) instead of the two-device keyed-mutex bridge. Every step falls back to the
legacy two-device path on failure. Confirm via `media_backend … single_device=1`. See
`docs/PREVIEW_VIDEO_IGPU_STUTTER_INVESTIGATION_AND_FIX_ZH.md` §4 Tier 3 H2.

**DComp/D3D11 (DEFAULT OFF — being decoupled):** `MIACODE_PREVIEW_USE_DCOMP` (default off,
`DebugOptions.h:194`), `MIACODE_TIMELINE_USE_DCOMP`, `MIACODE_PREVIEW_DCOMP_EXCLUSIVE`,
`MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA`, `MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND`,
`MIACODE_PREVIEW_DCOMP_QUIESCE_QSG`, `MIACODE_PREVIEW_QSG_FULL_DISABLE`, `MIACODE_POPUP_TRACKER`,
`MIACODE_SKIP_DIAG_D3D11`.

**Export diagnostics/tuning** (owner: `src/tools/video_export/VideoExportController.cpp` +
`VideoExportRuntimePolicy.h`): `MIACODE_EXPORT_DIAG_*` (REPEAT, CROP_BOTTOM, MAX_LINES,
OBJECT_{HASH,TRACE,TRACE_MAX_LINES,DIFF_THRESHOLD}, COMPARE_{RENDER_PATHS,RADIUS,MAX_LINES,LOG_THRESHOLD},
PIPE_HASH(`_MAX_LINES`), RAW_DUMP_PATH, LOG_ALL_REPEATS); backend forcing
`MIACODE_EXPORT_{ENABLE_GPU_RENDER,ENABLE_OFFSCREEN_PBO,DISABLE_OFFSCREEN_PBO,DISABLE_PBO_READBACK}`;
encoder `MIACODE_EXPORT_{SKIP_ENCODER_RUNTIME_PROBE,FORCE_ENCODER,ENCODER_MODE,ENCODER_THREADS,
FILTER_THREADS,X264_PRESET,X264_CRF,X264_BFRAMES}`.

**Misc/runtime:** `MIACODE_FFMPEG`(`_PATH`), `MIACODE_LANG`, `MIACODE_DISPLAY_VERSION_STRING`,
`MIACODE_DISABLE_MMCSS`, `MIACODE_SKIP_PREFLIGHT`.

**Build-time (CMake, not env):** `MIACODE_USE_QTAVPLAYER` (see above), `MIACODE_BUILD_DEV_TOOLS`
(configure option gating dev-tool spec executables + CTest registration; appears in
`docs/DEBUG_INDEX.md` only because the `debug_flag_index_spec` guard greps every `MIACODE_*`
literal in `src/`, including comments).

## 4. Runtime trace tags (Runtime channel `scope` values)

Stable tags include: `window/focus`, `app_shutdown`, `close_timing/*`, `preview/quick_runtime`,
`preview/quick_scene`, `preview/interaction`, `preview/frame_pacing`, `timeline/interaction`,
`timeline/bridge`, `timeline/quick_scene`, `timeline/cursor_map`, and `edit/*_perf` editor
performance tags. High-frequency tags (`timeline/bridge` scroll pushes, `timeline/quick_scene`
scroll-only paints) require `MIACODE_TIMELINE_HOTPATH_DIAG=1` even in debug mode.

Leak/resource gauges (beta4→beta7, both **once per user pause**, never per-frame, `--debug`-gated):
`preview/resource_gauge` (GUI thread, `pauseQtPreviewPlaybackExact` — process handles/commit/
page_faults + `d_play_kb` private-bytes-grown-during-play + `presents_in_play` + preview
`scene_revision`/`cached_tex*`/`present_total`) and `timeline/leak_gauge` (render thread, end of
`TimelineQuickItem::updatePaintNode`, armed by the pause — `d_render_kb`, QSG `nodes`/`gbytes_kb`,
`gpu_kb` via `QueryVideoMemoryInfo`, `geom_create`, texture-cache stats, per-layer breakdown).
These pinned the 0.5.0 preview leak (spec §8); keep them wired for regression checks —
healthy `d_play_kb` is < ~10 MB per play, beta7's leak showed 30-44 MB/s of play time.

## Update this file when

- A debug flag or log channel is added, removed, or re-gated (and update `DebugOptions.h`).
- A flag moves between the active / DComp-off categories.
- The logging mechanism or default log directory changes.
