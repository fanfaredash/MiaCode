#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "core/chart/document/SimaiTimingMetadata.h"
#include "core/chart/parser/SimaiNativeParser.h"
#include "timeline/TimelineData.h"

struct TimelineSlowRefreshRequest {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    double firstSeconds = 0.0;
    miacode::simai::SimaiTimingMetadata timingMetadata;
    SimaiNativeValidationLocale validationLocale = SimaiNativeValidationLocale::English;
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

struct TimelineAnalysisRefreshRequest {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    SimaiNativeValidationLocale validationLocale = SimaiNativeValidationLocale::English;
    miacode::simai::SimaiTimingMetadata timingMetadata;
    SimaiNativeParseResult parseResult;
    QByteArray noteMarkerSignature;
    QVector<TimelineNoteMarker> noteMarkers;
    MuriRenderOptions renderOptions;
    double staticTapOnSlideThresholdSeconds = 0.0;
};

struct TimelineAnalysisRefreshResult {
    quint64 revision = 0;
    int difficultyId = 0;
    QString chartText;
    SimaiNativeValidationLocale validationLocale = SimaiNativeValidationLocale::English;
    miacode::simai::SimaiTimingMetadata timingMetadata;
    QByteArray noteMarkerSignature;
    SimaiNativeValidationReport validationReport;
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
TimelineAnalysisRefreshResult buildTimelineAnalysisRefreshResult(const TimelineAnalysisRefreshRequest& request);
