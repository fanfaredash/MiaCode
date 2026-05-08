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
// Used by previewUseDCompEnabled() / previewOutOfProcessEnabled() to
// fall back to the legacy QSG-only render path (the beta19-equivalent
// pre-DComp pipeline) where neither path creates a popup HWND.
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

inline void setDebugModeEnabled(bool enabled)
{
    debugModeState().store(enabled, std::memory_order_relaxed);
}

inline bool debugModeEnabled()
{
    return debugModeState().load(std::memory_order_relaxed);
}

inline bool debugCategoryEnabled(const char* disableKey)
{
    return debugModeEnabled() && !envFlagEnabled(disableKey);
}

inline bool startupTimingEnabled()
{
    return debugCategoryEnabled("MIACODE_DISABLE_STARTUP_TIMING");
}

inline bool runtimeDebugOutputEnabled()
{
    return debugCategoryEnabled("MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT");
}

inline bool audioDebugOutputEnabled()
{
    return debugCategoryEnabled("MIACODE_DISABLE_AUDIO_DEBUG_OUTPUT");
}

inline bool exportDebugOutputEnabled()
{
    return debugCategoryEnabled("MIACODE_DISABLE_EXPORT_DEBUG_OUTPUT");
}

inline bool previewProfileOutputEnabled()
{
    return debugCategoryEnabled("MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT");
}

inline bool previewFramePacingDiagnosticsEnabled()
{
    return envFlagEnabled("MIACODE_PREVIEW_FRAME_PACING_DIAG");
}

inline int previewFramePacingDiagnosticSampleMs()
{
    const int value = envIntValue("MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS", 1000);
    return value > 0 ? value : 1000;
}

inline bool previewFixedTimerHighResolutionEnabled()
{
    return envFlagEnabled("MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES");
}

inline bool previewVisualSmoothingEnabled()
{
    // Visual-time smoothing (doc section 6.3): bound per-frame visual delta so render-time
    // variance doesn't feed audio-time jumps straight into the rendered playhead. Default ON
    // because the choppy motion from audio-authority + render-variance interaction is the most
    // visible artefact during playback. Set MIACODE_PREVIEW_VISUAL_SMOOTHING=0 to disable.
    return envOptionalFlagValue("MIACODE_PREVIEW_VISUAL_SMOOTHING").value_or(true);
}

inline bool previewUseDCompEnabled()
{
    // DirectComposition preview path (see local plan doc
    // docs/PREVIEW_FRAME_PACING_FEASIBILITY_AND_IMPLEMENTATION_PLAN_ZH.md).
    // When set, QuickShellBootstrap creates a PreviewDCompSurface attached
    // to the main QQuickWindow on first show.
    //
    // Default ON as of Phase 4e — the DComp path is the supported preview
    // pipeline (per-pixel alpha popups, frame-latency-waitable pacing,
    // device-removed recovery, owner-followed positioning). Set
    // `MIACODE_PREVIEW_USE_DCOMP=0` to fall back to the legacy QSG-only
    // pipeline (kept around as a safety hatch for unusual graphics
    // drivers; will be retired once Phase 5 multi-monitor / DPI edge
    // cases are signed off).
    //
    // Auto-disabled on Apple Silicon Windows VMs (Windows-on-ARM running
    // x86/x64 emulation): the DComp popup HWND + WS_EX_NOREDIRECTIONBITMAP
    // + DXGI swap-chain combination has been observed to crash under
    // that emulation. The legacy QSG-only path (beta19-equivalent) is
    // the safe fallback there. Explicit `MIACODE_PREVIEW_USE_DCOMP=1`
    // overrides the auto-fallback for users who want to test on that
    // hardware.
    const auto override = envOptionalFlagValue("MIACODE_PREVIEW_USE_DCOMP");
    if (override.has_value()) {
        return *override;
    }
    if (runningOnArm64WindowsEmulation()) {
        return false;
    }
    return true;
}

inline bool previewOutOfProcessEnabled()
{
    // Out-of-process preview worker (see
    // docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md
    // section 7.5). The supervisor spawns a child MiaCode.exe
    // --preview-worker process that owns the chart popup HWND + render
    // path, so DXGI_ERROR_DEVICE_REMOVED on the chart popup is
    // contained by a respawning supervisor rather than cascading into
    // the editor.
    //
    // Default ON. Combined with the QSG render flag (also default ON),
    // the worker is the chart preview's authoritative renderer; the
    // editor's in-process PreviewDCompSurface is suppressed and the
    // editor's main render thread drops the dual-D3D11-device load
    // from the original Phase 4e architecture.
    //
    // Auto-disabled on Apple Silicon Windows VMs — the worker also
    // creates an owned-popup HWND, which has the same crash risk under
    // ARM64 → x86 emulation as the in-process DComp popup. Set
    // `MIACODE_PREVIEW_OUT_OF_PROCESS=0` to opt out (e.g. while
    // diagnosing worker-specific issues).
    const auto override = envOptionalFlagValue("MIACODE_PREVIEW_OUT_OF_PROCESS");
    if (override.has_value()) {
        return *override;
    }
    if (runningOnArm64WindowsEmulation()) {
        return false;
    }
    return true;
}

inline bool previewWorkerQsgRenderEnabled()
{
    // When set, the out-of-process preview worker swaps its DComp /
    // PreviewDCompSpritePipeline render path for a QSG path: a
    // QQuickWindow hosting a PreviewQuickSceneRoot that reuses the
    // editor's existing layer code (head, track, slide motion, judge
    // effect, judge firework, touch, touch-hold, ...). Pixel-perfect
    // chart fidelity by reusing the editor's QSG layer code.
    //
    // Default ON. Pairs with previewOutOfProcessEnabled to make the
    // worker the chart preview's authoritative renderer with full
    // chart fidelity. Set `MIACODE_PREVIEW_WORKER_QSG_RENDER=0` to
    // fall back to the worker's legacy circles-MVP DComp render path.
    return envOptionalFlagValue("MIACODE_PREVIEW_WORKER_QSG_RENDER").value_or(true);
}

inline bool previewWorkerRealPublisherEnabled()
{
    // When the out-of-process worker is enabled, this flag controls
    // whether the supervisor publishes the editor's live PreviewRuntime
    // state (real publisher) or a 60 Hz synthetic frame stream (Phase
    // 1 latency-harness mode).
    //
    // Default ON. Real publisher is the production path; the synthetic
    // mode is only useful for IPC-latency benchmarking. Set
    // `MIACODE_PREVIEW_WORKER_REAL_PUBLISHER=0` to switch the supervisor
    // back into the synthetic timer (the worker will then render its
    // legacy circles-MVP demo against fake playhead data, useful for
    // measuring ring-buffer round-trip latency on a given hardware).
    return envOptionalFlagValue("MIACODE_PREVIEW_WORKER_REAL_PUBLISHER").value_or(true);
}

inline bool previewDCompPerPixelAlphaEnabled();  // forward decl for use below

inline bool previewDCompExclusiveEnabled()
{
    // Phase 4b — when on, the legacy QSG chart-layer pipeline
    // (PreviewQuickSceneRoot's updatePaintNode) is short-circuited so
    // the DComp surface is the only thing rendering chart content.
    // The HUD layer (PreviewQuickHudLayer) ALSO short-circuits (see
    // PreviewQuickHudLayer.cpp:150). Only PreviewStageMediaItem
    // (image / video bg) keeps rendering in QSG. Implies and requires
    // previewUseDCompEnabled — otherwise nothing renders the chart.
    //
    // Phase 4d — automatically enabled when per-pixel alpha is on.
    // Without exclusive, both QSG and DComp render chart sprites +
    // HUD; LWA used to occlude the QSG copy, but per-pixel alpha
    // lets QSG show through, producing visible duplicate rendering.
    if (!previewUseDCompEnabled()) {
        return false;
    }
    if (envFlagEnabled("MIACODE_PREVIEW_DCOMP_EXCLUSIVE")) {
        return true;
    }
    return previewDCompPerPixelAlphaEnabled();
}

inline bool previewDCompTopLevelHwndEnabled()
{
    // When on, the DComp visual tree is hosted by a separate top-level
    // borderless transparent HWND that's owned (not parented) by the
    // editor's QQuickWindow HWND. DWM treats top-level HWNDs as
    // independent composition planes, so the editor's QSG swap chain
    // and DComp's swap chain no longer serialise on the same HWND.
    // This is the architectural fix that lets the chart-preview swap
    // chain present without inter-swap-chain serialisation in DWM.
    //
    // Phase 3a — default-on when DComp is enabled. The env flag is now
    // an *override* rather than an opt-in: leave it unset (the common
    // case) and you get the new behaviour; set it explicitly to "0" /
    // "false" to fall back to the legacy in-place mode. This lets us
    // ship the new pipeline as the default while keeping the A/B
    // escape hatch around for diagnostics. Implies and requires
    // previewUseDCompEnabled.
    if (!previewUseDCompEnabled()) {
        return false;
    }
    const std::optional<bool> override = envOptionalFlagValue(
        "MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND");
    return override.value_or(true);
}

inline bool previewDCompPerPixelAlphaEnabled()
{
    // Phase 4d — per-pixel alpha composition for the DComp top-level
    // popup HWND. The popup is created with WS_EX_NOREDIRECTIONBITMAP
    // (Win10+) instead of WS_EX_LAYERED + LWA_ALPHA(255). NRB tells
    // the OS not to allocate a redirection bitmap; the DComp visual
    // tree composes directly into DWM, allowing per-pixel alpha to
    // "see through" to whatever's behind the HWND (the editor's QML
    // scene). This is the proper architecture: QML renders bg
    // image/video natively via QRhi (GPU-direct), DComp paints
    // chart sprites + HUD on top with per-pixel alpha.
    //
    // Phase 4d-final: now default-on whenever DComp is enabled, with
    // the env flag acting as an override (set explicitly to "0" to
    // fall back to legacy LWA mode + CPU bg detour). Verified working
    // on the user's Win11 setup (image bg from Lone Wolf / love
    // machine 3 visible, video bg from ECHO smooth, HUD stutters
    // back to image-level after auto-exclusive suppresses duplicate
    // QSG chart-rendering).
    //
    // Pairs with previewDCompExclusiveEnabled() which auto-enables
    // when this is on, so the QSG chart + HUD layers don't duplicate
    // DComp's painting through the now-transparent popup.
    if (!previewUseDCompEnabled()) {
        return false;
    }
    const std::optional<bool> override = envOptionalFlagValue(
        "MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA");
    return override.value_or(true);
}

inline bool previewTimelineUseDCompEnabled()
{
    // Phase 3c of the v2-refactor — DComp pipeline for the timeline
    // pane. When on, TimelineQuickItem's updatePaintNode returns null
    // (skipping the QSG path) and pushes scene-state to a sibling
    // TimelineRenderView that owns its own top-level popup HWND and
    // composites the timeline via DirectComposition. When off, the
    // QSG paint node renders the timeline through Qt Quick's normal
    // scene graph.
    //
    // **Default ON as of the QML-timeline temporary deprecation
    // (beta31).** A reproducible editor-process crash was traced to
    // the QSG/QML path under rapid wheel-scroll (~60 events / 0.26 s)
    // — see docs/OPERATION_LOG_PATTERNS_SPEC.md §4.5. The HWND
    // timeline path is more battle-tested under the same workload
    // because its scene-state pipeline has been the chart-preview
    // primary since beta25, with the same scroll-bucket invalidation
    // discipline. While we triage the QSG-path crash, HWND is the
    // safer default for the editor's timeline pane too.
    //
    // Set `MIACODE_TIMELINE_USE_DCOMP=0` to opt back into the QSG/QML
    // path — useful for performance comparison, for the QSG-path
    // crash repro, or for environments where the HWND popup topology
    // doesn't behave correctly (e.g. some screen-recording tools).
    //
    // Auto-disabled on Apple Silicon Windows VMs alongside the chart
    // preview's DComp path, via the previewUseDCompEnabled cascade.
    // On those VMs the QSG path is the only working option regardless
    // of this default.
    if (!previewUseDCompEnabled()) {
        return false;
    }
    const std::optional<bool> override = envOptionalFlagValue(
        "MIACODE_TIMELINE_USE_DCOMP");
    return override.value_or(true);
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
    // anything DComp / external presents alongside it has no Qt-side
    // GPU contention to compete with.
    //
    // Used to answer: "is the residual lag fundamental to mixing Qt's
    // native render path with our DComp present, or fixable on the
    // Qt side?". If playback is still laggy with this on, the cause
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

inline bool previewDCompQuiesceQsgEnabled()
{
    // Phase 4-perf (post-Option-1) — when on AND the DComp path is
    // active, PreviewQuickSceneRoot and PreviewQuickHudLayer skip
    // their `runtime->frameStateChanged → update()` subscriptions.
    //
    // Why: those subscriptions originally existed because the
    // playback tick was present-driven (gated on QQuickWindow's
    // frameSwapped) and the QSG items had to keep firing
    // updatePaintNode at 60Hz to keep the present cadence alive.
    // Commit 18a9813 disabled present-driven pacing for all
    // frame-rate modes, so the tick is now timer-driven; the
    // QSG items no longer need to drive QQuickWindow Presents.
    // With DComp painting all chart content, the QSG side has
    // nothing useful to refresh — its 60Hz Presents purely
    // contend with DComp's swap chain in DWM.
    //
    // Risk: if a QML item somewhere still depends on the QSG
    // items repainting (unlikely — they short-circuit
    // updatePaintNode in DComp-exclusive mode), it would go
    // stale. Keep gated behind a flag so the user can A/B
    // confirm before making it the default.
    return previewUseDCompEnabled()
        && envFlagEnabled("MIACODE_PREVIEW_DCOMP_QUIESCE_QSG");
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

inline bool hasDebugArg(const QStringList& args)
{
    return args.contains(QStringLiteral("--debug"));
}

inline bool hasRuntimeDebugArg(const QStringList& args)
{
    return hasDebugArg(args);
}

}  // namespace miacode::debug_options
