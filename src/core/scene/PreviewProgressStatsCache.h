#pragma once

#include <QVector>

#include "core/scene/PreviewHudState.h"
#include "timeline/TimelineData.h"

namespace miacode::preview::scene {

struct PreviewObjectStatsSnapshot {
    int tapTotal = 0;
    int tapPlayed = 0;
    int holdTotal = 0;
    int holdPlayed = 0;
    int slideTotal = 0;
    int slidePlayed = 0;
    int touchTotal = 0;
    int touchPlayed = 0;
    int breakTotal = 0;
    int breakPlayed = 0;
    int totalCount = 0;
    int totalPlayed = 0;
};

class PreviewProgressStatsCache
{
public:
    void rebuild(const QVector<TimelineNoteMarker>& markers);
    bool isEmpty() const;
    PreviewObjectStatsSnapshot snapshotAt(double second) const;
    PreviewHudStats hudStatsAt(double second) const;

private:
    struct ObjectEventPrefix {
        double judgeSecond = 0.0;
        int tapPlayed = 0;
        int holdPlayed = 0;
        int slidePlayed = 0;
        int touchPlayed = 0;
        int breakPlayed = 0;
        int totalPlayed = 0;
    };

    struct HudEventPrefix {
        double judgeSecond = 0.0;
        int tapPlayed = 0;
        int holdPlayed = 0;
        int slidePlayed = 0;
        int touchPlayed = 0;
        int breakPlayed = 0;
        double finaleCurrent = 0.0;
        double deluxeBaseCurrent = 0.0;
        int deluxeBreakCurrent = 0;
    };

    PreviewObjectStatsSnapshot totals_;
    QVector<ObjectEventPrefix> objectEventPrefixes_;
    QVector<HudEventPrefix> hudEventPrefixes_;
    double finaleBaseTotal_ = 0.0;
    double deluxeBaseTotal_ = 0.0;
    int deluxeBreakTotal_ = 0;
};

}  // namespace miacode::preview::scene
