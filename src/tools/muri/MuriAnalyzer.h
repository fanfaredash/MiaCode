#pragma once

#include <QVector>

#include "common/MuriTypes.h"

struct TimelineNoteMarker;

class MuriAnalyzer
{
public:
    static MuriAnalysisReport analyze(const QVector<TimelineNoteMarker>& noteMarkers);
};
