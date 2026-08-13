#include <QTextStream>

#include "timeline/TimelineCadenceArbitrationPolicy.h"

namespace {

using namespace miacode::timeline::cadence;

bool expect(bool condition, const char* message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

ArbitrationState playing(qint64 lastCadenceMs, qint64 nowMs, qint64 thresholdMs = 67)
{
    ArbitrationState state;
    state.playing = true;
    state.lastCadenceMs = lastCadenceMs;
    state.nowMs = nowMs;
    state.thresholdMs = thresholdMs;
    return state;
}

// While paused the render loop is idle by design, so afterAnimating never arrives. The
// watchdog has to stay the driver or paused seeks would stop reaching the timeline.
bool verifyPausedAlwaysFlushes(QTextStream& err)
{
    ArbitrationState state;
    state.playing = false;
    state.lastCadenceMs = -1;
    state.nowMs = 10'000;
    bool ok = expect(watchdogShouldFlush(state), "paused with no cadence must flush", err);

    // Even a fresh cadence marker must not suppress the paused path.
    state.lastCadenceMs = 9'999;
    ok &= expect(watchdogShouldFlush(state), "paused with recent cadence must still flush", err);
    return ok;
}

// Playback start resets the marker to -1; until a real afterAnimating tick lands the watchdog
// is the only thing advancing the playhead, so it must not yield on the strength of a sentinel.
bool verifyUnprovenCadenceFlushes(QTextStream& err)
{
    return expect(
        watchdogShouldFlush(playing(-1, 5'000)),
        "playing with no cadence tick yet must flush",
        err);
}

// The core of the fix: a live cadence owns the sampling phase and the watchdog stays silent,
// because a watchdog sample lands at a drifting offset from the frame that presents it.
bool verifyLiveCadenceSuppressesWatchdog(QTextStream& err)
{
    bool ok = expect(
        !watchdogShouldFlush(playing(1'000, 1'000)),
        "cadence tick in the same millisecond must suppress the watchdog",
        err);
    ok &= expect(
        !watchdogShouldFlush(playing(1'000, 1'016)),
        "one frame of silence must suppress the watchdog",
        err);
    // 61ms was the worst present gap observed on a healthy 139s capture; the watchdog must
    // ride through that rather than injecting an off-phase sample.
    ok &= expect(
        !watchdogShouldFlush(playing(1'000, 1'061)),
        "a 61ms healthy present gap must still suppress the watchdog",
        err);
    return ok;
}

// A genuinely stopped cadence (hidden window, torn-down scene graph) must hand back over, or
// the playhead freezes.
bool verifyDeadCadenceHandsBackToWatchdog(QTextStream& err)
{
    bool ok = expect(
        watchdogShouldFlush(playing(1'000, 1'067)),
        "silence at exactly the threshold must flush",
        err);
    ok &= expect(
        watchdogShouldFlush(playing(1'000, 5'000)),
        "long silence must flush",
        err);
    return ok;
}

// QElapsedTimer is restarted across playback transactions; a stale marker from a previous
// epoch can read as "in the future". Trusting it would freeze the playhead indefinitely.
bool verifyClockRestartFlushes(QTextStream& err)
{
    return expect(
        watchdogShouldFlush(playing(9'000, 12)),
        "a cadence marker ahead of now must flush rather than be trusted",
        err);
}

bool verifyThresholdScalesWithFrameInterval(QTextStream& err)
{
    // 60Hz -> 16ms frames -> 64ms is below the 50ms floor's reach, so four frames wins.
    bool ok = expect(watchdogThresholdMs(16) == 64, "60Hz threshold is four frames", err);
    // 120Hz -> 8ms frames -> 32ms, which the floor lifts to 50ms so the watchdog still rides
    // through ordinary hiccups on high-refresh displays.
    ok &= expect(watchdogThresholdMs(8) == 50, "120Hz threshold is floored at 50ms", err);
    // 30Hz -> 33ms frames -> 132ms.
    ok &= expect(watchdogThresholdMs(33) == 132, "30Hz threshold is four frames", err);
    // Degenerate inputs must not produce a zero/negative window that disables the cadence.
    ok &= expect(watchdogThresholdMs(0) == 50, "zero frame interval falls back to the floor", err);
    ok &= expect(watchdogThresholdMs(-5) == 50, "negative frame interval falls back to the floor", err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;
    ok &= verifyPausedAlwaysFlushes(err);
    ok &= verifyUnprovenCadenceFlushes(err);
    ok &= verifyLiveCadenceSuppressesWatchdog(err);
    ok &= verifyDeadCadenceHandsBackToWatchdog(err);
    ok &= verifyClockRestartFlushes(err);
    ok &= verifyThresholdScalesWithFrameInterval(err);
    if (ok) {
        out << "timeline_cadence_arbitration_policy_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
