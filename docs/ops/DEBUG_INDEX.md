# Debug Index

This document is the current user-facing index for MiaCode debug mode, log files, and preview/export diagnostics after the Qt Quick migration.

> Reconciled against the code on 2026-08-10: **86 live `MIACODE_*` environment flags across 26 files**, including the four the idle-freeze diagnostics added — `MIACODE_UI_HANG_ACTIVE_PHASE_MS`, `MIACODE_UI_HANG_IDLE_HEARTBEAT_MS`, `MIACODE_ENABLE_DIAG_D3D11`, `MIACODE_ENABLE_DIAG_MODULE_LIST`.
>
> Recount rather than trusting that number: `grep -rhoE '"MIACODE_[A-Z0-9_]+"' src --include="*.cpp" --include="*.h" | sort -u` yields 101 quoted literals, of which 86 are live env flags — subtract `MIACODE_SOURCE_ROOT` (a CMake compile definition) and the fourteen retired flags that survive only inside `kRetiredFlags` in `src/tools/debug_index/DebugFlagIndexSpec.cpp`. The drift guard `ctest -R debug_flag_index_spec` is the real enforcement — it fails if a flag read in `src/` is missing from this doc or a flag named here is no longer read. Its own summary reports a **larger** total (96) because its regex also counts the build-time compile definitions and hang-watchdog macros listed under "Other `MIACODE_*` tokens" below; that number is not the env-flag count.
>
> When you add/remove a flag, update this index (and `.codex/skills/miacode-dev-guide/references/debug-flags.md`).

## Debug Entry Points

- Preferred CLI switch: `--debug`
- Windows release helper:
  - `Start_MiaCode_Debug.bat`
- macOS release helper:
  - Double-click `Start_MiaCode_Debug.command` at the package root. It creates
    `<package-root>/logs/`, sets `MIACODE_LOG_DIR` to that absolute path, and
    launches `MiaCode.app` with `--debug`.
- Windows focused diagnostic helpers retained in the public repo:
  - `Start_MiaCode_SoftwareVideoDecode.bat`
  - `Start_MiaCode_QtPluginDiag.bat`
  - `Start_MiaCode_IdleFreezeRepro.ps1` — launches the real `app\MiaCode.exe` in a timestamped evidence directory with an explicit `GpuBound` / `GpuOff` profile; see [Windows 空闲冻结复现与取证指南](WINDOWS_IDLE_FREEZE_REPRO_ZH.md)

Inside debug mode, runtime, audio, export, startup-timing, preview-profile, and PV-memory outputs are enabled unless they are individually disabled.
Outside debug mode, the export log still keeps a concise stage/failure summary so users can report export issues without reproducing under `--debug`.

## Default Log Files

Default directory order:

1. channel-specific override path
2. `MIACODE_LOG_DIR`
3. project-local `.miacode/logs/` once a chart file is bound
4. app-local `logs/` next to `MiaCode.exe` while `--debug` is active
5. system temp directory

The macOS package-root `Start_MiaCode_Debug.command` explicitly selects
`<package-root>/logs/` through `MIACODE_LOG_DIR`, so its debug logs stay beside
the release package rather than following the usual fallback order.

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
- PV memory: `miacode_pv_memory_debug.log`

Crash breadcrumb path note:

- `miacode_startup_beacon_<pid>.txt` and `miacode_op_chain_<pid>.log` are resolved at process startup from `MIACODE_OPLOG_SHADOW_PATH`, then `MIACODE_LOG_DIR`, then `%TEMP%`.
- Binding a chart-local `.miacode/logs/` directory later does not rebind those already-captured crash breadcrumb paths yet.
- When a project log dir is bound while runtime debug output or `MIACODE_PREVIEW_HUD_PAINT_DIAG=1` is active, the runtime log writes `logging/crash_breadcrumb_hint` with the current PID plus `startup_beacon_hint` and `op_chain_hint`.
- Longer-term work should rebind or mirror the crash shadow safely; the current line is a path signpost so investigations do not only check the chart log directory.

Path overrides:

- `MIACODE_RUNTIME_LOG_PATH`
- `MIACODE_AUDIO_LOG_PATH`
- `MIACODE_EXPORT_LOG_PATH`
- `MIACODE_STARTUP_LOG_PATH`
- `MIACODE_FATAL_LOG_PATH`
- `MIACODE_PREVIEW_PROFILE_PATH`
- `MIACODE_PV_MEMORY_LOG_PATH`
- `MIACODE_OPERATION_LOG_PATH` (operation-breadcrumb log)
- `MIACODE_OPLOG_SHADOW_PATH` (operation-log shadow copy)

Other logging knobs:

- `MIACODE_SKIP_ASYNCLOG_FLUSH` — skip the async-log flush on shutdown (diagnostics only).
- `MIACODE_SKIP_CRASH_HANDLER` — skip `crash_recovery::install()` (SIGSEGV / SEH / terminate hooks). Use with Windows LocalDumps so an access violation stays `0xC0000005` and writes a dump; MinGW's SIGSEGV handler otherwise ends the process without WER.

## PV Memory Diagnostic

Launch the app with `--debug` and inspect the dedicated
`miacode_pv_memory_debug.log` file. Set `MIACODE_PV_MEMORY_LOG_PATH` to place
that file at an explicit path. The channel resets at debug-session startup and
writes `action=session_start`, even when no chart has a PV.

The channel records only current-process memory. `process_resident_bytes`,
`process_footprint_bytes`, `process_internal_bytes`, and
`process_compressed_bytes` are byte counts; `-1` means that the platform could
not provide that value. While a video PV is active it emits one periodic
`action=sample` at most every five seconds, alongside lifecycle boundaries
such as source load, first frame, clear, and shutdown. `to_image_*` values
describe the existing preview image conversion; in particular,
`to_image_output_bytes_estimate` is an accumulated output-size estimate, not
allocator ownership or proof that those bytes remain live.

This is an internal diagnostic. It does not enumerate or sample
`VTDecoderXPCService` or any other external process, log PV filenames or frame
content, add a frame mapping/readback, or add PV-memory records to the runtime
or audio logs.

Use the records to rank a capture in this order:

1. Investigate existing per-frame `toImage()` churn when successful conversions
   and current-process footprint rise together.
2. If conversion activity is low or absent, investigate `QMediaPlayer` or
   output-sink lifetime when memory grows during playback or has not settled by
   `post_clear_15s` after `no_media`.
3. Compare output attach/detach boundaries for VideoOutput or scene-graph
   last-frame retention when the remaining playback counters are stable.
4. Consider persistent Quick graphics resources only when a preview surface is
   hidden/shown or remains retained after stop; it is not the leading
   explanation for steady-playback growth.

C++ hang-watchdog instrumentation macros, not environment variables:

- `MIACODE_HANG_PHASE`
- `MIACODE_HANG_JOIN`
- `MIACODE_HANG_JOIN_IMPL`

Freeze / contention diagnostics (debug mode only; the probes below are on by default in debug mode, but four flags tune or gate them — `MIACODE_UI_HANG_ACTIVE_PHASE_MS`, `MIACODE_UI_HANG_IDLE_HEARTBEAT_MS`, `MIACODE_ENABLE_DIAG_D3D11`, `MIACODE_ENABLE_DIAG_MODULE_LIST`):

- `startup/process_identity` records the product `version`, build `git_revision`, `git_dirty`, and real executable identity so transient packages can be mapped precisely.
- `ui/hang_watchdog` samples the GUI heartbeat every `250 ms`; it reports a marked phase after about `2 s` or an unmarked idle heartbeat stall after about `5 s` using a durable fatal-grade runtime record. Both timeouts are overridable per run — see `MIACODE_UI_HANG_ACTIVE_PHASE_MS` / `MIACODE_UI_HANG_IDLE_HEARTBEAT_MS`; the defaults are too coarse for a stall of only a couple of seconds. The watchdog deliberately performs no GUI-thread handle duplication, suspension, symbol initialization, stack walking, or process termination.
- **If `miacode_durable_fallback_<pid>.log` exists, read it next to the runtime log.** Fatal-grade lines take a durable synchronous path that holds the log mutex across an open/write/flush/close; that wait is bounded at `2 s`, and a line that cannot get the lock in time is written to this per-process file instead. The file existing means some thread held the mutex longer than that — a stalled disk or a disconnected network share — and that those lines are **missing from the normal log**. It is a separate file on purpose: a writer that just failed to acquire the mutex must not append to the file the mutex guards. Without the bound, the hang watchdog reporting a frozen GUI thread would block on its own first line, so the freeze report never landed at all.
- **A GUI stall shorter than the hang thresholds still gets a row.** `ui/hang_watchdog` `action=gui_thread_stall` opens with `edge=began` as soon as the heartbeat is `1000 ms` stale and closes with `edge=ended stall_ms=` when the GUI thread comes back, correlated by `episode=`. This exists because the hang thresholds leave a dead band: a stall between `2 s` (active-phase, and only inside a *marked* phase — ordinary playback marks none) and `5 s` (idle heartbeat) produced **no watchdog rows at all**, which is exactly where the observed audio-device-switch freezes landed (`2075 ms` and `4683 ms` in one capture). `stall_ms` is measured between two GUI-thread heartbeat timestamps, so it is the time the GUI thread actually went unserviced, not the `500 ms`-quantised window the watchdog noticed it in. Info level, never carries a stack — `edge=began` is written immediately so the fact survives a kill mid-stall. The threshold is a fixed constant, not an env flag. A stall that keeps going past the hang thresholds produces these rows *and* the `gui_thread_stale` ones.
- `idle/resource_gauge` writes sample `0` immediately and then process counters about every `30 s`.
- `idle/vram_gauge` runs on the same `30 s` tick: one `action=adapter_scan` line with `adapter_count`, then one `action=sample` line per DXGI adapter with `local_budget_mb` / `local_usage_mb` / `nonlocal_*` and the derived `local_over_budget` / `nonlocal_over_budget`. Per-adapter because MiaCode straddles two GPUs on the reported hardware (root window on the high-performance adapter, preview composite on the default one) and `over_budget=1` is the DXGI-eviction cliff that explains order-of-magnitude playback collapse. Empty on non-Windows; the scan line is still written so "probe ran, no adapters" is distinguishable from "diagnostics absent".
- `window/visibility` records occlusion / minimize / expose transitions for the default QSG path — `surface=root_window|preview_composite`, `from` / `to` visibility, `exposed`, `minimized`, `occluded`, and `occluded_for_ms` on the transition back out. Transition-triggered, never per frame. `action=graphics_persistence` states once that the preview composite surface keeps `persistent_graphics` / `persistent_scene_graph`, i.e. minimizing does **not** release its GPU resources.
- Audio channel `bass_audio_health` / `bass_audio_stall` cover preview audio underruns — see the audio-log notes below.

Other `MIACODE_*` tokens seen by the source-index guard but not runtime flags:

- `MIACODE_GIT_REVISION` / `MIACODE_GIT_DIRTY` — CMake-generated build identity macros embedded in `startup/process_identity`.
- `MIACODE_HAS_BASS_AUDIO` — build-time compile definition for BASS support.
- `MIACODE_EXTENSION_DEV_PATHS` — test-only environment literal used by `ExtensionManifestSpec`; production extension discovery does not read it.

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
- **GUI RHI backend:** the desktop GUI no longer forces OpenGL. It uses `--rhi=<name>`, the persisted choice, or Qt's platform default (Direct3D11 on Windows / Qt 6). See the `startup/graphics_backend` and `quick_shell/device` runtime logs for the applied backend + the actual bound adapter.
- **CLI export / export worker:** `--export-video` and `--export-video-worker` default Qt Quick to Direct3D11 and drive the D3D11/QRhi render-control session. `MIACODE_EXPORT_RENDER_BACKEND=opengl` keeps the stable offscreen `QQuickRenderControl` + FBO/PBO path as an explicit rollback. The export log's `render_backend` line reports `render_backend=d3d11_qrhi_rendercontrol` or `render_backend=opengl_qquick_rendercontrol`, the active RHI, adapter/renderer, and readback mode.
- **Windows dist package:** the clickable `MiaCode.exe` at the package root is only the launcher; the real GUI/export worker is `app\MiaCode.exe`. Windows Graphics Settings and the NVIDIA/AMD control panels must target `app\MiaCode.exe`, not the root launcher. The `startup/process_identity` runtime log spells out the product version, source revision/dirty state, real exe path, whether it is running from the packaged `app\` dir, and the launcher parent process.
- **High-performance GPU hint (P2):** `MiaCode.exe` and `MiaCodeLauncher.exe` export the `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` symbols so hybrid-graphics laptops prefer the discrete GPU. This is a process-level *preference*, not a precise adapter binding — Windows Graphics Settings / vendor control panels can still override it. Confirmed by the `startup/gpu_hint` runtime log.
- **Root Quick GPU binding:** root-window `fromAdapter` binding is now default ON. Set `MIACODE_GPU_BIND_HIGH_PERFORMANCE=0` / `off` / `false` to keep Qt's platform-default adapter as a rollback. The preview composite remains never `fromAdapter`-bound.
- Export PBO diagnostics now describe the headless Quick export session, not a removed legacy renderer.
- Background **PV/BG video decoding** in realtime preview (and therefore the export preview dialog) uses **QtAVPlayer (FFmpeg)** on Windows and macOS instead of Qt Multimedia's `QMediaPlayer`. This is selected at **build time** by the `MIACODE_USE_QTAVPLAYER` compile macro — a CMake build-time macro, **not** an environment flag, so it cannot be toggled at runtime. Windows uses D3D11VA; macOS uses VideoToolbox with QtAVPlayer's Metal bridge; other platforms keep the `QMediaPlayer` path. `MIACODE_FFMPEG_DEV_DIR` is the separate CMake/package SDK-root input (Windows is provisioned by `scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1`; macOS packaging resolves an explicit SDK or Homebrew `ffmpeg@6`/`ffmpeg`). `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=1` is an explicit fallback on both platforms, but hardware decoding is the default. The export *encoder output* is unaffected (still the standalone `ffmpeg` filtergraph). On `InvalidMedia` the preview backend retries once forcing FFmpeg software decode (`preview/stage_media action=video_software_fallback`).
- `MIACODE_BUILD_DEV_TOOLS` is the **CMake configure option** (not an environment flag) that builds the developer spec/eval executables and registers them with CTest (`cmake -D MIACODE_BUILD_DEV_TOOLS=ON`; `scripts/build/build-win.ps1` turns it on for Debug configs). Some dev tools mention it in comments (e.g. `src/tools/latency/LatencyBatchTest.cpp`), which is why it appears in this index — it has no runtime effect in a shipped build.
- `MIACODE_SIMAI_REPRO_CHART` is a **CTest/dev-tool-only** parser-spec repro hook. When set to a `maidata.txt` path containing `&inote_5=`, `simai_parser_spec` parses that real chart as an optional local corpus check; unset keeps the public spec deterministic. It has no runtime effect in `MiaCode.exe`.

Relevant export backend toggles:

- `MIACODE_EXPORT_ENABLE_GPU_RENDER`
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`
- Default export path keeps GPU offscreen render enabled and now uses D3D11 QRhi synchronous staging-map readback; the OpenGL rollback path still requests PBO readback unless `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1`.
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
- `render_backend` export-log entries report the active backend and readback mode; D3D11 QRhi logs `pboRequested=0 pboEnabled=0` because it has no PBO path, while the OpenGL rollback path reports PBO capability state.
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
- `MIACODE_EXPORT_PREMULTIPLIED_PIPE=0` — restore straight-RGBA CPU conversion for Fast D3D11 export. Unset/default keeps premultiplied RGBA and uses FFmpeg `overlay alpha=premultiplied`; HighQuality and OpenGL remain straight RGBA.

Render-session backend (P5 — hidden, diagnostic):

- `MIACODE_EXPORT_RENDER_BACKEND` — `d3d11_qrhi` (default) | `opengl` | `auto`. Selects the offscreen chart-render session for CLI export and the export worker. The default `d3d11_qrhi` path puts the export process on the Direct3D11 Qt Quick graphics API and drives `PreviewQuickD3D11ExportSession` (device created on the P3-policy adapter via `QQuickGraphicsDevice::fromDeviceAndContext`, `QQuickRenderTarget::fromD3D11Texture` R8G8B8A8 target, three-texture staging-ring CopyResource+Map readback). Init failure auto-falls back to the OpenGL session (`render_backend_fallback` export-log line: `fallback_from=d3d11_qrhi fallback_to=opengl reason=…`). `opengl` is the explicit rollback path. `auto` is currently identical to `d3d11_qrhi`. Windows-only; unknown values keep OpenGL. Selected backend, adapter LUID and readback mode appear in the `render_backend` export-log summary line; `render_stage_timing_summary` reports state/polish/sync/submit averages.

## GPU Device Policy (P3/P4 — hidden, diagnostic)

Internal high-performance GPU device-policy skeleton (`src/common/GpuDevicePolicy.*`, namespace `miacode::gpu`; provider `src/app/gpu_device_provider.cpp`). These are **hidden developer/support overrides**, not surfaced in the settings UI. **P3** **resolves + logs** the preferred adapter (`startup/gpu_policy` runtime log, gated on `--debug`); adapter enumeration is read-only DXGI (`IDXGIFactory6::EnumAdapterByGpuPreference` high-performance ordering, software adapters skipped) — no device creation. The GUI forwards the raw request to the export worker via env so both processes log a matching policy. **P4** can then actually **bind** the root Quick window to the resolved adapter via `QQuickGraphicsDevice::fromAdapter` (Qt owns the device) — logged at `startup/gpu_provider`, verified by the `quick_shell/device` actual-adapter probe.

- `MIACODE_GPU_POLICY` — `auto_high_performance` (default) | `platform_default` | `software`. Selects the internal device policy. The `--gpu-policy=<value>` CLI flag takes precedence over this env var. `auto_high_performance` resolves to the first DXGI high-performance hardware adapter's LUID; on failure it falls back to `platform_default` and logs the reason.
- `MIACODE_GPU_ADAPTER_LUID` — `<high>:<low>` (each hex `0x…` or decimal). Forces the policy to `adapter_luid` and A/B-selects a specific DXGI adapter by LUID. The `--gpu-adapter-luid=<high>:<low>` CLI flag takes precedence. An unknown / illegal LUID never blocks startup — the policy falls back to `platform_default` and logs `fallback_reason=adapter_luid_not_found_among_hw_adapters`.
- `MIACODE_GPU_BIND_HIGH_PERFORMANCE` — **P4 master gate, default ON.** Binds the **root** Quick window to the resolved high-performance adapter (`fromAdapter`) when the RHI is D3D11, the policy yields a hardware LUID, and it **differs** from the default adapter (else redundant → skipped). Set `MIACODE_GPU_BIND_HIGH_PERFORMANCE=0` / `off` / `false` to keep Qt's default adapter. The **preview composite** (video surface) is never `fromAdapter`-bound — it keeps Qt's default adapter (or the H2 `MIACODE_PREVIEW_SINGLE_D3D11_DEVICE` path) to preserve the QtAVPlayer D3D11VA same-adapter video bridge.

Related CLI flags (same resolver, no env literal): `--gpu-policy=<value>`, `--gpu-adapter-luid=<high>:<low>`.

## Preview-Side Notes

Still active:

- `MIACODE_PREVIEW_SFX_DIR`
- `MIACODE_TRACK_PATH`
- `MIACODE_BASS_BGM_RATE_MODE` (Windows/macOS BASS preview BGM; unset defaults to pitch-preserving BASS_FX `tempo`, while `rate_transpose` / `transpose` / `source_time` / `accurate` switches to source-time-priority rate transpose for A/B listening)
- `MIACODE_BASS_BGM_TEMPO_PRESET` (Windows/macOS BASS preview BGM, only when tempo mode is active; BASS_FX window presets: unset = `compact40`, `stock` = plugin default, `auto` = `0/0/8`, `tight20` = `20/8/4`, `balanced30` = `30/10/6`, `compact40` = `40/15/8`, `smooth60` = `60/20/8`, `wide82` = `82/28/8`)
- `MIACODE_BASS_BGM_TEMPO_PARAMS` (Windows/macOS BASS preview BGM, only when tempo mode is active; overrides preset with custom `sequence_ms,seek_ms,overlap_ms`, accepting comma, slash, semicolon, pipe, `x`, or spaces as separators)
- `MIACODE_PREVIEW_FRAME_PACING_DIAG` (`1` enables low-noise `preview/frame_pacing` diagnostics. Combined with `--debug`, it also enables the first-PV-play two-device bridge trace and its per-frame timing counters.)
- `MIACODE_PREVIEW_HUD_PAINT_DIAG` (`1` enables focused `PreviewQuickHudLayer` / HUD painter crash breadcrumbs in the runtime log. It force-writes `preview/hud_state` GUI-thread HUD mutations and `preview/hud_paint` render-thread paint stages, including a flushed `draw_text_before` line before each HUD `QPainter::drawText` call. It also allows the project-log binding `logging/crash_breadcrumb_hint` signpost to be written without global `--debug`.)
- `MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS`
- `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG` (requires `--debug`; adds focused waveform/BGM alignment evidence in the audio log, raises BASS `bass_status` cadence for short-lived 1x offset repros, and emits runtime `timeline/render_map` rows from the final Quick timeline scene state)
- `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS` (default `250`; BASS `bass_status` interval while waveform-alignment diagnostics are enabled)
- `MIACODE_UI_HANG_ACTIVE_PHASE_MS` (default `2000`; how long a marked GUI phase may stay active before `ui/hang_watchdog` reports. Read once when the watchdog installs and echoed in `action=installed`, so a capture always states the thresholds that produced it. Non-positive or unparsable falls back to the default.)
- `MIACODE_UI_HANG_IDLE_HEARTBEAT_MS` (default `5000`; how long the GUI heartbeat may stop, with no phase marked, before `ui/hang_watchdog` reports. Lower this to promote a short stall to a fatal-grade watchdog record. Since `action=gui_thread_stall` landed, a sub-threshold stall is always logged with its duration, so this no longer decides whether a short freeze is *visible*.)
- `MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES`
- `MIACODE_PREVIEW_REJECT_NEGATIVE_HS` (default off; opt-OUT escape hatch — negative HS `<HS*-N>` is ON by default. `1` restores the strict stance where the parser rejects `hs <= 0` (Q7). Read once at app boot into `SimaiNativeParser::setAllowNegativeHsEnabled`. By default negative HS parses and the preview/export renderer flies tap / star / each-line notes inward from OUTSIDE the judgement ring (hold / touch / slide take the HS magnitude, never reverse); zero is always rejected. Inherited by the CLI export-worker subprocess so preview and export agree. Off-canvas reverse spawn relies on the playfield clip..)
- `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO` (`1` forces QtAVPlayer to decode preview video in software instead of the selected platform hardware decoder — D3D11VA on Windows, VideoToolbox on macOS. It remains a diagnostic/workaround for the Windows green NV12-padding artifact on videos whose dimensions aren't 16-aligned, e.g. 300×300; hardware textures are allocated at the macroblock-aligned coded size and the uninitialized padding samples as pure green.)
- `MIACODE_PREVIEW_SINGLE_D3D11_DEVICE` (default off; `1` enables H2 single-device preview decode — share ONE video-capable, multithread-protected `ID3D11Device` between the QtAVPlayer/FFmpeg D3D11VA decoder and the preview `QQuickView`'s QRhi, eliminating the per-frame cross-device keyed-mutex texture bridge and its render-thread `AcquireSync(INFINITE)` freeze that garbles/stalls preview on Intel/Arc iGPUs. Experimental, needs GUI acceptance on an affected iGPU; every step falls back to the legacy two-device bridge on failure, so the worst case is "no change". Confirm via the `media_backend … single_device=1` log line.)
- `MIACODE_PREVIEW_DUMP_HWFRAMES` (default `0`=off; requires `--debug`. `N` = read back + stat-classify the first N D3D11VA decoder NV12 surfaces per "arm" (an arm = playback start; re-armed on each seek) to localize the hardware-decode green/garble bug. Render-thread bounded readback: Maps a STAGING copy of the decoder DPB slot (single-device path) or the bridge output (two-device path), computes NV12-domain stats (`y_mean`, `y_rowdelta` garbage proxy, `uv_zero`/`uv_neutral`, interior-vs-edge zeroed-chroma = green) + coded-vs-display size, and emits ONE `preview/hwframe` line per readback with a verdict `hint`. **No image files are written** — the stat line is the decisive output. Zero cost when `0` (single relaxed-atomic load+compare per frame, no env read on the hot path). ⚠ Repro precondition: integrated GPUs default to software decode, so to exercise the buggy hardware path launch with `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=0` [+ `MIACODE_PREVIEW_SINGLE_D3D11_DEVICE=1` for the H2 path].. The decode-path summary lands in `preview/hwdecode_summary`, seek catch-up in `preview/seek_landing`.)
- `MIACODE_PREVIEW_D3D11_DEBUG_LAYER` (default off; requires `--debug`. `1` adds `D3D11_CREATE_DEVICE_DEBUG` to the H2 shared device (`PreviewSharedD3D11Device::createSharedDevice`) so the D3D11 debug layer's typed-SRV / NV12 PlaneSlice warnings (the decisive H-FMT signal) and resource-not-ready / device-removed errors (H-DEC) are pumped into `preview/hwframe_d3d11dbg` on the low-frequency summary cadence. Falls back to no-debug creation if the SDK debug layer isn't installed. Only the *imported* H2 device needs this; for the legacy two-device path set the Qt env `QSG_RHI_DEBUG_LAYER=1` instead, since that device is created by Qt's QRhi.)
- `MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT` (default **off** — RESERVED / experimental; **not** `--debug`-gated. Was the §10 "completion-order" fix attempt: before the copy reads the DPB slot, `qavhwdevice_d3d11.cpp` (`copyToShared` two-device + `copyTextureSameDevice` single-device) issues an `ID3D11Query(EVENT)` + `Flush` + bounded ~100ms spin to force the decode GPU work to complete. ⚠ It did **NOT** resolve the post-seek green on the user's Intel Arc 130T (A/B verified), so it is now **off by default** — the user-facing fix is the 硬件渲染/软件渲染 preference (default hardware; switch to software on an affected iGPU, hot-switchable, no restart). The mechanism is kept as env-gated reserved code (a legitimate D3D11 sync that may help on other hardware): set `MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT=1` to re-enable. Active state shows in `preview/hwdecode_summary … completion_waits=` / `preview/hwframe … completion_wait=`. Published via `qavSetPreviewHwDecodeFixConfig`.)
- `MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT` (default off; **not** `--debug`-gated. **Safety net** (secondary) for the same green/garble bug: when on, the D3D11VA copy path drops any decoded frame FFmpeg flagged corrupt/decode-error (`AV_FRAME_FLAG_CORRUPT` or `AVFrame::decode_error_flags != 0`) instead of sampling it, so the RHI holds the previous good frame (a brief freeze beats a green flash). Free — reads existing AVFrame flags, no GPU readback. Default off because it only helps if the driver/decoder actually reports those flags for the green frames (unverified) and because dropping on benign concealment flags could over-discard. First confirm via the `preview/hwframe … decode_err= corrupt=` dump fields and the `preview/hwdecode_summary frames_decode_error=` counter that FFmpeg flags the green frames, then set `MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT=1`. Published via `qavSetPreviewHwDecodeFixConfig`; drops counted in `preview/hwdecode_summary corrupt_dropped=`.)
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
- `MIACODE_SKIP_DIAG_D3D11` (skipped the startup D3D11 diagnostic probe back when that probe ran by default. The probe is now opt-in behind `MIACODE_ENABLE_DIAG_D3D11`, which made the skip unreachable unless an operator enabled and disabled the same probe in one run, so the enable flag is the single control.)
- The DirectComposition / D3D11 render backend flags, all retired together on 2026-08-07 when `src/render/` and `src/sources/` were deleted. Every one of them cascaded off `MIACODE_PREVIEW_USE_DCOMP`, which had defaulted to off since beta34, so removing the backend made all six unreachable. The in-process QSG path is now the only preview and timeline renderer:
  - `MIACODE_PREVIEW_USE_DCOMP` (parent opt-in; created the DComp-backed chart preview popup)
  - `MIACODE_TIMELINE_USE_DCOMP` (DComp pipeline for the timeline pane, via `TimelineRenderView`)
  - `MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND` (hosted DComp in an owned top-level HWND)
  - `MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA` (per-pixel-alpha `WS_EX_NOREDIRECTIONBITMAP` popup)
  - `MIACODE_PREVIEW_DCOMP_EXCLUSIVE` (made DComp the sole chart renderer)
  - `MIACODE_PREVIEW_DCOMP_QUIESCE_QSG` (skipped QSG repaint subscriptions while DComp was active)
- `MIACODE_PREVIEW_VISUAL_SMOOTHING` (gated visual-clock smoothing, which bounded the per-frame visual playhead delta so BASS-master-mixer cursor jitter could not reach the rendered scene. The G1 wall-clock flip made `qtPreviewElapsed_` the master timeline — monotonic and rate-correct by construction — so `applyVisualClockSmoothing` was collapsed to exactly its old smoothing-disabled branch and the gate was dropped with it. The flag had been a no-op ever since. The lookahead-vsync shift that survives in that function was always outside this gate and has its own `MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS` control.)

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
  - `preview/frame_pacing` is off by default and turns on with `MIACODE_PREVIEW_FRAME_PACING_DIAG=1`; normal request/tick/present samples are capped by `MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS` while watchdog, fixed timer present-miss/hard-resync, and large-step events log immediately. With `--debug`, the same flag also enables `preview/first_playback_bridge_trace` plus its per-frame two-device bridge timing counters; without both switches those counters are off.
  - `preview/quick_scene action=render_frame_profile` (needs `MIACODE_PREVIEW_FRAME_PACING_DIAG=1`) splits `render_submit_ms` into `resource_prep_ms` (beforeRendering → beforeRenderPassRecording: QRhi texture/buffer uploads, resource creation, swap-chain image acquire), `pass_record_ms` (the render pass itself — where a first-use graphics pipeline / shader variant is built by the driver) and `submit_tail_ms`. Before this split the first-play stall was a single opaque `render_submit_ms`, which is why `docs/audit/PREVIEW_FIRST_PLAY_RENDER_STALL_HANDOFF_AUDIT_ZH.md` §5.1 could not name the blocking phase. `na` means the frame did not emit that signal.
  - `preview/quick_scene action=render_stall_context` is written once per scene-graph lifetime for the first frame whose `render_submit_ms >= 250`; `dominant=resource_prep|pass_record|submit_tail` is the verdict, so a capture that lost its sampled profile lines still carries it. Render-thread only — no DXGI work happens there.
  - `preview/quick_scene action=layer_first_draw` (same flag) records the FIRST frame in which each preview layer produced a node — i.e. the frame that compiles that layer's material pipeline and uploads its textures. It is emitted from `updatePaintNode`, so it lands immediately before that same frame's `render_frame_profile`: a `layer_first_draw` directly ahead of a multi-hundred-millisecond `pass_record_ms` names the layer whose first use paid for the stall (audit §5.4).
  - `preview/first_playback_bridge_trace action=render_stall_vram` follows `action=render_stall` with one `formatAdapterVideoMemoryPayload` row per DXGI adapter, so the stall sample carries this process's VRAM budget/usage (`local_over_budget=1` is the alarm). GUI thread, once per trace.
  - `quick_shell/device` now also carries `umd_driver_version=` (the graphics user-mode driver build, via `IDXGIAdapter::CheckInterfaceSupport`) and `feature_level=`. Nothing else in the log names the driver build, which is the first question asked of any first-submit stall report.
  - `preview/stage_media action=end_of_media_classified` is written for every backend `EndOfMedia`. `eom_class=natural` means the decoder reached the media's own duration (a legitimately short PV — keep the last frame, never touch the main transport); `eom_class=stale` means EndOfMedia arrived with substantial media left, the audit §5.2 signature (121 s clip, EndOfMedia at 1.4 s, last pts 1.267 s); `unknown` means duration or decode progress was unavailable and nothing is done. The `demux_eof_*` fields carry the FFmpeg-side provenance: `demux_eof_averror` counts ends that came from `AVERROR_EOF`, `demux_eof_avio` counts ends that came only from `avio_feof()` — which FFmpeg also latches after a failed byte read — and `demux_last_avio_error` is `AVIOContext::error` at that moment. A stale end with `demux_eof_avio` incremented and a non-zero `demux_last_avio_error` is an I/O failure wearing an end-of-stream costume.
  - `preview/stage_media action=stale_end_of_media_recover` / `…_resumed` / `…_resume_timeout` / `…_exhausted` trace the bounded recovery for a stale end: `strategy=seek_resume` for the first attempts (a seek clears FFmpeg's AVIO end-of-file latch, and the resume waits for the `seeked` acknowledgement because `QAVPlayer::play()` restarts from zero while that latch is set), `strategy=reload` for the last, then the PV keeps its last frame rather than looping. The main transport is never stopped on any path.
  - `preview/stage_media action=commit_prepared_playback` now also reports `frames_before_commit=` / `video_prewarmed=`, i.e. whether the video pipeline had already been exercised when the user pressed play or the renderer is being handed a new material on the frame being waited on (audit §5.4).
  - fixed timer high-resolution status events `fixed_timer_high_res_requested`, `fixed_timer_high_res_enabled`, `fixed_timer_high_res_failed`, and `fixed_timer_high_res_disabled` are written as sparse status lines so env propagation can be verified even when per-frame pacing diagnostics are off
  - `tick_sample` / `tick_large_step` include `fallback_second`, `audio_second`, `fallback_delta_ms`, `audio_delta_ms`, `visual_delta_ms`, `audio_minus_fallback_ms`, and `time_authority`
  - `preview/stage_media` now also emits low-noise `action=video_frame_stall_begin` / `action=video_frame_stall_end` transitions when external video stops delivering frames for longer than expected
  - `preview/waveform` records waveform cache/request/apply evidence, including source (`memory`, `disk`, `build`, `placeholder`), track id, file stamp, duration, top-level column count, milliseconds per column, first onset at `0.02` / `0.05` / `0.10`, and peak position; `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG=1` adds focused `preview/waveform_align` breadcrumbs, lowers `bass_status` interval via `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS`, and adds runtime `timeline/render_map` rows with visible range, scroll, primitive counts, waveform/grid/note first-visible coordinates, playhead/cursor/entry second-to-X roundtrips, the waveform column under the playhead, and the strongest waveform column near the playhead
  - `window/focus` records app-level focus transitions, activation edges, watched editor focus events, and text-focus restore attempts for focus-regression diagnosis
  - BASS `bass_status` sampling is now reduced to about once per second in debug mode, and includes `bgm_delta_ms` (`auth - bgm_chart`) so waveform/playhead complaints can be separated from generic fallback-clock drift; waveform-alignment diagnostics can temporarily lower that interval
  - BASS `bass_audio_health` is the underrun / buffer-level probe: it polls `BASS_ChannelIsActive` on the master mixer and `BASS_Mixer_ChannelIsActive` on the background source at tick rate, but writes its full line only about every `5 s` of playback time. Fields: `mixer_active` / `bgm_active` (`playing`, `stalled`, `paused`, …), `underrun`, `stall_count`, `stall_ms`, `buffered_ms` / `buffered_bytes` (playback-buffer fill from `BASS_DATA_AVAILABLE` — this is what collapses when BASS's update thread loses its CPU race), `config_buffer_ms`, `min_buffer_ms`, `update_period_ms`, `update_threads`, `device_freq`, and `init_latency_ms`. `init_latency_ms` reads `0` by design: `BASS_Init` is not given `BASS_DEVICE_LATENCY`. `bass_mmcss_registered_by_app=0` records the standing fact that no BASS thread is MMCSS-registered — the only registration in the default path is the QSG render thread — with `app_mmcss_task_class` showing what that render-thread registration got
  - BASS `bass_audio_stall` is written immediately on each underrun edge (`edge=begin` / `edge=end`) so an underrun shorter than the `bass_audio_health` sampling interval still leaves a trace; `stall_ms` on the end edge is the underrun length
  - `bass_sfx_voices op=stop reason=audio_device_change stopped=N` is written when the device-change auto-pause hard-stops the note-SFX one-shots. It follows `pause_exact` and appears on that path only — a manual pause deliberately lets one-shots ring out, so its absence after a normal `pause_exact` is correct, not a missing row.
  - `preview/playback action=pause_second_moved_back reason=… from=… to=… delta_ms=…` names the author of any BACKWARD move of the preview pause second while the preview is not playing. `reason` is the writing function. The coverage rule is that `qtPreviewPauseSecond_` is never assigned except through `shared::writePreviewPauseSecond`; a backward move goes unrecorded exactly to the extent that rule is broken, so verify it rather than assume it — `grep -rn "qtPreviewPauseSecond_[[:space:]]*=" src --include="*.cpp" --include="*.h" --include="*.inc" | grep -v writePreviewPauseSecond` should match only the declaration and the reference alias in `MainWindowMemberStorage.inc`. (It did not when the wrapper landed: `latency::LatencySandboxController::applyPlayheadToScene` still wrote the member directly, and its hot-apply re-anchor on BPM / offset / subdivision changes is precisely a backward-move source; it now routes through the wrapper as `reason=latency_sandbox_apply_playhead`.) Forward motion is not logged — `applyQtPreviewPosition` writes on every playback tick and would bury the channel. Built for the reported "timeline steps back some time AFTER a pause": leave the preview paused and idle for a while rather than resuming, and the row identifies the caller.
  - `preview/playback action=pause_audio_stall_observed wall_second=… audio_second=… stall_ms=…` quantifies how long the process stalled through an output-device switch: the wall clock is a `QElapsedTimer` and does not stall with the audio, so the two diverge by exactly the cost of the switch. **Measurement only — nothing acts on it.** The wall clock remains the pause second, because at a device switch the audio path is already broken and those milliseconds are lost rather than deferred. Absent when the switch cost nothing, which is the healthy case. `bass_transport op=pause_exact second=…` carries the audio-side value for comparison.
  - `preview/audio_device` (Audio channel, BASS platforms only) records an output-device configuration change: `change=output_list|default_output|output_list_and_default`, the before/after device counts, and the before/after default-output ids. Qt re-notifies on plain re-enumeration, so a row is written only when the sorted device list or the default id actually differs. A row while the preview is playing freezes GUI/video at its captured wall second before it enqueues the reserved worker pause, then emits `preview/playback action=pause_audio_device_change` and the normal `pause_exact` sequence. The matching completion is accepted only for the current playback generation, transaction, and first pause token; it stays paused until explicit Play. A row while already paused stands alone by design.
  - Windows hardware acceptance for repeated output-device switches and shutdown during an
    in-flight change remains outstanding when verification is performed from the macOS worktree.
  - BASS `bass_sfx_scheduler action=anchor` records a chart/decode-position anchor.
  - BASS `bass_sfx_scheduler action=disarm` is retained unless it proves a no-op
    (`was_active=0 had_sync=0 group_idx=-1`).
  - Ordinary `preview/stage_media action=playback_rate` rows are edge-triggered by `(rate, media
    kind)` and reset on a chart/media change. `playback_rate_deferred`, `playback_rate_flushed`, and
    error records remain independent evidence.
  - **Note-SFX emission is fully accounted for by four rows, and every sound belongs to exactly one of them.** `bass_sfx_mixer_trigger group_idx=… group_second=… count=… started_bgm=… played=…` — the master-mixer sync path, which is what a LIVE session runs on; written after the scheduler mutex is released because the callback is on the BASS mixer thread. `bass_sfx_drain at_chart=… drained=… first_idx=… last_idx=… played=…` — the GUI fallback path. `bass_sfx_audition kind=… gain=… started=…` — `audition()`, which emits a real note sound while bypassing the scheduler, the group cursor, and both group rows (settings-dialog previews, the 片头 `track_start` jingle, the count-in `clock`). `bass_sfx_touchhold action=start|stop owner=… second=…` — the shared touch-hold voice, which starts through neither group path. The `played=` field is the list of samples that ACTUALLY started (`kind:gain`, `(none)` if a group resolved to silence): the group rows record a decision to trigger, `played=` records the sound, and the two are not the same thing. Reading only the group rows will make an audition or a touch-hold look like "nothing played".
  - BASS BGM rate-mode decisions log as low-frequency `bgm_speed_mode` / `bgm_rate_mode` rows, not per tick; BASS_FX tempo-window experiments additionally log `bgm_tempo_window` with requested and read-back `sequence_ms`, `seek_ms`, and `overlap_ms`
  - `preview/interaction` correlates user-facing preview actions such as `play`, `pause`, `stop`, and `ctrl+click` seek
  - `ui/hang_watchdog` is installed only when runtime debug output is active; if a marked GUI phase remains active for about `2s`, or the GUI heartbeat stops for about `5s` while no phase is marked, it writes a fatal-grade runtime line with `trigger=active_phase|idle_heartbeat`, flushes the op-chain shadow, and includes async-log writer stats. Repeated `action=report_gate will_report=0` rows are summarized on the next stale row (or a trigger-clear/rearm/shutdown end row) with `suppressed_count` and `episode_id`; no evidence is silently discarded. On Windows it then captures the GUI thread's own call stack into `ui/hang_watchdog_stack` (see the freeze-diagnostics list above)
  - `window/visibility` records default-QSG-path occlusion / minimize / expose transitions for the root window and the preview composite surface, plus a one-shot `action=graphics_persistence` note
  - `layout/export_page`, `layout/rehosted_widget`, `export_page/embedded_video_panel`, `video_export/embedded_layout`, and `quick_shell/layout` are runtime-debug-only slow-path layout diagnostics for QuickShell/native-surface and embedded export-panel relayout work
  - `timeline/interaction` records quick timeline drag, wheel-scroll, and held-key horizontal scroll inputs
  - `timeline/bridge` records quick timeline hot-path state pushes such as `action=set_horizontal_scroll_value` only when `MIACODE_TIMELINE_HOTPATH_DIAG=1`
  - `timeline/quick_scene` aggregates ordinary `<2 ms` rebuilds for one second into
    `action=scene_state_rebuild_summary` (`window_start_ms`, `rebuild_count`,
    `total_elapsed_ms`, `max_elapsed_ms`, `last_lines`). Slow/error rebuild boundaries and
    `action=update_paint_node_stats` remain individual; scroll-only `action=content_transform_update`
    paints require `MIACODE_TIMELINE_HOTPATH_DIAG=1`
  - `quick_shell/arrow_dispatch action=mouse_press_arm` includes `press_sequence` and suppresses
    only exact per-press signatures; `quick_shell/controller` suppresses unchanged sidebar/workspace
    `(handle,size)` records but retains slow workspace calls (`elapsed_ms >= 50`).
    `ui/app_background action=set_settings_unchanged` emits once per settings signature/lifecycle.
  - `timeline/cursor_map` profiles `timelineSecondForCursor()` so editor cursor-to-second mapping can be separated from redraw cost
  - `timeline/render_map` is emitted only with `MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG=1` and samples final Quick scene-state mappings so waveform/playhead complaints can be checked against actual rendered world-X and viewport-X positions

## Render Path Toggles (diagnostics / A·B only)

The in-process Qt Quick (QSG) path is the default and the only one needed for normal use; both realtime preview and export run through it. The toggles below select alternate render topologies for diagnostics and are **off by default**.

The DirectComposition / D3D11 preview and timeline backend was **removed on 2026-08-07**. `src/render/` and `src/sources/` are gone, along with all six `MIACODE_*_DCOMP*` flags (see the retired list above). The QSG path is now the only render path; there is no alternate backend to A/B against.

- `MIACODE_PREVIEW_QSG_FULL_DISABLE` — force QSG to the software/basic path (GPU-contention isolation test).
- `MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP` — force `QSG_RENDER_LOOP=basic` at startup.
- `MIACODE_ENABLE_DIAG_D3D11` — explicitly enable the supplementary startup D3D11 diagnostic probe. It is disabled by default because device creation loads vendor graphics drivers before Qt initializes.
- `MIACODE_ENABLE_DIAG_MODULE_LIST` — explicitly enable the supplementary pre-Qt process-module list in the startup beacon. It is disabled by default because it traverses third-party/injected modules; normal startup records `phase=diag_modlist_skipped_default_safe` instead.

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
