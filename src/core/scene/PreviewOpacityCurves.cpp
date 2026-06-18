#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewSceneConstants.h"

#include <QtMath>

#include <cmath>

namespace {

qreal touchPreHitElapsedSeconds(qreal deltaSeconds, qreal touchDurationSeconds)
{
    return qBound<qreal>(0.0, deltaSeconds + touchDurationSeconds, touchDurationSeconds);
}

}  // namespace

namespace miacode::preview::scene {

PreviewTapTiming previewTapTimingForFlowSpeed(qreal flowSpeed)
{
    const double normalizedFlowSpeed =
        miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(static_cast<double>(flowSpeed));
    const double timingScaleFrames =
        miacode::preview_gameplay::previewTimingScaleFramesAt120Fps(normalizedFlowSpeed);

    PreviewTapTiming timing;
    timing.spawnDurationSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(timingScaleFrames)
    );
    timing.flyDurationSeconds = timing.spawnDurationSeconds;
    timing.lifecycleDurationSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(timingScaleFrames * 2.0)
    );
    timing.unitsPerSecond = static_cast<qreal>(
        (miacode::preview_gameplay::kLogicalDistanceEdge - miacode::preview_gameplay::kLogicalDistanceTap)
        / qMax(0.0001, static_cast<double>(timing.flyDurationSeconds))
    );
    return timing;
}

PreviewSlideTrackTiming previewSlideTrackTimingForFlowSpeed(qreal flowSpeed)
{
    const double normalizedFlowSpeed =
        miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(static_cast<double>(flowSpeed));
    const double timingScaleFrames =
        miacode::preview_gameplay::previewTimingScaleFramesAt120Fps(normalizedFlowSpeed);

    PreviewSlideTrackTiming timing;
    timing.appearLeadInSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(timingScaleFrames + 2.0)
    );
    timing.fullBrightLeadInSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(
            miacode::preview_gameplay::kSlideTrackFullBrightLeadInFramesAt120Fps
        )
    );
    return timing;
}

PreviewTouchTiming previewTouchTimingForFlowSpeed(qreal flowSpeed)
{
    const PreviewTapTiming tapTiming = previewTapTimingForFlowSpeed(flowSpeed);

    PreviewTouchTiming timing;
    timing.durationSeconds = tapTiming.lifecycleDurationSeconds;
    timing.showDurationSeconds = timing.durationSeconds / 5.0;
    timing.closeDurationSeconds = timing.durationSeconds - timing.showDurationSeconds;
    return timing;
}

PreviewTapTiming previewTapTimingForEffectiveFlowSpeed(qreal effectiveFlowSpeed)
{
    // Magnitude/sign split: the magnitude drives all durations and the
    // (positive) radial speed; the sign is carried out separately as
    // directionSign so a negative HS reverses the flow direction WITHOUT
    // collapsing the timing (a naive qMax(0.0001, speed) floor would turn a
    // negative speed into a ~5h crawl instead of reverse flow).
    const double signedFlowSpeed = static_cast<double>(effectiveFlowSpeed);
    const double safeFlowSpeed = qMax(0.0001, qAbs(signedFlowSpeed));
    const double timingScaleFrames =
        miacode::preview_gameplay::kPreviewTimingBaseFrames
        + miacode::preview_gameplay::kPreviewTimingFlowFramesNumerator / safeFlowSpeed;

    PreviewTapTiming timing;
    timing.spawnDurationSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(timingScaleFrames)
    );
    timing.flyDurationSeconds = timing.spawnDurationSeconds;
    timing.lifecycleDurationSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(timingScaleFrames * 2.0)
    );
    timing.unitsPerSecond = static_cast<qreal>(
        (miacode::preview_gameplay::kLogicalDistanceEdge - miacode::preview_gameplay::kLogicalDistanceTap)
        / qMax(0.0001, static_cast<double>(timing.flyDurationSeconds))
    );
    timing.directionSign = signedFlowSpeed < 0.0 ? -1.0 : 1.0;
    return timing;
}

PreviewSlideTrackTiming previewSlideTrackTimingForEffectiveFlowSpeed(qreal effectiveFlowSpeed)
{
    // Slides never reverse (per-type policy uses the magnitude), so fold the
    // sign defensively here too — a leaked negative must not collapse timing.
    const double safeFlowSpeed = qMax(0.0001, qAbs(static_cast<double>(effectiveFlowSpeed)));
    const double timingScaleFrames =
        miacode::preview_gameplay::kPreviewTimingBaseFrames
        + miacode::preview_gameplay::kPreviewTimingFlowFramesNumerator / safeFlowSpeed;

    PreviewSlideTrackTiming timing;
    timing.appearLeadInSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(timingScaleFrames + 2.0)
    );
    timing.fullBrightLeadInSeconds = static_cast<qreal>(
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(
            miacode::preview_gameplay::kSlideTrackFullBrightLeadInFramesAt120Fps
        )
    );
    return timing;
}

PreviewTouchTiming previewTouchTimingForEffectiveFlowSpeed(qreal effectiveFlowSpeed)
{
    const PreviewTapTiming tapTiming = previewTapTimingForEffectiveFlowSpeed(effectiveFlowSpeed);

    PreviewTouchTiming timing;
    timing.durationSeconds = tapTiming.lifecycleDurationSeconds;
    timing.showDurationSeconds = timing.durationSeconds / 5.0;
    timing.closeDurationSeconds = timing.durationSeconds - timing.showDurationSeconds;
    return timing;
}

qreal touchPreHitAlpha(qreal deltaSeconds, qreal touchDurationSeconds, qreal touchShowDurationSeconds)
{
    if (deltaSeconds >= 0.0) {
        return 1.0;
    }
    return qBound<qreal>(
        0.0,
        touchPreHitElapsedSeconds(deltaSeconds, touchDurationSeconds)
            / qMax<qreal>(miacode::preview::scene::kTouchPhaseDivisionEpsilonSeconds, touchShowDurationSeconds),
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
            / qMax<qreal>(miacode::preview::scene::kTouchPhaseDivisionEpsilonSeconds, touchCloseDurationSeconds),
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
        miacode::preview::scene::kTouchCloseCurveResidualBias
            - qExp(
                miacode::preview::scene::kTouchCloseCurveExponent * clampedProgress
                - miacode::preview::scene::kTouchCloseCurveProgressBias
            ),
        miacode::preview::scene::kTouchPrefabMaxCloseAmountNormalized
    );
    return range * (1.0 - closeResidualNormalized / miacode::preview::scene::kTouchPrefabMaxCloseAmountNormalized);
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

qreal maimuriDxSimpleJudgeAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeInEndSeconds, qreal fadeOutStartSeconds, qreal fadeOutEndSeconds)
{
    if (elapsedSeconds < 0.0 || elapsedSeconds > lifetimeSeconds) {
        return 0.0;
    }
    if (elapsedSeconds < fadeInEndSeconds) {
        return qBound<qreal>(
            0.0,
            elapsedSeconds / qMax<qreal>(miacode::preview::scene::kRenderDurationEpsilon, fadeInEndSeconds),
            1.0
        );
    }
    if (elapsedSeconds <= fadeOutStartSeconds) {
        return 1.0;
    }
    if (elapsedSeconds >= fadeOutEndSeconds) {
        return 0.0;
    }
    return qBound<qreal>(
        0.0,
        1.0 - (elapsedSeconds - fadeOutStartSeconds)
            / qMax<qreal>(miacode::preview::scene::kRenderDurationEpsilon, fadeOutEndSeconds - fadeOutStartSeconds),
        1.0
    );
}

qreal maimuriDxSimpleJudgeScale(qreal elapsedSeconds, qreal startScale, qreal endScale, qreal popEndSeconds)
{
    if (elapsedSeconds <= 0.0) {
        return startScale;
    }
    if (popEndSeconds <= 0.0 || elapsedSeconds >= popEndSeconds) {
        return endScale;
    }
    const qreal t = elapsedSeconds / popEndSeconds;
    return startScale + (endScale - startScale) * t;
}

qreal maimuriDxSimpleJudgeBreakFlash(qreal elapsedSeconds, qreal periodSeconds, qreal endSeconds, qreal floorValue)
{
    if (elapsedSeconds < 0.0 || elapsedSeconds >= endSeconds || periodSeconds <= 0.0) {
        return 1.0;
    }
    // Triangle 0->1->0 each period, starting at 0 (off) and peaking at period/2 — matches the
    // MajdataPlay JudgeCPBreak alpha keyframes (off at frame 0, on at the half-period).
    const qreal phase = std::fmod(elapsedSeconds, periodSeconds) / periodSeconds;
    const qreal triangle = phase < 0.5 ? (phase * 2.0) : (2.0 - phase * 2.0);
    const qreal clampedFloor = qBound<qreal>(0.0, floorValue, 1.0);
    return clampedFloor + (1.0 - clampedFloor) * triangle;
}

qreal maimuriDxSlideJudgeAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeInEndSeconds, qreal fadeOutStartSeconds, qreal fadeOutEndSeconds)
{
    if (elapsedSeconds < 0.0 || elapsedSeconds > lifetimeSeconds) {
        return 0.0;
    }
    if (elapsedSeconds < fadeInEndSeconds) {
        return qBound<qreal>(
            0.0,
            elapsedSeconds / qMax<qreal>(miacode::preview::scene::kRenderDurationEpsilon, fadeInEndSeconds),
            1.0
        );
    }
    if (elapsedSeconds <= fadeOutStartSeconds) {
        return 1.0;
    }
    if (elapsedSeconds >= fadeOutEndSeconds) {
        return 0.0;
    }
    return qBound<qreal>(
        0.0,
        1.0 - (elapsedSeconds - fadeOutStartSeconds)
            / qMax<qreal>(miacode::preview::scene::kRenderDurationEpsilon, fadeOutEndSeconds - fadeOutStartSeconds),
        1.0
    );
}

TapApproachSample sampleTapApproach(
    qreal deltaSeconds,
    qreal tapLifecycleDurationSeconds,
    qreal tapSpawnDurationSeconds,
    qreal tapFlyDurationSeconds,
    qreal tapUnitsPerSecond,
    qreal logicalDistanceTap,
    qreal logicalDistanceEdge,
    qreal directionSign
)
{
    // directionSign defaults to +1 (forward). With +1 this function is
    // byte-identical to the original inner->ring flow. directionSign < 0 is
    // reverse flow (negative HS): the note comes from the OUTER side and flies
    // INWARD to the judgement ring, HIT AT THE RING (delta=0 -> edge) exactly
    // like a forward note — only the approach direction is mirrored. This is
    // the MajdataPlay-faithful semantics. Per product decision NO clipping is
    // applied — semantic correctness only; the off-screen outer portion of the
    // flight is simply not visible (natural preview bounds), it is not masked.
    const bool reverse = directionSign < 0.0;
    const qreal outerSpawnDistance = 2.0 * logicalDistanceEdge - logicalDistanceTap;
    const qreal spawnDistance = reverse ? outerSpawnDistance : logicalDistanceTap;

    TapApproachSample approach;
    approach.distance = spawnDistance;
    if (tapLifecycleDurationSeconds <= 0.0 || deltaSeconds <= -tapLifecycleDurationSeconds) {
        return approach;
    }

    if (deltaSeconds < -tapFlyDurationSeconds) {
        approach.scale = qBound<qreal>(
            0.0,
            (deltaSeconds + tapLifecycleDurationSeconds)
                / qMax<qreal>(miacode::preview::scene::kRenderDurationEpsilon, tapSpawnDurationSeconds),
            1.0
        );
        return approach;
    }

    if (deltaSeconds < 0.0) {
        if (reverse) {
            // [outer -> edge] as delta goes -fly -> 0: starts beyond the ring
            // and decreases to the ring. Hit position is the ring (edge), same
            // as forward — semantics preserved.
            approach.distance = qBound<qreal>(
                logicalDistanceEdge,
                logicalDistanceEdge - deltaSeconds * tapUnitsPerSecond,
                outerSpawnDistance
            );
        } else {
            approach.distance = qBound<qreal>(
                logicalDistanceTap,
                logicalDistanceTap + (deltaSeconds + tapFlyDurationSeconds) * tapUnitsPerSecond,
                logicalDistanceEdge
            );
        }
        approach.scale = 1.0;
        return approach;
    }

    approach.distance = logicalDistanceEdge;
    approach.scale = 1.0;
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
    if (playheadSecond < startSecond) {
        return -1.0;
    }
    const qreal brightStartSecond = markerSecond - fullBrightLeadInSeconds;
    if (playheadSecond >= brightStartSecond) {
        return 1.0;
    }
    return qBound<qreal>(
        0.0,
        (playheadSecond - startSecond)
            / qMax<qreal>(miacode::preview::scene::kRenderDurationEpsilon, brightStartSecond - startSecond),
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
