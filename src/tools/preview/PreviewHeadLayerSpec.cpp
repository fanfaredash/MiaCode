#include <QCoreApplication>
#include <QImage>
#include <QRectF>
#include <QTextStream>

#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewHeadLayerState.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"
#include "core/chart/parser/SimaiNativeParser.h"

namespace {

using miacode::preview::scene::PreviewFrameState;
using miacode::preview::scene::PreviewHeadLayerState;
using miacode::preview::scene::PreviewTapTiming;

constexpr qreal kEpsilon = 1e-3;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool requireNear(qreal actual, qreal expected, const QString& label, QTextStream& err)
{
    if (qAbs(actual - expected) <= kEpsilon) {
        return true;
    }
    err << label << " expected " << expected << " but got " << actual << Qt::endl;
    return false;
}

QImage solidImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(qRgba(255, 255, 255, 255));
    return image;
}

qreal totalSlideTraceDurationSeconds(const TimelineNoteMarker& marker)
{
    qreal duration = 0.0;
    for (double segmentDuration : marker.slideSegmentDurations) {
        duration += static_cast<qreal>(qMax(0.0, segmentDuration));
    }
    if (duration > 0.0) {
        return duration;
    }
    if (marker.endSecond > marker.slideTraceSecond) {
        return static_cast<qreal>(marker.endSecond - marker.slideTraceSecond);
    }
    return 0.0;
}

qreal slideHeadRotateSpeedDegreesPerSecond(const TimelineNoteMarker& marker)
{
    const qreal totalDuration = totalSlideTraceDurationSeconds(marker);
    return -static_cast<qreal>(
        miacode::preview_gameplay::previewSlideHeadRotationSpeedDegreesPerSecond(
            marker.slideNativeTrackLength,
            totalDuration));
}

qreal slideHeadFallRotationDegrees(
    const TimelineNoteMarker& marker,
    qreal deltaSeconds,
    qreal tapLifecycleDurationSeconds
)
{
    if (deltaSeconds >= 0.0 || tapLifecycleDurationSeconds <= 0.0) {
        return 0.0;
    }
    const qreal elapsedSeconds =
        qBound<qreal>(0.0, deltaSeconds + tapLifecycleDurationSeconds, tapLifecycleDurationSeconds);
    return slideHeadRotateSpeedDegreesPerSecond(marker) * elapsedSeconds;
}

qreal mirroredStarAngleDegrees(qreal angleDegrees)
{
    return 360.0 - angleDegrees;
}

PreviewHeadLayerState buildPreparedHeadLayerState(
    const PreviewFrameState& state,
    miacode::preview::scene::PreviewHeadRenderAssetCache* renderAssetCache = nullptr)
{
    miacode::preview::scene::PreviewPreparedSceneCache preparedCache;
    preparedCache.sync(state);
    miacode::preview::scene::PreviewLayerWindowCursor headCursor;
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache.headLayer(), state.playheadSeconds, &headCursor);
    const miacode::preview::scene::PreviewActiveMarkerView activeMarkers(
        state.noteMarkers,
        preparedCache.headLayer(),
        headCursor
    );
    return miacode::preview::scene::buildPreviewHeadLayerState(
        state,
        activeMarkers,
        QRectF(
            0.0,
            0.0,
            miacode::preview::scene::kLogicalCanvasSize,
            miacode::preview::scene::kLogicalCanvasSize),
        renderAssetCache
    );
}

PreviewHeadLayerState buildFallbackHeadLayerState(
    const PreviewFrameState& state,
    miacode::preview::scene::PreviewHeadRenderAssetCache* renderAssetCache = nullptr)
{
    return miacode::preview::scene::buildPreviewHeadLayerState(
        state,
        miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers),
        QRectF(
            0.0,
            0.0,
            miacode::preview::scene::kLogicalCanvasSize,
            miacode::preview::scene::kLogicalCanvasSize),
        renderAssetCache
    );
}

QPointF expectedHeadCenterForLane(
    int lane,
    qreal second,
    qreal playheadSeconds,
    const PreviewTapTiming& tapTiming)
{
    const miacode::preview::scene::TapApproachSample approach = miacode::preview::scene::sampleTapApproach(
        playheadSeconds - second,
        tapTiming.lifecycleDurationSeconds,
        tapTiming.spawnDurationSeconds,
        tapTiming.flyDurationSeconds,
        tapTiming.unitsPerSecond,
        miacode::preview::scene::kLogicalDistanceTap,
        miacode::preview::scene::kLogicalDistanceEdge);
    const QPointF unit = miacode::preview::scene::laneUnitVector(lane);
    const QPointF logicalPoint(
        miacode::preview::scene::kLogicalCanvasCenter + unit.x() * approach.distance,
        miacode::preview::scene::kLogicalCanvasCenter + unit.y() * approach.distance
    );
    return miacode::preview::scene::mapLogicalPointToRect(
        logicalPoint,
        QRectF(
            0.0,
            0.0,
            miacode::preview::scene::kLogicalCanvasSize,
            miacode::preview::scene::kLogicalCanvasSize));
}

bool verifySameHeadBranchesCollapseToFastestRotation(QTextStream& err)
{
    const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
        QStringLiteral("(120){8}1-3[8:1]*-4[8:2]*-5[8:3],\nE")
    );
    if (!require(parsed.ok, QStringLiteral("branching slide chart parses"), err)) {
        return false;
    }

    QVector<const TimelineNoteMarker*> slideMarkers;
    for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
        if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
            slideMarkers.append(&marker);
        }
    }
    if (!require(slideMarkers.size() == 3, QStringLiteral("branching slide chart emits three slide markers"), err)) {
        return false;
    }

    const TimelineNoteMarker* fastestMarker = slideMarkers.first();
    qreal fastestSpeed = slideHeadRotateSpeedDegreesPerSecond(*fastestMarker);
    for (const TimelineNoteMarker* marker : slideMarkers) {
        const qreal speed = slideHeadRotateSpeedDegreesPerSecond(*marker);
        if (speed < fastestSpeed) {
            fastestSpeed = speed;
            fastestMarker = marker;
        }
    }
    if (!require(fastestMarker != nullptr, QStringLiteral("fastest branching slide marker is found"), err)) {
        return false;
    }

    PreviewFrameState state;
    state.noteMarkers = parsed.noteMarkers;
    state.skin.tapImage = solidImage(128, 128);
    state.skin.starImage = solidImage(126, 126);

    const PreviewTapTiming tapTiming =
        miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(state.render.tapFlowSpeed));
    state.playheadSeconds = fastestMarker->second - tapTiming.flyDurationSeconds * 0.5;

    const PreviewHeadLayerState layerState = buildPreparedHeadLayerState(state);

    if (!require(layerState.sprites.size() == 1, QStringLiteral("same-head falling slide renders exactly one head sprite"), err)) {
        return false;
    }

    const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - fastestMarker->second);
    const qreal expectedRotation =
        mirroredStarAngleDegrees(
            miacode::preview::scene::laneRotationDegrees(fastestMarker->lane)
            + slideHeadFallRotationDegrees(*fastestMarker, deltaSeconds, tapTiming.lifecycleDurationSeconds)
        );
    if (!requireNear(
            layerState.sprites.constFirst().rotationDegrees,
            expectedRotation,
            QStringLiteral("same-head falling slide rotation"),
            err)) {
        return false;
    }

    return true;
}

bool verifyCalibratedSlideHeadRotation(QTextStream& err)
{
    struct RotationCase {
        int slideImageCount;
        int expectedRoundedFrames;
    };
    const RotationCase cases[] = {
        {18, 16},
        {23, 14},
        {30, 12},
        {39, 10},
    };

    for (const RotationCase& testCase : cases) {
        const double nativeTrackLength = static_cast<double>(testCase.slideImageCount + 1);
        const double speed =
            miacode::preview_gameplay::previewSlideHeadRotationSpeedDegreesPerSecond(
                nativeTrackLength,
                0.5);
        const int roundedFrames = qRound(
            miacode::preview_gameplay::kSlideHeadRotationReferenceDegrees
            * miacode::preview_gameplay::kPreviewTimingFramesPerSecond
            / speed);
        if (!require(
                roundedFrames == testCase.expectedRoundedFrames,
                QStringLiteral("%1 slide images rotate 72 degrees in %2 rounded frames, got %3")
                    .arg(testCase.slideImageCount)
                    .arg(testCase.expectedRoundedFrames)
                    .arg(roundedFrames),
                err)) {
            return false;
        }
    }
    return true;
}

bool verifyCalibratedTapTiming(QTextStream& err)
{
    struct TimingCase {
        qreal flowSpeed;
        qreal expectedLifecycleFrames;
    };
    const TimingCase cases[] = {
        {7.5, 66.0},
        {8.0, 62.0},
    };

    for (const TimingCase& testCase : cases) {
        const PreviewTapTiming timing =
            miacode::preview::scene::previewTapTimingForFlowSpeed(testCase.flowSpeed);
        const qreal lifecycleFrames =
            timing.lifecycleDurationSeconds
            * miacode::preview_gameplay::kPreviewTimingFramesPerSecond;
        if (!requireNear(
                lifecycleFrames,
                testCase.expectedLifecycleFrames,
                QStringLiteral("tap speed %1 lifecycle frames").arg(testCase.flowSpeed),
                err)) {
            return false;
        }
    }
    return true;
}

bool verifyHeadRenderAssetCacheReuseAndInvalidation(QTextStream& err)
{
    PreviewFrameState state;
    TimelineNoteMarker marker;
    marker.type = QStringLiteral("tap");
    marker.second = 1.0;
    marker.lane = 1;
    marker.isEx = true;
    state.noteMarkers.append(marker);
    state.playheadSeconds = 0.8;
    state.skin.tapImage = solidImage(128, 128);
    state.skin.tapExImage = solidImage(96, 96);

    miacode::preview::scene::PreviewHeadRenderAssetCache renderAssetCache;
    const PreviewHeadLayerState firstPass = buildPreparedHeadLayerState(state, &renderAssetCache);
    if (!require(firstPass.sprites.size() == 1, QStringLiteral("first ex tap render produces one sprite"), err)) {
        return false;
    }
    if (!require(renderAssetCache.missCount() == 1, QStringLiteral("first ex tap render records one cache miss"), err)) {
        return false;
    }
    if (!require(renderAssetCache.hitCount() == 0, QStringLiteral("first ex tap render records zero cache hits"), err)) {
        return false;
    }
    if (!require(renderAssetCache.entryCount() == 1, QStringLiteral("first ex tap render caches one composed image"), err)) {
        return false;
    }

    const PreviewHeadLayerState secondPass = buildPreparedHeadLayerState(state, &renderAssetCache);
    if (!require(secondPass.sprites.size() == 1, QStringLiteral("second ex tap render produces one sprite"), err)) {
        return false;
    }
    if (!require(
            secondPass.sprites.constFirst().image == firstPass.sprites.constFirst().image,
            QStringLiteral("second ex tap render reuses the cached composed image pointer"),
            err)) {
        return false;
    }
    if (!require(renderAssetCache.missCount() == 1, QStringLiteral("second ex tap render does not add a cache miss"), err)) {
        return false;
    }
    if (!require(renderAssetCache.hitCount() == 1, QStringLiteral("second ex tap render records one cache hit"), err)) {
        return false;
    }

    state.skin.tapExImage = solidImage(64, 64);
    const PreviewHeadLayerState thirdPass = buildPreparedHeadLayerState(state, &renderAssetCache);
    if (!require(thirdPass.sprites.size() == 1, QStringLiteral("skin-invalidated ex tap render produces one sprite"), err)) {
        return false;
    }
    if (!require(renderAssetCache.missCount() == 2, QStringLiteral("skin-invalidated render records a second cache miss"), err)) {
        return false;
    }
    if (!require(renderAssetCache.entryCount() == 1, QStringLiteral("skin-invalidated render replaces the composed image cache entry"), err)) {
        return false;
    }

    return true;
}

bool verifyPreparedHeadOrderAndBaseImageSelection(QTextStream& err)
{
    PreviewFrameState state;
    state.skin.tapImage = solidImage(96, 96);
    state.skin.tapEachImage = solidImage(88, 88);
    state.skin.starImage = solidImage(104, 104);
    state.skin.starEachImage = solidImage(92, 92);

    TimelineNoteMarker tapNormal;
    tapNormal.type = QStringLiteral("tap");
    tapNormal.second = 1.0;
    tapNormal.lane = 1;
    tapNormal.sourceLine = 1;
    tapNormal.sourceCol = 1;
    tapNormal.parseOrder = 0;

    TimelineNoteMarker tapEach;
    tapEach.type = QStringLiteral("tap");
    tapEach.second = 1.0;
    tapEach.lane = 2;
    tapEach.isEach = true;
    tapEach.sourceLine = 1;
    tapEach.sourceCol = 2;
    tapEach.parseOrder = 1;

    TimelineNoteMarker slideEach;
    slideEach.type = QStringLiteral("slide");
    slideEach.second = 1.05;
    slideEach.slideTraceSecond = 1.35;
    slideEach.endSecond = 1.8;
    slideEach.lane = 3;
    slideEach.endLane = 5;
    slideEach.headEach = true;
    slideEach.hasHeadStar = true;
    slideEach.slideHeadUsesTapMaterial = false;
    slideEach.sourceLine = 1;
    slideEach.sourceCol = 3;
    slideEach.parseOrder = 2;

    TimelineNoteMarker tapFarFuture;
    tapFarFuture.type = QStringLiteral("tap");
    tapFarFuture.second = 5.0;
    tapFarFuture.lane = 4;
    tapFarFuture.sourceLine = 1;
    tapFarFuture.sourceCol = 4;
    tapFarFuture.parseOrder = 3;

    state.noteMarkers = {tapNormal, tapEach, slideEach, tapFarFuture};
    state.playheadSeconds = 0.9;

    const PreviewHeadLayerState layerState = buildPreparedHeadLayerState(state);
    if (!require(layerState.sprites.size() == 3, QStringLiteral("prepared head state renders only visible head sprites"), err)) {
        return false;
    }
    if (!require(
            layerState.sprites.at(0).image == &state.skin.starEachImage,
            QStringLiteral("highest-second slide head renders first with starEach image"),
            err)) {
        return false;
    }
    if (!require(
            layerState.sprites.at(1).image == &state.skin.tapEachImage,
            QStringLiteral("same-second later tap renders second with tapEach image"),
            err)) {
        return false;
    }
    if (!require(
            layerState.sprites.at(2).image == &state.skin.tapImage,
            QStringLiteral("same-second earlier tap renders last with tap image"),
            err)) {
        return false;
    }

    return true;
}

bool verifySameSecondSlideTextOrderMatchesBetweenPreparedAndFallback(QTextStream& err)
{
    PreviewFrameState state;
    state.skin.starImage = solidImage(104, 104);

    TimelineNoteMarker earlierTextSlide;
    earlierTextSlide.type = QStringLiteral("slide");
    earlierTextSlide.second = 1.0;
    earlierTextSlide.slideTraceSecond = 1.3;
    earlierTextSlide.endSecond = 1.8;
    earlierTextSlide.lane = 1;
    earlierTextSlide.endLane = 3;
    earlierTextSlide.hasHeadStar = true;
    earlierTextSlide.sourceLine = 4;
    earlierTextSlide.sourceCol = 2;
    earlierTextSlide.parseOrder = 0;

    TimelineNoteMarker laterTextSlide = earlierTextSlide;
    laterTextSlide.lane = 5;
    laterTextSlide.endLane = 7;
    laterTextSlide.sourceCol = 6;
    laterTextSlide.parseOrder = 1;

    state.noteMarkers = {earlierTextSlide, laterTextSlide};
    const PreviewTapTiming tapTiming =
        miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(state.render.tapFlowSpeed));
    state.playheadSeconds = earlierTextSlide.second - tapTiming.flyDurationSeconds * 0.5;

    const PreviewHeadLayerState preparedLayerState = buildPreparedHeadLayerState(state);
    const PreviewHeadLayerState fallbackLayerState = buildFallbackHeadLayerState(state);
    if (!require(preparedLayerState.sprites.size() == 2, QStringLiteral("prepared same-second slides render two head sprites"), err)) {
        return false;
    }
    if (!require(fallbackLayerState.sprites.size() == 2, QStringLiteral("fallback same-second slides render two head sprites"), err)) {
        return false;
    }

    const QPointF expectedEarlierCenter = expectedHeadCenterForLane(
        earlierTextSlide.lane,
        static_cast<qreal>(earlierTextSlide.second),
        static_cast<qreal>(state.playheadSeconds),
        tapTiming
    );
    const QPointF expectedLaterCenter = expectedHeadCenterForLane(
        laterTextSlide.lane,
        static_cast<qreal>(laterTextSlide.second),
        static_cast<qreal>(state.playheadSeconds),
        tapTiming
    );
    if (!requireNear(
            preparedLayerState.sprites.at(0).center.x(),
            expectedLaterCenter.x(),
            QStringLiteral("prepared same-second later-text slide renders first center x"),
            err)) {
        return false;
    }
    if (!requireNear(
            preparedLayerState.sprites.at(1).center.x(),
            expectedEarlierCenter.x(),
            QStringLiteral("prepared same-second earlier-text slide renders last center x"),
            err)) {
        return false;
    }
    for (int index = 0; index < preparedLayerState.sprites.size(); ++index) {
        if (!requireNear(
                fallbackLayerState.sprites.at(index).center.x(),
                preparedLayerState.sprites.at(index).center.x(),
                QStringLiteral("fallback same-second slide order matches prepared center x %1").arg(index),
                err)) {
            return false;
        }
        if (!requireNear(
                fallbackLayerState.sprites.at(index).center.y(),
                preparedLayerState.sprites.at(index).center.y(),
                QStringLiteral("fallback same-second slide order matches prepared center y %1").arg(index),
                err)) {
            return false;
        }
    }

    return true;
}

bool verifyNegativePlayheadKeepsFallingTapVisible(QTextStream& err)
{
    PreviewFrameState state;
    TimelineNoteMarker marker;
    marker.type = QStringLiteral("tap");
    marker.second = -0.1;
    marker.lane = 1;
    state.noteMarkers.append(marker);
    state.skin.tapImage = solidImage(96, 96);

    const PreviewTapTiming tapTiming =
        miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(state.render.tapFlowSpeed));
    state.playheadSeconds = marker.second - tapTiming.flyDurationSeconds * 0.5;

    const PreviewHeadLayerState layerState = buildPreparedHeadLayerState(state);
    if (!require(layerState.sprites.size() == 1, QStringLiteral("negative playhead tap render produces one sprite"), err)) {
        return false;
    }

    const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
    const miacode::preview::scene::TapApproachSample approach = miacode::preview::scene::sampleTapApproach(
        deltaSeconds,
        tapTiming.lifecycleDurationSeconds,
        tapTiming.spawnDurationSeconds,
        tapTiming.flyDurationSeconds,
        tapTiming.unitsPerSecond,
        miacode::preview::scene::kLogicalDistanceTap,
        miacode::preview::scene::kLogicalDistanceEdge);
    if (!require(approach.scale > 0.0, QStringLiteral("negative playhead tap stays inside the visible approach window"), err)) {
        return false;
    }

    const QPointF unit = miacode::preview::scene::laneUnitVector(marker.lane);
    const QPointF expectedLogicalPoint(
        miacode::preview::scene::kLogicalCanvasCenter + unit.x() * approach.distance,
        miacode::preview::scene::kLogicalCanvasCenter + unit.y() * approach.distance
    );
    const QPointF expectedPoint = miacode::preview::scene::mapLogicalPointToRect(
        expectedLogicalPoint,
        QRectF(
            0.0,
            0.0,
            miacode::preview::scene::kLogicalCanvasSize,
            miacode::preview::scene::kLogicalCanvasSize));
    if (!requireNear(
            layerState.sprites.constFirst().center.x(),
            expectedPoint.x(),
            QStringLiteral("negative playhead tap center x"),
            err)) {
        return false;
    }
    if (!requireNear(
            layerState.sprites.constFirst().center.y(),
            expectedPoint.y(),
            QStringLiteral("negative playhead tap center y"),
            err)) {
        return false;
    }

    return true;
}

bool verifyHoldExUsesHeadRenderAssetCache(QTextStream& err)
{
    PreviewFrameState state;
    TimelineNoteMarker holdMarker;
    holdMarker.type = QStringLiteral("hold");
    holdMarker.second = 1.0;
    holdMarker.endSecond = 1.4;
    holdMarker.lane = 1;
    holdMarker.isEx = true;
    state.noteMarkers.append(holdMarker);
    state.playheadSeconds = 0.9;
    state.skin.holdImage = solidImage(64, 192);
    state.skin.holdExImage = solidImage(32, 96);

    miacode::preview::scene::PreviewHeadRenderAssetCache renderAssetCache;
    const PreviewHeadLayerState firstPass = buildPreparedHeadLayerState(state, &renderAssetCache);
    if (!require(!firstPass.sprites.isEmpty(), QStringLiteral("hold ex first render emits sprites"), err)) {
        return false;
    }
    if (!require(renderAssetCache.missCount() == 1, QStringLiteral("hold ex first render records one cache miss"), err)) {
        return false;
    }

    const PreviewHeadLayerState secondPass = buildPreparedHeadLayerState(state, &renderAssetCache);
    if (!require(secondPass.sprites.size() == firstPass.sprites.size(), QStringLiteral("hold ex second render keeps sprite count"), err)) {
        return false;
    }
    if (!require(renderAssetCache.hitCount() == 1, QStringLiteral("hold ex second render records one cache hit"), err)) {
        return false;
    }
    for (int index = 0; index < secondPass.sprites.size(); ++index) {
        if (!require(
                secondPass.sprites.at(index).image == firstPass.sprites.at(index).image,
                QStringLiteral("hold ex second render reuses cached composed image for sprite %1").arg(index),
                err)) {
            return false;
        }
    }

    return true;
}

bool verifyNegativeHsReverseFlow(QTextStream& err)
{
    using miacode::preview::scene::previewTapTimingForEffectiveFlowSpeed;
    using miacode::preview::scene::sampleTapApproach;
    using miacode::preview::scene::TapApproachSample;
    using miacode::preview::scene::kLogicalDistanceTap;
    using miacode::preview::scene::kLogicalDistanceEdge;

    // Sign split: magnitude drives durations (identical for ±speed); sign maps
    // to directionSign without collapsing timing.
    const PreviewTapTiming fwd = previewTapTimingForEffectiveFlowSpeed(7.5);
    const PreviewTapTiming rev = previewTapTimingForEffectiveFlowSpeed(-7.5);
    if (!require(fwd.directionSign > 0.0, QStringLiteral("positive effective speed -> directionSign +1"), err)) {
        return false;
    }
    if (!require(rev.directionSign < 0.0, QStringLiteral("negative effective speed -> directionSign -1"), err)) {
        return false;
    }
    if (!require(rev.flyDurationSeconds > 0.0
                     && qFuzzyCompare(rev.flyDurationSeconds + 1.0, fwd.flyDurationSeconds + 1.0),
                 QStringLiteral("negative HS keeps the same (magnitude) fly duration, not a ~5h collapse"), err)) {
        return false;
    }

    const qreal fly = fwd.flyDurationSeconds;

    // Forward stays inner->ring: distance never exceeds the judgement ring.
    const TapApproachSample fwdMid = sampleTapApproach(
        -fly * 0.5, fwd.lifecycleDurationSeconds, fwd.spawnDurationSeconds,
        fwd.flyDurationSeconds, fwd.unitsPerSecond, kLogicalDistanceTap, kLogicalDistanceEdge, fwd.directionSign);
    if (!require(fwdMid.distance <= kLogicalDistanceEdge + 0.001 && fwdMid.distance >= kLogicalDistanceTap - 0.001,
                 QStringLiteral("forward note flies within [tap, edge]"), err)) {
        return false;
    }

    // Reverse flies outer->ring: distance starts beyond the ring and decreases
    // to the ring as the playhead approaches the hit time. Hit position is the
    // ring (edge), same as forward — semantics preserved. No clipping is applied
    // (product decision): the trajectory is the whole contract here.
    const TapApproachSample revEarly = sampleTapApproach(
        -fly * 0.5, rev.lifecycleDurationSeconds, rev.spawnDurationSeconds,
        rev.flyDurationSeconds, rev.unitsPerSecond, kLogicalDistanceTap, kLogicalDistanceEdge, rev.directionSign);
    const TapApproachSample revLate = sampleTapApproach(
        -fly * 0.1, rev.lifecycleDurationSeconds, rev.spawnDurationSeconds,
        rev.flyDurationSeconds, rev.unitsPerSecond, kLogicalDistanceTap, kLogicalDistanceEdge, rev.directionSign);
    const TapApproachSample revHit = sampleTapApproach(
        0.0, rev.lifecycleDurationSeconds, rev.spawnDurationSeconds,
        rev.flyDurationSeconds, rev.unitsPerSecond, kLogicalDistanceTap, kLogicalDistanceEdge, rev.directionSign);
    if (!require(revEarly.distance > kLogicalDistanceEdge,
                 QStringLiteral("reverse note approaches from OUTSIDE the ring (distance > edge during flight)"), err)) {
        return false;
    }
    if (!require(revEarly.distance > revLate.distance && revLate.distance >= kLogicalDistanceEdge - 0.001,
                 QStringLiteral("reverse note flies inward toward the ring as the playhead advances"), err)) {
        return false;
    }
    if (!require(qAbs(revHit.distance - kLogicalDistanceEdge) < 0.001,
                 QStringLiteral("reverse note is HIT at the ring (edge), same as forward"), err)) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifySameHeadBranchesCollapseToFastestRotation(err)) {
        return 1;
    }
    if (!verifyCalibratedSlideHeadRotation(err)) {
        return 1;
    }
    if (!verifyCalibratedTapTiming(err)) {
        return 1;
    }
    if (!verifyHeadRenderAssetCacheReuseAndInvalidation(err)) {
        return 1;
    }
    if (!verifyPreparedHeadOrderAndBaseImageSelection(err)) {
        return 1;
    }
    if (!verifySameSecondSlideTextOrderMatchesBetweenPreparedAndFallback(err)) {
        return 1;
    }
    if (!verifyNegativePlayheadKeepsFallingTapVisible(err)) {
        return 1;
    }
    if (!verifyHoldExUsesHeadRenderAssetCache(err)) {
        return 1;
    }
    if (!verifyNegativeHsReverseFlow(err)) {
        return 1;
    }

    out << "preview_head_layer_spec ok" << Qt::endl;
    return 0;
}
