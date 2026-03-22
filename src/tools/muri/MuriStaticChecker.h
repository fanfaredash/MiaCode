#pragma once

#include <QVector>

struct TimelineNoteMarker;
struct MuriStaticReference;

namespace miacode::muri {

constexpr int kStaticTapOnSlideThresholdMinMs = 150;
constexpr int kStaticTapOnSlideThresholdMaxMs = 250;
constexpr int kStaticTapOnSlideThresholdDefaultMs = 200;

QVector<MuriStaticReference> buildStaticMuriReferences(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double collideThresholdSeconds);

}  // namespace miacode::muri
