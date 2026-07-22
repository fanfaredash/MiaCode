#pragma once

namespace miacode::video_export {

struct BatchExportTaskGridPosition {
    int row = 0;
    int column = 0;

    bool operator==(const BatchExportTaskGridPosition& other) const
    {
        return row == other.row && column == other.column;
    }
};

inline constexpr int kBatchExportTaskDifficultyGridColumns = 4;

// Keeps the task page's difficulty selector within the embedded panel width.
// Callers place options in row-major order; incomplete final rows stay compact
// instead of widening into a single checkbox strip.
BatchExportTaskGridPosition batchExportTaskDifficultyGridPosition(int index);

}  // namespace miacode::video_export
