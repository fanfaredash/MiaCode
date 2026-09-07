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
// text and the range instead (一键清空), because whether an element is complete
// depends on the line it sits in. The same whole-text path is used by reset
// tap notes, which preserves directives and timing skeletons while replacing
// each complete selected element.
struct ChartTransformSpec {
    QString id;
    std::function<QString(const QString& selection, const QString& suffix, int* changed)> apply;
    // UiText key for the menu row, so the shortcut editor, the menubar and the
    // editor's context menu all name an operation the same way.
    QString labelKey;
    // Menu grouping, matching the Widgets 调整 menu the QML one replaces:
    // 0 mirrors and rotations, 1 subdivision steps, 2 一键清空, 3 inside 更多.
    int section = 0;
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
        {QStringLiteral("transform.mirror_lr"),
         plain(ChartTransformOp::MirrorLeftRight),
         QStringLiteral("action.transform.mirror_lr"), 0},
        {QStringLiteral("transform.mirror_ud"),
         plain(ChartTransformOp::MirrorUpDown),
         QStringLiteral("action.transform.mirror_ud"), 0},
        {QStringLiteral("transform.rotate_180"),
         plain(ChartTransformOp::Rotate180),
         QStringLiteral("action.transform.rotate_180"), 0},
        {QStringLiteral("transform.rotate_ccw_45"),
         plain(ChartTransformOp::Rotate45CounterClockwise),
         QStringLiteral("action.transform.rotate_ccw_45"), 0},
        {QStringLiteral("transform.rotate_cw_45"),
         plain(ChartTransformOp::Rotate45Clockwise),
         QStringLiteral("action.transform.rotate_cw_45"), 0},
        {QStringLiteral("transform.subdivision_up"),
         withSuffix(&transform::raiseSubdivisionForSelection),
         QStringLiteral("document.subdivision_plus_1"), 1},
        {QStringLiteral("transform.subdivision_down"),
         withSuffix(&transform::lowerSubdivisionForSelection),
         QStringLiteral("document.subdivision_minus_1"), 1},
        {QStringLiteral("transform.subdivision_half_up"),
         withSuffix(&transform::raiseSubdivisionHalfStepForSelection),
         QStringLiteral("document.subdivision_plus_half"), 1},
        {QStringLiteral("transform.subdivision_half_down"),
         withSuffix(&transform::lowerSubdivisionHalfStepForSelection),
         QStringLiteral("document.subdivision_minus_half"), 1},
        {QStringLiteral("transform.clear_complete_elements"),
         {},
         QStringLiteral("menu.clear_elements"), 2},
        {QStringLiteral("transform.reset_tap_notes"),
         {},
         QStringLiteral("menu.reset_tap_notes"), 2},
        {QStringLiteral("transform.toggle_break"),
         selectionOnly(&transform::toggleBreakForSelection),
         QStringLiteral("action.transform.toggle_break"), 3},
        {QStringLiteral("transform.toggle_ex"),
         selectionOnly(&transform::toggleExForSelection),
         QStringLiteral("action.transform.toggle_ex"), 3},
        {QStringLiteral("transform.toggle_firework"),
         selectionOnly(&transform::toggleFireworkForSelection),
         QStringLiteral("action.transform.toggle_firework"), 3},
        {QStringLiteral("transform.random_rotate"),
         [](const QString& selection, const QString&, int* changed) {
             return transform::randomRotateForSelection(selection, changed);
         },
         QStringLiteral("action.transform.random_rotate"), 3},
    };
}

}  // namespace miacode::qml_ui
