#include "preview/scene/PreviewPreparedSceneCache.h"

#include "common/PreviewGameplayConfig.h"
#include "preview/scene/PreviewJudgeOverlayShared.h"
#include "preview/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "preview/scene/PreviewOpacityCurves.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace {

QString slideHeadEventKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.eachGroupId);
}

double markerLifecycleEndSecond(const TimelineNoteMarker& marker)
{
    double result = marker.second;
    result = qMax(result, marker.endSecond);
    result = qMax(result, marker.slideTraceSecond);
    result = qMax(result, marker.availableSecond);
    for (double shootSecond : marker.slideSegmentShootSeconds) {
        result = qMax(result, shootSecond);
    }
    return result;
}

void appendPreparedMarkerEntry(
    miacode::preview::scene::PreviewPreparedLayerWindow<miacode::preview::scene::PreviewPreparedMarkerEntry>* layer,
    int markerIndex,
    double activeStart,
    double activeEnd
)
{
    if (layer == nullptr || markerIndex < 0 || activeEnd + 1e-6 < activeStart) {
        return;
    }
    layer->entries.append(miacode::preview::scene::PreviewPreparedMarkerEntry{
        markerIndex,
        activeStart,
        activeEnd
    });
}

}  // namespace

namespace miacode::preview::scene {

bool PreviewPreparedSceneCache::sync(const PreviewFrameState& state)
{
    PreviewPreparedSceneCacheKey nextKey;
    nextKey.sceneContentRevision = state.sceneContentRevision;
    nextKey.noteFlowSpeed = state.render.noteFlowSpeed;
    nextKey.renderMode = state.muriRenderOptions.renderMode;
    nextKey.showSlideTracks = state.muriRenderOptions.showSlideTracks;
    nextKey.showChartReviewSlideJudgeOverlay = state.muriRenderOptions.showChartReviewSlideJudgeOverlay;
    nextKey.showChartReviewSimpleJudgeOverlay = state.muriRenderOptions.showChartReviewSimpleJudgeOverlay;

    if (key_ == nextKey) {
        return false;
    }

    key_ = nextKey;
    rebuild(state);
    return true;
}

void PreviewPreparedSceneCache::reset()
{
    key_ = PreviewPreparedSceneCacheKey();
    guideLayer_.clear();
    headLayer_.clear();
    slideLikeLayer_.clear();
    judgeEffectLayer_.clear();
    judgeFireworkLayer_.clear();
    touchLayer_.clear();
    touchJudgeLayer_.clear();
    touchHoldLayer_.clear();
    chartReviewLayer_.clear();
    maimuriDxJudgeLayer_.clear();
}

void PreviewPreparedSceneCache::rebuild(const PreviewFrameState& state)
{
    guideLayer_.clear();
    headLayer_.clear();
    slideLikeLayer_.clear();
    judgeEffectLayer_.clear();
    judgeFireworkLayer_.clear();
    touchLayer_.clear();
    touchJudgeLayer_.clear();
    touchHoldLayer_.clear();
    chartReviewLayer_.clear();
    maimuriDxJudgeLayer_.clear();

    const PreviewTapTiming tapTiming =
        previewTapTimingForFlowSpeed(static_cast<qreal>(state.render.noteFlowSpeed));
    const PreviewSlideTrackTiming trackTiming =
        previewSlideTrackTimingForFlowSpeed(static_cast<qreal>(state.render.noteFlowSpeed));
    QHash<QString, int> markerIndexByKey;
    markerIndexByKey.reserve(state.noteMarkers.size() * 2);

    for (int markerIndex = 0; markerIndex < state.noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = state.noteMarkers.at(markerIndex);
        markerIndexByKey.insert(makeMarkerAnalysisKey(marker), markerIndex);
        if ((marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) && marker.hasHeadStar) {
            markerIndexByKey.insert(slideHeadEventKey(marker), markerIndex);
        }

        const QString type = marker.type.toLower();
        if (type == QLatin1String("tap")) {
            appendPreparedMarkerEntry(
                &guideLayer_,
                markerIndex,
                marker.second - tapTiming.lifecycleDurationSeconds,
                marker.second
            );
            appendPreparedMarkerEntry(
                &headLayer_,
                markerIndex,
                marker.second - tapTiming.lifecycleDurationSeconds,
                marker.second
            );
            appendPreparedMarkerEntry(
                &judgeEffectLayer_,
                markerIndex,
                marker.second,
                marker.second + miacode::preview_gameplay::kJudgeEffectDurationSeconds
            );
            continue;
        }

        if (type == QLatin1String("hold")) {
            appendPreparedMarkerEntry(
                &guideLayer_,
                markerIndex,
                marker.second - tapTiming.lifecycleDurationSeconds,
                qMax(marker.second, marker.endSecond)
            );
            appendPreparedMarkerEntry(
                &headLayer_,
                markerIndex,
                marker.second - tapTiming.lifecycleDurationSeconds,
                qMax(marker.second, marker.endSecond)
            );
            appendPreparedMarkerEntry(
                &judgeEffectLayer_,
                markerIndex,
                marker.second,
                qMax(marker.second, marker.endSecond) + miacode::preview_gameplay::kJudgeEffectDurationSeconds
            );
            continue;
        }

        if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
            if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
                appendPreparedMarkerEntry(
                    &guideLayer_,
                    markerIndex,
                    marker.second - tapTiming.lifecycleDurationSeconds,
                    marker.second
                );
                appendPreparedMarkerEntry(
                    &headLayer_,
                    markerIndex,
                    marker.second - tapTiming.lifecycleDurationSeconds,
                    marker.second
                );
                appendPreparedMarkerEntry(
                    &slideLikeLayer_,
                    markerIndex,
                    marker.second - trackTiming.appearLeadInSeconds,
                    qMax(marker.second, qMax(marker.endSecond, marker.slideTraceSecond))
                );
                appendPreparedMarkerEntry(
                    &judgeEffectLayer_,
                    markerIndex,
                    marker.second,
                    marker.second + miacode::preview_gameplay::kJudgeEffectDurationSeconds
                );
            }
            continue;
        }

        if (type == QLatin1String("touch")) {
            appendPreparedMarkerEntry(
                &touchLayer_,
                markerIndex,
                marker.second - miacode::preview_gameplay::kTouchDurationSeconds,
                marker.second
            );
            appendPreparedMarkerEntry(
                &touchJudgeLayer_,
                markerIndex,
                marker.second,
                marker.second + miacode::preview_gameplay::kJudgeEffectTouchDurationSeconds
            );
            if (marker.isFirework) {
                appendPreparedMarkerEntry(
                    &judgeFireworkLayer_,
                    markerIndex,
                    marker.second + miacode::preview_gameplay::kJudgeEffectFireworkTouchTriggerDelaySeconds,
                    marker.second
                        + miacode::preview_gameplay::kJudgeEffectFireworkTouchTriggerDelaySeconds
                        + miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds
                );
            }
            continue;
        }

        if (type == QLatin1String("touch_hold")) {
            appendPreparedMarkerEntry(
                &touchHoldLayer_,
                markerIndex,
                marker.second - miacode::preview_gameplay::kTouchDurationSeconds,
                qMax(marker.second, marker.endSecond)
            );
            appendPreparedMarkerEntry(
                &judgeEffectLayer_,
                markerIndex,
                marker.second,
                qMax(marker.second, marker.endSecond) + miacode::preview_gameplay::kJudgeEffectDurationSeconds
            );
            if (marker.isFirework) {
                appendPreparedMarkerEntry(
                    &judgeFireworkLayer_,
                    markerIndex,
                    qMax(marker.second, marker.endSecond),
                    qMax(marker.second, marker.endSecond) + miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds
                );
            }
            continue;
        }
    }

    const PreviewChartReviewPreparedEvents chartReviewEvents = buildPreviewChartReviewPreparedEvents(
        state.noteMarkers,
        state.muriRenderOptions.showChartReviewSlideJudgeOverlay,
        state.muriRenderOptions.showChartReviewSimpleJudgeOverlay
    );
    chartReviewLayer_.entries.reserve(chartReviewEvents.size());
    for (const PreviewChartReviewPreparedEvent& event : chartReviewEvents) {
        chartReviewLayer_.entries.append(PreviewPreparedChartReviewEntry{
            event,
            event.second,
            event.second + kMaimuriDxJudgeLifetimeSeconds
        });
    }

    maimuriDxJudgeLayer_.entries.reserve(state.muriAnalysisReport.judgeSpriteEvents.size());
    for (const MuriJudgeSpriteEvent& event : state.muriAnalysisReport.judgeSpriteEvents) {
        PreviewPreparedMaimuriDxJudgeEntry entry;
        entry.event = event;
        entry.activeStart = qMax(event.second, event.spawnSecond);
        entry.activeEnd = event.second + kMaimuriDxJudgeLifetimeSeconds;
        if (!event.markerKey.isEmpty()) {
            const auto it = markerIndexByKey.constFind(event.markerKey);
            if (it != markerIndexByKey.constEnd()) {
                entry.markerIndices.append(*it);
            }
        }
        maimuriDxJudgeLayer_.entries.append(std::move(entry));
    }

    finalizePreparedLayerWindow(&guideLayer_);
    finalizePreparedLayerWindow(&headLayer_);
    finalizePreparedLayerWindow(&slideLikeLayer_);
    finalizePreparedLayerWindow(&judgeEffectLayer_);
    finalizePreparedLayerWindow(&judgeFireworkLayer_);
    finalizePreparedLayerWindow(&touchLayer_);
    finalizePreparedLayerWindow(&touchJudgeLayer_);
    finalizePreparedLayerWindow(&touchHoldLayer_);
    finalizePreparedLayerWindow(&chartReviewLayer_);
    finalizePreparedLayerWindow(&maimuriDxJudgeLayer_);
}

void PreviewPreparedSceneCache::collectMarkers(
    const QVector<TimelineNoteMarker>& sourceMarkers,
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& layer,
    const QVector<int>& activePreparedIndices,
    QVector<TimelineNoteMarker>* outMarkers,
    QVector<int>* outMarkerIndices
) const
{
    Q_ASSERT(outMarkers != nullptr);

    outMarkers->clear();
    outMarkers->reserve(activePreparedIndices.size());
    if (outMarkerIndices != nullptr) {
        outMarkerIndices->clear();
        outMarkerIndices->reserve(activePreparedIndices.size());
    }

    for (int preparedIndex : activePreparedIndices) {
        if (preparedIndex < 0 || preparedIndex >= layer.entries.size()) {
            continue;
        }
        const int markerIndex = layer.entries[preparedIndex].markerIndex;
        if (markerIndex < 0 || markerIndex >= sourceMarkers.size()) {
            continue;
        }
        outMarkers->append(sourceMarkers.at(markerIndex));
        if (outMarkerIndices != nullptr) {
            outMarkerIndices->append(markerIndex);
        }
    }
}

void PreviewPreparedSceneCache::collectChartReviewEvents(
    const QVector<int>& activePreparedIndices,
    PreviewChartReviewPreparedEvents* outEvents
) const
{
    Q_ASSERT(outEvents != nullptr);

    outEvents->clear();
    outEvents->reserve(activePreparedIndices.size());
    for (int preparedIndex : activePreparedIndices) {
        if (preparedIndex < 0 || preparedIndex >= chartReviewLayer_.entries.size()) {
            continue;
        }
        outEvents->append(chartReviewLayer_.entries[preparedIndex].event);
    }
}

void PreviewPreparedSceneCache::collectMaimuriDxJudgeData(
    const QVector<int>& activePreparedIndices,
    QVector<MuriJudgeSpriteEvent>* outEvents,
    QVector<int>* outMarkerIndices
) const
{
    Q_ASSERT(outEvents != nullptr);
    Q_ASSERT(outMarkerIndices != nullptr);

    outEvents->clear();
    outMarkerIndices->clear();
    outEvents->reserve(activePreparedIndices.size());

    QSet<int> uniqueMarkerIndices;
    uniqueMarkerIndices.reserve(activePreparedIndices.size() * 2);
    for (int preparedIndex : activePreparedIndices) {
        if (preparedIndex < 0 || preparedIndex >= maimuriDxJudgeLayer_.entries.size()) {
            continue;
        }
        const PreviewPreparedMaimuriDxJudgeEntry& entry = maimuriDxJudgeLayer_.entries[preparedIndex];
        outEvents->append(entry.event);
        for (int markerIndex : entry.markerIndices) {
            uniqueMarkerIndices.insert(markerIndex);
        }
    }

    outMarkerIndices->reserve(uniqueMarkerIndices.size());
    for (int markerIndex : uniqueMarkerIndices) {
        outMarkerIndices->append(markerIndex);
    }
    std::sort(outMarkerIndices->begin(), outMarkerIndices->end());
}

}  // namespace miacode::preview::scene
