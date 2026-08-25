#pragma once

#include <QByteArray>
#include <QVector>

#include "ChartWorkspace.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "core/chart/parser/SimaiNativeParser.h"
#include "timeline/TimelineData.h"

namespace miacode::v2 {

// Immutable, revision-stamped output of one workspace analysis transaction.
// A later async scheduler can discard this whole value when its revision no
// longer matches ChartWorkspace; it never needs to inspect a MainWindow cache.
struct AnalysisSnapshot {
    quint64 revision = 0;
    int difficultyId = 0;
    bool available = false;
    SimaiNativeValidationReport validation;
    QVector<TimelineNoteMarker> noteMarkers;
    QByteArray noteMarkerSignature;
    MuriAnalysisReport muri;
    QVector<MuriStaticReference> muriStaticReferences;
};

class AnalysisService final
{
public:
    static AnalysisSnapshot analyze(
        const ChartWorkspace& workspace,
        SimaiNativeValidationLocale locale = SimaiNativeValidationLocale::English,
        const MuriRenderOptions& renderOptions = {},
        double staticTapOnSlideThresholdSeconds = -1.0);
};

}  // namespace miacode::v2
