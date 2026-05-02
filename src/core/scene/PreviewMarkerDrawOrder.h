#pragma once

#include <QVector>

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewPreparedSceneCache.h"

namespace miacode::preview::scene {

int comparePreviewMarkerTopPriority(
    const TimelineNoteMarker& left,
    const TimelineNoteMarker& right,
    bool earlierOnTop
);

void sortPreviewMarkerViewIndicesForDraw(
    const PreviewActiveMarkerView& markers,
    QVector<int>* markerViewIndices,
    bool earlierOnTop
);

void rebuildPreviewPreparedMarkerDrawOrder(
    const QVector<TimelineNoteMarker>& sourceMarkers,
    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry>* layer,
    bool earlierOnTop
);

}  // namespace miacode::preview::scene
