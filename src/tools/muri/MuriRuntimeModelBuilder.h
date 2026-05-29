#pragma once

#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

#include "tools/muri/MuriAnalyzerModel.h"

struct TimelineNoteMarker;

// Runtime-model build stage of the Muri analyzer (2026-05-29 god-file
// decomposition). Turns parsed note markers into the runtime note / touch-group
// model that the slide/wifi and simple-note judge stages consume. Internal to
// the Muri analyzer — namespace miacode::muri::detail.
namespace miacode::muri::detail {

// The runtime model produced from a chart's note markers. This is the data
// object passed from the model-builder stage to the judge stages.
struct MuriRuntimeModel {
    QHash<QString, MarkerSourceRef> markerRefs;
    QVector<JudgeableSimpleNote> notes;
    QHash<QString, int> noteIndexByMarkerKey;
    QVector<RuntimeTouchGroup> touchGroups;
    QHash<int, int> touchGroupByChildNoteIndex;
};

// Granular build steps (also used directly by the judge stages that need only
// part of the model).
QHash<QString, MarkerSourceRef> buildMarkerSourceRefs(
    const QVector<TimelineNoteMarker>& noteMarkers);
QVector<JudgeableSimpleNote> buildJudgeableSimpleNotes(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    QHash<QString, int>* noteIndexByMarkerKey);
QMap<int, QVector<int>> buildNoteExpiryBuckets(const QVector<JudgeableSimpleNote>& notes);
QVector<RuntimeTouchGroup> buildRuntimeTouchGroups(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QHash<QString, int>& noteIndexByMarkerKey,
    QHash<int, int>* touchGroupByChildNoteIndex);
QVector<RuntimeTopLevelNote> buildRuntimeTopLevelNotes(
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex);

// Assembles the full runtime-model bundle in one call. Used by the analyze
// orchestrator; the judges may instead call the granular builders above.
class MuriRuntimeModelBuilder {
public:
    static MuriRuntimeModel build(const QVector<TimelineNoteMarker>& noteMarkers);
};

}  // namespace miacode::muri::detail
