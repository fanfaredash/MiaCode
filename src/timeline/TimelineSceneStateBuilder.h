#pragma once

#include <QFont>
#include <QHash>
#include <QSize>

#include <memory>

#include "common/WaveformCache.h"
#include "timeline/TimelineRenderData.h"
#include "timeline/TimelineSceneState.h"

namespace miacode::timeline {

struct TimelineSceneBuildRequest {
    TimelineRenderSnapshot snapshot;
    std::shared_ptr<const miacode::waveform::WaveformData> waveformData;
    QHash<quint64, QVector<TimelineMuriMarkerPlacement>> muriMarkersByLocation;
    QHash<quint64, QString> muriMarkerTooltips;
    QSize viewportSize;
    QFont headerLineNumberFont;
    int horizontalScrollValue = 0;
    int headerLeftLimit = 0;
    int headerRightLimit = 0;
    double zoomScale = 0.5;
    double playbackEntrySeconds = 0.0;
    double playheadSeconds = 0.0;
    double cursorSeconds = 0.0;
    double playheadUpperLimitSeconds = -1.0;
    bool showSlideTracks = true;
    bool playheadIndicatorSuppressed = false;
    bool dragActive = false;
    quint64 appearanceRevision = 0;
    quint64 gridRevision = 0;
    quint64 waveformRevision = 0;
    quint64 headerRevision = 0;
    quint64 notesRevision = 0;
    quint64 overlayRevision = 0;
    quint64 overlayDynamicRevision = 0;
};

class TimelineSceneStateBuilder
{
public:
    static TimelineSceneLayoutMetrics layoutMetrics(const TimelineSceneBuildRequest& request);
    static int maxHorizontalScrollValue(const TimelineSceneLayoutMetrics& metrics);
    static int secondToSceneX(const TimelineSceneLayoutMetrics& metrics, double second);
    static TimelineSceneState build(const TimelineSceneBuildRequest& request);
    static int secondToSceneX(const TimelineSceneState& state, double second);
    static double sceneXToSecond(const TimelineSceneState& state, qreal x);
};

}  // namespace miacode::timeline
