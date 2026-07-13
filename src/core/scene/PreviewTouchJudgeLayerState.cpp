#include "core/scene/PreviewTouchJudgeLayerState.h"

#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>
#include <array>

namespace {

constexpr qreal kJudgeEffectTouchFallbackPixels = 96.0;
constexpr qreal kJudgeEffectTouchMinUnitPixels = 6.0;
constexpr qreal kJudgeEffectTouchCircleContentRadiusRatio = 118.0 / 512.0;
constexpr qreal kJudgeEffectTouchTextureOuterDiagonalAngleDegrees = -45.0;
constexpr qreal kJudgeEffectTouchSparkleOuterRadiusScale = 0.5;
constexpr qreal kJudgeEffectTouchSparkleInnerRadiusScale = 0.42;
constexpr int kJudgeEffectTouchSparklePointCount = 8;
constexpr qreal kJudgeEffectTouchSparklePointAngleStepDegrees = 45.0;
constexpr qreal kJudgeEffectTouchSparkleGlowAlphaScale = 0.38;
constexpr qreal kJudgeEffectTouchSparkleGlowScale = 1.22;
constexpr qreal kJudgeEffectTouchCircleOuterAlphaScale = 0.45;
constexpr qreal kJudgeEffectTouchCircleCoreAlphaScale = 0.95;
constexpr qreal kJudgeEffectTouchOuterCardinalAxisScaleX = 1.18;
constexpr qreal kJudgeEffectTouchOuterCardinalAxisScaleY = 0.85;
constexpr qreal kJudgeEffectTouchDurationSeconds =
    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectTouchDurationSeconds);
// Unity touchPerfect.anim StopTime (m_SampleRate 60): the circle and every star part fade
// their m_Color.a 1->0 over [0, this] and the clip ends here. (Was a 0.25 s hard destroy
// that popped the still-opaque sparkles off-screen; they now fade out like the reference.)
constexpr qreal kJudgeEffectTouchFadeEndSeconds = 0.31666666;
constexpr qreal kJudgeEffectTouchCircleFadeEndSeconds = kJudgeEffectTouchFadeEndSeconds * 0.5;
constexpr qreal kJudgeEffectTouchUnitRelativeToTouch = 3.125;
constexpr qreal kJudgeEffectTouchOuterScaleBase = 0.282;
constexpr qreal kJudgeEffectTouchInnerToOuterSpriteScale = 0.5;
constexpr qreal kJudgeEffectTouchSolidSpriteScale = 1.16;
constexpr qreal kJudgeEffectTouchOuterSpriteGrowScale = 1.02;
constexpr qreal kJudgeEffectTouchPartSpriteWidthUnits = 103.948685 / 100.0;
constexpr qreal kJudgeEffectTouchInnerRingRadiusUnits = 0.24;
constexpr qreal kJudgeEffectTouchOuterRingRadiusUnits = 0.5;
constexpr qreal kJudgeEffectTouchTriggerSecondQuantize = 1000000.0;
constexpr quint64 kJudgeEffectTouchRotationHashMultiplier = 0x9e3779b97f4a7c15ULL;
const QColor kJudgeEffectTouchCircleTint = QColor::fromRgbF(1.0, 0.995, 0.35, 1.0);
const QColor kJudgeEffectTouchPartTint = QColor::fromRgb(0xF6, 0xC9, 0x04);
const std::array<qreal, 5> kJudgeEffectTouchLayoutRotationDegrees = {{
    -45.0,
    -22.5,
    0.0,
    22.5,
    45.0,
}};

struct ScalarCurveKey {
    qreal time = 0.0;
    qreal value = 0.0;
};

const std::array<ScalarCurveKey, 3> kJudgeEffectTouchCircleScaleKeys = {{
    {0.0, 0.18},
    {0.08333333, 0.21},
    {0.15833333, 0.24},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchInnerParentScaleKeys = {{
    {0.0, 0.9},
    {0.31666666, 1.15},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchOuterRadiusScaleKeys = {{
    {0.0, 0.82},
    {0.31666666, 1.22},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchOuterSpriteScaleKeys = {{
    {0.0, 1.0},
    {0.31666666, kJudgeEffectTouchOuterSpriteGrowScale},
}};

template <std::size_t N>
qreal sampleScalarCurve(const std::array<ScalarCurveKey, N>& keys, qreal time)
{
    if (keys.empty()) {
        return 0.0;
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const ScalarCurveKey& next = keys[i];
        if (time > next.time) {
            continue;
        }
        const ScalarCurveKey& prev = keys[i - 1];
        const qreal span = next.time - prev.time;
        if (qFuzzyIsNull(span)) {
            return next.value;
        }
        const qreal t = (time - prev.time) / span;
        return prev.value + (next.value - prev.value) * t;
    }
    return keys.back().value;
}

qreal judgeEffectTouchClipTime(qreal elapsedSeconds)
{
    return qBound<qreal>(0.0, elapsedSeconds, kJudgeEffectTouchDurationSeconds);
}

QImage buildTouchJudgeCircleFallbackImage()
{
    constexpr int kSize = 512;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal radius = 118.0;

    QColor ringOuter = kJudgeEffectTouchCircleTint;
    ringOuter.setAlphaF(kJudgeEffectTouchCircleOuterAlphaScale);
    QColor ringCore = kJudgeEffectTouchCircleTint;
    ringCore.setAlphaF(kJudgeEffectTouchCircleCoreAlphaScale);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ringOuter, 26.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(ringCore, 14.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, radius, radius);
    painter.end();
    return image;
}

QImage buildTouchJudgePartFallbackImage(qreal axisScaleX, qreal axisScaleY)
{
    constexpr int kSize = 192;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal baseSize = 54.0;
    const qreal outer = baseSize * kJudgeEffectTouchSparkleOuterRadiusScale;
    const qreal inner = outer * kJudgeEffectTouchSparkleInnerRadiusScale;

    QPainterPath path;
    for (int pointIndex = 0; pointIndex < kJudgeEffectTouchSparklePointCount; ++pointIndex) {
        const qreal radius = (pointIndex % 2 == 0) ? outer : inner;
        const qreal radians =
            qDegreesToRadians(static_cast<qreal>(pointIndex) * kJudgeEffectTouchSparklePointAngleStepDegrees);
        const QPointF point(
            center.x() + qCos(radians) * radius * axisScaleX,
            center.y() + qSin(radians) * radius * axisScaleY
        );
        if (pointIndex == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }
    path.closeSubpath();

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    QColor glow = kJudgeEffectTouchPartTint;
    glow.setAlphaF(kJudgeEffectTouchSparkleGlowAlphaScale);
    painter.setBrush(glow);
    QTransform grow;
    grow.translate(center.x(), center.y());
    grow.scale(kJudgeEffectTouchSparkleGlowScale, kJudgeEffectTouchSparkleGlowScale);
    grow.translate(-center.x(), -center.y());
    painter.drawPath(grow.map(path));

    painter.setBrush(kJudgeEffectTouchPartTint);
    painter.drawPath(path);
    painter.end();
    return image;
}

const QImage& fallbackTouchJudgeCircleImage()
{
    static const QImage image = buildTouchJudgeCircleFallbackImage();
    return image;
}

const QImage& fallbackTouchJudgePart01Image()
{
    static const QImage image = buildTouchJudgePartFallbackImage(
        kJudgeEffectTouchOuterCardinalAxisScaleX,
        kJudgeEffectTouchOuterCardinalAxisScaleY
    );
    return image;
}

const QImage& fallbackTouchJudgePart02Image()
{
    static const QImage image = buildTouchJudgePartFallbackImage(1.0, 1.0);
    return image;
}

qreal touchJudgeLayoutRotationDegrees(qreal triggerSecond)
{
    const qint64 quantizedSecond = qRound64(triggerSecond * kJudgeEffectTouchTriggerSecondQuantize);
    quint64 hash = static_cast<quint64>(quantizedSecond) + kJudgeEffectTouchRotationHashMultiplier;
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
    hash ^= hash >> 31;
    const qsizetype rotationIndex =
        static_cast<qsizetype>(hash % static_cast<quint64>(kJudgeEffectTouchLayoutRotationDegrees.size()));
    return kJudgeEffectTouchLayoutRotationDegrees.at(rotationIndex);
}

QPointF sparkleRingOffset(qreal radiusUnits, int pointIndex, qreal layoutRotationDegrees)
{
    const qreal angleDegrees =
        layoutRotationDegrees + static_cast<qreal>(pointIndex) * kJudgeEffectTouchSparklePointAngleStepDegrees;
    const qreal radians = qDegreesToRadians(angleDegrees);
    return QPointF(qCos(radians) * radiusUnits, qSin(radians) * radiusUnits);
}

qreal sparkleRingSpriteRotationDegrees(int pointIndex, qreal layoutRotationDegrees)
{
    const bool diagonalLocalPoint = (pointIndex % 2) != 0;
    return layoutRotationDegrees
        + (diagonalLocalPoint ? kJudgeEffectTouchTextureOuterDiagonalAngleDegrees : 0.0);
}

}  // namespace

namespace miacode::preview::scene {

PreviewTouchJudgeLayerState buildPreviewTouchJudgeLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewTouchJudgeLayerState layerState;

    struct TouchJudgeTrigger {
        const TimelineNoteMarker* marker = nullptr;
        qreal second = -1.0;
    };

    QHash<quint64, TouchJudgeTrigger> positionTriggers;
    positionTriggers.reserve(kPreviewSmallCollectionReserve);
    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        if (marker.type != QLatin1String("touch")) {
            continue;
        }
        if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
            continue;
        }
        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectTouchDurationSeconds) {
            continue;
        }
        const quint64 key = touchPointKey(marker.touchPoint);
        const auto existing = positionTriggers.constFind(key);
        if (existing == positionTriggers.constEnd() || marker.second >= existing->second) {
            TouchJudgeTrigger trigger;
            trigger.marker = &marker;
            trigger.second = static_cast<qreal>(marker.second);
            positionTriggers.insert(key, trigger);
        }
    }
    if (positionTriggers.isEmpty()) {
        return layerState;
    }

    layerState.sprites.reserve(positionTriggers.size() * 17);
    const qreal canvasScale = qMin(playfieldRect.width(), playfieldRect.height()) / kLogicalCanvasSize;
    const qreal fallbackTouchPixels = kJudgeEffectTouchFallbackPixels * kTouchAssetScale * canvasScale;
    const qreal touchBasePixels = (!state.skin.touchPointImage.isNull())
        ? (state.skin.touchPointImage.width() * kTouchAssetScale * canvasScale)
        : fallbackTouchPixels;
    const qreal prefabUnitPixels =
        qMax<qreal>(kJudgeEffectTouchMinUnitPixels, touchBasePixels * kJudgeEffectTouchUnitRelativeToTouch);

    const QImage* circleImage = state.judgeEffect.touchCircleImage.isNull()
        ? &fallbackTouchJudgeCircleImage()
        : &state.judgeEffect.touchCircleImage;
    const QImage* part01Image = state.judgeEffect.touchPart01Image.isNull()
        ? &fallbackTouchJudgePart01Image()
        : &state.judgeEffect.touchPart01Image;
    const QImage* part02Image = state.judgeEffect.touchPart02Image.isNull()
        ? &fallbackTouchJudgePart02Image()
        : &state.judgeEffect.touchPart02Image;

    const auto appendSprite = [&](const QImage* image,
                                  const QPointF& center,
                                  qreal widthPixels,
                                  qreal angleDegrees,
                                  qreal opacity) {
        if (image == nullptr || image->isNull() || widthPixels <= 0.0 || opacity <= kRenderDurationEpsilon) {
            return;
        }
        const qreal aspect = image->width() > 0
            ? static_cast<qreal>(image->height()) / static_cast<qreal>(image->width())
            : 1.0;
        PreviewSpriteDescriptor sprite;
        sprite.image = image;
        sprite.center = center;
        sprite.width = qMax<qreal>(1.0, qRound(widthPixels));
        sprite.height = qMax<qreal>(1.0, qRound(widthPixels * aspect));
        sprite.rotationDegrees = angleDegrees;
        sprite.opacity = opacity;
        layerState.sprites.append(sprite);
    };

    for (auto it = positionTriggers.cbegin(); it != positionTriggers.cend(); ++it) {
        const TouchJudgeTrigger& trigger = it.value();
        if (trigger.marker == nullptr) {
            continue;
        }
        const TimelineNoteMarker& marker = *trigger.marker;
        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectTouchDurationSeconds) {
            continue;
        }
        const qreal clipTime = judgeEffectTouchClipTime(elapsedSeconds);
        if (clipTime > kJudgeEffectTouchFadeEndSeconds) {
            continue;
        }

        const qreal circleRadiusUnits = sampleScalarCurve(kJudgeEffectTouchCircleScaleKeys, clipTime);
        const qreal innerRadiusScale = sampleScalarCurve(kJudgeEffectTouchInnerParentScaleKeys, clipTime);
        const qreal outerRadiusScale = sampleScalarCurve(kJudgeEffectTouchOuterRadiusScaleKeys, clipTime);
        const qreal outerSpriteScale = sampleScalarCurve(kJudgeEffectTouchOuterSpriteScaleKeys, clipTime);
        // Reference: TouchCircle and every star part share the same m_Color.a fade (1 -> 0
        // over the clip), so one value drives the circle and all sparkles.
        const qreal clipFade = qBound<qreal>(0.0, 1.0 - clipTime / kJudgeEffectTouchFadeEndSeconds, 1.0);
        const qreal circleAlpha = qBound<qreal>(0.0, 1.0 - clipTime / kJudgeEffectTouchCircleFadeEndSeconds, 1.0);
        const QPointF center = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
        const qreal layoutRotationDegrees = touchJudgeLayoutRotationDegrees(trigger.second);

        appendSprite(
            circleImage,
            center,
            circleRadiusUnits / kJudgeEffectTouchCircleContentRadiusRatio * prefabUnitPixels,
            0.0,
            circleAlpha
        );

        const auto appendSparkleRing = [&](qreal radiusUnits,
                                           qreal radiusScale,
                                           qreal pieceWidthPixels) {
            for (int pointIndex = 0; pointIndex < kJudgeEffectTouchSparklePointCount; ++pointIndex) {
                const bool solidStar = (pointIndex % 2) == 0;
                const QPointF offset =
                    sparkleRingOffset(radiusUnits, pointIndex, layoutRotationDegrees) * (prefabUnitPixels * radiusScale);
                appendSprite(
                    solidStar ? part02Image : part01Image,
                    center + offset,
                    pieceWidthPixels * (solidStar ? kJudgeEffectTouchSolidSpriteScale : 1.0),
                    sparkleRingSpriteRotationDegrees(pointIndex, layoutRotationDegrees),
                    clipFade
                );
            }
        };

        const qreal outerPieceWidthPixels =
            kJudgeEffectTouchPartSpriteWidthUnits * kJudgeEffectTouchOuterScaleBase * outerSpriteScale * prefabUnitPixels;
        const qreal innerPieceWidthPixels = outerPieceWidthPixels * kJudgeEffectTouchInnerToOuterSpriteScale;
        appendSparkleRing(
            kJudgeEffectTouchInnerRingRadiusUnits,
            innerRadiusScale,
            innerPieceWidthPixels
        );

        appendSparkleRing(
            kJudgeEffectTouchOuterRingRadiusUnits,
            outerRadiusScale,
            outerPieceWidthPixels
        );
    }

    return layerState;
}

}  // namespace miacode::preview::scene
