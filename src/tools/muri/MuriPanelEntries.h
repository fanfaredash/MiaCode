#pragma once

#include <QVector>

#include "common/MuriTypes.h"

namespace miacode::muri {

struct MuriPanelEntry {
    bool isStatic = false;
    MuriKind kind = MuriKind::Overlap;
    MuriAlertLevel alertLevel = MuriAlertLevel::Muri;
    double second = 0.0;
    double occurrenceSecond = 0.0;
    int line = 1;
    int col = 1;
    QString rawDetail;
};

QVector<MuriPanelEntry> buildVisibleMuriPanelEntries(
    const MuriAnalysisReport& analysisReport,
    const QVector<MuriStaticReference>& staticReferences);

}  // namespace miacode::muri
