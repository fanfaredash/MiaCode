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
constexpr qreal kJudgeEffectTouchDestroySeconds = 0.25;
constexpr qreal kJudgeEffectTouchCircleFadeEndSeconds = 0.31666666;
constexpr qreal kJudgeEffectTouchUnitRelativeToTouch = 3.125;
constexpr qreal kJudgeEffectTouchInnerScaleBase = 0.1725238;
constexpr qreal kJudgeEffectTouchOuterScaleBase = 0.3146144;
constexpr qreal kJudgeEffectTouchCircleSpriteWidthUnits = 512.0 / 100.0;
constexpr qreal kJudgeEffectTouchPartSpriteWidthUnits = 103.948685 / 100.0;
const QColor kJudgeEffectTouchCircleTint = QColor::fromRgbF(1.0, 0.9943893, 0.4669811, 1.0);
const QColor kJudgeEffectTouchPartTint = QColor::fromRgbF(1.0, 0.9000474, 0.4666667, 1.0);
const std::array<QPointF, 4> kJudgeEffectTouchInnerOffsets = {
    QPointF(0.0, -0.246),
    QPointF(0.0, 0.246),
    QPointF(0.246, 0.0),
    QPointF(-0.246, 0.0),
};
const std::array<QPointF, 4> kJudgeEffectTouchOuterDiagonalOffsets = {
    QPointF(0.254, 0.256),
    QPointF(0.254, -0.256),
    QPointF(-0.254, -0.256),
    QPointF(-0.254, 0.256),
};
const std::array<QPointF, 4> kJudgeEffectTouchOuterCardinalOffsets = {
    QPointF(0.0, 0.359),
    QPointF(0.0, -0.359),
    QPointF(-0.359, 0.0),
    QPointF(0.359, 0.0),
};

struct ScalarCurveKey {
    qreal time = 0.0;
    qreal value = 0.0;
};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchCircleScaleKeys = {{
    {0.0, 0.1},
    {0.3, 0.35},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchInnerParentScaleKeys = {{
    {0.0, 1.0},
    {0.25, 1.5},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchOuterParentScaleKeys = {{
    {0.0, 2.0},
    {0.25, 0.2},
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

    layerState.sprites.reserve(positionTriggers.size() * 9);
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
        if (clipTime > kJudgeEffectTouchDestroySeconds) {
            continue;
        }

        const qreal circleScale = sampleScalarCurve(kJudgeEffectTouchCircleScaleKeys, clipTime);
        const qreal innerParentScale = sampleScalarCurve(kJudgeEffectTouchInnerParentScaleKeys, clipTime);
        const qreal outerParentScale = sampleScalarCurve(kJudgeEffectTouchOuterParentScaleKeys, clipTime);
        const qreal circleFade = qBound<qreal>(0.0, 1.0 - clipTime / kJudgeEffectTouchCircleFadeEndSeconds, 1.0);
        const qreal circleAlpha = circleFade;
        const QPointF center = mapLogicalPointToRect(marker.touchPoint, playfieldRect);

        appendSprite(
            circleImage,
            center,
            kJudgeEffectTouchCircleSpriteWidthUnits * circleScale * prefabUnitPixels,
            0.0,
            circleAlpha
        );

        const qreal innerPieceWidthPixels =
            kJudgeEffectTouchPartSpriteWidthUnits * kJudgeEffectTouchInnerScaleBase * innerParentScale * prefabUnitPixels;
        for (const QPointF& offset : kJudgeEffectTouchInnerOffsets) {
            appendSprite(
                part02Image,
                center + offset * (prefabUnitPixels * innerParentScale),
                innerPieceWidthPixels,
                0.0,
                1.0
            );
        }

        const qreal outerPieceWidthPixels =
            kJudgeEffectTouchPartSpriteWidthUnits * kJudgeEffectTouchOuterScaleBase * outerParentScale * prefabUnitPixels;
        for (const QPointF& offset : kJudgeEffectTouchOuterDiagonalOffsets) {
            appendSprite(
                part02Image,
                center + offset * (prefabUnitPixels * outerParentScale),
                outerPieceWidthPixels,
                kJudgeEffectTouchTextureOuterDiagonalAngleDegrees,
                1.0
            );
        }
        for (const QPointF& offset : kJudgeEffectTouchOuterCardinalOffsets) {
            appendSprite(
                part01Image,
                center + offset * (prefabUnitPixels * outerParentScale),
                outerPieceWidthPixels,
                0.0,
                1.0
            );
        }
    }

    return layerState;
}

}  // namespace miacode::preview::scene
