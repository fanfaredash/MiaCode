#include "core/scene/PreviewPreparedSceneCache.h"

#include "common/DebugLog.h"
#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewJudgeOverlayShared.h"
#include "core/scene/PreviewMarkerDrawOrder.h"
#include "core/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "core/scene/PreviewOpacityCurves.h"

#include <QHash>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace {

struct SlideHeadRepresentative {
    int markerIndex = -1;
    qreal rotateSpeedDegreesPerSecond = 0.0;
};

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
    if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
        return 0.0;
    }

    const qreal totalLen = static_cast<qreal>(marker.slideNativeTrackLength);
    const qreal totalDuration = totalSlideTraceDurationSeconds(marker);
    if (totalLen <= 0.0 || totalDuration <= 0.0) {
        return 0.0;
    }
    return qMax<qreal>(-4.500 * totalLen / totalDuration, -1080.0);
}

QHash<QString, SlideHeadRepresentative> buildSlideHeadRepresentatives(
    const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, SlideHeadRepresentative> representatives;
    representatives.reserve(noteMarkers.size());

    for (int markerIndex = 0; markerIndex < noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers.at(markerIndex);
        if ((marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) || !marker.hasHeadStar) {
            continue;
        }

        const QString key = slideHeadEventKey(marker);
        const qreal rotateSpeed = slideHeadRotateSpeedDegreesPerSecond(marker);
        auto it = representatives.find(key);
        if (it == representatives.end()
            || rotateSpeed < it->rotateSpeedDegreesPerSecond
            || (qFuzzyCompare(rotateSpeed + 1.0, it->rotateSpeedDegreesPerSecond + 1.0)
                && markerIndex > it->markerIndex)) {
            representatives.insert(key, SlideHeadRepresentative{markerIndex, rotateSpeed});
        }
    }

    return representatives;
}

void appendPreparedMarkerEntry(
    miacode::preview::scene::PreviewPreparedLayerWindow<miacode::preview::scene::PreviewPreparedMarkerEntry>* layer,
    int markerIndex,
    double activeStart,
    double activeEnd,
    int headRepresentativeMarkerIndex = -1
)
{
    if (layer == nullptr || markerIndex < 0 || activeEnd + 1e-6 < activeStart) {
        return;
    }
    layer->entries.append(miacode::preview::scene::PreviewPreparedMarkerEntry{
        markerIndex,
        activeStart,
        activeEnd,
        headRepresentativeMarkerIndex
    });
}

}  // namespace

namespace miacode::preview::scene {

bool PreviewPreparedSceneCache::sync(const PreviewFrameState& state)
{
    PreviewPreparedSceneCacheKey nextKey;
    nextKey.sceneContentRevision = state.sceneContentRevision;
    nextKey.tapFlowSpeed = state.render.tapFlowSpeed;
    nextKey.touchFlowSpeed = state.render.touchFlowSpeed;
    nextKey.renderMode = state.muriRenderOptions.renderMode;
    nextKey.showSlideTracks = state.muriRenderOptions.showSlideTracks;
    nextKey.slideEarlierSecondAndTextOnTop = state.render.slideEarlierSecondAndTextOnTop;
    nextKey.showChartReviewSlideJudgeOverlay = state.muriRenderOptions.showChartReviewSlideJudgeOverlay;
    nextKey.showChartReviewTapJudgeOverlay = state.muriRenderOptions.showChartReviewTapJudgeOverlay;
    nextKey.showChartReviewTouchJudgeOverlay = state.muriRenderOptions.showChartReviewTouchJudgeOverlay;

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

    // HS diagnostic: once per cache rebuild, log a histogram of marker
    // hsMultiplier values. Routed through the Runtime channel so it
    // appears in miacode_runtime_debug.log alongside other scene-cache
    // diagnostics. Cheap (one map insertion per marker, one log line).
    {
        QHash<double, int> hsHist;
        for (const TimelineNoteMarker& marker : state.noteMarkers) {
            hsHist[marker.hsMultiplier]++;
        }
        QString hsSummary;
        QTextStream out(&hsSummary);
        out << "tapFlowSpeed=" << state.render.tapFlowSpeed
            << " touchFlowSpeed=" << state.render.touchFlowSpeed
            << " hs_histogram:";
        for (auto it = hsHist.constBegin(); it != hsHist.constEnd(); ++it) {
            const PreviewTapTiming sample = previewTapTimingForEffectiveFlowSpeed(
                static_cast<qreal>(state.render.tapFlowSpeed * it.key()));
            out << " hs=" << it.key()
                << ",count=" << it.value()
                << ",fly=" << sample.flyDurationSeconds
                << ",life=" << sample.lifecycleDurationSeconds;
        }
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview_prepared_scene_hs"),
            hsSummary);
    }

    // Per-marker timing: hsMultiplier scales the user-set flow speed
    // (Q4 — effective speed is unclamped). The visibility windows below
    // are precomputed per-marker so a low-HS note appears earlier and a
    // high-HS note appears later than the default.
    const auto markerTapTiming = [&state](double hsMultiplier) {
        return previewTapTimingForEffectiveFlowSpeed(
            static_cast<qreal>(state.render.tapFlowSpeed * hsMultiplier));
    };
    const auto markerTrackTiming = [&state](double hsMultiplier) {
        return previewSlideTrackTimingForEffectiveFlowSpeed(
            static_cast<qreal>(state.render.tapFlowSpeed * hsMultiplier));
    };
    // Per-type sign policy: touch takes the HS magnitude (never reverses).
    const auto markerTouchTiming = [&state](double hsMultiplier) {
        return previewTouchTimingForEffectiveFlowSpeed(
            static_cast<qreal>(state.render.touchFlowSpeed * qAbs(hsMultiplier)));
    };
    QHash<QString, int> markerIndexByKey;
    markerIndexByKey.reserve(state.noteMarkers.size() * 2);
    const QHash<QString, SlideHeadRepresentative> slideHeadRepresentatives =
        buildSlideHeadRepresentatives(state.noteMarkers);

    for (int markerIndex = 0; markerIndex < state.noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = state.noteMarkers.at(markerIndex);
        markerIndexByKey.insert(makeMarkerAnalysisKey(marker), markerIndex);
        if ((marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) && marker.hasHeadStar) {
            markerIndexByKey.insert(slideHeadEventKey(marker), markerIndex);
        }

        const QString type = marker.type.toLower();
        const PreviewTapTiming tapTiming = markerTapTiming(marker.hsMultiplier);
        const PreviewSlideTrackTiming trackTiming = markerTrackTiming(marker.hsMultiplier);
        const PreviewTouchTiming touchTiming = markerTouchTiming(marker.hsMultiplier);
        int headRepresentativeMarkerIndex = markerIndex;
        if ((type == QLatin1String("slide") || type == QLatin1String("wifi")) && marker.hasHeadStar) {
            const auto it = slideHeadRepresentatives.constFind(slideHeadEventKey(marker));
            if (it != slideHeadRepresentatives.constEnd()) {
                headRepresentativeMarkerIndex = it->markerIndex;
            }
        }
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
                marker.second,
                headRepresentativeMarkerIndex
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
                qMax(marker.second, marker.endSecond),
                headRepresentativeMarkerIndex
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
                    marker.second,
                    headRepresentativeMarkerIndex
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
                marker.second - touchTiming.durationSeconds,
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
                marker.second - touchTiming.durationSeconds,
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
        state.muriRenderOptions.showChartReviewTapJudgeOverlay,
        state.muriRenderOptions.showChartReviewTouchJudgeOverlay
    );
    chartReviewLayer_.entries.reserve(chartReviewEvents.size());
    for (const PreviewChartReviewPreparedEvent& event : chartReviewEvents) {
        chartReviewLayer_.entries.append(PreviewPreparedChartReviewEntry{
            event,
            event.second,
            event.second + kMaimuriDxJudgeMaxLifetimeSeconds
        });
    }

    maimuriDxJudgeLayer_.entries.reserve(state.muriAnalysisReport.judgeSpriteEvents.size());
    for (const MuriJudgeSpriteEvent& event : state.muriAnalysisReport.judgeSpriteEvents) {
        PreviewPreparedMaimuriDxJudgeEntry entry;
        entry.event = event;
        entry.activeStart = qMax(event.second, event.spawnSecond);
        entry.activeEnd = event.second + kMaimuriDxJudgeMaxLifetimeSeconds;
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
    // Beta21-fix13 — the slide-stacking-order setting
    // (`slideEarlierSecondAndTextOnTop`) MUST NOT influence the head
    // layer's draw rank. The setting's documented purpose is the slide
    // TRACK layer's internal stacking only — slide head stars (this
    // layer) and any other non-track layers must remain deterministic
    // and option-independent. PreviewHeadLayerState's fallback path at
    // lines ~310-322 already forces `earlierOnTop = true` for the same
    // reason; this mirrors that behaviour for the prepared (cached)
    // draw-order path so toggling the option in Video Settings doesn't
    // shuffle which slide head star renders on top.
    constexpr bool kHeadLayerAlwaysEarlierOnTop = true;
    rebuildPreviewPreparedMarkerDrawOrder(
        state.noteMarkers,
        &headLayer_,
        kHeadLayerAlwaysEarlierOnTop
    );
    rebuildPreviewPreparedMarkerDrawOrder(
        state.noteMarkers,
        &slideLikeLayer_,
        state.render.slideEarlierSecondAndTextOnTop
    );
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
