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
    // Phase 1 of the DirectComposition preview path (see local plan doc
    // docs/PREVIEW_FRAME_PACING_FEASIBILITY_AND_IMPLEMENTATION_PLAN_ZH.md).
    // When set, QuickShellBootstrap creates a PreviewDCompSurface attached
    // to the main QQuickWindow on first show. Phase 1 deliverable is just a
    // red test rectangle in the top-left of the window — Phase 4 wires it
    // to the actual preview slot via a QML placeholder. Default off so
    // every release build behaves exactly like the legacy path until each
    // phase is fully validated.
    return envFlagEnabled("MIACODE_PREVIEW_USE_DCOMP");
}

inline bool previewDCompExclusiveEnabled()
{
    // Phase 4b — when on, the legacy QSG chart-layer pipeline
    // (PreviewQuickSceneRoot's updatePaintNode) is short-circuited so
    // the DComp surface is the only thing rendering chart content.
    // The other QSG items inside QuickShellPreviewSurface
    // (PreviewStageMediaItem for video background, PreviewQuickHudLayer
    // for the FPS overlay) keep running. Implies and requires
    // previewUseDCompEnabled — otherwise nothing renders the chart.
    return previewUseDCompEnabled()
        && envFlagEnabled("MIACODE_PREVIEW_DCOMP_EXCLUSIVE");
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
    // Phase 3 of the v2-refactor turns this on unconditionally; for
    // now it remains opt-in via env flag so we can A/B against the
    // legacy in-place mode while the rest of the refactor lands.
    // Implies and requires previewUseDCompEnabled.
    return previewUseDCompEnabled()
        && envFlagEnabled("MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND");
}

inline bool previewDCompFixedNominalClockEnabled()
{
    // Experimental playhead-clock mode for the DComp surface. When on,
    // renderPlayheadSeconds_ advances by a hardcoded nominal 60 Hz vsync
    // interval (16.67 ms) per render-thread-driven publish, instead of
    // by measured wall-clock between publishes.
    //
    // Goal: drive playhead_delta_stats stddev_ms toward 0 — the residual
    // variance under wall-clock advance comes from Present(1,0) return
    // timing variance + DXGI flip-queue scheduling, not from the
    // monotonic clock or queued-dispatch jitter (those were already
    // fixed by the monotonic-clock + FIFO-mailbox commits).
    //
    // Why opt-in for now: a previous attempt to plumb the user's
    // Video-Settings rate selection through PreviewRuntime broke the
    // preview render path (rendering stalled mid-session, root cause
    // not yet pinned). Until we understand why, this experimental flag
    // exercises the playhead-clock change in isolation — no
    // PreviewRuntime API changes, no MOC churn, no MainWindow plumbing —
    // so we can isolate whether the fix concept itself is sound.
    //
    // Hardcoded 60 Hz only. Phase 5 polish will plumb the user-selected
    // rate (60 / 120 / Display Refresh) through once this is proven safe.
    return previewUseDCompEnabled()
        && envFlagEnabled("MIACODE_PREVIEW_DCOMP_FIXED_NOMINAL_CLOCK");
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
