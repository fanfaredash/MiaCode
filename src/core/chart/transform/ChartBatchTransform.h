#pragma once

#include <functional>

#include <QString>

namespace miacode::chart_transform {

enum class ChartTransformOp {
    MirrorLeftRight,
    MirrorUpDown,
    Rotate180,
    Rotate45CounterClockwise,
    Rotate45Clockwise,
};

QString transformChartText(const QString& input, ChartTransformOp op, int* changedCount = nullptr);
QString transformChartSelectionText(const QString& input, ChartTransformOp op, int* changedCount = nullptr);
QString toggleBreakForSelection(const QString& input, int* changedCount = nullptr);
QString toggleExForSelection(const QString& input, int* changedCount = nullptr);
QString toggleFireworkForSelection(const QString& input, int* changedCount = nullptr);
QString randomRotateForSelection(const QString& input, int* changedCount = nullptr);
QString randomRotateForSelection(
    const QString& input,
    const std::function<int()>& nextStep,
    int* changedCount = nullptr);
QString raiseSubdivisionForSelection(const QString& input, int* changedCount = nullptr);
QString lowerSubdivisionForSelection(const QString& input, int* changedCount = nullptr);
QString raiseSubdivisionHalfStepForSelection(const QString& input, int* changedCount = nullptr);
QString lowerSubdivisionHalfStepForSelection(const QString& input, int* changedCount = nullptr);

// The chart text around a selection. A subdivision step needs both sides: the
// {N} governing the selection's first beats may be written before it, and the
// text after it decides whether the original {N} has to be restored.
struct SelectionContext {
    QString before;
    QString after;
};

QString raiseSubdivisionForSelection(const QString& input, const SelectionContext& context, int* changedCount = nullptr);
QString lowerSubdivisionForSelection(const QString& input, const SelectionContext& context, int* changedCount = nullptr);
QString raiseSubdivisionHalfStepForSelection(const QString& input, const SelectionContext& context, int* changedCount = nullptr);
QString lowerSubdivisionHalfStepForSelection(const QString& input, const SelectionContext& context, int* changedCount = nullptr);

// Clears every complete note element inside the selection, leaving the timing
// skeleton. Lived in PlainCodeEditor.h while the Widgets editor owned the
// chart-editing commands; it is a text transform over a selection like the rest
// of this header, and has no widget in it.
QString clearCompleteElementsInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    int* changedCount = nullptr);

// Reduces every complete note element in the selection to one lane-1 tap while
// retaining timing controls, whitespace, empty beats, and comments.
QString resetTapNotesInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    int* changedCount = nullptr);

}  // namespace miacode::chart_transform
