#include <QCoreApplication>
#include <QImage>
#include <QRectF>
#include <QTextStream>

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"

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

bool verifyLegacyAlignedSample(
    qreal clipTimeSeconds,
    qreal expectedFireworkScale,
    qreal expectedFireworkAlpha,
    qreal expectedSmallScale,
    qreal expectedSmallAlpha,
    qreal expectedBigScale,
    qreal expectedBigAlpha,
    qreal expectedHoleRadius,
    qreal expectedHoleMaskRadius,
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
    if (!requireNear(layerState.holeRadius, expectedHoleRadius, prefix + QStringLiteral(" hole radius"), err)) {
        return false;
    }
    if (!requireNear(layerState.holeMaskRadius, expectedHoleMaskRadius, prefix + QStringLiteral(" hole mask radius"), err)) {
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

bool verifyLegacyDev5Alignment(QTextStream& err)
{
    if (!verifyLegacyAlignedSample(0.0, 0.0, 0.7068, 0.2, 1.0, 1.0, 1.0, 12.906, 14.906, false, true, true, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(0.05, 0.0, 0.7068, 0.35, 0.93, 1.025, 0.876, 13.8359881, 15.8359881, false, true, true, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(0.1, 0.0, 0.7068, 0.5, 0.66, 1.05, 0.552, 16.5305691, 18.5305691, false, true, true, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(0.13333334, 0.6, 0.7068, 0.5, 0.48, 1.0666667, 0.336, 19.2366320, 21.2564784, true, true, true, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(0.2, 1.0333333, 0.7068, 0.5, 0.12, 1.1, 0.114419, 26.6412092, 29.4385362, true, true, true, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(0.5, 2.1590909, 0.7068, 0.5, 0.0, 1.15, 0.0641860, 84.4435487, 93.3101213, true, false, true, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(0.8, 3.1818182, 0.2912092, 0.5, 0.0, 1.15, 0.0139535, 159.4149022, 176.1534670, true, false, true, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(1.0, 3.8636364, 0.1137120, 0.5, 0.0, 1.15, 0.0, 203.6728030, 225.0584473, true, false, false, true, err)) {
        return false;
    }
    if (!verifyLegacyAlignedSample(1.2, 4.5454545, 0.0250920, 0.5, 0.0, 1.15, 0.0, 232.6693625, 257.0996456, true, false, false, true, err)) {
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

    if (!verifyLegacyDev5Alignment(err)) {
        return 1;
    }
    if (!verifyClipCenterStaysOnPlayfield(err)) {
        return 1;
    }

    out << "preview_firework_lifecycle_spec ok" << Qt::endl;
    return 0;
}
