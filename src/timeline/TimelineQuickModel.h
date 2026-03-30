#pragma once

#include <QString>
#include <QVector>

#include "timeline/TimelineRenderData.h"

class QTextDocument;

class TimelineQuickModel
{
public:
    enum class PreviewFollowMode {
        EveryComma,
        NonEmptyComma,
        LineOnly,
    };

    TimelineQuickModel() = default;

    void clear();
    bool rebuildFromText(const QString& text, double firstSeconds);
    bool rebuildFromDocument(const QTextDocument* document, double firstSeconds);
    bool applyContentsChange(
        const QTextDocument* document,
        int position,
        int charsRemoved,
        int charsAdded,
        double firstSeconds);

    const TimelineRenderSnapshot& snapshot() const;

    double timelineSecondForCursor(int lineNumber, int col) const;
    bool resolveTimelineNavigateCursor(double second, int* line, int* col, double* cursorSecond) const;
    bool resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const;
    bool resolvePreviewFollowCursor(
        PreviewFollowMode mode,
        double second,
        int anchorLine,
        int anchorCol,
        bool useAbsoluteSeekAnchor,
        int* line,
        int* col,
        double* noteSecond) const;

private:
    struct ParseState {
        double second = 0.0;
        double bpm = 120.0;
        int beats = 4;
    };

    struct LineCursorCache {
        QVector<TimelineCursorAnchor> everyComma;
        QVector<TimelineCursorAnchor> nonEmptyComma;
        TimelineCursorAnchor lineOnly;
        bool hasLineOnly = false;
    };

    struct LineState {
        int lineId = 0;
        int lineNumber = 1;
        int startPosition = 0;
        QString text;
        ParseState startState;
        ParseState endState;
        TimelineRenderLine render;
        LineCursorCache cursorCache;
        bool hasRawZeroAnchor = false;
        bool hasNotes = false;
        double firstNoteSecond = 0.0;
        double lastNoteSecond = 0.0;
    };

    bool replaceDocumentTail(
        const QTextDocument* document,
        int startLineIndex,
        double firstSeconds);
    bool rebuildFromLineTexts(const QVector<QString>& lines, double firstSeconds);
    QVector<QString> collectDocumentLines(const QTextDocument* document) const;
    QVector<QString> collectDocumentLines(const QTextDocument* document, int startLineIndex, int endLineIndex) const;
    int lineIndexForDocumentPosition(const QTextDocument* document, int position) const;
    int lineIndexForStoredPosition(int position) const;
    bool parseLine(LineState* lineState, const ParseState& startState);
    void shiftLineTiming(LineState* lineState, double deltaSeconds) const;
    void rebuildSlideDerivedFlags();
    void rebuildSnapshotDuration();
    void resequenceLineMetadata(int startIndex);
    void rebuildAnchorLineIndices();
    bool resolvePreviousCursorAnchorForTextPosition(
        int lineNumber,
        int sourceCol,
        int* line,
        int* col,
        double* second) const;
    bool resolvePreviousCursorAnchorForSecond(
        double second,
        int* line,
        int* col,
        double* anchorSecond) const;
    int resolveRecentPastLineIndex(double second) const;
    bool parseNoteToken(
        LineState* lineState,
        ParseState* state,
        const QString& token,
        int lineNumber,
        int column,
        QVector<int>* groupIndices) const;
    void finalizeEachGroup(LineState* lineState, const QVector<int>& groupIndices);
    int allocateLineId();
    static bool parseStatesEqual(const ParseState& a, const ParseState& b);

    int nextLineId_ = 1;
    int nextEachGroupId_ = 0;
    QVector<LineState> lines_;
    TimelineRenderSnapshot snapshot_;
    QVector<int> linesWithEveryComma_;
    QVector<int> linesWithNonEmptyComma_;
    QVector<int> linesWithLineOnly_;
    QVector<int> linesWithNotes_;
};
