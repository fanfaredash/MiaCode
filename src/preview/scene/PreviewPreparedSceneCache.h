#pragma once

#include <QVector>

#include "common/MuriTypes.h"
#include "preview/scene/PreviewChartReviewLayerState.h"
#include "preview/scene/PreviewFrameState.h"

#include <algorithm>

namespace miacode::preview::scene {

struct PreviewPreparedSceneCacheKey {
    quint64 sceneContentRevision = 0;
    double noteFlowSpeed = 0.0;
    RenderMode renderMode = RenderMode::Native;
    bool showSlideTracks = true;
    bool showChartReviewSlideJudgeOverlay = false;
    bool showChartReviewSimpleJudgeOverlay = false;

    bool operator==(const PreviewPreparedSceneCacheKey& other) const
    {
        return sceneContentRevision == other.sceneContentRevision
            && qFuzzyCompare(noteFlowSpeed + 1.0, other.noteFlowSpeed + 1.0)
            && renderMode == other.renderMode
            && showSlideTracks == other.showSlideTracks
            && showChartReviewSlideJudgeOverlay == other.showChartReviewSlideJudgeOverlay
            && showChartReviewSimpleJudgeOverlay == other.showChartReviewSimpleJudgeOverlay;
    }
};

struct PreviewPreparedMarkerEntry {
    int markerIndex = -1;
    double activeStart = 0.0;
    double activeEnd = 0.0;
};

struct PreviewPreparedChartReviewEntry {
    PreviewChartReviewPreparedEvent event;
    double activeStart = 0.0;
    double activeEnd = 0.0;
};

struct PreviewPreparedMaimuriDxJudgeEntry {
    MuriJudgeSpriteEvent event;
    QVector<int> markerIndices;
    double activeStart = 0.0;
    double activeEnd = 0.0;
};

template <typename EntryT>
struct PreviewPreparedLayerWindow {
    QVector<EntryT> entries;
    QVector<int> activationOrder;
    QVector<int> deactivationOrder;

    void clear()
    {
        entries.clear();
        activationOrder.clear();
        deactivationOrder.clear();
    }
};

struct PreviewLayerWindowCursor {
    bool valid = false;
    double lastPlayhead = 0.0;
    int nextActivationIndex = 0;
    int nextDeactivationIndex = 0;
    QVector<int> activePreparedIndices;

    void reset()
    {
        valid = false;
        lastPlayhead = 0.0;
        nextActivationIndex = 0;
        nextDeactivationIndex = 0;
        activePreparedIndices.clear();
    }
};

class PreviewPreparedSceneCache
{
public:
    bool sync(const PreviewFrameState& state);
    void reset();

    const PreviewPreparedSceneCacheKey& key() const { return key_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& guideLayer() const { return guideLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& headLayer() const { return headLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& slideLikeLayer() const { return slideLikeLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& judgeEffectLayer() const { return judgeEffectLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& judgeFireworkLayer() const { return judgeFireworkLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& touchLayer() const { return touchLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& touchJudgeLayer() const { return touchJudgeLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& touchHoldLayer() const { return touchHoldLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedChartReviewEntry>& chartReviewLayer() const { return chartReviewLayer_; }
    const PreviewPreparedLayerWindow<PreviewPreparedMaimuriDxJudgeEntry>& maimuriDxJudgeLayer() const { return maimuriDxJudgeLayer_; }

    void collectMarkers(
        const QVector<TimelineNoteMarker>& sourceMarkers,
        const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& layer,
        const QVector<int>& activePreparedIndices,
        QVector<TimelineNoteMarker>* outMarkers,
        QVector<int>* outMarkerIndices = nullptr
    ) const;

    void collectChartReviewEvents(
        const QVector<int>& activePreparedIndices,
        PreviewChartReviewPreparedEvents* outEvents
    ) const;

    void collectMaimuriDxJudgeData(
        const QVector<int>& activePreparedIndices,
        QVector<MuriJudgeSpriteEvent>* outEvents,
        QVector<int>* outMarkerIndices
    ) const;

private:
    void rebuild(const PreviewFrameState& state);

    PreviewPreparedSceneCacheKey key_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> guideLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> headLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> slideLikeLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> judgeEffectLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> judgeFireworkLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> touchLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> touchJudgeLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> touchHoldLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedChartReviewEntry> chartReviewLayer_;
    PreviewPreparedLayerWindow<PreviewPreparedMaimuriDxJudgeEntry> maimuriDxJudgeLayer_;
};

template <typename EntryT>
inline void finalizePreparedLayerWindow(PreviewPreparedLayerWindow<EntryT>* layer)
{
    Q_ASSERT(layer != nullptr);

    layer->activationOrder.resize(layer->entries.size());
    layer->deactivationOrder.resize(layer->entries.size());
    for (int index = 0; index < layer->entries.size(); ++index) {
        layer->activationOrder[index] = index;
        layer->deactivationOrder[index] = index;
    }

    std::stable_sort(layer->activationOrder.begin(), layer->activationOrder.end(), [layer](int a, int b) {
        return layer->entries[a].activeStart < layer->entries[b].activeStart;
    });
    std::stable_sort(layer->deactivationOrder.begin(), layer->deactivationOrder.end(), [layer](int a, int b) {
        return layer->entries[a].activeEnd < layer->entries[b].activeEnd;
    });
}

template <typename EntryT>
inline void syncPreviewLayerWindowCursor(
    const PreviewPreparedLayerWindow<EntryT>& layer,
    double playheadSeconds,
    PreviewLayerWindowCursor* cursor
)
{
    Q_ASSERT(cursor != nullptr);

    const double kEpsilon = 1e-6;
    const auto addActive = [&cursor](int preparedIndex) {
        auto insertPos = std::lower_bound(
            cursor->activePreparedIndices.begin(),
            cursor->activePreparedIndices.end(),
            preparedIndex);
        if (insertPos == cursor->activePreparedIndices.end() || *insertPos != preparedIndex) {
            cursor->activePreparedIndices.insert(insertPos, preparedIndex);
        }
    };
    const auto removeActive = [&cursor](int preparedIndex) {
        auto it = std::lower_bound(
            cursor->activePreparedIndices.begin(),
            cursor->activePreparedIndices.end(),
            preparedIndex);
        if (it != cursor->activePreparedIndices.end() && *it == preparedIndex) {
            cursor->activePreparedIndices.erase(it);
        }
    };
    const auto resetForTime = [&layer, &cursor, playheadSeconds]() {
        cursor->activePreparedIndices.clear();
        const double activationTarget = playheadSeconds + 1e-6;
        const int startedCount = static_cast<int>(std::upper_bound(
            layer.activationOrder.begin(),
            layer.activationOrder.end(),
            activationTarget,
            [&layer](double value, int preparedIndex) {
                return value < layer.entries[preparedIndex].activeStart;
            }) - layer.activationOrder.begin());
        const int endedCount = static_cast<int>(std::upper_bound(
            layer.deactivationOrder.begin(),
            layer.deactivationOrder.end(),
            playheadSeconds - 1e-6,
            [&layer](double value, int preparedIndex) {
                return value < layer.entries[preparedIndex].activeEnd;
            }) - layer.deactivationOrder.begin());
        cursor->nextActivationIndex = startedCount;
        cursor->nextDeactivationIndex = endedCount;
        for (int i = 0; i < startedCount; ++i) {
            const int preparedIndex = layer.activationOrder[i];
            if (layer.entries[preparedIndex].activeEnd + 1e-6 >= playheadSeconds) {
                cursor->activePreparedIndices.append(preparedIndex);
            }
        }
        std::sort(cursor->activePreparedIndices.begin(), cursor->activePreparedIndices.end());
        cursor->valid = true;
        cursor->lastPlayhead = playheadSeconds;
    };

    if (layer.entries.isEmpty()) {
        cursor->reset();
        cursor->valid = true;
        cursor->lastPlayhead = playheadSeconds;
        return;
    }

    if (!cursor->valid || playheadSeconds + kEpsilon < cursor->lastPlayhead) {
        resetForTime();
        return;
    }

    while (cursor->nextActivationIndex < layer.activationOrder.size()) {
        const int preparedIndex = layer.activationOrder[cursor->nextActivationIndex];
        if (layer.entries[preparedIndex].activeStart > playheadSeconds + kEpsilon) {
            break;
        }
        if (layer.entries[preparedIndex].activeEnd + kEpsilon >= playheadSeconds) {
            addActive(preparedIndex);
        }
        ++cursor->nextActivationIndex;
    }
    while (cursor->nextDeactivationIndex < layer.deactivationOrder.size()) {
        const int preparedIndex = layer.deactivationOrder[cursor->nextDeactivationIndex];
        if (layer.entries[preparedIndex].activeEnd >= playheadSeconds - kEpsilon) {
            break;
        }
        removeActive(preparedIndex);
        ++cursor->nextDeactivationIndex;
    }
    cursor->valid = true;
    cursor->lastPlayhead = playheadSeconds;
}

}  // namespace miacode::preview::scene
