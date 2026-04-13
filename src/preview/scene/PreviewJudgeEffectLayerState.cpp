#include "preview/scene/PreviewJudgeEffectLayerState.h"

#include "preview/scene/PreviewSceneConstants.h"
#include "preview/scene/PreviewSceneMath.h"

#include <QPainter>
#include <QPen>
#include <QtMath>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr qreal kJudgeEffectFallbackTapPixels = 96.0;
constexpr qreal kJudgeEffectMinBasePixels = 8.0;
constexpr qreal kJudgeEffectMinOffsetPixels = 4.0;
constexpr qreal kJudgeEffectHoldSustainMinBasePixels = 6.0;
constexpr quint64 kJudgeEffectParticleSeedMultiplier = 0x9e3779b97f4a7c15ULL;
constexpr int kJudgeEffectParticleJitterBitShift = 11;
constexpr quint64 kJudgeEffectParticleJitterMask = 0xFFULL;
constexpr qreal kJudgeEffectParticleJitterCenter = 0.5;
constexpr qreal kJudgeEffectParticleJitterScale = 0.06;
constexpr qreal kJudgeEffectBaseRelativeToTap = 173.87256 / 122.0;
constexpr qreal kJudgeEffectOffsetRelativeToTap = 100.0 / 122.0;
constexpr qreal kJudgeEffectEdgeGlowScale = 1.012;
constexpr qreal kJudgeEffectEdgeGlowAlpha = 0.14;
constexpr qreal kJudgeEffectAlphaTailGamma = 1.00;
constexpr qreal kJudgeEffectLaneFacingAngleOffsetDegrees = 0.0;
constexpr qreal kJudgeEffectTapTextureAngleOffsetDegrees = 0.0;
constexpr qreal kJudgeEffectBreakTextureAngleOffsetDegrees = 132.5;
constexpr qreal kJudgeEffectClipDurationSeconds =
    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectDurationSeconds);
constexpr qreal kJudgeEffectDurationSeconds = kJudgeEffectClipDurationSeconds;
constexpr qreal kJudgeEffectHoldSustainLifetimeSeconds = 0.6;
constexpr int kJudgeEffectHoldSustainParticleCount = 5;
constexpr qreal kJudgeEffectHoldSustainBaseRelativeToTap = (256.0 * 1.2) / 122.0;

struct ScalarCurveKey {
    qreal time = 0.0;
    qreal value = 0.0;
};

struct ScalarHermiteKey {
    qreal time = 0.0;
    qreal value = 0.0;
    qreal inSlope = 0.0;
    qreal outSlope = 0.0;
};

struct Vec2CurveKey {
    qreal time = 0.0;
    QPointF value;
};

const std::array<ScalarCurveKey, 13> kJudgeEffectRootScaleKeys = {{
    {0.0, 0.5},
    {0.016666668, 0.50919664},
    {0.033333335, 0.53476083},
    {0.05, 0.57365394},
    {0.06666667, 0.6228372},
    {0.083333336, 0.67927206},
    {0.1, 0.7399199},
    {0.11666667, 0.80174196},
    {0.13333334, 0.8616997},
    {0.15, 0.91675436},
    {0.16666667, 0.96386737},
    {0.18333334, 1.0},
    {0.45, 1.3},
}};

const std::array<ScalarHermiteKey, 4> kJudgeEffectAlphaKeys = {{
    {0.0, 1.0, 0.0, 0.0},
    {0.23333333, 0.995328, -0.04485111, -0.044851113},
    {0.41666666, 8.059014e-08, 1.7378422e-06, -std::numeric_limits<qreal>::infinity()},
    {0.5833333, 0.0, 0.0, 0.0},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectRotationKeys = {{
    {0.0, 0.0},
    {0.46666667, 180.0},
    {0.71666664, 360.0},
}};

const std::array<std::array<Vec2CurveKey, 4>, 4> kJudgeEffectTapHexPositionKeys = {{
    {{
        {0.0, QPointF(0.552, 0.5)},
        {0.25, QPointF(0.43683612, 0.39568493)},
        {0.56666666, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
    {{
        {0.0, QPointF(-0.431, -0.527)},
        {0.25, QPointF(-0.31925926, -0.39037037)},
        {0.5, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
    {{
        {0.0, QPointF(0.473, -0.468)},
        {0.25, QPointF(0.35037035, -0.34666663)},
        {0.5, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
    {{
        {0.0, QPointF(-0.502, 0.492)},
        {0.25, QPointF(-0.3718518, 0.36444443)},
        {0.5, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
}};

const std::array<std::array<ScalarCurveKey, 2>, 4> kJudgeEffectTapHexScaleKeys = {{
    {{
        {0.0, 0.4788},
        {0.26666668, 0.6},
    }},
    {{
        {0.0, 0.4788},
        {0.26666668, 0.6},
    }},
    {{
        {0.0, 0.4788},
        {0.26666668, 0.8},
    }},
    {{
        {0.0, 0.4788},
        {0.26666668, 0.8},
    }},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectHoldSustainSizeKeys = {{
    {0.0, 0.22448978},
    {0.43861136, 0.8877539},
    {0.99853516, 1.0},
}};

const std::array<ScalarCurveKey, 7> kJudgeEffectHoldSustainAlphaKeys = {{
    {0.0, 0.007843138},
    {0.09459098, 0.5411765},
    {0.14285539, 0.9774867},
    {0.50193027, 1.0},
    {0.59652174, 0.30085242},
    {0.66602579, 0.30980393},
    {1.0, 0.0},
}};

const std::array<qreal, 5> kJudgeEffectHoldSustainPhaseOffsets = {
    0.00, 0.18, 0.43, 0.67, 0.86,
};

const std::array<qreal, 4> kJudgeEffectParentRotationDirection = {1.0, 1.0, -1.0, -1.0};

struct JudgeEffectTrigger {
    qreal second = -1.0;
    QPointF logicalCenter;
    qreal facingAngle = 0.0;
    bool useBreakShape = false;
    qreal visibleStartSecond = -1.0;
    qreal visibleEndSecond = -1.0;
    bool useApproxAlpha = false;
};

struct LaneJudgeEffectTrigger {
    bool active = false;
    JudgeEffectTrigger trigger;
};

struct HoldSustainTrigger {
    QPointF logicalCenter;
    qreal startSecond = 0.0;
};

QImage buildFallbackHoldRingImage();

qreal approximateLaneJudgeAlpha(qreal normalizedTime)
{
    constexpr qreal kDelayNorm = 0.56;
    constexpr qreal kSigmaNorm = 0.252;
    constexpr qreal kExponent = 2.3;
    if (normalizedTime <= 0.0) {
        return 1.0;
    }
    if (normalizedTime >= 1.0) {
        return 0.0;
    }
    const qreal delayedTime = qMax<qreal>(0.0, normalizedTime - kDelayNorm);
    if (delayedTime <= 0.0) {
        return 1.0;
    }
    return qExp(-qPow(delayedTime / kSigmaNorm, kExponent));
}

qreal judgeEffectClipTime(qreal elapsedSeconds)
{
    return qBound<qreal>(0.0, elapsedSeconds, kJudgeEffectClipDurationSeconds);
}

const QImage& fallbackHoldRingImage()
{
    static const QImage image = buildFallbackHoldRingImage();
    return image;
}

QPointF rotatePointDegrees(const QPointF& point, qreal angleDegrees)
{
    const qreal radians = qDegreesToRadians(angleDegrees);
    const qreal c = qCos(radians);
    const qreal s = qSin(radians);
    return QPointF(point.x() * c - point.y() * s, point.x() * s + point.y() * c);
}

QImage buildFallbackHoldRingImage()
{
    constexpr int kSize = 256;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal radius = 102.0;
    QColor outer(255, 253, 119, qRound(255.0 * 0.42));
    QColor core(255, 253, 119, qRound(255.0 * 0.98));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(outer, 18.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(core, 9.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, radius, radius);
    painter.end();
    return image;
}

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

template <std::size_t N>
qreal sampleScalarHermiteCurve(const std::array<ScalarHermiteKey, N>& keys, qreal time)
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
        const ScalarHermiteKey& next = keys[i];
        if (time > next.time) {
            continue;
        }
        const ScalarHermiteKey& prev = keys[i - 1];
        const qreal span = next.time - prev.time;
        if (qFuzzyIsNull(span)) {
            return next.value;
        }
        const qreal t = (time - prev.time) / span;
        const qreal prevOutSlope = prev.outSlope;
        const qreal nextInSlope = next.inSlope;
        if (!std::isfinite(static_cast<double>(prevOutSlope))
            || !std::isfinite(static_cast<double>(nextInSlope))) {
            return prev.value + (next.value - prev.value) * t;
        }
        const qreal t2 = t * t;
        const qreal t3 = t2 * t;
        const qreal h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const qreal h10 = t3 - 2.0 * t2 + t;
        const qreal h01 = -2.0 * t3 + 3.0 * t2;
        const qreal h11 = t3 - t2;
        return h00 * prev.value
            + h10 * span * prevOutSlope
            + h01 * next.value
            + h11 * span * nextInSlope;
    }
    return keys.back().value;
}

template <std::size_t N>
QPointF sampleVec2Curve(const std::array<Vec2CurveKey, N>& keys, qreal time)
{
    if (keys.empty()) {
        return QPointF();
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const Vec2CurveKey& next = keys[i];
        if (time > next.time) {
            continue;
        }
        const Vec2CurveKey& prev = keys[i - 1];
        const qreal span = next.time - prev.time;
        if (qFuzzyIsNull(span)) {
            return next.value;
        }
        const qreal t = (time - prev.time) / span;
        return QPointF(
            prev.value.x() + (next.value.x() - prev.value.x()) * t,
            prev.value.y() + (next.value.y() - prev.value.y()) * t
        );
    }
    return keys.back().value;
}

}  // namespace

namespace miacode::preview::scene {

PreviewJudgeEffectLayerState buildPreviewJudgeEffectLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewJudgeEffectLayerState layerState;
    if (state.judgeEffect.tapImage.isNull() && state.judgeEffect.tapBreakImage.isNull()) {
        return layerState;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const qreal fallbackTapPixels = kJudgeEffectFallbackTapPixels * canvasScale;
    const qreal tapBasePixels = (!state.skin.tapImage.isNull())
        ? (state.skin.tapImage.width() * kSkinAssetScale * canvasScale)
        : fallbackTapPixels;
    const qreal effectBasePixels = qMax<qreal>(kJudgeEffectMinBasePixels, tapBasePixels * kJudgeEffectBaseRelativeToTap);
    const qreal effectOffsetPixels = qMax<qreal>(kJudgeEffectMinOffsetPixels, tapBasePixels * kJudgeEffectOffsetRelativeToTap);

    std::array<LaneJudgeEffectTrigger, 8> laneTriggers;
    QVector<JudgeEffectTrigger> freeTriggers;
    freeTriggers.reserve(kPreviewSmallCollectionReserve);
    QHash<quint64, HoldSustainTrigger> holdSustainTriggers;
    holdSustainTriggers.reserve(kPreviewSmallCollectionReserve);

    const auto laneFacingAngleFor = [](int lane) {
        return kLaneUnitVectorBaseDegrees
            + (lane - 1) * kLaneAngleStepDegrees
            + kJudgeEffectLaneFacingAngleOffsetDegrees;
    };

    const auto queueLaneTrigger =
        [&](int lane, qreal second, bool useBreakShape, qreal visibleStartOffsetSeconds, qreal visibleEndOffsetSeconds, bool useApproxAlpha) {
            if (lane < 1 || lane > 8) {
                return;
            }
            const qreal visibleStartSecond = second + visibleStartOffsetSeconds;
            const qreal visibleEndSecond = second + visibleEndOffsetSeconds;
            if (visibleEndSecond <= visibleStartSecond) {
                return;
            }
            const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - second);
            if (state.playheadSeconds < visibleStartSecond
                || state.playheadSeconds > visibleEndSecond
                || elapsedSeconds < 0.0
                || elapsedSeconds > kJudgeEffectDurationSeconds) {
                return;
            }

            const QPointF laneUnit = laneUnitVector(lane);
            JudgeEffectTrigger trigger;
            trigger.second = second;
            trigger.logicalCenter = QPointF(
                kLogicalCanvasCenter + laneUnit.x() * kLogicalDistanceEdge,
                kLogicalCanvasCenter + laneUnit.y() * kLogicalDistanceEdge
            );
            trigger.facingAngle = laneFacingAngleFor(lane);
            trigger.useBreakShape = useBreakShape;
            trigger.visibleStartSecond = visibleStartSecond;
            trigger.visibleEndSecond = visibleEndSecond;
            trigger.useApproxAlpha = useApproxAlpha;

            LaneJudgeEffectTrigger& laneTrigger = laneTriggers[static_cast<std::size_t>(lane - 1)];
            if (!laneTrigger.active || second >= laneTrigger.trigger.second) {
                laneTrigger.active = true;
                laneTrigger.trigger = trigger;
            }
        };

    const auto queueFreeTrigger = [&](qreal second, const QPointF& logicalCenter, qreal facingAngle, bool useBreakShape) {
        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectDurationSeconds) {
            return;
        }
        JudgeEffectTrigger trigger;
        trigger.second = second;
        trigger.logicalCenter = logicalCenter;
        trigger.facingAngle = facingAngle + kJudgeEffectLaneFacingAngleOffsetDegrees;
        trigger.useBreakShape = useBreakShape;
        trigger.visibleStartSecond = second;
        trigger.visibleEndSecond = second + kJudgeEffectDurationSeconds;
        trigger.useApproxAlpha = false;
        freeTriggers.append(trigger);
    };

    const auto queueHoldSustain = [&](const QPointF& logicalCenter, qreal startSecond, qreal endSecond) {
        const qreal effectiveStartSecond =
            startSecond + static_cast<qreal>(miacode::preview_gameplay::kHoldSustainEffectStartOffsetSeconds);
        const qreal effectiveEndSecond =
            endSecond - static_cast<qreal>(miacode::preview_gameplay::kHoldSustainEffectEndOffsetSeconds);
        if (effectiveEndSecond <= effectiveStartSecond) {
            return;
        }
        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - effectiveStartSecond);
        if (elapsedSeconds < 0.0 || state.playheadSeconds >= effectiveEndSecond) {
            return;
        }
        const quint64 key = touchPointKey(logicalCenter);
        if (const auto existing = holdSustainTriggers.constFind(key);
            existing != holdSustainTriggers.constEnd() && existing->startSecond > effectiveStartSecond) {
            return;
        }
        HoldSustainTrigger trigger;
        trigger.logicalCenter = logicalCenter;
        trigger.startSecond = effectiveStartSecond;
        holdSustainTriggers.insert(key, trigger);
    };

    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        if (marker.type == QLatin1String("tap")) {
            queueLaneTrigger(
                marker.lane,
                static_cast<qreal>(marker.second),
                marker.isBreak,
                static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleStartSeconds),
                static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleEndSeconds),
                true
            );
            continue;
        }
        if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
            if (marker.hasHeadStar) {
                queueLaneTrigger(
                    marker.lane,
                    static_cast<qreal>(marker.second),
                    marker.headBreak,
                    0.0,
                    kJudgeEffectDurationSeconds,
                    false
                );
            }
            continue;
        }
        if (marker.type == QLatin1String("hold")) {
            if (marker.endSecond >= 0.0) {
                const int holdEndLane = (marker.endLane >= 1 && marker.endLane <= 8) ? marker.endLane : marker.lane;
                queueLaneTrigger(
                    holdEndLane,
                    static_cast<qreal>(marker.endSecond),
                    marker.isBreak,
                    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleStartSeconds),
                    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleEndSeconds),
                    true
                );
            }
            if (marker.endSecond > marker.second && marker.lane >= 1 && marker.lane <= 8) {
                const QPointF laneUnit = laneUnitVector(marker.lane);
                queueHoldSustain(
                    QPointF(
                        kLogicalCanvasCenter + laneUnit.x() * kLogicalDistanceEdge,
                        kLogicalCanvasCenter + laneUnit.y() * kLogicalDistanceEdge
                    ),
                    static_cast<qreal>(marker.second),
                    static_cast<qreal>(marker.endSecond)
                );
            }
            continue;
        }
        if (marker.type == QLatin1String("touch_hold")) {
            if (marker.endSecond > marker.second
                && !(qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y()))) {
                queueHoldSustain(
                    marker.touchPoint,
                    static_cast<qreal>(marker.second),
                    static_cast<qreal>(marker.endSecond)
                );
            }
            if (marker.endSecond < 0.0
                || (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y()))) {
                continue;
            }
            const QPointF radial = marker.touchPoint - QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter);
            const qreal facingAngle = (qFuzzyIsNull(radial.x()) && qFuzzyIsNull(radial.y()))
                ? 0.0
                : qRadiansToDegrees(qAtan2(radial.y(), radial.x()));
            queueFreeTrigger(static_cast<qreal>(marker.endSecond), marker.touchPoint, facingAngle, false);
        }
    }

    layerState.sprites.reserve(
        static_cast<int>(laneTriggers.size()) * 10
        + freeTriggers.size() * 10
        + holdSustainTriggers.size() * kJudgeEffectHoldSustainParticleCount * 2
    );
    const QImage* holdTexture = state.judgeEffect.holdSustainCircleImage.isNull()
        ? &fallbackHoldRingImage()
        : &state.judgeEffect.holdSustainCircleImage;

    const auto appendSprite = [&](const QImage* image,
                                  const QRectF& sourceRect,
                                  const QPointF& center,
                                  qreal width,
                                  qreal height,
                                  qreal angleDegrees,
                                  qreal opacity) {
        if (image == nullptr || image->isNull() || width <= 0.0 || height <= 0.0 || opacity <= kRenderDurationEpsilon) {
            return;
        }
        PreviewSpriteDescriptor sprite;
        sprite.image = image;
        sprite.center = center;
        sprite.width = width;
        sprite.height = height;
        sprite.rotationDegrees = angleDegrees;
        sprite.opacity = opacity;
        sprite.sourceRect = sourceRect;
        sprite.cacheable = true;
        layerState.sprites.append(sprite);
    };

    const auto drawJudgeEffectShapeWithEdgeGlow =
        [&](const QImage* image, const QRectF& sourceRect, const QPointF& center, int size, qreal angleDegrees, qreal opacity) {
            if (image == nullptr || image->isNull() || size <= 0 || opacity <= kRenderDurationEpsilon) {
                return;
            }
            const qreal sourceWidth = sourceRect.isValid() && !sourceRect.isEmpty()
                ? sourceRect.width()
                : static_cast<qreal>(image->width());
            const qreal sourceHeight = sourceRect.isValid() && !sourceRect.isEmpty()
                ? sourceRect.height()
                : static_cast<qreal>(image->height());
            const qreal aspect = (sourceWidth > 0.0 && sourceHeight > 0.0) ? (sourceHeight / sourceWidth) : 1.0;
            const qreal targetWidth = qMax<qreal>(1.0, size);
            const qreal targetHeight = qMax<qreal>(1.0, qRound(size * aspect));
            const qreal glowWidth = qMax<qreal>(1.0, qRound(targetWidth * kJudgeEffectEdgeGlowScale));
            const qreal glowHeight = qMax<qreal>(1.0, qRound(targetHeight * kJudgeEffectEdgeGlowScale));
            const qreal glowAlpha = qBound<qreal>(0.0, opacity * kJudgeEffectEdgeGlowAlpha, 1.0);
            if (glowAlpha > kRenderDurationEpsilon) {
                appendSprite(image, sourceRect, center, glowWidth, glowHeight, angleDegrees, glowAlpha);
            }
            appendSprite(image, sourceRect, center, targetWidth, targetHeight, angleDegrees, opacity);
        };

    if (!holdSustainTriggers.isEmpty()) {
        const qreal holdBasePixels = mapLogicalLengthToRect(kHoldTargetWidth, playfieldRect);
        const qreal sustainBasePixels =
            qMax<qreal>(kJudgeEffectHoldSustainMinBasePixels, holdBasePixels * kJudgeEffectHoldSustainBaseRelativeToTap);
        for (auto it = holdSustainTriggers.cbegin(); it != holdSustainTriggers.cend(); ++it) {
            const HoldSustainTrigger& trigger = it.value();
            const qreal elapsedSeconds = qMax<qreal>(0.0, static_cast<qreal>(state.playheadSeconds - trigger.startSecond));
            const QPointF center = mapLogicalPointToRect(trigger.logicalCenter, playfieldRect);
            for (int particleIndex = 0; particleIndex < kJudgeEffectHoldSustainParticleCount; ++particleIndex) {
                const qreal basePhaseNorm =
                    kJudgeEffectHoldSustainPhaseOffsets[static_cast<std::size_t>(particleIndex)
                    % kJudgeEffectHoldSustainPhaseOffsets.size()];
                const quint64 jitterSeed =
                    it.key() ^ (kJudgeEffectParticleSeedMultiplier * static_cast<quint64>(particleIndex + 1));
                const qreal phaseJitterNorm =
                    (static_cast<qreal>((jitterSeed >> kJudgeEffectParticleJitterBitShift) & kJudgeEffectParticleJitterMask) / 255.0
                        - kJudgeEffectParticleJitterCenter) * kJudgeEffectParticleJitterScale;
                qreal phaseNorm = basePhaseNorm + phaseJitterNorm;
                phaseNorm -= qFloor(phaseNorm);
                const qreal phase = phaseNorm * kJudgeEffectHoldSustainLifetimeSeconds;
                qreal particleAge = std::fmod(elapsedSeconds + phase, kJudgeEffectHoldSustainLifetimeSeconds);
                if (particleAge < 0.0) {
                    particleAge += kJudgeEffectHoldSustainLifetimeSeconds;
                }

                const qreal normalizedAge = particleAge / kJudgeEffectHoldSustainLifetimeSeconds;
                const qreal particleScale = sampleScalarCurve(kJudgeEffectHoldSustainSizeKeys, normalizedAge);
                const qreal particleAlpha = qBound<qreal>(
                    0.0,
                    sampleScalarCurve(kJudgeEffectHoldSustainAlphaKeys, normalizedAge),
                    1.0
                );
                if (particleAlpha <= kRenderDurationEpsilon) {
                    continue;
                }

                const qreal particleSize = qMax<qreal>(1.0, qRound(sustainBasePixels * particleScale));
                const qreal glowSize = qMax<qreal>(1.0, qRound(particleSize * kJudgeEffectEdgeGlowScale));
                const qreal glowAlpha = qBound<qreal>(0.0, particleAlpha * kJudgeEffectEdgeGlowAlpha, 1.0);
                if (glowAlpha > kRenderDurationEpsilon) {
                    appendSprite(holdTexture, QRectF(), center, glowSize, glowSize, 0.0, glowAlpha);
                }
                appendSprite(holdTexture, QRectF(), center, particleSize, particleSize, 0.0, particleAlpha);
            }
        }
    }

    const auto drawJudgeEffectTrigger = [&](const JudgeEffectTrigger& trigger) {
        const bool useBreakShape = trigger.useBreakShape;
        const QImage* effectImage = useBreakShape ? &state.judgeEffect.tapBreakImage : &state.judgeEffect.tapImage;
        QRectF effectSourceRect = useBreakShape ? state.judgeEffect.tapBreakSourceRect : state.judgeEffect.tapSourceRect;
        if (effectImage == nullptr || effectImage->isNull()) {
            effectImage = useBreakShape ? &state.judgeEffect.tapImage : &state.judgeEffect.tapBreakImage;
            effectSourceRect = useBreakShape ? state.judgeEffect.tapSourceRect : state.judgeEffect.tapBreakSourceRect;
        }
        if (effectImage == nullptr || effectImage->isNull()) {
            return;
        }

        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - trigger.second);
        const qreal clipTime = judgeEffectClipTime(elapsedSeconds);
        qreal alpha = 0.0;
        qreal normalizedTime = 0.0;
        qreal motionClipTime = clipTime;
        if (trigger.useApproxAlpha) {
            const qreal visibleDuration = trigger.visibleEndSecond - trigger.visibleStartSecond;
            if (visibleDuration <= 0.0) {
                return;
            }
            normalizedTime = qBound<qreal>(
                0.0,
                static_cast<qreal>((state.playheadSeconds - trigger.visibleStartSecond) / visibleDuration),
                1.0
            );
            const qreal legacyMotionStart = qMax<qreal>(0.0, trigger.visibleStartSecond - trigger.second);
            const qreal legacyMotionEnd = qMax<qreal>(legacyMotionStart, trigger.visibleEndSecond - trigger.second);
            const qreal legacyMotionDuration = legacyMotionEnd - legacyMotionStart;
            motionClipTime = qBound<qreal>(
                0.0,
                legacyMotionStart + legacyMotionDuration * normalizedTime,
                kJudgeEffectDurationSeconds
            );
            alpha = qBound<qreal>(0.0, approximateLaneJudgeAlpha(normalizedTime), 1.0);
        } else {
            const qreal sampledAlpha = qBound<qreal>(0.0, sampleScalarHermiteCurve(kJudgeEffectAlphaKeys, clipTime), 1.0);
            alpha = qBound<qreal>(0.0, qPow(sampledAlpha, kJudgeEffectAlphaTailGamma), 1.0);
        }
        if (alpha <= kRenderDurationEpsilon) {
            return;
        }

        const qreal rootScale = sampleScalarCurve(kJudgeEffectRootScaleKeys, motionClipTime);
        const qreal laneFacingAngle = trigger.facingAngle;
        const qreal spriteAngleOffset =
            useBreakShape ? kJudgeEffectBreakTextureAngleOffsetDegrees : kJudgeEffectTapTextureAngleOffsetDegrees;
        const QPointF effectCenter = mapLogicalPointToRect(trigger.logicalCenter, playfieldRect);
        const int rootSize = qMax(1, qRound(effectBasePixels * rootScale));
        drawJudgeEffectShapeWithEdgeGlow(effectImage, effectSourceRect, effectCenter, rootSize, laneFacingAngle + spriteAngleOffset, alpha);

        const qreal rotationBase = trigger.useApproxAlpha
            ? (180.0 * normalizedTime)
            : sampleScalarCurve(kJudgeEffectRotationKeys, motionClipTime);
        for (int index = 0; index < static_cast<int>(kJudgeEffectParentRotationDirection.size()); ++index) {
            const qreal parentRotation =
                rotationBase * kJudgeEffectParentRotationDirection[static_cast<std::size_t>(index)];
            const QPointF localOffset =
                sampleVec2Curve(kJudgeEffectTapHexPositionKeys[static_cast<std::size_t>(index)], motionClipTime)
                * (effectOffsetPixels * rootScale);
            const QPointF childCenter = effectCenter + rotatePointDegrees(localOffset, laneFacingAngle + parentRotation);
            const qreal childScale =
                sampleScalarCurve(kJudgeEffectTapHexScaleKeys[static_cast<std::size_t>(index)], motionClipTime);
            const int childSize = qMax(1, qRound(effectBasePixels * rootScale * childScale));
            drawJudgeEffectShapeWithEdgeGlow(
                effectImage,
                effectSourceRect,
                childCenter,
                childSize,
                laneFacingAngle + spriteAngleOffset,
                alpha
            );
        }
    };

    for (const LaneJudgeEffectTrigger& laneTrigger : laneTriggers) {
        if (!laneTrigger.active) {
            continue;
        }
        drawJudgeEffectTrigger(laneTrigger.trigger);
    }
    for (const JudgeEffectTrigger& trigger : freeTriggers) {
        drawJudgeEffectTrigger(trigger);
    }

    return layerState;
}

}  // namespace miacode::preview::scene
