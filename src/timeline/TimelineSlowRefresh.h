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

struct TimelinePreviewRefreshResult {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    double firstSeconds = 0.0;
    SimaiNativeParseResult parseResult;
    QVector<TimelineBeatMarker> shiftedBeatMarkers;
    QVector<TimelineNoteMarker> shiftedNoteMarkers;
    QByteArray noteMarkerSignature;
    double durationSeconds = 0.0;
};

struct TimelinePreviewRefreshState {
    QVector<TimelineNoteMarker> shiftedNoteMarkers;
    QByteArray noteMarkerSignature;
};

struct TimelineValidationRefreshRequest {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    bool chineseUi = false;
    SimaiNativeParseResult parseResult;
};

struct TimelineValidationRefreshResult {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    bool chineseUi = false;
    SimaiNativeValidationReport validationReport;
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

TimelinePreviewRefreshState buildTimelinePreviewRefreshState(
    const SimaiNativeParseResult& parseResult,
    double firstSeconds);
TimelinePreviewRefreshResult buildTimelinePreviewRefreshResult(
    const TimelineSlowRefreshRequest& request,
    const SimaiNativeParseResult& parseResult,
    const TimelinePreviewRefreshState& previewState);
TimelinePreviewRefreshResult buildTimelinePreviewRefreshResult(const TimelineSlowRefreshRequest& request);
TimelinePreviewRefreshState buildTimelinePreviewRefreshState(const QString& chartText, double firstSeconds);
TimelineValidationRefreshResult buildTimelineValidationRefreshResult(const TimelineValidationRefreshRequest& request);
TimelineMuriRefreshResult buildTimelineMuriRefreshResult(const TimelineMuriRefreshRequest& request);
