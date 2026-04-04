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

PreviewTapTiming previewTapTimingForFlowSpeed(qreal flowSpeed);
PreviewSlideTrackTiming previewSlideTrackTimingForFlowSpeed(qreal flowSpeed);
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
qreal maimuriDxJudgeFadeOutAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeOutStartSeconds);
qreal maimuriDxSimpleJudgeAlpha(qreal elapsedSeconds, qreal lifetimeSeconds, qreal fadeOutStartSeconds, qreal fadeInSeconds);
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
