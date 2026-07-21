#include <QCoreApplication>
#include <QTextStream>

#include "tools/video_export/BatchExportPanel.h"
#include "tools/video_export/BatchExportTaskLayout.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyDifficultySelectionStaysIndependentFromPreviewBadge(QTextStream& err)
{
    miacode::video_export::BatchExportSelectionState state({1, 2, 3, 4, 5, 6, 7}, 3);
    if (!require(
            state.selectedDifficultyIds() == QList<int>({3}),
            QStringLiteral("initial badge should seed exactly one selected difficulty"),
            err)) {
        return false;
    }

    state.setSelectedDifficultyIds({2, 5});
    state.updatePreviewDifficulty(6);
    return require(
        state.selectedDifficultyIds() == QList<int>({2, 5}),
        QStringLiteral("changing the preview badge must not reset user-selected difficulties"),
        err);
}

bool verifyDifficultySelectionIsFilteredAndDeduplicated(QTextStream& err)
{
    miacode::video_export::BatchExportSelectionState state({1, 2, 3, 4, 5, 6, 7}, 4);
    state.setSelectedDifficultyIds({7, 7, 99, 2, 2, 0});
    if (!require(
            state.selectedDifficultyIds() == QList<int>({7, 2}),
            QStringLiteral("batch difficulty selection must ignore unavailable ids and duplicates"),
            err)) {
        return false;
    }

    state.updatePreviewDifficulty(99);
    return require(
        state.previewDifficultyId() == 4,
        QStringLiteral("an unavailable preview badge must not replace the current preview target"),
        err);
}

bool verifyTaskDifficultyGridUsesFourColumns(QTextStream& err)
{
    using miacode::video_export::BatchExportTaskGridPosition;
    using miacode::video_export::batchExportTaskDifficultyGridPosition;
    using miacode::video_export::kBatchExportTaskDifficultyGridColumns;

    if (!require(
            kBatchExportTaskDifficultyGridColumns == 4,
            QStringLiteral("batch task difficulty grid must reserve exactly four columns"),
            err)) {
        return false;
    }

    const QList<BatchExportTaskGridPosition> expected{
        {0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 0}, {1, 1}, {1, 2},
    };
    for (int index = 0; index < expected.size(); ++index) {
        const BatchExportTaskGridPosition actual = batchExportTaskDifficultyGridPosition(index);
        if (!require(
                actual == expected.at(index),
                QStringLiteral("difficulty %1 must use a row-major four-column task grid").arg(index),
                err)) {
            return false;
        }
        if (!require(
                actual.column >= 0 && actual.column < kBatchExportTaskDifficultyGridColumns,
                QStringLiteral("difficulty %1 task-grid column must remain in range").arg(index),
                err)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyDifficultySelectionStaysIndependentFromPreviewBadge(err)) {
        return 1;
    }
    if (!verifyDifficultySelectionIsFilteredAndDeduplicated(err)) {
        return 1;
    }
    if (!verifyTaskDifficultyGridUsesFourColumns(err)) {
        return 1;
    }

    out << "batch_export_panel_spec ok" << Qt::endl;
    return 0;
}
