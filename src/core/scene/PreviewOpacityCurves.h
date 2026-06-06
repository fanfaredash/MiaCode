#pragma once

#include <QPointF>

namespace miacode::preview::scene {

struct TapApproachSample {
    qreal distance = 0.0;
    qreal scale = 0.0;
};

struct PreviewTapTiming {
    qreal lifecycleDurationSeconds = 0.0;
    qreal spawnDurationSeconds = 0.0;
    qreal flyDurationSeconds = 0.0;
    qreal unitsPerSecond = 0.0;
};

struct PreviewSlideTrackTiming {
    qreal appearLeadInSeconds = 0.0;
    qreal fullBrightLeadInSeconds = 0.0;
};

struct PreviewTouchTiming {
    qreal durationSeconds = 0.0;
    qreal showDurationSeconds = 0.0;
    qreal closeDurationSeconds = 0.0;
};

PreviewTapTiming previewTapTimingForFlowSpeed(qreal flowSpeed);
PreviewSlideTrackTiming previewSlideTrackTimingForFlowSpeed(qreal flowSpeed);
PreviewTouchTiming previewTouchTimingForFlowSpeed(qreal flowSpeed);

// HS-aware variants that take the *effective* speed (= userFlowSpeed *
// hsMultiplier) without re-clamping to the user-setting slider range.
// Per Q4 the user setting is clamped at the boundary where it enters the
// system; HS-multiplied effective speed must be allowed to range beyond
// those bounds. A small positive floor is still applied to avoid
// divide-by-zero / runaway timings on HS=0 (which the parser already
// rejects upstream, but defensively floor here too).
PreviewTapTiming previewTapTimingForEffectiveFlowSpeed(qreal effectiveFlowSpeed);
PreviewSlideTrackTiming previewSlideTrackTimingForEffectiveFlowSpeed(qreal effectiveFlowSpeed);
PreviewTouchTiming previewTouchTimingForEffectiveFlowSpeed(qreal effectiveFlowSpeed);
qreal touchPreHitAlpha(qreal deltaSeconds, qreal touchDurationSeconds, qreal touchShowDurationSeconds);
qreal touchCloseProgress(qreal deltaSeconds, qreal touchDurationSeconds, qreal touchShowDurationSeconds, qreal touchCloseDurationSeconds);
qreal touchCloseAmount(qreal progress, qreal range);
qreal touchLogicalOffsetForDelta(
    qreal deltaSeconds,
    qreal startOffset,
    qreal endOffset,
    qreal touchDurationSeconds,
    qreal touchShowDurationSeconds,
    qreal touchCloseDurationSeconds
);
qreal muriFlashOpacity(double flashSecond, double playheadSeconds);
qreal maimuriDxSimpleJudgeAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeInEndSeconds, qreal fadeOutStartSeconds, qreal fadeOutEndSeconds);
// Judge-text scale-pop entrance: linear startScale -> endScale over popEndSeconds, then holds endScale.
qreal maimuriDxSimpleJudgeScale(qreal elapsedSeconds, qreal startScale, qreal endScale, qreal popEndSeconds);
// Break judge-text flash: 0<->1 triangle wave (period periodSeconds) mapped to [floorValue, 1.0],
// active over [0, endSeconds); returns 1.0 outside that window.
qreal maimuriDxSimpleJudgeBreakFlash(qreal elapsedSeconds, qreal periodSeconds, qreal endSeconds, qreal floorValue);
qreal maimuriDxSlideJudgeAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeInEndSeconds, qreal fadeOutStartSeconds, qreal fadeOutEndSeconds);
TapApproachSample sampleTapApproach(
    qreal deltaSeconds,
    qreal tapLifecycleDurationSeconds,
    qreal tapSpawnDurationSeconds,
    qreal tapFlyDurationSeconds,
    qreal tapUnitsPerSecond,
    qreal logicalDistanceTap,
    qreal logicalDistanceEdge
);
qreal sampleSlideTrackPreTraceOpacity(
    qreal markerSecond,
    qreal playheadSecond,
    qreal appearLeadInSeconds,
    qreal fullBrightLeadInSeconds
);
qreal waitingStarOpacity(qreal waitT, qreal waitStartOpacity, qreal waitOpacityDelta);
qreal exportWifiTrackCompensationOpacity(qreal opacity);

}  // namespace miacode::preview::scene
