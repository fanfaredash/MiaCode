# Debug & Logging

The `--debug` logging system, log channels, and the `MIACODE_*` env-var index. The legacy
canonical doc is `docs/ops/DEBUG_INDEX.md`; this file is the code-owner-oriented summary.

> **Remaining-refactor backlog:** the 2026-06-19 logging-design audit shipped its high-priority
> fixes in commit `034769c` (Level/UTC/pid-tid records, drop markers, rename rotation, flag cache,
> `ProcessDiagnostics` split, scaffolding cleanup). The remaining logging-design backlog is kept
> in maintainer-local notes; re-audit the channel model before changing logging design again.

## 1. Logging architecture (this is the ONE mechanism — use it)

- Core: `miacode::debug_log` in `src/common/DebugLog.{h,cpp}` — an **async, channelized log
  writer** (background thread + queue + overflow/drop stats + flush/shutdown).
- Channels (`debug_log::Channel`): `Runtime`, `Audio`, `Export`, `StartupTiming`, `Fatal`,
  `PreviewProfile`, `Operation`.
- API: `debug_log::appendLine(channel, scope, payload, force=false, level=Level::Info)` /
  `appendTimingLine(...)` / `appendFatalMessage(...)`. ~183 call sites go through this; there is
  essentially no raw `qDebug`/`std::cout` in mainline code — **keep it that way.**
- **Line format** (`appendLine`): `<UTC-ISO8601-Z> <LEVEL> pid=<n> tid=<n> [<channel>/<scope>] <payload>`
  (e.g. `2026-06-18T18:36:51.109Z ERROR pid=68408 tid=52460 [op/failed] op=… reason=…`). Timestamps are
  **UTC** (aligned with the crash shadow/beacon). `Level` (`Trace/Debug/Info/Warn/Error/Fatal`) is
  **orthogonal** to `Channel`: a fatal-grade line on ANY channel gets a durable synchronous flush (the
  sync path keys off `level==Fatal`, not the Fatal *channel*); `Channel::Fatal` always renders/flushes
  `FATAL`; oplog failures log at `Error`.
- **Overflow** (async queue full, `kMaxQueueSize=4096`): drops the OLDEST entry but emits a coalesced
  `[<channel>/asynclog] dropped=N reason=queue_overflow` gap marker so loss is visible, not silent.
- **Rotation** is rename-based: a channel file over 4 MB rotates to `miacode_<x>.1.log` (chain `.1`→`.2`→`.3`,
  `kMaxLogSegments=3`), preserving the session start — NOT in-place head-truncation.
- **Process/leak diagnostics** (`processResourceGaugePayload`, `MemoryStageScope`, `processPrivateBytes`,
  `leak_gauge`) are in `src/common/ProcessDiagnostics.{h,cpp}` (namespace **`miacode::diag`**), split OUT of
  DebugLog so the writer stays a pure channelized log (the profiler depends on it, not the reverse).
- One process-level `qInstallMessageHandler` is installed in `src/app/main.cpp` (~`:2066`).
- Options live in `src/common/DebugOptions.h` (env reads via `envFlagEnabled` /
  `envOptionalFlagValue`).

**Rule:** new logging = a `debug_log` channel line gated by `--debug`. Do not add raw
`qDebug`/`qInfo`/`std::cout`/`printf`/`OutputDebugString`. (`PreviewPopupHwndTracker`'s old quadruple-write —
force=true debug_log + qDebug + raw fopen + OutputDebugStringW — was the last such offender and is now a single
gated `appendLine`; don't reintroduce that pattern.)

### Gating
- Runtime/Audio/StartupTiming/PreviewProfile detail is gated by process debug mode (`--debug`).
- `Fatal` is intentionally **not** gated.
- `Export` keeps a concise stage/failure summary even without `--debug`; detail needs `--debug`.
- The per-category `MIACODE_DISABLE_*` gates are **snapshot into atomics at `setDebugModeEnabled`** (not
  re-read per log line). If code/tests mutate one of those env vars at runtime, call
  `debug_options::refreshDebugCategoryCache()` to re-snapshot. `MIACODE_SKIP_ASYNCLOG_FLUSH` is read once.

### Log file locations
- Shared dir env: `MIACODE_LOG_DIR`. Default: project-local `.miacode/logs/` once a chart is
  bound, else app-local `logs/` next to the executable.
- Per-channel path overrides: `MIACODE_RUNTIME_LOG_PATH`, `MIACODE_AUDIO_LOG_PATH`,
  `MIACODE_EXPORT_LOG_PATH`, `MIACODE_STARTUP_LOG_PATH`, `MIACODE_FATAL_LOG_PATH`,
  `MIACODE_OPERATION_LOG_PATH`, `MIACODE_PREVIEW_PROFILE_PATH`.
- Default channel files: `miacode_runtime_debug.log`, `miacode_audio_debug.log`,
  `miacode_video_export.log`, `miacode_startup_timing.log`, `miacode_fatal.log`,
  `miacode_preview_profile_summary.txt`. Logs live per-chart under `<chart>/.miacode/logs/`.
- **⚠ Preview-log channel split (easy time-sink):** `PreviewStageMediaHost::appendPreviewStageMediaLog`
  (`src/preview/runtime/PreviewStageMediaHost.cpp:127`) routes to **`Channel::Audio`** (scope
  `preview/stage_media`) → **`miacode_audio_debug.log`**, NOT the runtime log. So `media_backend`,
  `media_status`, `video_frame_first`, `set_chart_path`, `bind_video_output`, and the render-mode
  toggle's `video_decode_preference` / `video_decode_reload` are in the **audio** log
  (`[audio/preview/stage_media] action=…`). The HW-decode green diagnostics in the SAME file use a
  direct `appendLine(Channel::Runtime, "preview/hwframe"|"preview/hwdecode_summary"|"preview/seek_landing", …)`
  → **runtime** log. So: video decode / PV playback / decode-toggle ⇒ grep `miacode_audio_debug.log`;
  hwframe / seek / summary ⇒ grep `miacode_runtime_debug.log`. (Grepping the runtime log for decode
  activity finds only the `runtime/app_shutdown/...stage_media_host` teardown lines — looks like "nothing logged.")

## 2. The env-flag situation (audit 2026-05-29)

The *writer* is unified and healthy. The *control surface* is fragmented: **72 distinct
`MIACODE_*` env vars** (reconciled 2026-06-02, after the out-of-process preview worker was
deleted) read as string literals across the code, with no single in-code registry. Fullest
catalog: `docs/ops/DEBUG_INDEX.md` (reconciled to match the code — all by category); this file is
the agent-facing quick index. Guidance:

- **Prefer not to add a new flag.** If you must, add it to `DebugOptions.h` with a default and a
  one-line purpose, and list it in `docs/ops/DEBUG_INDEX.md` in the same change — otherwise the
  `debug_flag_index_spec` CTest fails (it greps every `MIACODE_*` literal in `src/` and requires a
  doc entry, and flags doc entries with no code as stale). That guard is the current drift defense.
- **Intended direction (still pending):** a single table-driven registry in `DebugOptions.h`
  ({name, default, category, purpose}) that can *generate* `docs/ops/DEBUG_INDEX.md` (today it is
  hand-written + `debug_flag_index_spec`-guarded), plus a clean split between user-facing `--debug`
  and developer-only diagnostic flags. Reduce the count further as DComp retires.
- **Already-dead flags** (removed from code; do not reintroduce): `MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY`,
  `MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY`, `MIACODE_PREVIEW_DONT_CREATE_NATIVE_WIDGET_SIBLINGS`,
  `MIACODE_PREVIEW_SESSION_SCRIPT`, `MIACODE_DISABLE_GL_DEBUG_MESSAGES`.
  `MIACODE_BUILD_DCOMP_SMOKE` is tied to the deleted `PreviewDCompPhase0Smoke.cpp`.
- **Hang-watchdog instrumentation macros, not env vars:** `MIACODE_HANG_PHASE`,
  `MIACODE_HANG_JOIN`, and `MIACODE_HANG_JOIN_IMPL` live in `src/common/UiHangWatchdog.h`;
  they are documented here and in `docs/ops/DEBUG_INDEX.md` because `debug_flag_index_spec`
  indexes every `MIACODE_*` token in `src/`.

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
public summary in `docs/ops/DEBUG_INDEX.md`.

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
`codec`. Detailed investigation is kept in local private notes. ⚠ Repro: iGPUs default to software
decode — launch with `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=0` (+ `MIACODE_PREVIEW_SINGLE_D3D11_DEVICE=1`
for H2). For the two-device path's RHI debug layer use the Qt env `QSG_RHI_DEBUG_LAYER=1` (Qt creates
that device; `MIACODE_PREVIEW_D3D11_DEBUG_LAYER` only reaches the imported H2 device).

**Preview video decode backend (Windows):** `MIACODE_USE_QTAVPLAYER` is a **build-time compile
macro** (CMake-defined on Windows, NOT an environment flag — it can't be toggled at runtime). It
switches `PreviewStageMediaHost` onto the FFmpeg/QtAVPlayer backend; other platforms keep the
`QMediaPlayer` path. It still appears in `docs/ops/DEBUG_INDEX.md` because the `debug_flag_index_spec`
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
the H2 single-device summary in `docs/ops/DEBUG_INDEX.md`.

**DComp/D3D11 (DEFAULT OFF — being decoupled):** `MIACODE_PREVIEW_USE_DCOMP` (default off,
`DebugOptions.h:194`), `MIACODE_TIMELINE_USE_DCOMP`, `MIACODE_PREVIEW_DCOMP_EXCLUSIVE`,
`MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA`, `MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND`,
`MIACODE_PREVIEW_DCOMP_QUIESCE_QSG`, `MIACODE_PREVIEW_QSG_FULL_DISABLE`,
`MIACODE_SKIP_DIAG_D3D11`.

**Export diagnostics/tuning** (owner: `src/tools/video_export/VideoExportController.cpp` +
`VideoExportRuntimePolicy.h`): `MIACODE_EXPORT_DIAG_*` (REPEAT, CROP_BOTTOM, MAX_LINES,
OBJECT_{HASH,TRACE,TRACE_MAX_LINES,DIFF_THRESHOLD}, COMPARE_{RENDER_PATHS,RADIUS,MAX_LINES,LOG_THRESHOLD},
PIPE_HASH(`_MAX_LINES`), RAW_DUMP_PATH, LOG_ALL_REPEATS); backend forcing
`MIACODE_EXPORT_{ENABLE_GPU_RENDER,ENABLE_OFFSCREEN_PBO,DISABLE_OFFSCREEN_PBO,DISABLE_PBO_READBACK,
PREMULTIPLIED_PIPE}`;
encoder `MIACODE_EXPORT_{SKIP_ENCODER_RUNTIME_PROBE,FORCE_ENCODER,ENCODER_MODE,ENCODER_THREADS,
FILTER_THREADS,X264_PRESET,X264_CRF,X264_BFRAMES}`.
`MIACODE_EXPORT_RENDER_BACKEND` (**P5 — default `d3d11_qrhi`**, `exportRenderBackendRequest()` in
`DebugOptions.h`): `opengl` | `d3d11_qrhi` | `auto` selects the offscreen chart-render session for CLI
export + the export worker. `d3d11_qrhi` makes `main.cpp` put the export process on the Direct3D11
Quick graphics API and `VideoExportPreparedTask` drive the new
`src/preview/runtime/PreviewQuickD3D11ExportSession` (device on the P3-policy adapter imported via
`fromDeviceAndContext` — export has NO video-decode bridge, so a non-default adapter is safe here,
unlike GUI surfaces; `QQuickRenderTarget::fromD3D11Texture` R8G8B8A8_UNORM target; a three-texture
staging ring overlaps frame N rendering with Map/readback of N-2, top-down so no vertical flip).
Fast D3D11 export preserves premultiplied RGBA by default and pairs it with FFmpeg
`overlay=...:alpha=premultiplied`, avoiding the scalar per-pixel unpremultiply pass;
`MIACODE_EXPORT_PREMULTIPLIED_PIPE=0` restores straight RGBA conversion. HighQuality and OpenGL keep
straight RGBA. Init failure auto-falls-back to the OpenGL session in-process (graphics API flipped back;
export log `render_backend_fallback` `fallback_from=d3d11_qrhi fallback_to=opengl reason=…`). `auto`
currently == `d3d11_qrhi`; set `MIACODE_EXPORT_RENDER_BACKEND=opengl` for rollback. Session backend dispatch lives
in `VideoExportQuickRenderBackend` (`setRenderSessionBackend`); selected backend/adapter
LUID/rt_format/readback_mode appear in the `render_backend` export-log summary line. Sync pair: the
D3D11 session mirrors the OpenGL session's scene mounting (scene root + HUD + intro overlay +
DCompFallbackActive override) — change both together.

The current FFmpeg subprocess path is intentionally CPU-frame based: bundled `h264_mf` accepts only
`nv12`/`yuv420p`, bundled FFmpeg has no D3D11 overlay filter, and the export filter graph still owns
PV/mask/alpha/audio composition. A true GPU-direct encoder therefore requires a separate in-process
D3D11 compositor + RGBA-to-NV12 + Media Foundation/libav hardware-frame pipeline; do not label a
download/re-upload bridge as zero-copy.

Current default: unset `MIACODE_EXPORT_RENDER_BACKEND` selects `d3d11_qrhi`; set it to `opengl` for
the stable OpenGL rollback path. `auto` remains equivalent to `d3d11_qrhi`.

**Misc/runtime:** `MIACODE_FFMPEG`(`_PATH`), `MIACODE_LANG`, `MIACODE_DISPLAY_VERSION_STRING`,
`MIACODE_DISABLE_MMCSS`, `MIACODE_SKIP_PREFLIGHT`.

**GPU device policy (P3/P4 — hidden diagnostic, `src/common/GpuDevicePolicy.{h,cpp}` ns `miacode::gpu`;
provider `src/app/gpu_device_provider.cpp` ns `miacode::app::entry`):**
`MIACODE_GPU_POLICY` (`auto_high_performance` default | `platform_default` | `software`) and
`MIACODE_GPU_ADAPTER_LUID` (`<high>:<low>` hex/dec → forces `adapter_luid`). Equivalent CLI flags
`--gpu-policy=` / `--gpu-adapter-luid=` (registered in `addSharedCliDebugOption`; GUI reads them via
raw-arg scan, so no strict-parser conflict). **P3** **resolves + logs** the preferred high-perf DXGI
adapter (read-only `IDXGIFactory6::EnumAdapterByGpuPreference`, no device creation) via
`resolveGpuPolicyOnce()`; forwards the raw request to the export worker via
`applyGpuPolicyToChildEnvironment` (env, not argv — the worker parser is strict). Accessors in
`DebugOptions.h` (`gpuPolicyRequestRaw` / `gpuAdapterLuidRaw`). Illegal LUID never blocks startup →
falls back to `platform_default` AND leaves `ResolvedGpuPolicy::adapterLuid` **empty** (the requested
LUID stays only in `request.explicitLuid` for logging), so nothing binds a stale LUID; the provider
also guards `resolvedKind ∈ {AutoHighPerformance, AdapterLuid}` before binding
(`reason=policy_not_adapter_binding` on any fallback). **P4** `bindHighPerformanceQuickGraphicsDevice(window, label,
preferVideoShareDevice)` optionally binds the **root** Quick window to the resolved adapter via
`QQuickGraphicsDevice::fromAdapter` (Qt owns the device) — gated behind
`MIACODE_GPU_BIND_HIGH_PERFORMANCE` (**default ON**, `gpuBindHighPerformanceEnabled()`) and skipped
when the high-perf LUID equals the DXGI default adapter (`defaultAdapterLuid()` — single-GPU no-op).
The **preview composite** (`preferVideoShareDevice=true`) is never `fromAdapter`-bound: it uses the H2
`sharedPreviewQuickGraphicsDevice()` (default OFF) or keeps Qt's default adapter, because the D3D11VA
two-device keyed-mutex bridge is **same-adapter only** (a non-default render adapter breaks
video-background playback). Decisions log to `startup/gpu_provider`; verify the actual bound adapter
via `quick_shell/device`.

Current default: root-window binding is ON; set `MIACODE_GPU_BIND_HIGH_PERFORMANCE=0` / `off` /
`false` to keep Qt's platform-default adapter.

**Build-time (CMake, not env):** `MIACODE_USE_QTAVPLAYER` (see above), `MIACODE_BUILD_DEV_TOOLS`
(configure option gating dev-tool spec executables + CTest registration; appears in
`docs/ops/DEBUG_INDEX.md` only because the `debug_flag_index_spec` guard greps every `MIACODE_*`
literal in `src/`, including comments).

## 4. Runtime trace tags (Runtime channel `scope` values)

Stable tags include: `window/focus`, `app_shutdown`, `close_timing/*`, `preview/quick_runtime`,
`preview/quick_scene`, `preview/interaction`, `preview/frame_pacing`, `timeline/interaction`,
`timeline/bridge`, `timeline/quick_scene`, `timeline/cursor_map`, and `edit/*_perf` editor
performance tags. **GPU/startup diagnostics (P0–P3, all `--debug`-gated):** `startup/process_identity`
(pid/ppid/real-exe/packaged-app-dir/launcher-parent/cwd/argv/role — answers "which exe did the user
run"), `startup/gpu_hint` (Windows; confirms the NvOptimus/AMD PowerXpress export symbols compiled
in), `startup/gpu_policy` (resolved GPU device policy request→adapter LUID + fallback reason). ⚠ These
three are a bundle emitted by `logProcessStartupDiagnostics(phase)` (`process_identity.cpp`): once
early in `main()` (`phase=boot`, lands in the app-local `logs/` **before** a chart binds) and again
after the runtime log dir rebinds to a chart's `.miacode/logs/` (`phase=log_dir_rebound`, from both
`MainWindow.PreviewTimelineFlow` chart-open and the export worker) — so a collected per-chart log is
self-contained instead of missing the pre-bind boot lines. Also:
`startup/gpu_provider` (P4 root/composite device-bind decision: `action=bound|skip source=… reason=…`),
`quick_shell/device` (actual RHI adapter Qt Quick bound per surface — D3D11 DXGI desc or GL renderer
string; scheduled render-thread probe in `gpu_adapter_probe.cpp`), `quick_shell/topology` (frontend /
hidden-MainWindow / stage-media-route / window counts). Export channel gains `export_gl_renderer`
(GL vendor/renderer/version at `PreviewQuickExportSession::initialize`) and the `render_backend`
summary now carries `render_backend=opengl_qquick_rendercontrol rhi_api=OpenGL adapter_or_renderer=…
readback_mode=…`. The old `media_backend adapter="…"` field was renamed `probe_adapter` (+
`probe_adapter_source=dxgi_enum0_heuristic`) to stop it being read as the Qt RHI device. High-frequency tags (`timeline/bridge` scroll pushes, `timeline/quick_scene`
scroll-only paints) require `MIACODE_TIMELINE_HOTPATH_DIAG=1` even in debug mode.

Leak/resource gauges (beta4→beta7, both **once per user pause**, never per-frame, `--debug`-gated; the gauge
APIs `MemoryStageScope` / `leak_gauge::*` / `processPrivateBytes` / `processResourceGaugePayload` now live in
`src/common/ProcessDiagnostics.h`, namespace `miacode::diag`):
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
