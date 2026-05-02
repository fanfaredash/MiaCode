#include "core/scene/PreviewMarkerDrawOrder.h"

#include <QtMath>

#include <algorithm>

namespace {

template <typename T>
int compareOrderedValues(const T& left, const T& right, bool ascending)
{
    if (left < right) {
        return ascending ? -1 : 1;
    }
    if (right < left) {
        return ascending ? 1 : -1;
    }
    return 0;
}

}  // namespace

namespace miacode::preview::scene {

int comparePreviewMarkerTopPriority(
    const TimelineNoteMarker& left,
    const TimelineNoteMarker& right,
    bool earlierOnTop
)
{
    if (!qFuzzyCompare(left.second + 1.0, right.second + 1.0)) {
        return compareOrderedValues(left.second, right.second, earlierOnTop);
    }
    if (left.sourceLine != right.sourceLine) {
        return compareOrderedValues(left.sourceLine, right.sourceLine, earlierOnTop);
    }
    if (left.sourceCol != right.sourceCol) {
        return compareOrderedValues(left.sourceCol, right.sourceCol, earlierOnTop);
    }
    if (left.parseOrder >= 0 && right.parseOrder >= 0 && left.parseOrder != right.parseOrder) {
        return compareOrderedValues(left.parseOrder, right.parseOrder, earlierOnTop);
    }
    return 0;
}

void sortPreviewMarkerViewIndicesForDraw(
    const PreviewActiveMarkerView& markers,
    QVector<int>* markerViewIndices,
    bool earlierOnTop
)
{
    if (markerViewIndices == nullptr || markerViewIndices->size() < 2) {
        return;
    }

    std::stable_sort(markerViewIndices->begin(), markerViewIndices->end(), [&markers, earlierOnTop](int a, int b) {
        const int compare = comparePreviewMarkerTopPriority(
            markers.markerAt(a),
            markers.markerAt(b),
            earlierOnTop
        );
        return compare > 0;
    });
}

void rebuildPreviewPreparedMarkerDrawOrder(
    const QVector<TimelineNoteMarker>& sourceMarkers,
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>* layer,
    bool earlierOnTop
)
{
    if (layer == nullptr) {
        return;
    }

    layer->drawOrder.resize(layer->entries.size());
    for (int index = 0; index < layer->entries.size(); ++index) {
        layer->drawOrder[index] = index;
    }

    std::stable_sort(layer->drawOrder.begin(), layer->drawOrder.end(), [&sourceMarkers, layer, earlierOnTop](int a, int b) {
        const int markerIndexA = layer->entries.at(a).markerIndex;
        const int markerIndexB = layer->entries.at(b).markerIndex;
        if (markerIndexA < 0
            || markerIndexA >= sourceMarkers.size()
            || markerIndexB < 0
            || markerIndexB >= sourceMarkers.size()) {
            return false;
        }

        const int compare = comparePreviewMarkerTopPriority(
            sourceMarkers.at(markerIndexA),
            sourceMarkers.at(markerIndexB),
            earlierOnTop
        );
        return compare > 0;
    });

    layer->drawOrderRanks.fill(-1, layer->entries.size());
    for (int rank = 0; rank < layer->drawOrder.size(); ++rank) {
        const int preparedIndex = layer->drawOrder.at(rank);
        if (preparedIndex >= 0 && preparedIndex < layer->drawOrderRanks.size()) {
            layer->drawOrderRanks[preparedIndex] = rank;
        }
    }
}

}  // namespace miacode::preview::scene
