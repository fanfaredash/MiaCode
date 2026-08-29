#pragma once

#include "core/chart/transform/ChartBatchTransform.h"

#include <QString>
#include <QVector>

#include <functional>

namespace miacode::qml_ui {

// The 谱面变换 commands, keyed by their ShortcutRegistry id so the shortcut
// table, the menu and the dispatch all name an operation identically. The table
// lives in a header with no MainWindow in it so a spec can check it against the
// registry directly — a typo used to yield a silently inert shortcut.
//
// `apply` transforms the selected text. The four subdivision operations also
// need the text that follows the selection: a duration written before the
// selection ends can only be adjusted correctly if the tail it applies to is
// visible. An empty `apply` marks the one operation that must see the whole
// text and the range instead, because whether an element is complete depends on
// the line it sits in.
struct ChartTransformSpec {
    QString id;
    std::function<QString(const QString& selection, const QString& suffix, int* changed)> apply;
};

inline QVector<ChartTransformSpec> chartTransformSpecs()
{
    namespace transform = miacode::chart_transform;
    using transform::ChartTransformOp;
    const auto plain = [](ChartTransformOp op) {
        return [op](const QString& selection, const QString&, int* changed) {
            return transform::transformChartSelectionText(selection, op, changed);
        };
    };
    const auto selectionOnly = [](QString (*fn)(const QString&, int*)) {
        return [fn](const QString& selection, const QString&, int* changed) {
            return fn(selection, changed);
        };
    };
    const auto withSuffix = [](QString (*fn)(const QString&, const QString&, int*)) {
        return [fn](const QString& selection, const QString& suffix, int* changed) {
            return fn(selection, suffix, changed);
        };
    };
    return {
        {QStringLiteral("transform.mirror_lr"), plain(ChartTransformOp::MirrorLeftRight)},
        {QStringLiteral("transform.mirror_ud"), plain(ChartTransformOp::MirrorUpDown)},
        {QStringLiteral("transform.rotate_180"), plain(ChartTransformOp::Rotate180)},
        {QStringLiteral("transform.rotate_ccw_45"), plain(ChartTransformOp::Rotate45CounterClockwise)},
        {QStringLiteral("transform.rotate_cw_45"), plain(ChartTransformOp::Rotate45Clockwise)},
        {QStringLiteral("transform.subdivision_up"), withSuffix(&transform::raiseSubdivisionForSelection)},
        {QStringLiteral("transform.subdivision_down"), withSuffix(&transform::lowerSubdivisionForSelection)},
        {QStringLiteral("transform.subdivision_half_up"),
         withSuffix(&transform::raiseSubdivisionHalfStepForSelection)},
        {QStringLiteral("transform.subdivision_half_down"),
         withSuffix(&transform::lowerSubdivisionHalfStepForSelection)},
        {QStringLiteral("transform.toggle_break"), selectionOnly(&transform::toggleBreakForSelection)},
        {QStringLiteral("transform.toggle_ex"), selectionOnly(&transform::toggleExForSelection)},
        {QStringLiteral("transform.toggle_firework"), selectionOnly(&transform::toggleFireworkForSelection)},
        {QStringLiteral("transform.random_rotate"),
         [](const QString& selection, const QString&, int* changed) {
             return transform::randomRotateForSelection(selection, changed);
         }},
        {QStringLiteral("transform.clear_complete_elements"), {}},
    };
}

}  // namespace miacode::qml_ui
