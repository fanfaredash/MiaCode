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

class SimaiNativeParser
{
public:
    static SimaiNativeParseResult parseForTimeline(const QString& text);
    static SimaiNativeParseResult validateSyntax(const QString& text);
};

