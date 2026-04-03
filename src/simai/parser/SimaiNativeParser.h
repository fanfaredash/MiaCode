#pragma once

#include <QString>
#include <QVector>

#include "simai/document/SimaiTimingMetadata.h"
#include "timeline/TimelineData.h"

struct SimaiNativeMessage {
    int line = 1;
    int col = 1;
    int endCol = 1;
    QString message;
};

struct SimaiNativeParseResult {
    bool ok = true;
    QVector<SimaiNativeMessage> errors;
    QVector<SimaiNativeMessage> warnings;
    QVector<double> measureLineSeconds;
    QVector<TimelineBeatMarker> beatMarkers;
    QVector<TimelineNoteMarker> noteMarkers;
    double durationSeconds = 0.0;
};

enum class SimaiNativeValidationLocale {
    English,
    Chinese,
};

enum class SimaiNativeValidationSeverity {
    Error,
    Warning,
};

struct SimaiNativeValidationIssue {
    int line = 1;
    int col = 1;
    int endCol = 1;
    SimaiNativeValidationSeverity severity = SimaiNativeValidationSeverity::Error;
    QString rawMessage;
    QString displayMessage;
};

struct SimaiNativeValidationReport {
    bool ok = true;
    int errorCount = 0;
    int warningCount = 0;
    int lenientNoteCount = 0;
    int lenientErrorCount = 0;
    int strictNoteCount = 0;
    int strictErrorCount = 0;
    QVector<SimaiNativeValidationIssue> issues;
};

class SimaiNativeParser
{
public:
    static SimaiNativeParseResult parseForTimeline(
        const QString& text,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());
    static SimaiNativeParseResult validateSyntax(
        const QString& text,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());
    static void setInvalidStarPreviewEnabled(bool enabled);
    static bool invalidStarPreviewEnabled();
    static SimaiNativeValidationReport buildValidationReport(
        const QString& text,
        SimaiNativeValidationLocale locale,
        const SimaiNativeParseResult* lenientResult = nullptr,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata()
    );
};
