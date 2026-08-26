#pragma once

#include <limits>

#include <QString>
#include <QVector>

#include "core/chart/document/SimaiTimingMetadata.h"
#include "timeline/TimelineRenderData.h"

class TimelineQuickModel
{
public:
    struct PreviewFollowSpan {
        int startLine = 1;
        int startCol = 1;
        int endLine = 1;
        int endCol = 1;
        int cursorLine = 1;
        int cursorCol = 1;
        int startPosition = 0;
        int endPositionExclusive = 0;
        int cursorPosition = 0;
        bool hasVisibleBody = false;
    };

    struct PreviewFollowBinding {
        double startSecond = 0.0;
        double endSecondExclusive = std::numeric_limits<double>::infinity();
        double anchorSecond = 0.0;
        int anchorLine = 1;
        int anchorCol = 1;
        PreviewFollowSpan span;
        bool resolved = false;
    };

    TimelineQuickModel() = default;

    void clear();
    bool rebuildFromText(
        const QString& text,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());
    bool applyTextChange(
        const QString& text,
        int position,
        int charsRemoved,
        int charsAdded,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());

    const TimelineRenderSnapshot& snapshot() const;

    double timelineSecondForCursor(int lineNumber, int col) const;
    bool resolveTimelineSecondForCursor(int lineNumber, int col, double* second) const;
    bool resolveTimelineNavigateCursor(double second, int* line, int* col, double* cursorSecond) const;
    bool resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const;
    bool resolvePreviewFollowSelectionRange(int line, int anchorCol, int* startCol, int* endCol) const;
    bool resolvePreviewFollowBinding(double second, PreviewFollowBinding* binding) const;
    bool resolvePreviewFollowSpan(double second, PreviewFollowSpan* span, double* anchorSecond = nullptr) const;
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
        // Whether the chart-start measure line has been emitted yet. Mirrors the
        // strict parser's `initializedMeasureLines`: the very first NON-terminal
        // line seeds it, so a leading terminal `E` line no longer suppresses the
        // chart-start grid.
        bool initialMeasureLineEmitted = false;
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

        struct FollowSelectionSpan {
            int anchorCol = 1;
            PreviewFollowSpan span;
        };

        QVector<TimelineCursorAnchor> segmentStarts;
        QVector<FollowSelectionRange> followSelectionRanges;
        QVector<FollowSelectionSpan> followSelectionSpans;
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

    bool rebuildFromLineTexts(
        const QVector<QString>& lines,
        double firstSeconds,
        const miacode::simai::SimaiTimingMetadata& timingMetadata);
    int lineIndexForStoredPosition(int position) const;
    bool parseLine(LineState* lineState, const ParseState& startState);
    void shiftLineTiming(LineState* lineState, double deltaSeconds) const;
    void rebuildSlideDerivedFlags();
    void rebuildSnapshotDuration();
    void resequenceLineMetadata(int startIndex);
    void rebuildAnchorLineIndices();
    void rebuildFollowSelectionRanges(LineState* lineState) const;
    void rebuildFollowSelectionSpans();
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
    QVector<PreviewFollowBinding> previewFollowBindings_;
    QVector<int> linesWithNotes_;
};
