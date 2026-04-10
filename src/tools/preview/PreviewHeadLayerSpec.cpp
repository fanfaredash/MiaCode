#include <QCoreApplication>
#include <QImage>
#include <QRectF>
#include <QTextStream>

#include "preview/scene/PreviewHeadLayerState.h"
#include "preview/scene/PreviewOpacityCurves.h"
#include "preview/scene/PreviewSceneConstants.h"
#include "preview/scene/PreviewSceneMath.h"
#include "simai/parser/SimaiNativeParser.h"

namespace {

using miacode::preview::scene::PreviewFrameState;
using miacode::preview::scene::PreviewHeadLayerState;
using miacode::preview::scene::PreviewTapTiming;

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

QImage solidImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(qRgba(255, 255, 255, 255));
    return image;
}

qreal totalSlideTraceDurationSeconds(const TimelineNoteMarker& marker)
{
    qreal duration = 0.0;
    for (double segmentDuration : marker.slideSegmentDurations) {
        duration += static_cast<qreal>(qMax(0.0, segmentDuration));
    }
    if (duration > 0.0) {
        return duration;
    }
    if (marker.endSecond > marker.slideTraceSecond) {
        return static_cast<qreal>(marker.endSecond - marker.slideTraceSecond);
    }
    return 0.0;
}

qreal slideHeadRotateSpeedDegreesPerSecond(const TimelineNoteMarker& marker)
{
    const qreal totalLen = static_cast<qreal>(marker.slideNativeTrackLength);
    const qreal totalDuration = totalSlideTraceDurationSeconds(marker);
    if (totalLen <= 0.0 || totalDuration <= 0.0) {
        return 0.0;
    }
    return qMax<qreal>(-4.500 * totalLen / totalDuration, -1080.0);
}

qreal slideHeadFallRotationDegrees(
    const TimelineNoteMarker& marker,
    qreal deltaSeconds,
    qreal tapLifecycleDurationSeconds
)
{
    if (deltaSeconds >= 0.0 || tapLifecycleDurationSeconds <= 0.0) {
        return 0.0;
    }
    const qreal elapsedSeconds =
        qBound<qreal>(0.0, deltaSeconds + tapLifecycleDurationSeconds, tapLifecycleDurationSeconds);
    return slideHeadRotateSpeedDegreesPerSecond(marker) * elapsedSeconds;
}

qreal mirroredStarAngleDegrees(qreal angleDegrees)
{
    return 360.0 - angleDegrees;
}

bool verifySameHeadBranchesCollapseToFastestRotation(QTextStream& err)
{
    const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
        QStringLiteral("(120){8}1-3[8:1]*-4[8:2]*-5[8:3],\nE")
    );
    if (!require(parsed.ok, QStringLiteral("branching slide chart parses"), err)) {
        return false;
    }

    QVector<const TimelineNoteMarker*> slideMarkers;
    for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
        if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
            slideMarkers.append(&marker);
        }
    }
    if (!require(slideMarkers.size() == 3, QStringLiteral("branching slide chart emits three slide markers"), err)) {
        return false;
    }

    const TimelineNoteMarker* fastestMarker = slideMarkers.first();
    qreal fastestSpeed = slideHeadRotateSpeedDegreesPerSecond(*fastestMarker);
    for (const TimelineNoteMarker* marker : slideMarkers) {
        const qreal speed = slideHeadRotateSpeedDegreesPerSecond(*marker);
        if (speed < fastestSpeed) {
            fastestSpeed = speed;
            fastestMarker = marker;
        }
    }
    if (!require(fastestMarker != nullptr, QStringLiteral("fastest branching slide marker is found"), err)) {
        return false;
    }

    PreviewFrameState state;
    state.noteMarkers = parsed.noteMarkers;
    state.skin.tapImage = solidImage(128, 128);
    state.skin.starImage = solidImage(126, 126);

    const PreviewTapTiming tapTiming =
        miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(state.render.noteFlowSpeed));
    state.playheadSeconds = fastestMarker->second - tapTiming.flyDurationSeconds * 0.5;

    const PreviewHeadLayerState layerState =
        miacode::preview::scene::buildPreviewHeadLayerState(
            state,
            QRectF(
                0.0,
                0.0,
                miacode::preview::scene::kLogicalCanvasSize,
                miacode::preview::scene::kLogicalCanvasSize)
        );

    if (!require(layerState.sprites.size() == 1, QStringLiteral("same-head falling slide renders exactly one head sprite"), err)) {
        return false;
    }

    const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - fastestMarker->second);
    const qreal expectedRotation =
        mirroredStarAngleDegrees(
            miacode::preview::scene::laneRotationDegrees(fastestMarker->lane)
            + slideHeadFallRotationDegrees(*fastestMarker, deltaSeconds, tapTiming.lifecycleDurationSeconds)
        );
    if (!requireNear(
            layerState.sprites.constFirst().rotationDegrees,
            expectedRotation,
            QStringLiteral("same-head falling slide rotation"),
            err)) {
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

    if (!verifySameHeadBranchesCollapseToFastestRotation(err)) {
        return 1;
    }

    out << "preview_head_layer_spec ok" << Qt::endl;
    return 0;
}
