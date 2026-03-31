#pragma once

#include <QVector>

#include "common/MuriConfig.h"

struct TimelineNoteMarker;
struct MuriStaticReference;

namespace miacode::muri {

QVector<MuriStaticReference> buildStaticMuriReferences(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double collideThresholdSeconds);

}  // namespace miacode::muri
