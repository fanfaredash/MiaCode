#pragma once

#include <QVector>

#include "common/MuriConfig.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"

struct TimelineNoteMarker;

class MuriAnalyzer
{
public:
    static MuriAnalysisReport analyze(
        const QVector<TimelineNoteMarker>& noteMarkers,
        const MuriRenderOptions& renderOptions = {});
    static MuriAnalysisReport analyze(
        const QVector<TimelineNoteMarker>& noteMarkers,
        const MuriRenderOptions& renderOptions,
        double staticTapOnSlideThresholdSeconds);
};
