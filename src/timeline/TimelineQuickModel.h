#pragma once

#include <QString>
#include <QVector>

#include "simai/document/SimaiTimingMetadata.h"
#include "timeline/TimelineRenderData.h"

class QTextDocument;

class TimelineQuickModel
{
public:
    TimelineQuickModel() = default;

    void clear();
    bool rebuildFromText(
        const QString& text,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());
    bool rebuildFromDocument(
        const QTextDocument* document,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());
    bool applyContentsChange(
        const QTextDocument* document,
        int position,
        int charsRemoved,
        int charsAdded,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());

    const TimelineRenderSnapshot& snapshot() const;

    double timelineSecondForCursor(int lineNumber, int col) const;
    bool resolveTimelineNavigateCursor(double second, int* line, int* col, double* cursorSecond) const;
    bool resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const;
    bool resolvePreviewFollowSelectionRange(int line, int anchorCol, int* startCol, int* endCol) const;
    bool resolvePreviewFollowCursor(
        double second,
        int* line,
        int* col,
        double* noteSecond) const;
    bool resolvePreviewFollowSelection(
        double second,
        int* line,
        int* startCol,
        int* endCol,
        double* anchorSecond) const;

private:
    struct ParseState {
        double second = 0.0;
        double bpm = 120.0;
        int beats = 4;
        int subdivisionIndex = 0;
        int meterNumerator = 4;
        int meterDenominator = 4;
        double currentMeasureStartSecond = 0.0;
    };

    struct AbsoluteCursorAnchor {
        int lineNumber = 1;
        int sourceCol = 1;
        double second = 0.0;
    };

    struct LineCursorCache {
        struct FollowSelectionRange {
            int anchorCol = 1;
            int startCol = 1;
            int endCol = 1;
        };

        QVector<TimelineCursorAnchor> segmentStarts;
        QVector<FollowSelectionRange> followSelectionRanges;
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
        bool isTerminalE = false;
        double terminalSecond = -1.0;
        bool hasNotes = false;
        double firstNoteSecond = 0.0;
        double lastNoteSecond = 0.0;
    };

    bool replaceDocumentTail(
        const QTextDocument* document,
        int startLineIndex,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata);
    bool rebuildFromLineTexts(
        const QVector<QString>& lines,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata);
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
    void rebuildFollowSelectionRanges(LineState* lineState) const;
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
    QVector<AbsoluteCursorAnchor> cursorAnchorsBySecond_;
    QVector<int> linesWithNotes_;
};
