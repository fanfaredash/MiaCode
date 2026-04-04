#include "preview/scene/PreviewJudgeOverlayShared.h"

#include "common/MuriConfig.h"
#include "preview/scene/PreviewSceneMath.h"
#include "timeline/TimelineData.h"

#include <QStringList>
#include <QtMath>

namespace {

QPointF normalizedLogicalVector(const QPointF& vector)
{
    const qreal length = std::hypot(vector.x(), vector.y());
    if (length <= miacode::preview::scene::kRenderDurationEpsilon) {
        return QPointF(0.0, 0.0);
    }
    return QPointF(vector.x() / length, vector.y() / length);
}

}  // namespace

namespace miacode::preview::scene {

QString buildJudgeOverlayMarkerSourceSignature(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QStringList signatureParts;
    signatureParts.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        signatureParts.append(makeMarkerAnalysisKey(marker));
    }
    return signatureParts.join(QLatin1Char(';'));
}

QString lanePadToken(int lane)
{
    if (lane < 1 || lane > 8) {
        return QString();
    }
    return QStringLiteral("A%1").arg(lane);
}

QString slideHeadEventKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.eachGroupId);
}

qreal chartReviewSlideJudgeSecond(const TimelineNoteMarker& marker)
{
    if (marker.endSecond > marker.second) {
        return static_cast<qreal>(marker.endSecond);
    }
    if (marker.slideTraceSecond > marker.second) {
        return static_cast<qreal>(marker.slideTraceSecond);
    }
    return static_cast<qreal>(marker.second);
}

bool slideKeyUsesCcwJudgeSprite(const QString& key)
{
    if (key.isEmpty()) {
        return false;
    }
    int startLane = key.at(0).digitValue();
    if (startLane < 1 || startLane > 8) {
        startLane = 1;
    }
    const bool outerStart = startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8;
    if (key.contains(QLatin1Char('<'))) {
        return outerStart;
    }
    if (key.contains(QLatin1Char('>'))) {
        return !outerStart;
    }
    return false;
}

bool slideKeyUsesCwJudgeSprite(const QString& key)
{
    if (key.isEmpty()) {
        return false;
    }
    int startLane = key.at(0).digitValue();
    if (startLane < 1 || startLane > 8) {
        startLane = 1;
    }
    const bool outerStart = startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8;
    if (key.contains(QLatin1Char('>'))) {
        return outerStart;
    }
    if (key.contains(QLatin1Char('<'))) {
        return !outerStart;
    }
    return false;
}

QPointF slideEndTangentLogical(const TimelineNoteMarker& marker)
{
    if (!marker.slideSegmentPoints.isEmpty()) {
        const QVector<QPointF>& lastSegment = marker.slideSegmentPoints.constLast();
        if (lastSegment.size() >= 2) {
            return normalizedLogicalVector(lastSegment.constLast() - lastSegment.at(lastSegment.size() - 2));
        }
        if (!lastSegment.isEmpty()) {
            return normalizedLogicalVector(lastSegment.constLast());
        }
    }
    return normalizedLogicalVector(laneUnitVector(marker.endLane));
}

QPointF padUnitVectorForToken(const QString& pad)
{
    const QPointF center = miacode::muri::padCenter(pad);
    const QPointF offset = center - QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter);
    return normalizedLogicalVector(offset);
}

qreal judgeSimpleAngleDegrees(const QString& pad)
{
    if (pad.isEmpty() || pad == QLatin1String("C")) {
        return 0.0;
    }
    const int lane = wrappedLane(pad.mid(1).toInt());
    const int laneMod = lane & 7;
    const QChar ring = pad.at(0).toUpper();
    if (ring == QLatin1Char('A') || ring == QLatin1Char('B')) {
        return 22.5 - 45.0 * laneMod;
    }
    if (ring == QLatin1Char('D') || ring == QLatin1Char('E')) {
        return 45.0 - 45.0 * laneMod;
    }
    return 0.0;
}

bool buildJudgeOverlaySimplePlacement(const QString& pad, PreviewJudgeOverlayPlacement* placement)
{
    if (placement == nullptr || (!miacode::muri::padTokenIsValid(pad) && pad != QLatin1String("C"))) {
        return false;
    }
    const QPointF base = miacode::muri::padCenter(pad);
    const QPointF unit = padUnitVectorForToken(pad);
    placement->logicalCenter = base - unit * kMaimuriDxJudgeSimpleOffsetLogical;
    placement->logicalWidth = kMaimuriDxJudgeSimpleWidthLogical;
    placement->angleDegrees = judgeSimpleAngleDegrees(pad);
    return true;
}

bool buildJudgeOverlayStraightPlacement(
    const TimelineNoteMarker& marker,
    bool includeNegativeBoundary,
    PreviewJudgeOverlayPlacement* placement,
    bool* useRightImage
)
{
    if (placement == nullptr) {
        return false;
    }
    const QPointF tangent = slideEndTangentLogical(marker);
    const qreal tangentAngleDegrees = qRadiansToDegrees(qAtan2(tangent.y(), tangent.x()));
    const bool shouldUseRightImage = includeNegativeBoundary
        ? (tangentAngleDegrees >= -90.0 && tangentAngleDegrees <= 90.0)
        : (tangentAngleDegrees > -90.0 && tangentAngleDegrees <= 90.0);
    const QPointF laneUnit = laneUnitVector(qBound(1, marker.endLane, 8));
    placement->logicalCenter =
        QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter)
        + laneUnit * kLogicalDistanceEdge
        - tangent * kMaimuriDxJudgeStraightOffsetLogical;
    placement->logicalWidth = kMaimuriDxJudgeStraightWidthLogical;
    placement->angleDegrees = shouldUseRightImage ? -tangentAngleDegrees : (180.0 - tangentAngleDegrees);
    if (useRightImage != nullptr) {
        *useRightImage = shouldUseRightImage;
    }
    return true;
}

PreviewJudgeOverlayPlacement buildJudgeOverlayCircleCcwPlacement(int lane)
{
    PreviewJudgeOverlayPlacement placement;
    const QPointF unit = padUnitVectorForToken(padTokenForRing(QLatin1Char('E'), lane + 1));
    placement.logicalCenter = QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter) + unit * kMaimuriDxJudgeCircleDistanceLogical;
    placement.logicalWidth = kMaimuriDxJudgeCircleWidthLogical;
    placement.angleDegrees = 45.0 * (8 - wrappedLane(lane));
    return placement;
}

PreviewJudgeOverlayPlacement buildJudgeOverlayCircleCwPlacement(int lane)
{
    PreviewJudgeOverlayPlacement placement;
    const QPointF unit = padUnitVectorForToken(padTokenForRing(QLatin1Char('E'), lane));
    placement.logicalCenter = QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter) + unit * kMaimuriDxJudgeCircleDistanceLogical;
    placement.logicalWidth = kMaimuriDxJudgeCircleWidthLogical;
    placement.angleDegrees = -45.0 * (wrappedLane(lane) - 1);
    return placement;
}

PreviewJudgeOverlayPlacement buildJudgeOverlayWifiPlacement(int lane, bool useUpImage)
{
    PreviewJudgeOverlayPlacement placement;
    const int wrapped = wrappedLane(lane);
    const QPointF unit = laneUnitVector(wrapped);
    placement.logicalCenter = QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter) + unit * kMaimuriDxJudgeWifiDistanceLogical;
    placement.logicalWidth = kMaimuriDxJudgeWifiWidthLogical;
    placement.angleDegrees = useUpImage ? (22.5 - 45.0 * wrapped) : (202.5 - 45.0 * wrapped);
    return placement;
}

PreviewSpriteDescriptor buildJudgeOverlaySpriteDescriptor(
    const QImage* image,
    const QRectF& sourceRect,
    const PreviewJudgeOverlayPlacement& placement,
    qreal opacity,
    const QRectF& playfieldRect
)
{
    PreviewSpriteDescriptor sprite;
    if (image == nullptr || image->isNull() || placement.logicalWidth <= 0.0 || opacity <= kRenderDurationEpsilon) {
        return sprite;
    }

    const qreal sourceWidth = sourceRect.isValid() && !sourceRect.isEmpty()
        ? sourceRect.width()
        : static_cast<qreal>(image->width());
    const qreal sourceHeight = sourceRect.isValid() && !sourceRect.isEmpty()
        ? sourceRect.height()
        : static_cast<qreal>(image->height());
    if (sourceWidth <= 0.0 || sourceHeight <= 0.0) {
        return sprite;
    }

    const qreal targetWidth = qMax<qreal>(1.0, qRound(mapLogicalLengthToRect(placement.logicalWidth, playfieldRect)));
    const qreal targetHeight = qMax<qreal>(1.0, qRound(targetWidth * (sourceHeight / sourceWidth)));
    sprite.image = image;
    sprite.center = mapLogicalPointToRect(placement.logicalCenter, playfieldRect);
    sprite.width = targetWidth;
    sprite.height = targetHeight;
    sprite.rotationDegrees = -placement.angleDegrees;
    sprite.opacity = opacity;
    sprite.sourceRect = sourceRect;
    return sprite;
}

}  // namespace miacode::preview::scene
