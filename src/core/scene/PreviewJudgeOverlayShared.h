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

// Judge text alpha curve matches MajdataPlay JudgePerfect.anim m_Color.a on JudgeCPNormal (60 fps keys: hold 0..10, fade-out 10..16, lifetime gate 21). Scale-pop entrance is not modelled.
inline constexpr qreal kMaimuriDxJudgeLifetimeSeconds = static_cast<qreal>(63.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxJudgeFadeOutStartSeconds = static_cast<qreal>(30.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSimpleJudgeFadeInSeconds = 0.0;
inline constexpr qreal kMaimuriDxSimpleJudgeFadeOutEndSeconds = static_cast<qreal>(48.0 / miacode::muri::kJudgeTps);
// Slide-OK timing matches MajdataPlay StarOver.anim modern variant (60 fps keyframes: fade-in 0..2, hold 2..17, fade-out 17..25, lifetime gate 30).
inline constexpr qreal kMaimuriDxSlideJudgeLifetimeSeconds = static_cast<qreal>(90.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSlideJudgeFadeInEndSeconds = static_cast<qreal>(6.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSlideJudgeFadeOutStartSeconds = static_cast<qreal>(51.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSlideJudgeFadeOutEndSeconds = static_cast<qreal>(75.0 / miacode::muri::kJudgeTps);
// Bound for culling/cache windows: must cover the longest-lived judge kind so per-kind alpha curves can finish naturally.
inline constexpr qreal kMaimuriDxJudgeMaxLifetimeSeconds =
    kMaimuriDxJudgeLifetimeSeconds > kMaimuriDxSlideJudgeLifetimeSeconds
        ? kMaimuriDxJudgeLifetimeSeconds
        : kMaimuriDxSlideJudgeLifetimeSeconds;
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
