#include "BatchExportTaskLayout.h"

#include <QtGlobal>

namespace miacode::video_export {

BatchExportTaskGridPosition batchExportTaskDifficultyGridPosition(int index)
{
    const int normalizedIndex = qMax(0, index);
    return {
        normalizedIndex / kBatchExportTaskDifficultyGridColumns,
        normalizedIndex % kBatchExportTaskDifficultyGridColumns,
    };
}

}  // namespace miacode::video_export
