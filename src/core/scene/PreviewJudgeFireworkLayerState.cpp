#include "core/scene/PreviewJudgeFireworkLayerState.h"

#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"

#include <QtMath>
#include <array>

namespace {

constexpr qreal kJudgeEffectFireworkFallbackTouchPixels = 96.0;
constexpr qreal kJudgeEffectFireworkMinUnitPixels = 6.0;
constexpr qreal kJudgeEffectFireworkClipInsetPixels = 1.0;
constexpr qreal kJudgeEffectFireworkHoleMinFeatherPixels = 2.0;
constexpr qreal kJudgeEffectTouchUnitRelativeToTouch = 3.125;
constexpr qreal kJudgeEffectFireworkMinSpriteWidthPixels = 1.0;
constexpr qreal kJudgeEffectFireworkTouchTriggerDelaySeconds =
    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectFireworkTouchTriggerDelaySeconds);
constexpr qreal kJudgeEffectFireworkDurationSeconds =
    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds);
constexpr qreal kJudgeEffectFireworkBaseWidthUnits = 10.8;
constexpr qreal kJudgeEffectFireworkColorBallBaseWidthUnits = 5.12;
// fire.anim Firework material._Alpha: hold 0.589 until 0.5 s, then a flat-tangent Hermite
// (== smoothstep) down to 0 at the clip end. Replaces the steeper PreviewCanvas falloff; the
// old +20% brightness gain is dropped so every alpha tracks the reference 1:1.
constexpr qreal kJudgeEffectFireworkStripeAlphaPeak = 0.589;
constexpr qreal kJudgeEffectFireworkStripeAlphaHoldSeconds = 0.5;
constexpr qreal kJudgeEffectFireworkColorBallBigAlphaEndSeconds =
    kJudgeEffectFireworkDurationSeconds * 0.75;
constexpr qreal kJudgeEffectFireworkColorBallAlphaEndSeconds =
    kJudgeEffectFireworkColorBallBigAlphaEndSeconds * 0.5;
constexpr int kJudgeEffectFireworkStepRotationSegmentCount = 3;
constexpr qreal kJudgeEffectFireworkStepRotationDegrees = 24.0;
constexpr qreal kFireworkInnerLB = 0.018;
constexpr qreal kFireworkInnerUB = 0.054;
constexpr qreal kFireworkOuterLB = 0.36;
constexpr qreal kFireworkOuterUB = 0.429;
constexpr qreal kJudgeEffectFireworkHoleStartRadiusRatio = kFireworkInnerUB;
constexpr qreal kJudgeEffectFireworkHoleEndRadiusRatio = 1.0;
constexpr qreal kJudgeEffectFireworkHoleBandRatio =
    (kFireworkInnerUB - kFireworkInnerLB) + (kFireworkOuterUB - kFireworkOuterLB);

struct ScalarCurveKey {
    qreal time = 0.0;
    qreal value = 0.0;
};

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

// fire.anim ColorBall (small) scale: 0.2 -> 0.5 by 0.16667 s, then hold. (Overrides the
// earlier PreviewCanvas 0.1 s ramp in favor of MajdataPlay fire.anim.)
const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkColorBallScaleKeys = {{
    {0.0, 0.2},
    {0.16666667, 0.5},
    {1.3333334, 0.5},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkColorBallBigScaleKeys = {{
    {0.0, 1.0},
    {0.3, 1.15},
    {1.3333334, 1.15},
}};

// fire.anim ColorBall (small) m_Color.a, time-stretched to half of ColorBallBig's alpha lifetime:
// keeps the source ratios (hold at 1/3, near-gone at 2/3, gone at end).
const std::array<ScalarCurveKey, 5> kJudgeEffectFireworkColorBallAlphaKeys = {{
    {0.0, 0.9},
    {kJudgeEffectFireworkColorBallAlphaEndSeconds / 3.0, 0.9},
    {kJudgeEffectFireworkColorBallAlphaEndSeconds * 2.0 / 3.0, 0.1},
    {kJudgeEffectFireworkColorBallAlphaEndSeconds, 0.0},
    {kJudgeEffectFireworkDurationSeconds, 0.0},
}};

// ColorBallBig m_Color.a, time-stretched to 75% of the firework stripe lifetime:
// keeps the source ratios (0.0667/0.8833 hold and 0.1667/0.8833 near-gone).
const std::array<ScalarCurveKey, 5> kJudgeEffectFireworkColorBallBigAlphaKeys = {{
    {0.0, 0.9},
    {kJudgeEffectFireworkColorBallBigAlphaEndSeconds * 0.1, 0.9},
    {0.2, 0.3},
    {0.5, 0.0},
    {kJudgeEffectFireworkDurationSeconds, 0.0},
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

}  // namespace

namespace miacode::preview::scene {

PreviewJudgeFireworkLayerState buildPreviewJudgeFireworkLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewJudgeFireworkLayerState layerState;

    const TimelineNoteMarker* latestTriggerMarker = nullptr;
    qreal latestTriggerSecond = -1.0;
    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
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
    const qreal clipRadius = qMax<qreal>(1.0, outlineRadius - kJudgeEffectFireworkClipInsetPixels);

    const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds) - latestTriggerSecond;
    const qreal clipTime = qBound<qreal>(0.0, elapsedSeconds, kJudgeEffectFireworkDurationSeconds);
    const qreal life01 = qBound<qreal>(0.0, clipTime / kJudgeEffectFireworkDurationSeconds, 1.0);
    const qreal fireworkScale = qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkScaleKeys, clipTime));
    // fire.anim material._Alpha: 0.589 held to the hold point, then smoothstep to 0 (no gain).
    qreal fireworkAlpha = kJudgeEffectFireworkStripeAlphaPeak;
    if (clipTime > kJudgeEffectFireworkStripeAlphaHoldSeconds) {
        const qreal fadeSpan = kJudgeEffectFireworkDurationSeconds - kJudgeEffectFireworkStripeAlphaHoldSeconds;
        const qreal fadeU = (clipTime - kJudgeEffectFireworkStripeAlphaHoldSeconds) / fadeSpan;
        fireworkAlpha = kJudgeEffectFireworkStripeAlphaPeak * (1.0 - smoothStep01(fadeU));
    }
    const qreal fireworkRotationDegrees = sampleScalarCurve(kJudgeEffectFireworkRotationKeys, clipTime);
    const qreal colorBallScale = qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkColorBallScaleKeys, clipTime));
    const qreal colorBallBigScale =
        qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkColorBallBigScaleKeys, clipTime));
    const qreal colorBallAlpha = qBound<qreal>(
        0.0,
        sampleScalarCurve(kJudgeEffectFireworkColorBallAlphaKeys, clipTime),
        1.0
    );
    const qreal colorBallBigAlpha = qBound<qreal>(
        0.0,
        sampleScalarCurve(kJudgeEffectFireworkColorBallBigAlphaKeys, clipTime),
        1.0
    );
    const int stepRotationIndex = qBound(
        0,
        static_cast<int>(qFloor(life01 * static_cast<qreal>(kJudgeEffectFireworkStepRotationSegmentCount) + 0.5)),
        kJudgeEffectFireworkStepRotationSegmentCount
    );
    const qreal steppedRotationDegrees =
        -static_cast<qreal>(stepRotationIndex) * kJudgeEffectFireworkStepRotationDegrees;

    const qreal holeRadius = clipRadius * (
        kJudgeEffectFireworkHoleStartRadiusRatio
        + (kJudgeEffectFireworkHoleEndRadiusRatio - kJudgeEffectFireworkHoleStartRadiusRatio) * smoothStep01(life01)
    );
    const qreal holeFeather = qMax<qreal>(
        kJudgeEffectFireworkHoleMinFeatherPixels,
        holeRadius * kJudgeEffectFireworkHoleBandRatio
    );
    const qreal holeMaskRadius = holeRadius + holeFeather;
    const qreal outerRadius =
        qMax<qreal>(1.0, (kJudgeEffectFireworkBaseWidthUnits * fireworkScale * worldToPixels) * 0.5);
    const qreal colorBallWidth = kJudgeEffectFireworkColorBallBaseWidthUnits * colorBallScale * worldToPixels;
    const qreal colorBallBigWidth = kJudgeEffectFireworkColorBallBaseWidthUnits * colorBallBigScale * worldToPixels;
    const qreal colorBallRadius = qMax<qreal>(0.0, colorBallWidth * 0.5);
    const qreal colorBallBigRadius = qMax<qreal>(0.0, colorBallBigWidth * 0.5);
    const bool hasColorBallSprite = !state.judgeEffect.fireworkColorBallImage.isNull();
    const bool drawFirework =
        fireworkScale > kRenderDurationEpsilon && fireworkAlpha > kRenderDurationEpsilon;
    const bool drawColorBall = hasColorBallSprite
        && colorBallScale > kRenderDurationEpsilon
        && colorBallAlpha > kRenderDurationEpsilon
        && colorBallWidth > kJudgeEffectFireworkMinSpriteWidthPixels;
    const bool drawColorBallBig = hasColorBallSprite
        && colorBallBigScale > kRenderDurationEpsilon
        && colorBallBigAlpha > kRenderDurationEpsilon
        && colorBallBigWidth > kJudgeEffectFireworkMinSpriteWidthPixels;
    const qreal fallbackColorBallScale = qMax(colorBallScale, colorBallBigScale);
    const qreal fallbackColorBallAlpha = qMax(colorBallAlpha, colorBallBigAlpha);
    const qreal fallbackColorBallRadius =
        qMax<qreal>(1.0, (kJudgeEffectFireworkColorBallBaseWidthUnits * fallbackColorBallScale * worldToPixels) * 0.5);
    const bool drawFallbackColorBall = !hasColorBallSprite
        && (colorBallScale > kRenderDurationEpsilon || colorBallBigScale > kRenderDurationEpsilon);

    layerState.active = drawFirework
        || drawColorBall
        || drawColorBallBig
        || (drawFallbackColorBall && fallbackColorBallAlpha > kRenderDurationEpsilon);
    if (!layerState.active) {
        return PreviewJudgeFireworkLayerState();
    }

    layerState.drawFirework = drawFirework;
    layerState.drawColorBall = drawColorBall;
    layerState.drawColorBallBig = drawColorBallBig;
    layerState.drawFallbackColorBall = drawFallbackColorBall;
    layerState.center = mapLogicalPointToRect(latestTriggerMarker->touchPoint, playfieldRect);
    layerState.clipRadius = clipRadius;
    layerState.fireworkScale = fireworkScale;
    layerState.outerRadius = outerRadius;
    layerState.fireworkAlpha = fireworkAlpha;
    layerState.fireworkRotationDegrees = fireworkRotationDegrees + steppedRotationDegrees;
    layerState.holeRadius = holeRadius;
    layerState.holeMaskRadius = holeMaskRadius;
    layerState.colorBallScale = colorBallScale;
    layerState.colorBallBigScale = colorBallBigScale;
    layerState.colorBallAlpha = colorBallAlpha;
    layerState.colorBallBigAlpha = colorBallBigAlpha;
    layerState.colorBallRadius = colorBallRadius;
    layerState.colorBallBigRadius = colorBallBigRadius;
    layerState.fallbackColorBallRadius = fallbackColorBallRadius;
    layerState.fallbackColorBallAlpha = fallbackColorBallAlpha;
    layerState.colorBallImage =
        state.judgeEffect.fireworkColorBallImage.isNull() ? nullptr : &state.judgeEffect.fireworkColorBallImage;
    layerState.colorBallSourceRect = state.judgeEffect.fireworkColorBallSourceRect;
    return layerState;
}

}  // namespace miacode::preview::scene
