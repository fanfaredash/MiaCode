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

// Splits the slide-judge layers' output by what they draw, so the slide-shape
// "just" rings (just_curv/just_str/just_wifi) can render on a lower slot than
// the per-note judge TEXT. This mirrors MajdataPlay, where the Slide-OK sprites
// sit below the notes/judge-text while the judge text stays on top.
enum class SlideJudgeRenderGroup {
    All,            // draw everything (back-compat default)
    JudgeTextOnly,  // only the Simple Perfect/Good/Break text sprites
    SlideShapeOnly, // only the slide-shape "just" ring sprites
};

// Judge text alpha curve matches MajdataPlay JudgePerfect.anim m_Color.a on JudgeCPNormal (60 fps keys: hold 0..10, fade-out 10..16, lifetime gate 21). Scale-pop entrance and break flash are modelled below (see kMaimuriDxSimpleJudgeScalePop*/BreakFlash*).
inline constexpr qreal kMaimuriDxJudgeLifetimeSeconds = static_cast<qreal>(63.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxJudgeFadeOutStartSeconds = static_cast<qreal>(30.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSimpleJudgeFadeInSeconds = 0.0;
inline constexpr qreal kMaimuriDxSimpleJudgeFadeOutEndSeconds = static_cast<qreal>(48.0 / miacode::muri::kJudgeTps);
// Scale-pop entrance ports MajdataPlay JudgePerfect.anim m_LocalScale on JudgeObject
// (60 fps keys: 1.2 @0 -> 1.0 @4, linear since outSlope -3.0 == secant). Holds 1.0 afterward.
inline constexpr qreal kMaimuriDxSimpleJudgeScalePopStartScale = 1.2;
inline constexpr qreal kMaimuriDxSimpleJudgeScalePopEndScale = 1.0;
inline constexpr qreal kMaimuriDxSimpleJudgeScalePopEndSeconds = static_cast<qreal>(12.0 / miacode::muri::kJudgeTps);
// Break flash ports MajdataPlay JudgeBreak.anim m_Color.a on JudgeCPBreak: a 0<->1 triangle,
// period 4 @60fps (= 12 @180tps), running until the word's fade-out end (16 @60fps = 48 @180tps).
// MiaCode has a single break word sprite (no separate outline overlay), so the flash modulates the
// word's own alpha between a readable floor and 1.0 rather than compositing an additive overlay.
inline constexpr qreal kMaimuriDxSimpleJudgeBreakFlashPeriodSeconds = static_cast<qreal>(12.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSimpleJudgeBreakFlashEndSeconds = static_cast<qreal>(48.0 / miacode::muri::kJudgeTps);
inline constexpr qreal kMaimuriDxSimpleJudgeBreakFlashFloor = 0.45;
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
    const QRectF& playfieldRect,
    qreal scale = 1.0
);

}  // namespace miacode::preview::scene
