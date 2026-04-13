#pragma once

#include <QVector>

#include "preview/scene/PreviewPreparedSceneCache.h"

namespace miacode::preview::scene {

class PreviewActiveMarkerView
{
public:
    PreviewActiveMarkerView() = default;

    explicit PreviewActiveMarkerView(const QVector<TimelineNoteMarker>& sourceMarkers)
        : sourceMarkers_(&sourceMarkers)
    {
    }

    PreviewActiveMarkerView(
        const QVector<TimelineNoteMarker>& sourceMarkers,
        const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>& layer,
        const PreviewLayerWindowCursor& cursor)
        : sourceMarkers_(&sourceMarkers)
        , layer_(&layer)
        , cursor_(&cursor)
    {
    }

    bool isValid() const
    {
        return sourceMarkers_ != nullptr;
    }

    bool usesPreparedLayer() const
    {
        return sourceMarkers_ != nullptr && layer_ != nullptr && cursor_ != nullptr;
    }

    int size() const
    {
        if (!usesPreparedLayer()) {
            return sourceMarkers_ != nullptr ? sourceMarkers_->size() : 0;
        }
        return cursor_->activePreparedIndices.size();
    }

    bool isEmpty() const
    {
        return size() == 0;
    }

    const TimelineNoteMarker& markerAt(int index) const
    {
        Q_ASSERT(sourceMarkers_ != nullptr);
        if (!usesPreparedLayer()) {
            return sourceMarkers_->at(index);
        }
        return markerForPreparedIndex(cursor_->activePreparedIndices.at(index));
    }

    int sourceIndexAt(int index) const
    {
        Q_ASSERT(sourceMarkers_ != nullptr);
        if (!usesPreparedLayer()) {
            return index;
        }
        return sourceIndexForPreparedIndex(cursor_->activePreparedIndices.at(index));
    }

    const QVector<int>& activePreparedIndices() const
    {
        return usesPreparedLayer() ? cursor_->activePreparedIndices : emptyIndexVector();
    }

    const QVector<int>& preparedDrawOrder() const
    {
        return usesPreparedLayer() ? layer_->drawOrder : emptyIndexVector();
    }

    int preparedDrawRank(int preparedIndex) const
    {
        if (!usesPreparedLayer()
            || preparedIndex < 0
            || preparedIndex >= layer_->drawOrderRanks.size()) {
            return preparedIndex;
        }
        return layer_->drawOrderRanks.at(preparedIndex);
    }

    bool isPreparedIndexActive(int preparedIndex) const
    {
        if (!usesPreparedLayer()) {
            return preparedIndex >= 0 && sourceMarkers_ != nullptr && preparedIndex < sourceMarkers_->size();
        }
        return preparedIndex >= 0
            && preparedIndex < cursor_->activePreparedMembership.size()
            && cursor_->activePreparedMembership.testBit(preparedIndex);
    }

    const PreviewPreparedMarkerEntry* preparedEntry(int preparedIndex) const
    {
        if (!usesPreparedLayer() || preparedIndex < 0 || preparedIndex >= layer_->entries.size()) {
            return nullptr;
        }
        return &layer_->entries.at(preparedIndex);
    }

    int sourceIndexForPreparedIndex(int preparedIndex) const
    {
        if (!usesPreparedLayer()) {
            return preparedIndex;
        }
        const PreviewPreparedMarkerEntry* entry = preparedEntry(preparedIndex);
        return entry != nullptr ? entry->markerIndex : -1;
    }

    const TimelineNoteMarker& markerForPreparedIndex(int preparedIndex) const
    {
        Q_ASSERT(sourceMarkers_ != nullptr);
        return sourceMarkers_->at(sourceIndexForPreparedIndex(preparedIndex));
    }

private:
    static const QVector<int>& emptyIndexVector()
    {
        static const QVector<int> empty;
        return empty;
    }

    const QVector<TimelineNoteMarker>* sourceMarkers_ = nullptr;
    const PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>* layer_ = nullptr;
    const PreviewLayerWindowCursor* cursor_ = nullptr;
};

}  // namespace miacode::preview::scene
