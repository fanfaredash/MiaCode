#include <QCoreApplication>
#include <QImage>
#include <QRectF>
#include <QTextStream>

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"
#include "common/PreviewGameplayConfig.h"

namespace {

using miacode::preview::scene::PreviewFrameState;
using miacode::preview::scene::PreviewJudgeFireworkLayerState;

constexpr qreal kEpsilon = 1e-3;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool requireNear(qreal actual, qreal expected, const QString& label, QTextStream& err)
{
    if (qAbs(actual - expected) <= kEpsilon) {
        return true;
    }
    err << label << " expected " << expected << " but got " << actual << Qt::endl;
    return false;
}

PreviewFrameState buildStateAt(qreal clipTimeSeconds)
{
    PreviewFrameState state;

    TimelineNoteMarker marker;
    marker.type = QStringLiteral("touch_hold");
    marker.isFirework = true;
    marker.endSecond = 0.0;
    marker.touchPoint = QPointF(270.0, 270.0);
    state.noteMarkers.append(marker);

    QImage colorBallImage(32, 32, QImage::Format_ARGB32_Premultiplied);
    colorBallImage.fill(qRgba(255, 255, 255, 255));
    state.judgeEffect.fireworkColorBallImage = colorBallImage;
    state.judgeEffect.fireworkColorBallSourceRect =
        QRectF(0.0, 0.0, colorBallImage.width(), colorBallImage.height());
    state.playheadSeconds = clipTimeSeconds;
    return state;
}

PreviewFrameState buildOffsetStateAt(qreal clipTimeSeconds)
{
    PreviewFrameState state = buildStateAt(clipTimeSeconds);
    state.noteMarkers[0].touchPoint = QPointF(360.0, 180.0);
    return state;
}

bool verifyVideoReferenceSample(
    qreal clipTimeSeconds,
    qreal expectedFireworkScale,
    qreal expectedFireworkAlpha,
    qreal expectedSmallScale,
    qreal expectedSmallAlpha,
    qreal expectedBigScale,
    qreal expectedBigAlpha,
    bool expectedDrawFirework,
    bool expectedDrawSmall,
    bool expectedDrawBig,
    bool expectedActive,
    QTextStream& err
)
{
    const PreviewFrameState state = buildStateAt(clipTimeSeconds);
    const PreviewJudgeFireworkLayerState layerState =
        miacode::preview::scene::buildPreviewJudgeFireworkLayerState(
            state,
            miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers),
            QRectF(0.0, 0.0, 540.0, 540.0)
        );

    const QString prefix = QStringLiteral("t=%1").arg(clipTimeSeconds, 0, 'f', 3);
    if (!require(layerState.active == expectedActive, prefix + QStringLiteral(" active mismatch"), err)) {
        return false;
    }
    if (!expectedActive) {
        return true;
    }
    if (!requireNear(layerState.fireworkScale, expectedFireworkScale, prefix + QStringLiteral(" stripe scale"), err)) {
        return false;
    }
    if (!requireNear(layerState.fireworkAlpha, expectedFireworkAlpha, prefix + QStringLiteral(" stripe alpha"), err)) {
        return false;
    }
    if (!requireNear(layerState.colorBallScale, expectedSmallScale, prefix + QStringLiteral(" small scale"), err)) {
        return false;
    }
    if (!requireNear(layerState.colorBallAlpha, expectedSmallAlpha, prefix + QStringLiteral(" small alpha"), err)) {
        return false;
    }
    if (!requireNear(layerState.colorBallBigScale, expectedBigScale, prefix + QStringLiteral(" big scale"), err)) {
        return false;
    }
    if (!requireNear(layerState.colorBallBigAlpha, expectedBigAlpha, prefix + QStringLiteral(" big alpha"), err)) {
        return false;
    }
    if (!requireNear(layerState.clipTimeSeconds, clipTimeSeconds, prefix + QStringLiteral(" explicit clip time"), err)) {
        return false;
    }
    if (!require(layerState.drawFirework == expectedDrawFirework, prefix + QStringLiteral(" drawFirework mismatch"), err)) {
        return false;
    }
    if (!require(layerState.drawColorBall == expectedDrawSmall, prefix + QStringLiteral(" drawColorBall mismatch"), err)) {
        return false;
    }
    if (!require(layerState.drawColorBallBig == expectedDrawBig, prefix + QStringLiteral(" drawColorBallBig mismatch"), err)) {
        return false;
    }
    return true;
}

// Samples correspond to the supplied 30 fps reference: center flash first, spokes at frame 4,
// full radial reach by frame 7, fade after frame 12, and no visible tail after frame 21.5.
bool verifyVideoReferenceAlignment(QTextStream& err)
{
    if (!verifyVideoReferenceSample(0.0, 0.0, 0.589, 0.2, 0.9, 1.0, 0.9, false, true, true, true, err)) {
        return false;
    }
    if (!verifyVideoReferenceSample(0.1, 0.0, 0.589, 0.38, 0.9, 1.05, 0.75, false, true, true, true, err)) {
        return false;
    }
    if (!verifyVideoReferenceSample(0.13333334, 0.6, 0.589, 0.44, 0.6333333, 1.0666667, 0.6, true, true, true, true, err)) {
        return false;
    }
    if (!verifyVideoReferenceSample(0.2, 3.5333332, 0.589, 0.5, 0.1, 1.1, 0.3, true, true, true, true, err)) {
        return false;
    }
    if (!verifyVideoReferenceSample(0.23333333, 5.0, 0.589, 0.5, 0.0666667, 1.1166667, 0.2666667, true, true, true, true, err)) {
        return false;
    }
    if (!verifyVideoReferenceSample(0.4, 5.0, 0.589, 0.5, 0.0, 1.15, 0.1, true, false, true, true, err)) {
        return false;
    }
    const qreal duration = static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds);
    const PreviewFrameState finalState = buildStateAt(duration);
    const PreviewJudgeFireworkLayerState finalLayer =
        miacode::preview::scene::buildPreviewJudgeFireworkLayerState(
            finalState,
            miacode::preview::scene::PreviewActiveMarkerView(finalState.noteMarkers),
            QRectF(0.0, 0.0, 540.0, 540.0)
        );
    if (!require(!finalLayer.active, QStringLiteral("video-reference tail should be gone at clip end"), err)) {
        return false;
    }

    return true;
}

bool verifyClipCenterStaysOnPlayfield(QTextStream& err)
{
    const PreviewFrameState state = buildOffsetStateAt(0.2);
    const PreviewJudgeFireworkLayerState layerState =
        miacode::preview::scene::buildPreviewJudgeFireworkLayerState(
            state,
            miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers),
            QRectF(0.0, 0.0, 540.0, 540.0)
        );
    if (!requireNear(layerState.center.x(), 360.0, QStringLiteral("offset firework center x"), err)) {
        return false;
    }
    if (!requireNear(layerState.center.y(), 180.0, QStringLiteral("offset firework center y"), err)) {
        return false;
    }
    if (!requireNear(layerState.clipCenter.x(), 270.0, QStringLiteral("stage clip center x"), err)) {
        return false;
    }
    if (!requireNear(layerState.clipCenter.y(), 270.0, QStringLiteral("stage clip center y"), err)) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyVideoReferenceAlignment(err)) {
        return 1;
    }
    if (!verifyClipCenterStaysOnPlayfield(err)) {
        return 1;
    }

    out << "preview_firework_lifecycle_spec ok" << Qt::endl;
    return 0;
}
