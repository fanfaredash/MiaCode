#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <optional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace miacode::debug_options {

// Detect "Windows on ARM64 running x86/x64 emulated process". This is
// the configuration Apple Silicon Macs run Windows in (UTM / Parallels
// install Windows-ARM, which then emulates x86/x64 binaries). Some
// graphics paths — DXGI swap chain creation, owned-popup HWND topology
// with WS_EX_NOREDIRECTIONBITMAP — have been observed to crash under
// this emulation. Detected once per process via IsWow64Process2 (Win10
// 1709+); cached in a function-local static so subsequent calls are a
// single atomic load.
//
// Logged once at startup (see main.cpp) so support can confirm which
// environment a report came from.
inline bool runningOnArm64WindowsEmulation()
{
#ifdef Q_OS_WIN
    static const bool result = []() -> bool {
        // IsWow64Process2 lives in kernel32.dll on Win10 1709+. Resolve
        // dynamically so a binary built against the Win10 SDK still
        // links and runs on Win8.1 / Win10 RS2 (which lack it).
        using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
        const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
        if (kernel32 == nullptr) {
            return false;
        }
        const auto fn = reinterpret_cast<IsWow64Process2Fn>(
            ::GetProcAddress(kernel32, "IsWow64Process2"));
        if (fn == nullptr) {
            return false;  // older OS — not in the affected matrix
        }
        USHORT processMachine = 0;
        USHORT nativeMachine = 0;
        if (!fn(::GetCurrentProcess(), &processMachine, &nativeMachine)) {
            return false;
        }
        // Native = ARM64, process = x86/x64 → emulated.
        return nativeMachine == IMAGE_FILE_MACHINE_ARM64
               && processMachine != IMAGE_FILE_MACHINE_ARM64
               && processMachine != IMAGE_FILE_MACHINE_UNKNOWN;
    }();
    return result;
#else
    return false;
#endif
}

inline bool isTruthyValue(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("1")
        || normalized == QStringLiteral("true")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("on");
}

inline bool isFalseyValue(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("0")
        || normalized == QStringLiteral("false")
        || normalized == QStringLiteral("no")
        || normalized == QStringLiteral("off");
}

inline QString envValue(const char* key, const char* legacyKey = nullptr)
{
    if (legacyKey == nullptr) {
        return qEnvironmentVariable(key).trimmed();
    }
    return qEnvironmentVariable(key, qEnvironmentVariable(legacyKey)).trimmed();
}

inline bool envFlagEnabled(const char* key, const char* legacyKey = nullptr)
{
    return isTruthyValue(envValue(key, legacyKey));
}

inline std::optional<bool> envOptionalFlagValue(const char* key, const char* legacyKey = nullptr)
{
    const QString value = envValue(key, legacyKey);
    if (value.isEmpty()) {
        return std::nullopt;
    }
    if (isTruthyValue(value)) {
        return true;
    }
    if (isFalseyValue(value)) {
        return false;
    }
    return std::nullopt;
}

inline int envIntValue(const char* key, int defaultValue, const char* legacyKey = nullptr)
{
    bool ok = false;
    const int value = envValue(key, legacyKey).toInt(&ok);
    return ok ? value : defaultValue;
}

inline double envDoubleValue(const char* key, double defaultValue, const char* legacyKey = nullptr)
{
    bool ok = false;
    const double value = envValue(key, legacyKey).toDouble(&ok);
    return ok ? value : defaultValue;
}

inline std::atomic_bool& debugModeState()
{
    static std::atomic_bool enabled{false};
    return enabled;
}

// Cached per-category gate state. The per-category disable env vars (e.g.
// MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT) are launch-time vars constant for a run, so
// we snapshot them ONCE when debug mode is applied rather than hitting the
// process-global environment lock on every log line via channelEnabled(). Each is a
// function-local-static atomic (one instance across TUs, like debugModeState).
inline std::atomic_bool& startupTimingCategoryState() { static std::atomic_bool s{false}; return s; }
inline std::atomic_bool& runtimeDebugCategoryState() { static std::atomic_bool s{false}; return s; }
inline std::atomic_bool& audioDebugCategoryState() { static std::atomic_bool s{false}; return s; }
inline std::atomic_bool& exportDebugCategoryState() { static std::atomic_bool s{false}; return s; }
inline std::atomic_bool& previewProfileCategoryState() { static std::atomic_bool s{false}; return s; }

// Re-read the per-category disable env vars into the cached atomics. Called from
// setDebugModeEnabled; also public so a test (or any future runtime env mutation)
// can force a re-snapshot after changing one of those env vars.
inline void refreshDebugCategoryCache()
{
    const bool dbg = debugModeState().load(std::memory_order_relaxed);
    startupTimingCategoryState().store(
        dbg && !envFlagEnabled("MIACODE_DISABLE_STARTUP_TIMING"), std::memory_order_relaxed);
    runtimeDebugCategoryState().store(
        dbg && !envFlagEnabled("MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT"), std::memory_order_relaxed);
    audioDebugCategoryState().store(
        dbg && !envFlagEnabled("MIACODE_DISABLE_AUDIO_DEBUG_OUTPUT"), std::memory_order_relaxed);
    exportDebugCategoryState().store(
        dbg && !envFlagEnabled("MIACODE_DISABLE_EXPORT_DEBUG_OUTPUT"), std::memory_order_relaxed);
    previewProfileCategoryState().store(
        dbg && !envFlagEnabled("MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT"), std::memory_order_relaxed);
}

inline void setDebugModeEnabled(bool enabled)
{
    debugModeState().store(enabled, std::memory_order_relaxed);
    refreshDebugCategoryCache();
}

inline bool debugModeEnabled()
{
    return debugModeState().load(std::memory_order_relaxed);
}

// Live (uncached) category check. Retained for ad-hoc use; the per-channel
// accessors below read the cached atomics so the logging hot path never reads env.
inline bool debugCategoryEnabled(const char* disableKey)
{
    return debugModeEnabled() && !envFlagEnabled(disableKey);
}

inline bool startupTimingEnabled()
{
    return startupTimingCategoryState().load(std::memory_order_relaxed);
}

inline bool runtimeDebugOutputEnabled()
{
    return runtimeDebugCategoryState().load(std::memory_order_relaxed);
}

inline bool audioDebugOutputEnabled()
{
    return audioDebugCategoryState().load(std::memory_order_relaxed);
}

inline bool exportDebugOutputEnabled()
{
    return exportDebugCategoryState().load(std::memory_order_relaxed);
}

inline bool previewProfileOutputEnabled()
{
    return previewProfileCategoryState().load(std::memory_order_relaxed);
}

inline bool previewFramePacingDiagnosticsEnabled()
{
    return envFlagEnabled("MIACODE_PREVIEW_FRAME_PACING_DIAG");
}

inline bool previewHudPaintDiagnosticsEnabled()
{
    return envFlagEnabled("MIACODE_PREVIEW_HUD_PAINT_DIAG");
}

inline int previewFramePacingDiagnosticSampleMs()
{
    const int value = envIntValue("MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS", 1000);
    return value > 0 ? value : 1000;
}

inline bool previewWaveformAlignmentDiagnosticsEnabled()
{
    return audioDebugOutputEnabled() && envFlagEnabled("MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG");
}

inline int previewWaveformAlignmentDiagnosticSampleMs()
{
    const int value = envIntValue("MIACODE_PREVIEW_WAVEFORM_ALIGNMENT_DIAG_SAMPLE_MS", 250);
    return value > 0 ? value : 250;
}

// UI-hang thresholds. The defaults are sized for the freeze reports the watchdog was
// built for — a window that stops responding — which makes them far too coarse for a
// stall that merely drops ~2 s of preview ticks: the idle heartbeat never reaches 5 s,
// so the Windows GUI-thread stack capture, the one probe that can name the blocking
// call, never arms. Lowering these is how a short but reproducible stall gets a stack.
// Non-positive or unparsable values fall back to the default.
inline int uiHangActivePhaseMs()
{
    const int value = envIntValue("MIACODE_UI_HANG_ACTIVE_PHASE_MS", 2000);
    return value > 0 ? value : 2000;
}

inline int uiHangIdleHeartbeatMs()
{
    const int value = envIntValue("MIACODE_UI_HANG_IDLE_HEARTBEAT_MS", 5000);
    return value > 0 ? value : 5000;
}

// Windows-only destructive diagnostic. Disabled by default: when positive, the
// watchdog terminates the process once its GUI-heartbeat age reaches this value.
// An external dump collector can then capture every thread at the actual blocking
// point, instead of after the GUI loop has resumed. It has no effect without runtime
// debug output because the watchdog is not installed in that mode.
inline int uiHangCrashAfterMs()
{
    const int value = envIntValue("MIACODE_UI_HANG_CRASH_AFTER_MS", 0);
    return value > 0 ? value : 0;
}

inline bool previewFixedTimerHighResolutionEnabled()
{
    return envFlagEnabled("MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES");
}

inline bool previewRejectNegativeHsEnabled()
{
    // Opt-OUT escape hatch for negative HS (`<HS*-N>`, a Majdata reverse-flow
    // gimmick). Negative HS is ON by default — the parser accepts negative hs
    // and the preview/export renderer flies tap/star/line notes inward from
    // outside the judgement ring (hold/touch/slide keep the magnitude). Set
    // MIACODE_PREVIEW_REJECT_NEGATIVE_HS=1 to restore the strict stance where
    // the parser rejects hs <= 0 (Q7). Read once at app boot into
    // SimaiNativeParser::setAllowNegativeHsEnabled (zero is always rejected).
    return envFlagEnabled("MIACODE_PREVIEW_REJECT_NEGATIVE_HS");
}

// Three-state preview video decode preference. The MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO
// env var is now a DEV OVERRIDE on top of the persisted 硬件渲染 / 软件渲染 user
// preference (owned by MainWindow in preferences.json, pushed to
// PreviewStageMediaHost::setVideoDecodePreference, hot-switched live with no restart):
//   unset   -> Auto          (honor the user preference; default = hardware D3D11VA)
//   1/true  -> ForceSoftware (always software, overrides the preference)
//   0/false -> ForceHardware (always hardware, overrides the preference)
// The legacy "Auto => integrated GPU silently defaults to software" behaviour was
// removed in the render-mode-toggle wrap-up: the default is hardware for everyone, and
// a user on an affected iGPU flips the preference to software (D3D11VA preview decode is
// unreliable on some Intel/Arc iGPUs — startup stutter, NV12 green, AV1 corruption — all
// absent in software, but most machines are fine, so default hardware + opt-in software).
enum class PreviewVideoDecodePreference { Auto, ForceSoftware, ForceHardware };

inline PreviewVideoDecodePreference previewVideoDecodePreference()
{
    if (!qEnvironmentVariableIsSet("MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO"))
        return PreviewVideoDecodePreference::Auto;
    return envFlagEnabled("MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO")
        ? PreviewVideoDecodePreference::ForceSoftware
        : PreviewVideoDecodePreference::ForceHardware;
}

inline bool previewSingleD3D11DeviceEnabled()
{
    // H2 single-device preview decode experiment:
    // share ONE ID3D11Device between the QtAVPlayer/FFmpeg D3D11VA decoder and the
    // preview QQuickView's QRhi, instead of FFmpeg self-creating a private decode
    // device and bridging every frame across to the render device via a keyed-mutex
    // shared texture (the cross-device copy + render-thread AcquireSync(INFINITE)
    // that freezes/garbles preview on Intel/Arc iGPUs). When enabled, MiaCode
    // creates a video-capable, multithread-protected device, hands it to the
    // QQuickView (setGraphicsDevice) AND publishes it to the decoder.
    //
    // Default OFF: experimental, needs GUI acceptance on an affected iGPU before
    // it can become the default (priority #1 is "no regressions on any machine").
    // If anything fails, every step falls back to the legacy two-device path, so
    // the worst case with this set is "no change". Set
    // MIACODE_PREVIEW_SINGLE_D3D11_DEVICE=1 to enable.
    //
    // ⚠ RESERVED / UI-hidden (render-mode-toggle wrap-up): single-device is
    // intentionally NOT surfaced in the 硬件渲染 / 软件渲染 preference. It is kept as
    // env-gated reserved code in the repo and is NOT a priority for further updates;
    // the shipped hardware path is the legacy two-device bridge. Leave it here for a
    // future revisit rather than deleting it.
    return envFlagEnabled("MIACODE_PREVIEW_SINGLE_D3D11_DEVICE");
}

inline int previewDumpHwFrameBudget()
{
    // Bounded D3D11VA hardware-frame readback for green/garble/seek diagnostics. N = how
    // many decoder NV12 surfaces to read back + stat-classify per "arm" (an arm is the
    // initial playback start, and is re-armed on each seek). 0 = OFF.
    //
    // The readback runs on the QSG render thread inside qavhwdevice_d3d11.cpp, so it
    // is DELIBERATELY bounded: it Maps a STAGING copy of the decoder DPB slot (a
    // CPU<->GPU sync point) at most N times, then permanently self-stops. Each readback
    // emits ONE preview/hwframe stat line (y/uv stats + coded-vs-display + verdict hint);
    // NO image files are written. When 0 (default) the hot path is a single relaxed-
    // atomic load + compare — zero readback, zero stall. Gated on --debug
    // (runtimeDebugOutputEnabled) because the stat lines are debug log artifacts. The
    // literal lives HERE in src/ (not in third_party/qavhwdevice) so the
    // debug_flag_index_spec drift guard governs its DEBUG_INDEX.md entry; the resolved
    // int is PUBLISHED into the decoder via qavSetPreviewHwFrameDumpConfig (mirroring
    // qavSetSharedRenderD3D11Device). Set MIACODE_PREVIEW_DUMP_HWFRAMES=15.
    //
    // ⚠ Repro precondition: on an integrated GPU the buggy D3D11VA path is NOT the
    // default (previewVideoDecodePreference() Auto => software), so launch with
    // MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=0 (ForceHardware) [+ optionally
    // MIACODE_PREVIEW_SINGLE_D3D11_DEVICE=1 for the H2 single-device path] + --debug.
    if (!runtimeDebugOutputEnabled()) {
        return 0;
    }
    const int value = envIntValue("MIACODE_PREVIEW_DUMP_HWFRAMES", 0);
    return value > 0 ? value : 0;
}

inline bool previewSharedD3D11DebugLayerEnabled()
{
    // Add D3D11_CREATE_DEVICE_DEBUG to the H2 shared device at creation time
    // (PreviewSharedD3D11Device::createSharedDevice) so the D3D11 debug layer reports
    // typed-SRV / NV12 PlaneSlice format warnings (the decisive H-FMT signal) and
    // resource-not-ready / device-removed errors (H-DEC) on the actual preview decode+
    // render device. The shared device is IMPORTED into Qt's QRhi, so Qt's own
    // QSG_RHI_DEBUG_LAYER=1 cannot reach it — the flag must be set at D3D11CreateDevice.
    // (For the legacy two-device path use QSG_RHI_DEBUG_LAYER=1 instead; that device is
    // created by Qt.) Creation falls back to no-debug when the SDK debug layer is absent.
    // Gated on --debug. Set MIACODE_PREVIEW_D3D11_DEBUG_LAYER=1.
    return runtimeDebugOutputEnabled() && envFlagEnabled("MIACODE_PREVIEW_D3D11_DEBUG_LAYER");
}

inline bool previewHwDecodeCompletionWaitEnabled()
{
    // PRIMARY FIX for the post-seek green / garble on D3D11VA hardware decode
    // for the post-seek green / garble on D3D11VA hardware decode. Located root
    // cause (logs_36, Arc 130T): the decoder's DecoderEndFrame and our
    // CopySubresourceRegion of the DPB slot are not implicitly ordered on some
    // Intel/Arc iGPU drivers, so just after a seek (decode queue backed up) the render
    // thread copies a half-decoded slot whose chroma is still zeroed and samples as
    // pure green. When ON, the copy paths in qavhwdevice_d3d11.cpp issue an
    // ID3D11Query(EVENT) + Flush + bounded spin to force the decode GPU work to
    // COMPLETE before the copy reads the slot. Codec-agnostic, low risk: the wait
    // returns near-instantly in the common case (frames are decoded ahead of the
    // render thread) and is bounded, so a stuck GPU drops a frame instead of freezing.
    //
    // ⚠ Default OFF as of the render-mode-toggle wrap-up: this completion-wait did
    // NOT resolve the post-seek green on the user's Arc 130T (verified A/B), so it is
    // no longer worth the per-frame GPU sync on every hardware-decode user. The
    // user-facing fix is now the 硬件渲染 / 软件渲染 preference (default hardware; flip
    // to software on an affected iGPU — hot-switchable). The completion-wait is kept as
    // env-gated RESERVED experimental code (it is a legitimate D3D11 sync that may help
    // on other hardware); set MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT=1 to re-enable.
    // Published via qavSetPreviewHwDecodeFixConfig; literal lives in src/ for the guard.
    return envOptionalFlagValue("MIACODE_PREVIEW_HWDECODE_COMPLETION_WAIT").value_or(false);
}

inline bool previewHwDecodeDropCorruptFramesEnabled()
{
    // SAFETY NET (secondary) for the same green / garble bug. When ON, the D3D11VA copy
    // path drops any decoded frame FFmpeg flagged corrupt / decode-error
    // (AV_FRAME_FLAG_CORRUPT or AVFrame::decode_error_flags != 0) instead of sampling
    // it, so the RHI holds the previous good frame (a brief freeze beats a green flash).
    // FREE: it only reads existing AVFrame flags — no GPU readback. Default OFF because
    // it only helps IF the driver/decoder actually reports these flags for the green
    // frames (unverified on the affected Arc 130T) and because dropping on benign
    // concealment flags could over-discard. First confirm via the preview/hwframe dump
    // line (decode_err= / corrupt= fields) and the preview/hwdecode_summary
    // frames_decode_error counter that FFmpeg flags the green frames, THEN enable. Set
    // MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT=1. Published via qavSetPreviewHwDecodeFixConfig.
    return envFlagEnabled("MIACODE_PREVIEW_HWDECODE_DROP_CORRUPT");
}

inline bool previewQsgFullDisableEnabled()
{
    // Diagnostic / fallback mode: disable Qt Quick's native (GPU) rendering
    // path entirely. When set, startup forces:
    //   - QSG_RENDER_LOOP=basic           (no separate QSG render thread)
    //   - setGraphicsApi(Software)        (no D3D11/D3D12/Vulkan backend;
    //                                      QPainter blits via raster paint
    //                                      engine)
    // Combined: Qt creates no GPU swap chain and no render thread, so
    // anything presenting alongside it has no Qt-side GPU contention to
    // compete with.
    //
    // Used to answer: "is the residual lag Qt-side, or fixable
    // elsewhere?". If playback is still laggy with this on, the cause
    // is *not* Qt-side GPU/render-thread interference and further
    // optimisation has to look elsewhere (or the residual is below
    // the perceptual threshold and tooling is needed to characterise
    // it). If playback becomes smooth with this on, that pinpoints
    // Qt's native render path as the contention source.
    //
    // Production-untenable on its own (the editor UI rasterising on
    // CPU is heavy), but a clean isolation test.
    return envFlagEnabled("MIACODE_PREVIEW_QSG_FULL_DISABLE");
}

inline bool previewQsgRenderTimingEnabled()
{
    // Captures Qt's built-in scene-graph timing (`qt.scenegraph.time.*` log
    // category) into our runtime log via a QtMessageHandler. Qt only emits
    // these when QSG_RENDER_TIMING=1 is set in the environment BEFORE
    // QApplication is constructed, so the flag is read at the very top of
    // main() and propagated as needed. Use this when the offscreen renderer
    // looks healthy but the present-driven gate still misses vsyncs — the
    // scene-graph timings tell you whether the main QSG render pass (which
    // also includes our offscreen output as a sub-texture) is over budget.
    return envFlagEnabled("MIACODE_PREVIEW_QSG_RENDER_TIMING");
}

inline double previewVisualLookaheadVsyncs()
{
    // Tier 2A predictive playhead. The audio-time sample we receive corresponds to "where audio
    // was when sampled"; the frame we render off it won't be visible until the next vsync swap
    // (~16.7ms at 60Hz, on top of the GUI→render→composite→present pipeline). Biasing the
    // visual playhead forward by ~1 vsync makes the rendered scene line up with the audio
    // moment when it's actually visible on screen, eliminating the perceived "audio leads
    // video by one frame" lag. Default 1.0 vsync ≈ one display interval; set to 0 to disable
    // (passes audio-time through unchanged) or to e.g. 1.5 / 2.0 to over/under-bias.
    const QString raw = envValue("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS");
    if (raw.isEmpty()) {
        return 1.0;
    }
    bool ok = false;
    const double parsed = raw.toDouble(&ok);
    if (!ok || parsed < 0.0 || parsed > 4.0) {
        return 1.0;
    }
    return parsed;
}

inline bool timelineHotpathDiagnosticsEnabled()
{
    return runtimeDebugOutputEnabled() && envFlagEnabled("MIACODE_TIMELINE_HOTPATH_DIAG");
}

inline bool previewCompareDumpFramesEnabled()
{
    return runtimeDebugOutputEnabled() && envFlagEnabled("MIACODE_PREVIEW_DIAG_COMPARE_DUMP_FRAMES");
}

inline int previewCompareDumpMaxSamples()
{
    if (!runtimeDebugOutputEnabled()) {
        return 0;
    }
    const int value = envIntValue("MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES", 8);
    return value >= 0 ? value : 8;
}

inline QString previewCompareDumpDirectoryOverride()
{
    if (!runtimeDebugOutputEnabled()) {
        return QString();
    }
    return envValue("MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR");
}

inline QString gpuPolicyRequestRaw()
{
    // P3 — hidden internal GPU device policy override (diagnostic / support
    // only; not surfaced in the user settings UI). Values:
    //   auto_high_performance (default) | platform_default | software
    // The `--gpu-policy=<value>` CLI flag takes precedence over this env var.
    // Resolved + logged by miacode::gpu (src/common/GpuDevicePolicy.*); P3 only
    // logs the resolved adapter — it does NOT yet bind a device to it.
    return envValue("MIACODE_GPU_POLICY");
}

inline QString gpuAdapterLuidRaw()
{
    // P3 — hidden diagnostic adapter override, "<high>:<low>" (hex 0x.. or
    // decimal). Forces the internal policy to `adapter_luid` and A/B-selects a
    // specific DXGI adapter by LUID. `--gpu-adapter-luid=<high>:<low>` CLI flag
    // takes precedence. An unknown / illegal LUID never blocks startup — the
    // policy falls back to platform_default and the reason is logged.
    return envValue("MIACODE_GPU_ADAPTER_LUID");
}

inline bool gpuBindHighPerformanceEnabled()
{
    // P4 master gate: bind the root Quick window to the resolved high-performance
    // DXGI adapter (QQuickGraphicsDevice::fromAdapter) by default. Set
    // MIACODE_GPU_BIND_HIGH_PERFORMANCE=0/off/false to roll back to Qt's default
    // adapter. The preview composite (video surface) is never fromAdapter-bound;
    // it keeps Qt's default adapter (or the H2 single-device path) to preserve
    // the QtAVPlayer D3D11VA same-adapter video bridge.
    if (const std::optional<bool> enabled =
            envOptionalFlagValue("MIACODE_GPU_BIND_HIGH_PERFORMANCE")) {
        return *enabled;
    }
    return true;
}

// P5.3/P5.4 hidden export render-backend selector. Chooses the offscreen chart
// render session used by CLI export (--export-video) and the export worker
// (--export-video-worker):
//   d3d11_qrhi (default) -> D3D11/QRhi session on Windows, binding the P3 policy
//                           adapter and using synchronous staging-map readback;
//                           init failure auto-falls back to OpenGL
//   opengl               -> stable QQuickRenderControl OpenGL FBO/PBO rollback
//   auto                 -> currently identical to d3d11_qrhi
// Windows-only; unknown values intentionally keep the OpenGL rollback path.
enum class ExportRenderBackendRequest { OpenGl, D3D11Qrhi, Auto };

inline ExportRenderBackendRequest exportRenderBackendRequest()
{
    const QString raw = envValue("MIACODE_EXPORT_RENDER_BACKEND").toLower();
    // Current default is D3D11 QRhi; set the env var to opengl for rollback.
    if (raw.isEmpty()) {
        return ExportRenderBackendRequest::D3D11Qrhi;
    }
    if (raw == QStringLiteral("d3d11_qrhi") || raw == QStringLiteral("d3d11")) {
        return ExportRenderBackendRequest::D3D11Qrhi;
    }
    if (raw == QStringLiteral("auto")) {
        return ExportRenderBackendRequest::Auto;
    }
    return ExportRenderBackendRequest::OpenGl;
}

inline std::optional<bool> exportPremultipliedPipeOverride()
{
    return envOptionalFlagValue("MIACODE_EXPORT_PREMULTIPLIED_PIPE");
}

inline bool hasDebugArg(const QStringList& args)
{
    return args.contains(QStringLiteral("--debug"));
}

}  // namespace miacode::debug_options
