#pragma once

#include <QtGlobal>

// Arbitration between the timeline's two possible playback-sampling drivers.
//
// The timeline playhead is sampled from a monotonic wall clock; the only thing that decides
// whether the motion looks smooth is WHEN that sample is taken relative to the frame it will
// be presented in. Two drivers exist:
//
//   1. Render cadence (preferred) — QQuickWindow::afterAnimating, emitted on the GUI thread
//      immediately before the render thread syncs that frame. Sampling here gives a fixed
//      one-frame sample->present latency.
//   2. Watchdog timer (fallback) — a free-running QChronoTimer. Its phase drifts against
//      vsync, so sample->present latency wanders across the whole frame interval. Measured on
//      a 139s capture: ~2.3us/frame of drift, and 16% of on-time frames drawn for the wrong
//      moment. This was the timeline judder.
//
// The watchdog must therefore stay silent while the render cadence is alive, but must take
// over promptly when it stops (hidden window, torn-down scene graph, stalled render loop) so a
// dead cadence can never freeze the playhead. This header owns that decision so it can be
// tested without a MainWindow, matching the policy/spec convention used elsewhere in the repo.
namespace miacode::timeline::cadence {

struct ArbitrationState {
    // Whether preview playback is currently driving the timeline.
    bool playing = false;
    // Monotonic ms of the most recent render-cadence tick, or -1 when none has been observed
    // for this playback (the state each playback start/stop resets to).
    qint64 lastCadenceMs = -1;
    // Monotonic ms now, from the same clock as lastCadenceMs.
    qint64 nowMs = 0;
    // Silence window before the watchdog concludes the cadence has stopped.
    qint64 thresholdMs = 50;
};

// How long the watchdog waits before deciding the render cadence is gone.
//
// Four frames, floored at 50ms. A captured playback showed present gaps up to 61ms while the
// render loop was perfectly healthy, so a shorter threshold would let the watchdog inject
// off-phase samples during ordinary hiccups — reintroducing the very jitter this replaces.
// Past this point a frozen playhead is the worse failure and the fallback should take over.
inline qint64 watchdogThresholdMs(qint64 frameIntervalMs)
{
    const qint64 frame = qMax<qint64>(1, frameIntervalMs);
    return qMax<qint64>(50, frame * 4);
}

// True when the watchdog wakeup should perform the flush itself.
//
// Yields to the render cadence only when playback is running AND a cadence tick has actually
// been seen AND that tick is recent. While paused the watchdog is the only driver (the render
// loop is idle by design), so it always flushes — preserving the pre-existing paused-seek path.
inline bool watchdogShouldFlush(const ArbitrationState& state)
{
    if (!state.playing) {
        return true;
    }
    if (state.lastCadenceMs < 0) {
        return true;
    }
    const qint64 sinceCadenceMs = state.nowMs - state.lastCadenceMs;
    // A negative delta means the clock was restarted under us; treat it as "no evidence the
    // cadence is alive" rather than silently trusting a stale marker.
    if (sinceCadenceMs < 0) {
        return true;
    }
    return sinceCadenceMs >= state.thresholdMs;
}

}  // namespace miacode::timeline::cadence
