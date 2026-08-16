#include <QCoreApplication>
#include <QRectF>
#include <QTextStream>

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewFireworkWarmupPolicy.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"
#include "common/PreviewGameplayConfig.h"

namespace {

using miacode::preview::scene::fireworkWarmupMarkerSecond;
using miacode::preview::scene::fireworkWarmupNeedsRecenter;
using miacode::preview::scene::kFireworkWarmupBackwardSlackSeconds;
using miacode::preview::scene::kFireworkWarmupForwardSlackSeconds;
using miacode::preview::scene::PreviewFrameState;
using miacode::preview::scene::PreviewJudgeFireworkLayerState;

bool expect(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
        return false;
    }
    return true;
}

// Build the frame state the runtime would produce for a synthetic centred at
// `centeredAt`, then ask the real layer builder whether it draws at `playhead`.
// This is the property the warm-up depends on: a synthetic the layer refuses to
// draw can never confirm the PSO compile.
bool syntheticDrawsAt(double centeredAt, double playhead)
{
    PreviewFrameState state;

    TimelineNoteMarker synth;
    synth.type = QStringLiteral("touch");
    synth.isFirework = true;
    synth.second = fireworkWarmupMarkerSecond(centeredAt);
    synth.endSecond = -1.0;
    synth.touchPoint = QPointF(-1.0e6, -1.0e6);
    synth.lane = 1;
    state.noteMarkers.append(synth);

    QImage colorBallImage(8, 8, QImage::Format_ARGB32_Premultiplied);
    colorBallImage.fill(qRgba(255, 255, 255, 255));
    state.judgeEffect.fireworkColorBallImage = colorBallImage;
    state.judgeEffect.fireworkColorBallSourceRect =
        QRectF(0.0, 0.0, colorBallImage.width(), colorBallImage.height());
    state.playheadSeconds = playhead;

    const PreviewJudgeFireworkLayerState layerState =
        miacode::preview::scene::buildPreviewJudgeFireworkLayerState(
            state,
            miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers),
            QRectF(0.0, 0.0, 540.0, 540.0));
    return layerState.active;
}

// The declared slack must match what the layer builder actually accepts, or the
// threshold is calibrated against a fiction.
bool verifySlackMatchesLayerLifecycle(QTextStream& err)
{
    constexpr double kCenter = 30.0;
    constexpr double kInsideMargin = 0.02;

    bool ok = true;
    ok &= expect(syntheticDrawsAt(kCenter, kCenter),
                 "synthetic draws at its own centre", err);
    ok &= expect(syntheticDrawsAt(kCenter, kCenter + kFireworkWarmupForwardSlackSeconds - kInsideMargin),
                 "synthetic still draws just inside the forward slack", err);
    ok &= expect(!syntheticDrawsAt(kCenter, kCenter + kFireworkWarmupForwardSlackSeconds + kInsideMargin),
                 "synthetic stops drawing past the forward slack", err);
    ok &= expect(syntheticDrawsAt(kCenter, kCenter - kFireworkWarmupBackwardSlackSeconds + kInsideMargin),
                 "synthetic still draws just inside the backward slack", err);
    ok &= expect(!syntheticDrawsAt(kCenter, kCenter - kFireworkWarmupBackwardSlackSeconds - kInsideMargin),
                 "synthetic stops drawing past the backward slack", err);
    return ok;
}

// Every re-centre decision must fire while the synthetic is STILL drawable,
// otherwise the warm-up can stall in a window where it cannot confirm.
bool verifyRecenterHappensBeforeTheEdge(QTextStream& err)
{
    constexpr double kCenter = 12.5;
    bool ok = true;

    ok &= expect(!fireworkWarmupNeedsRecenter(kCenter, kCenter),
                 "no re-centre needed at the centre", err);

    // Sample the whole reachable range; wherever the policy says "keep it", the
    // layer must agree the synthetic is still drawable.
    for (int step = 0; step <= 400; ++step) {
        const double travel = -kFireworkWarmupBackwardSlackSeconds
            + (static_cast<double>(step) / 400.0)
                * (kFireworkWarmupForwardSlackSeconds + kFireworkWarmupBackwardSlackSeconds);
        const double playhead = kCenter + travel;
        if (fireworkWarmupNeedsRecenter(playhead, kCenter)) {
            continue;
        }
        if (!syntheticDrawsAt(kCenter, playhead)) {
            ok &= expect(false,
                         QStringLiteral("policy kept a synthetic the layer will not draw at travel=%1")
                             .arg(travel, 0, 'f', 4),
                         err);
            break;
        }
    }
    return ok;
}

// A seek is exactly the case the contract in cross-chain-linkage.md calls out:
// it must still force a re-centre, in both directions.
bool verifySeeksForceRecenter(QTextStream& err)
{
    constexpr double kCenter = 60.0;
    bool ok = true;
    ok &= expect(fireworkWarmupNeedsRecenter(kCenter + 5.0, kCenter),
                 "forward seek forces a re-centre", err);
    ok &= expect(fireworkWarmupNeedsRecenter(kCenter - 5.0, kCenter),
                 "backward seek forces a re-centre", err);
    ok &= expect(fireworkWarmupNeedsRecenter(0.0, kCenter),
                 "jump to chart start forces a re-centre", err);
    // Negative pre-roll: playhead sits left of chart 0.
    ok &= expect(fireworkWarmupNeedsRecenter(-2.0, kCenter),
                 "negative pre-roll forces a re-centre", err);
    return ok;
}

// The regression this policy exists to prevent: steady playback must NOT
// re-centre on every frame. At 60 Hz and 1.0x, consecutive frames advance
// ~16.7 ms, which has to be far below the threshold.
bool verifySteadyPlaybackDoesNotThrash(QTextStream& err)
{
    constexpr double kCenter = 45.0;
    constexpr double kFrameSeconds = 1.0 / 60.0;
    bool ok = true;

    ok &= expect(!fireworkWarmupNeedsRecenter(kCenter + kFrameSeconds, kCenter),
                 "a single 60 Hz frame does not force a re-centre", err);
    ok &= expect(!fireworkWarmupNeedsRecenter(kCenter + kFrameSeconds * 4.0, kCenter),
                 "four 180 Hz-equivalent frames do not force a re-centre", err);

    // Count re-centres over one second of 1.0x playback sampled at 180 Hz.
    int recenters = 0;
    double center = kCenter;
    for (int frame = 1; frame <= 180; ++frame) {
        const double playhead = kCenter + static_cast<double>(frame) / 180.0;
        if (fireworkWarmupNeedsRecenter(playhead, center)) {
            ++recenters;
            center = playhead;
        }
    }
    ok &= expect(recenters <= 4,
                 QStringLiteral("one second of 180 Hz playback re-centres at most 4 times (got %1)")
                     .arg(recenters),
                 err);
    ok &= expect(recenters >= 1,
                 QStringLiteral("one second of playback still re-centres at least once (got %1)")
                     .arg(recenters),
                 err);
    return ok;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    bool ok = true;
    ok &= verifySlackMatchesLayerLifecycle(err);
    ok &= verifyRecenterHappensBeforeTheEdge(err);
    ok &= verifySeeksForceRecenter(err);
    ok &= verifySteadyPlaybackDoesNotThrash(err);

    if (!ok) {
        return 1;
    }
    out << "preview_firework_warmup_policy_spec ok" << Qt::endl;
    return 0;
}
