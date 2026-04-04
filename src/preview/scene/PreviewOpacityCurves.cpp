#include "preview/scene/PreviewOpacityCurves.h"

#include <QtMath>

namespace {

constexpr qreal kRenderDurationEpsilon = 1e-4;
constexpr qreal kTouchPhaseDivisionEpsilonSeconds = 1e-6;
constexpr qreal kTouchCloseCurveResidualBias = 1.35;
constexpr qreal kTouchCloseCurveExponent = 3.0;
constexpr qreal kTouchCloseCurveProgressBias = 0.60;
constexpr qreal kTouchPrefabMaxCloseAmountNormalized = 0.94;

qreal touchPreHitElapsedSeconds(qreal deltaSeconds, qreal touchDurationSeconds)
{
    return qBound<qreal>(0.0, deltaSeconds + touchDurationSeconds, touchDurationSeconds);
}

}  // namespace

namespace miacode::preview::scene {

qreal touchPreHitAlpha(qreal deltaSeconds, qreal touchDurationSeconds, qreal touchShowDurationSeconds)
{
    if (deltaSeconds >= 0.0) {
        return 1.0;
    }
    return qBound<qreal>(
        0.0,
        touchPreHitElapsedSeconds(deltaSeconds, touchDurationSeconds)
            / qMax<qreal>(kTouchPhaseDivisionEpsilonSeconds, touchShowDurationSeconds),
        1.0
    );
}

qreal touchCloseProgress(qreal deltaSeconds, qreal touchDurationSeconds, qreal touchShowDurationSeconds, qreal touchCloseDurationSeconds)
{
    if (deltaSeconds >= 0.0) {
        return 1.0;
    }
    const qreal preHitElapsed = touchPreHitElapsedSeconds(deltaSeconds, touchDurationSeconds);
    if (preHitElapsed <= touchShowDurationSeconds) {
        return 0.0;
    }
    return qBound<qreal>(
        0.0,
        (preHitElapsed - touchShowDurationSeconds)
            / qMax<qreal>(kTouchPhaseDivisionEpsilonSeconds, touchCloseDurationSeconds),
        1.0
    );
}

qreal touchCloseAmount(qreal progress, qreal range)
{
    if (range <= 0.0) {
        return 0.0;
    }
    const qreal clampedProgress = qBound<qreal>(0.0, progress, 1.0);
    const qreal closeResidualNormalized = qBound<qreal>(
        0.0,
        kTouchCloseCurveResidualBias - qExp(kTouchCloseCurveExponent * clampedProgress - kTouchCloseCurveProgressBias),
        kTouchPrefabMaxCloseAmountNormalized
    );
    return range * (1.0 - closeResidualNormalized / kTouchPrefabMaxCloseAmountNormalized);
}

qreal touchLogicalOffsetForDelta(
    qreal deltaSeconds,
    qreal startOffset,
    qreal endOffset,
    qreal touchDurationSeconds,
    qreal touchShowDurationSeconds,
    qreal touchCloseDurationSeconds
)
{
    if (deltaSeconds >= 0.0) {
        return endOffset;
    }
    const qreal range = qMax<qreal>(0.0, startOffset - endOffset);
    return startOffset - touchCloseAmount(
        touchCloseProgress(deltaSeconds, touchDurationSeconds, touchShowDurationSeconds, touchCloseDurationSeconds),
        range
    );
}

qreal muriFlashOpacity(double flashSecond, double playheadSeconds)
{
    if (flashSecond < 0.0) {
        return 0.0;
    }
    constexpr qreal kFlashDurationSeconds = 0.14;
    const qreal elapsed = static_cast<qreal>(playheadSeconds - flashSecond);
    if (elapsed < 0.0 || elapsed > kFlashDurationSeconds) {
        return 0.0;
    }
    return qBound<qreal>(0.0, 0.45 * (1.0 - elapsed / kFlashDurationSeconds), 0.45);
}

qreal maimuriDxJudgeFadeOutAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeOutStartSeconds)
{
    if (elapsedSeconds < 0.0 || elapsedSeconds > lifetimeSeconds) {
        return 0.0;
    }
    if (elapsedSeconds <= fadeOutStartSeconds) {
        return 1.0;
    }
    return qBound<qreal>(
        0.0,
        1.0 - (elapsedSeconds - fadeOutStartSeconds)
            / qMax<qreal>(kRenderDurationEpsilon, lifetimeSeconds - fadeOutStartSeconds),
        1.0
    );
}

qreal maimuriDxSimpleJudgeAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeOutStartSeconds, qreal fadeInSeconds)
{
    if (elapsedSeconds < 0.0 || elapsedSeconds > lifetimeSeconds) {
        return 0.0;
    }
    if (elapsedSeconds < fadeInSeconds) {
        return qBound<qreal>(
            0.0,
            elapsedSeconds / qMax<qreal>(kRenderDurationEpsilon, fadeInSeconds),
            1.0
        );
    }
    return maimuriDxJudgeFadeOutAlpha(elapsedSeconds, lifetimeSeconds, fadeOutStartSeconds);
}

TapApproachSample sampleTapApproach(
    qreal deltaSeconds,
    qreal tapLifecycleDurationSeconds,
    qreal tapSpawnDurationSeconds,
    qreal tapFlyDurationSeconds,
    qreal tapUnitsPerSecond,
    qreal logicalDistanceTap,
    qreal logicalDistanceEdge
)
{
    TapApproachSample approach;
    if (tapLifecycleDurationSeconds <= 0.0 || deltaSeconds < -tapLifecycleDurationSeconds || deltaSeconds > 0.0) {
        return approach;
    }

    const qreal elapsed = deltaSeconds + tapLifecycleDurationSeconds;
    if (elapsed <= tapSpawnDurationSeconds) {
        const qreal t = qBound<qreal>(0.0, elapsed / qMax<qreal>(kRenderDurationEpsilon, tapSpawnDurationSeconds), 1.0);
        approach.distance = logicalDistanceTap;
        approach.scale = t;
        return approach;
    }

    const qreal flyElapsed = elapsed - tapSpawnDurationSeconds;
    const qreal rawDistance = logicalDistanceTap + flyElapsed * tapUnitsPerSecond;
    approach.distance = qBound<qreal>(logicalDistanceTap, rawDistance, logicalDistanceEdge);
    approach.scale = 1.0;
    if (tapFlyDurationSeconds > 0.0 && flyElapsed > tapFlyDurationSeconds) {
        const qreal fadeElapsed = flyElapsed - tapFlyDurationSeconds;
        const qreal fadeDuration = qMax<qreal>(kRenderDurationEpsilon, tapLifecycleDurationSeconds - tapSpawnDurationSeconds - tapFlyDurationSeconds);
        approach.scale = qBound<qreal>(0.0, 1.0 - fadeElapsed / fadeDuration, 1.0);
    }
    return approach;
}

qreal sampleSlideTrackPreTraceOpacity(
    qreal markerSecond,
    qreal playheadSecond,
    qreal appearLeadInSeconds,
    qreal fullBrightLeadInSeconds
)
{
    const qreal startSecond = markerSecond - appearLeadInSeconds;
    if (playheadSecond < startSecond || playheadSecond >= markerSecond) {
        return 0.0;
    }
    const qreal brightStartSecond = markerSecond - fullBrightLeadInSeconds;
    if (playheadSecond >= brightStartSecond) {
        return 1.0;
    }
    return qBound<qreal>(
        0.0,
        (playheadSecond - startSecond)
            / qMax<qreal>(kRenderDurationEpsilon, brightStartSecond - startSecond),
        1.0
    );
}

qreal waitingStarOpacity(qreal waitT, qreal waitStartOpacity, qreal waitOpacityDelta)
{
    return waitStartOpacity + waitOpacityDelta * waitT;
}

qreal exportWifiTrackCompensationOpacity(qreal opacity)
{
    return qBound<qreal>(0.0, opacity * 0.18, 0.20);
}

}  // namespace miacode::preview::scene
