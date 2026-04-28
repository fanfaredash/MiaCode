#pragma once

#include "common/MuriConfig.h"
#include "common/MuriTypes.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSpriteDescriptor.h"

#include <QImage>
#include <QPointF>
#include <QString>

struct TimelineNoteMarker;

namespace miacode::preview::scene {

inline constexpr qreal kMaimuriDxJudgeLifetimeSeconds = static_cast<qreal>(90.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxJudgeFadeOutStartSeconds = static_cast<qreal>(45.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSimpleJudgeFadeInSeconds = static_cast<qreal>(22.5 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxJudgeSimpleWidthLogical = 112.0;
inline constexpr qreal kMaimuriDxJudgeStraightWidthLogical = 214.0;
inline constexpr qreal kMaimuriDxJudgeCircleWidthLogical = 205.0;
inline constexpr qreal kMaimuriDxJudgeWifiWidthLogical = 334.0;
inline constexpr qreal kMaimuriDxJudgeSimpleOffsetLogical = kLogicalCanvasSize * 80.0 / 1080.0;
inline constexpr qreal kMaimuriDxJudgeStraightOffsetLogical = kLogicalCanvasSize * 220.0 / 1080.0;
inline constexpr qreal kMaimuriDxJudgeCircleDistanceLogical = kLogicalCanvasSize * 463.0 / 1080.0;
inline constexpr qreal kMaimuriDxJudgeWifiDistanceLogical = kLogicalCanvasSize * 406.0 / 1080.0;

struct PreviewJudgeOverlayPlacement {
    QPointF logicalCenter;
    qreal logicalWidth = 0.0;
    qreal angleDegrees = 0.0;
};

QString buildJudgeOverlayMarkerSourceSignature(const QVector<TimelineNoteMarker>& noteMarkers);
QString lanePadToken(int lane);
QString slideHeadEventKey(const TimelineNoteMarker& marker);
qreal chartReviewSlideJudgeSecond(const TimelineNoteMarker& marker);
bool slideKeyUsesCcwJudgeSprite(const QString& key);
bool slideKeyUsesCwJudgeSprite(const QString& key);
QPointF slideEndTangentLogical(const TimelineNoteMarker& marker);
QPointF padUnitVectorForToken(const QString& pad);
qreal judgeSimpleAngleDegrees(const QString& pad);
bool buildJudgeOverlaySimplePlacement(const QString& pad, PreviewJudgeOverlayPlacement* placement);
bool buildJudgeOverlayStraightPlacement(
    const TimelineNoteMarker& marker,
    bool includeNegativeBoundary,
    PreviewJudgeOverlayPlacement* placement,
    bool* useRightImage = nullptr
);
PreviewJudgeOverlayPlacement buildJudgeOverlayCircleCcwPlacement(int lane);
PreviewJudgeOverlayPlacement buildJudgeOverlayCircleCwPlacement(int lane);
PreviewJudgeOverlayPlacement buildJudgeOverlayWifiPlacement(int lane, bool useUpImage);
PreviewSpriteDescriptor buildJudgeOverlaySpriteDescriptor(
    const QImage* image,
    const QRectF& sourceRect,
    const PreviewJudgeOverlayPlacement& placement,
    qreal opacity,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
