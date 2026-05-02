#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <optional>

namespace miacode::debug_options {

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
    return envOptionalFlagValue("MIACODE_PREVIEW_USE_DCOMP").value_or(true);
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
    // pane. When on, TimelineQuickItem instantiates a TimelineRenderView
    // alongside its existing QSG paint node and pushes scene-state
    // updates to both. The QSG path stays alive (so timeline drawing
    // keeps working if the DComp visual fails), and the DComp popup
    // overlays it on top via the top-level HWND. Phase 3e turns this on
    // unconditionally and removes the QSG paint code.
    //
    // Phase 9d-final: now default-on whenever DComp is enabled — the
    // env flag is an *override* rather than an opt-in. Leave it unset
    // (the common case) to get the new behaviour; set it explicitly to
    // "0" / "false" to fall back to the legacy QSG-only path. This
    // mirrors the previewDCompTopLevelHwndEnabled() pattern: ship the
    // new pipeline as the default while keeping the A/B escape hatch
    // around for diagnostics.
    //
    // Implies and requires previewUseDCompEnabled (the timeline view
    // shares D3D11 device + waitable + texture cache infrastructure
    // with the chart-preview path).
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
