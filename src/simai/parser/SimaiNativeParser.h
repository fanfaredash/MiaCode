#pragma once

#include <QString>
#include <QVector>

#include "TimelineView.h"

struct SimaiNativeMessage {
    int line = 1;
    int col = 1;
    QString message;
};

struct SimaiNativeParseResult {
    bool ok = true;
    QVector<SimaiNativeMessage> errors;
    QVector<SimaiNativeMessage> warnings;
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
    static SimaiNativeParseResult parseForTimeline(const QString& text);
    static SimaiNativeParseResult validateSyntax(const QString& text);
    static SimaiNativeValidationReport buildValidationReport(
        const QString& text,
        SimaiNativeValidationLocale locale
    );
};
