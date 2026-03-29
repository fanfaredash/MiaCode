#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "simai/parser/SimaiNativeParser.h"
#include "timeline/TimelineData.h"

struct TimelineSlowRefreshRequest {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    double firstSeconds = 0.0;
    bool chineseUi = false;
};

struct TimelineSlowRefreshResult {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    double firstSeconds = 0.0;
    bool chineseUi = false;
    SimaiNativeParseResult parseResult;
    QVector<TimelineBeatMarker> shiftedBeatMarkers;
    QVector<TimelineNoteMarker> shiftedNoteMarkers;
    QByteArray noteMarkerSignature;
    SimaiNativeValidationReport validationReport;
    double durationSeconds = 0.0;
};

struct TimelineMuriRefreshRequest {
    quint64 revision = 0;
    int difficultyId = 0;
    QByteArray noteMarkerSignature;
    QVector<TimelineNoteMarker> noteMarkers;
    MuriRenderOptions renderOptions;
    double staticTapOnSlideThresholdSeconds = 0.0;
};

struct TimelineMuriRefreshResult {
    quint64 revision = 0;
    int difficultyId = 0;
    QByteArray noteMarkerSignature;
    MuriAnalysisReport analysisReport;
    QVector<MuriStaticReference> staticReferences;
};

TimelineSlowRefreshResult buildTimelineSlowRefreshResult(const TimelineSlowRefreshRequest& request);
TimelineMuriRefreshResult buildTimelineMuriRefreshResult(const TimelineMuriRefreshRequest& request);
