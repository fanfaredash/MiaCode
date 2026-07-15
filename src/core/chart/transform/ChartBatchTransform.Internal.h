#pragma once

#include "common/OperationLog.h"

#include <functional>

#include <QChar>
#include <QString>
#include <QStringList>

namespace miacode::chart_transform::detail {

struct TouchTokenParts {
    QString prefix;
    QString bracketSuffix;
    bool hasHold = false;
    bool hasBreak = false;
    bool hasEx = false;
    bool hasFirework = false;
    bool valid = false;
};

struct NoteTokenParts {
    QChar lane;
    QString bracketSuffix;
    bool hasHold = false;
    bool hasBreak = false;
    bool hasEx = false;
    bool hasMine = false;
    bool tapUsesStarMaterial = false;
    bool tapStarDouble = false;
    bool valid = false;
};

struct SlideSegmentParts {
    QString text;          // segment text with all 'b' chars stripped
    bool segmentBreak = false;  // there was a 'b' anywhere in this segment
};

struct SlideTokenParts {
    QChar lane;
    bool headBreak = false;
    bool headEx = false;
    bool headUsesTapMaterial = false;
    QChar headlessModifier;
    QVector<SlideSegmentParts> segments;
    bool valid = false;
};

struct ToggleStats {
    int eligibleObjects = 0;
    int flaggedObjects = 0;
};

struct SelectionEdgeSplit {
    QString prefix;
    QString core;
    QString suffix;
};

// Parsers TU
bool isDigitLane(QChar ch);
bool isSimpleDigitCluster(const QString& token);
bool isSlideOperatorChar(QChar ch);
bool parseTouchTokenParts(const QString& token, TouchTokenParts* parts);
bool parseNoteTokenParts(const QString& token, NoteTokenParts* parts);
bool parseSlideTokenParts(const QString& token, SlideTokenParts* parts);
QString buildTouchToken(const TouchTokenParts& parts, bool hasBreak, bool hasEx, bool hasFirework);
QString buildNoteToken(const NoteTokenParts& parts, bool hasBreak, bool hasEx);
QString buildSlideToken(const SlideTokenParts& parts);
QChar rotateLaneChar(QChar lane, int steps);
QString rotateSlideCoreOutsideBrackets(const QString& text, int steps);

// Subdivision TU
SelectionEdgeSplit splitSelectionEdges(const QString& input);
QString rewriteSelectionCore(const SelectionEdgeSplit& split, const std::function<QString(const QString&)>& rewriteCore);
QString rewriteSelectionCore(const QString& input, const std::function<QString(const QString&)>& rewriteCore);

}  // namespace miacode::chart_transform::detail
