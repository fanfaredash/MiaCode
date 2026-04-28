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

inline bool previewDCompChildHwndEnabled()
{
    // Phase 4-perf-test (Option 3) — when on, the DComp visual tree
    // is hosted by a dedicated child HWND parented to the
    // QQuickWindow, instead of attaching directly to the
    // QQuickWindow's HWND. The child HWND is positioned (MoveWindow)
    // to track the QML preview pane's bounds, so DWM sees a
    // separate top-level-style window for the chart preview rather
    // than a visual sub-tree under the editor's main HWND.
    //
    // Goal: test whether DWM's compositor treats sibling HWNDs
    // differently from DComp visuals on a single HWND, which would
    // affect how the chart preview's swap chain serialises against
    // the editor's QSG swap chain at vsync. If perf improves with
    // this on (and the flag combination QT_QUICK_BACKEND not set),
    // it confirms separate-HWND is a viable production path.
    //
    // Caveats:
    //   - The child HWND is WS_EX_TRANSPARENT (click-through) and
    //     WS_EX_NOACTIVATE so input still routes to QML below.
    //   - Z-ordering: the HWND is a real Win32 child, so it sits
    //     above QSG content within the parent. Modal dialogs that
    //     overlap the preview pane may need extra care.
    //   - Resize/move tracking has to keep the HWND in sync with
    //     QML layout changes.
    return envFlagEnabled("MIACODE_PREVIEW_DCOMP_CHILD_HWND");
}

inline bool previewDCompVsyncPresentEnabled()
{
    // Phase 4-perf-fix — when on, the DComp render thread uses
    // Present(1, 0) (block on vsync) instead of Present(0, 0)
    // (non-blocking). Pace the render loop to the display refresh
    // rate so each snapshot is rendered exactly once instead of
    // 2-3× (the over-render comes from Present(0, 0) draining DXGI's
    // back-buffer queue faster than the snapshot rate can refill it
    // — that produces visible motion jitter when the publish timing
    // has any variance).
    //
    // Trade-off: with Qt's QSG GPU backend also presenting at 60 Hz,
    // both swap chains may serialise on vsync via DWM, reintroducing
    // the dual-swap-chain stutter the user originally diagnosed.
    // Safer to gate this behind a flag the user can toggle:
    //   - With QT_QUICK_BACKEND=software: turn this on, motion
    //     becomes locked to vsync without contention.
    //   - With Qt GPU backend: leaving this off keeps the
    //     non-blocking present pattern.
    return envFlagEnabled("MIACODE_PREVIEW_DCOMP_VSYNC_PRESENT");
}

inline bool disableTimelineEnabled()
{
    // Phase 4-perf experiment — when on, hides the Timeline QSG item
    // entirely so its render cost (custom QQuickItem with note
    // textures, scrolling waveform, etc.) is removed. Used to test
    // the dual-swap-chain compositing hypothesis: if QT_QUICK_BACKEND
    // =software combined with this flag substantially reduces stutter,
    // the QSG GPU work in the editor was contending with DComp's
    // present cycle. Not a permanent feature — the user re-enables
    // by unsetting the flag.
    return envFlagEnabled("MIACODE_DISABLE_TIMELINE");
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

inline int previewTimelineThrottleHz()
{
    // Phase 4-perf (Option 1) — caps the timeline UI tick timer to
    // at most this rate. Default 0 means uncapped (the timeline ticks
    // at the same rate as the preview canvas, which is what the
    // editor has historically done). Set e.g. to 30 to halve the
    // editor's QSG present rate during playback.
    //
    // The hypothesis: when the DComp preview path runs, every
    // timeline tick during playback causes Qt to push a frame on
    // the editor swap chain (one Present per playhead bump), and
    // DWM has to composite *both* swap chains every refresh. Cutting
    // the editor present rate to 30 Hz halves DWM's compositing
    // load on the editor side without making playhead motion look
    // worse to a human (a 1px/frame line at 60 vs 30 Hz is visually
    // indistinguishable). Trade-off: the playhead lags audio by up
    // to ~33 ms in the worst case, which is still well under
    // perceptual threshold.
    //
    // Values 1..240 are accepted; anything else (including the
    // default 0 / empty) leaves the timeline rate unchanged.
    const int v = envIntValue("MIACODE_PREVIEW_TIMELINE_THROTTLE_HZ", 0);
    if (v <= 0 || v > 240) {
        return 0;
    }
    return v;
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

inline bool glDebugMessagesEnabled()
{
    return debugCategoryEnabled("MIACODE_DISABLE_GL_DEBUG_MESSAGES");
}

inline bool forceDontCreateNativeWidgetSiblings()
{
    return envFlagEnabled("MIACODE_FORCE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS");
}

inline bool forceBasicQsgRenderLoop()
{
    return envFlagEnabled("MIACODE_FORCE_BASIC_QSG_RENDER_LOOP");
}

inline int previewVideoFallbackCompareEveryFrames()
{
    if (!runtimeDebugOutputEnabled()) {
        return 0;
    }
    const int value = envIntValue("MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY", 0);
    return value > 0 ? value : 0;
}

inline int previewPresentCompareEveryFrames()
{
    if (!runtimeDebugOutputEnabled()) {
        return 0;
    }
    const int value = envIntValue("MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY", 0);
    return value > 0 ? value : 0;
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
