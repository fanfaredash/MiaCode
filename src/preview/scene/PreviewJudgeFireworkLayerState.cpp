#include "preview/scene/PreviewJudgeFireworkLayerState.h"

#include "preview/scene/PreviewSceneConstants.h"
#include "preview/scene/PreviewSceneMath.h"

#include <QPainter>
#include <QtMath>
#include <array>

namespace {

constexpr qreal kJudgeEffectFireworkFallbackTouchPixels = 96.0;
constexpr qreal kJudgeEffectFireworkMinUnitPixels = 6.0;
constexpr qreal kJudgeEffectFireworkClipInsetPixels = 1.0;
constexpr qreal kJudgeEffectFireworkSectorAlphaScale = 0.88;
constexpr qreal kJudgeEffectFireworkColorBallCoreStop = 0.0;
constexpr qreal kJudgeEffectFireworkColorBallMidStop = 0.3;
constexpr qreal kJudgeEffectFireworkColorBallOuterStop = 0.68;
constexpr qreal kJudgeEffectFireworkHoleMinFeatherPixels = 2.0;
constexpr qreal kJudgeEffectTouchUnitRelativeToTouch = 3.125;
constexpr qreal kJudgeEffectFireworkTouchTriggerDelaySeconds =
    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectFireworkTouchTriggerDelaySeconds);
constexpr qreal kJudgeEffectFireworkDurationSeconds =
    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds);
constexpr qreal kJudgeEffectFireworkBaseWidthUnits = 10.8;
constexpr qreal kJudgeEffectFireworkColorBallBaseWidthUnits = 5.12;
constexpr qreal kJudgeEffectFireworkBrightnessGain = 1.20;
constexpr int kJudgeEffectFireworkSectorCount = 30;
constexpr int kJudgeEffectFireworkColoredSectorCount = kJudgeEffectFireworkSectorCount / 2;
constexpr qreal kJudgeEffectFireworkSectorSpanDegrees =
    360.0 / static_cast<qreal>(kJudgeEffectFireworkSectorCount);
constexpr qreal kJudgeEffectFireworkSectorStepDegrees = kJudgeEffectFireworkSectorSpanDegrees * 2.0;
constexpr qreal kJudgeEffectFireworkSectorPhaseDegrees = -102.0;
constexpr int kJudgeEffectFireworkStepRotationSegmentCount = 3;
constexpr qreal kJudgeEffectFireworkStepRotationDegrees = 24.0;
constexpr qreal kFireworkInnerUB = 0.16;
constexpr qreal kJudgeEffectFireworkHoleStartRadiusRatio = kFireworkInnerUB;
constexpr qreal kJudgeEffectFireworkHoleEndRadiusRatio = 1.0;
constexpr qreal kJudgeEffectFireworkHoleBandRatio =
    (1.0 - kJudgeEffectFireworkHoleStartRadiusRatio) * 0.06;

struct ScalarCurveKey {
    qreal time = 0.0;
    qreal value = 0.0;
};

const std::array<QColor, 5> kJudgeEffectFireworkSectorColors = {{
    QColor(214, 106, 59),
    QColor(188, 86, 165),
    QColor(88, 157, 212),
    QColor(156, 186, 71),
    QColor(202, 178, 70),
}};

const std::array<ScalarCurveKey, 5> kJudgeEffectFireworkScaleKeys = {{
    {0.0, 0.0},
    {0.1, 0.0},
    {0.13333334, 0.6},
    {0.23333333, 1.25},
    {1.3333334, 5.0},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkRotationKeys = {{
    {0.0, 0.0},
    {1.2166667, -72.0},
    {1.3333334, -78.85715},
}};

const std::array<ScalarCurveKey, 12> kJudgeEffectFireworkAlphaKeys = {{
    {0.0, 0.589},
    {0.5, 0.589},
    {0.5833333, 0.47709},
    {0.6666667, 0.37696},
    {0.75, 0.28861},
    {0.8333333, 0.21205},
    {0.9166667, 0.14725},
    {1.0, 0.09476},
    {1.0833334, 0.05516},
    {1.1666666, 0.02899},
    {1.25, 0.00879},
    {1.3333334, 0.0},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkColorBallScaleKeys = {{
    {0.0, 0.0},
    {0.2, 0.87},
    {1.2, 0.95},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkColorBallBigScaleKeys = {{
    {0.0, 0.0},
    {0.2, 1.0},
    {1.2, 1.1},
}};

const std::array<ScalarCurveKey, 4> kJudgeEffectFireworkColorBallAlphaKeys = {{
    {0.0, 0.0},
    {0.2, 0.75},
    {0.8, 0.60},
    {1.2, 0.0},
}};

const std::array<ScalarCurveKey, 4> kJudgeEffectFireworkColorBallBigAlphaKeys = {{
    {0.0, 0.0},
    {0.2, 0.55},
    {0.8, 0.42},
    {1.2, 0.0},
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

qreal smoothStep01(qreal t)
{
    const qreal x = qBound<qreal>(0.0, t, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

QColor scaleRgb(const QColor& color, qreal gain, qreal alpha)
{
    QColor result(
        qBound(0, qRound(static_cast<qreal>(color.red()) * gain), 255),
        qBound(0, qRound(static_cast<qreal>(color.green()) * gain), 255),
        qBound(0, qRound(static_cast<qreal>(color.blue()) * gain), 255)
    );
    result.setAlphaF(qBound<qreal>(0.0, alpha, 1.0));
    return result;
}

QImage buildFireworkColorBallImage(
    const QImage* sourceImage,
    const QRectF& sourceRect,
    const QSize& targetSize,
    qreal holeRadius,
    qreal holeMaskRadius
)
{
    if (targetSize.isEmpty()) {
        return QImage();
    }

    QImage image(targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF targetRect(QPointF(0.0, 0.0), QSizeF(targetSize));
    if (sourceImage != nullptr && !sourceImage->isNull()) {
        if (sourceRect.isValid() && !sourceRect.isEmpty()) {
            painter.drawImage(targetRect, *sourceImage, sourceRect);
        } else {
            painter.drawImage(targetRect, *sourceImage);
        }
    } else {
        const QPointF center(targetRect.center());
        const qreal radius = qMax<qreal>(1.0, qMin(targetRect.width(), targetRect.height()) * 0.5);
        QRadialGradient glow(center, radius);
        glow.setColorAt(kJudgeEffectFireworkColorBallCoreStop, QColor(255, 245, 160, 235));
        glow.setColorAt(kJudgeEffectFireworkColorBallMidStop, QColor(255, 110, 220, 166));
        glow.setColorAt(kJudgeEffectFireworkColorBallOuterStop, QColor(110, 190, 255, 107));
        glow.setColorAt(1.0, QColor(255, 240, 120, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(center, radius, radius);
    }

    if (holeMaskRadius > 0.0) {
        const QPointF center(targetRect.center());
        const qreal clampedMaskRadius = qMax<qreal>(1.0, holeMaskRadius);
        const qreal holeSolidRatio =
            qBound<qreal>(0.0, holeRadius / qMax<qreal>(clampedMaskRadius, 0.001), 1.0);
        QRadialGradient holeGradient(center, clampedMaskRadius);
        holeGradient.setColorAt(0.0, QColor(0, 0, 0, 255));
        holeGradient.setColorAt(holeSolidRatio, QColor(0, 0, 0, 255));
        holeGradient.setColorAt(1.0, QColor(0, 0, 0, 0));

        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.setPen(Qt::NoPen);
        painter.setBrush(holeGradient);
        painter.drawEllipse(center, clampedMaskRadius, clampedMaskRadius);
        painter.restore();
    }
    painter.end();

    return image;
}

}  // namespace

namespace miacode::preview::scene {

PreviewJudgeFireworkLayerState buildPreviewJudgeFireworkLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
)
{
    PreviewJudgeFireworkLayerState layerState;

    const TimelineNoteMarker* latestTriggerMarker = nullptr;
    qreal latestTriggerSecond = -1.0;
    for (const TimelineNoteMarker& marker : state.noteMarkers) {
        if (!marker.isFirework) {
            continue;
        }
        if (marker.type != QLatin1String("touch") && marker.type != QLatin1String("touch_hold")) {
            continue;
        }
        if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
            continue;
        }

        const qreal triggerSecond = marker.type == QLatin1String("touch")
            ? static_cast<qreal>(marker.second + kJudgeEffectFireworkTouchTriggerDelaySeconds)
            : static_cast<qreal>(marker.endSecond);
        if (triggerSecond < 0.0) {
            continue;
        }

        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds) - triggerSecond;
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectFireworkDurationSeconds) {
            continue;
        }
        if (latestTriggerMarker == nullptr || triggerSecond >= latestTriggerSecond) {
            latestTriggerMarker = &marker;
            latestTriggerSecond = triggerSecond;
        }
    }
    if (latestTriggerMarker == nullptr) {
        return layerState;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const qreal fallbackTouchPixels = kJudgeEffectFireworkFallbackTouchPixels * kTouchAssetScale * canvasScale;
    const qreal touchBasePixels = (!state.skin.touchPointImage.isNull())
        ? (state.skin.touchPointImage.width() * kTouchAssetScale * canvasScale)
        : fallbackTouchPixels;
    const qreal worldToPixels =
        qMax<qreal>(kJudgeEffectFireworkMinUnitPixels, touchBasePixels * kJudgeEffectTouchUnitRelativeToTouch);

    layerState.clipCenter = mapLogicalPointToRect(QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter), playfieldRect);
    const qreal outlineRadius = qMax<qreal>(1.0, mapLogicalLengthToRect(kLogicalDistanceEdge, playfieldRect));
    layerState.clipRadius = qMax<qreal>(1.0, outlineRadius - kJudgeEffectFireworkClipInsetPixels);

    const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds) - latestTriggerSecond;
    const qreal clipTime = qBound<qreal>(0.0, elapsedSeconds, kJudgeEffectFireworkDurationSeconds);
    const qreal life01 = qBound<qreal>(0.0, clipTime / kJudgeEffectFireworkDurationSeconds, 1.0);
    const qreal fireworkScale = qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkScaleKeys, clipTime));
    const qreal fireworkAlpha = qBound<qreal>(
        0.0,
        sampleScalarCurve(kJudgeEffectFireworkAlphaKeys, clipTime) * kJudgeEffectFireworkBrightnessGain,
        1.0
    );
    const qreal fireworkRotationDegrees = sampleScalarCurve(kJudgeEffectFireworkRotationKeys, clipTime);
    const qreal colorBallScale = qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkColorBallScaleKeys, clipTime));
    const qreal colorBallBigScale =
        qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkColorBallBigScaleKeys, clipTime));
    const qreal colorBallAlpha = qBound<qreal>(
        0.0,
        sampleScalarCurve(kJudgeEffectFireworkColorBallAlphaKeys, clipTime) * kJudgeEffectFireworkBrightnessGain,
        1.0
    );
    const qreal colorBallBigAlpha = qBound<qreal>(
        0.0,
        sampleScalarCurve(kJudgeEffectFireworkColorBallBigAlphaKeys, clipTime) * kJudgeEffectFireworkBrightnessGain,
        1.0
    );
    const int stepRotationIndex = qBound(
        0,
        static_cast<int>(qFloor(life01 * static_cast<qreal>(kJudgeEffectFireworkStepRotationSegmentCount) + 0.5)),
        kJudgeEffectFireworkStepRotationSegmentCount
    );
    const qreal steppedRotationDegrees =
        -static_cast<qreal>(stepRotationIndex) * kJudgeEffectFireworkStepRotationDegrees;
    const QPointF center = mapLogicalPointToRect(latestTriggerMarker->touchPoint, playfieldRect);

    const qreal holeRadius = layerState.clipRadius * (
        kJudgeEffectFireworkHoleStartRadiusRatio
        + (kJudgeEffectFireworkHoleEndRadiusRatio - kJudgeEffectFireworkHoleStartRadiusRatio) * smoothStep01(life01)
    );
    const qreal holeFeather = qMax<qreal>(
        kJudgeEffectFireworkHoleMinFeatherPixels,
        holeRadius * kJudgeEffectFireworkHoleBandRatio
    );
    const qreal holeMaskRadius = holeRadius + holeFeather;

    if (fireworkScale > kRenderDurationEpsilon && fireworkAlpha > kRenderDurationEpsilon) {
        const qreal outerRadius =
            qMax<qreal>(1.0, (kJudgeEffectFireworkBaseWidthUnits * fireworkScale * worldToPixels) * 0.5);
        if (outerRadius > holeRadius + kRenderDurationEpsilon) {
            layerState.sectors.reserve(kJudgeEffectFireworkColoredSectorCount);
            for (int sectorIndex = 0; sectorIndex < kJudgeEffectFireworkColoredSectorCount; ++sectorIndex) {
                PreviewSectorDescriptor sector;
                sector.center = center;
                sector.innerRadius = qBound<qreal>(0.0, holeRadius, outerRadius - kRenderDurationEpsilon);
                sector.outerRadius = outerRadius;
                sector.startDegrees =
                    kJudgeEffectFireworkSectorPhaseDegrees
                    + fireworkRotationDegrees
                    + steppedRotationDegrees
                    + static_cast<qreal>(sectorIndex) * kJudgeEffectFireworkSectorStepDegrees;
                sector.sweepDegrees = kJudgeEffectFireworkSectorSpanDegrees;
                sector.color = scaleRgb(
                    kJudgeEffectFireworkSectorColors[static_cast<std::size_t>(
                        sectorIndex % static_cast<int>(kJudgeEffectFireworkSectorColors.size())
                    )],
                    kJudgeEffectFireworkBrightnessGain,
                    fireworkAlpha * kJudgeEffectFireworkSectorAlphaScale
                );
                layerState.sectors.append(sector);
            }
        }
    }

    layerState.sprites.reserve(2);
    layerState.ownedImages.reserve(2);
    const auto appendOwnedImage = [&layerState](QImage image) -> const QImage* {
        if (image.isNull()) {
            return nullptr;
        }
        layerState.ownedImages.append(QSharedPointer<QImage>(new QImage(std::move(image))));
        return layerState.ownedImages.last().data();
    };
    const auto appendColorBallSprite = [&](qreal scale, qreal alpha) {
        if (scale <= kRenderDurationEpsilon || alpha <= kRenderDurationEpsilon) {
            return;
        }

        const QImage* sourceImage =
            state.judgeEffect.fireworkColorBallImage.isNull() ? nullptr : &state.judgeEffect.fireworkColorBallImage;
        const QRectF sourceRect = state.judgeEffect.fireworkColorBallSourceRect;
        const qreal sourceWidth = sourceRect.isValid() && !sourceRect.isEmpty()
            ? sourceRect.width()
            : (sourceImage != nullptr && !sourceImage->isNull() ? static_cast<qreal>(sourceImage->width()) : 1.0);
        const qreal sourceHeight = sourceRect.isValid() && !sourceRect.isEmpty()
            ? sourceRect.height()
            : (sourceImage != nullptr && !sourceImage->isNull() ? static_cast<qreal>(sourceImage->height()) : 1.0);
        if (sourceWidth <= 0.0 || sourceHeight <= 0.0) {
            return;
        }

        const qreal widthPixels =
            qMax<qreal>(1.0, kJudgeEffectFireworkColorBallBaseWidthUnits * scale * worldToPixels);
        const qreal aspect = sourceHeight / sourceWidth;
        const QSize targetSize(
            qMax(1, qRound(widthPixels)),
            qMax(1, qRound(widthPixels * aspect))
        );
        const QImage* image = appendOwnedImage(
            buildFireworkColorBallImage(sourceImage, sourceRect, targetSize, holeRadius, holeMaskRadius)
        );
        if (image == nullptr || image->isNull()) {
            return;
        }

        PreviewSpriteDescriptor sprite;
        sprite.image = image;
        sprite.center = center;
        sprite.width = targetSize.width();
        sprite.height = targetSize.height();
        sprite.opacity = alpha;
        sprite.cacheable = false;
        layerState.sprites.append(sprite);
    };

    appendColorBallSprite(colorBallBigScale, colorBallBigAlpha);
    appendColorBallSprite(colorBallScale, colorBallAlpha);

    return layerState;
}

}  // namespace miacode::preview::scene
