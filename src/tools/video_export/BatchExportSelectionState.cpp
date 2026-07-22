#include "BatchExportPanel.h"

#include <QSet>

#include <utility>

namespace miacode::video_export {

BatchExportSelectionState::BatchExportSelectionState(
    QList<int> availableDifficultyIds,
    int initialPreviewDifficultyId)
    : availableDifficultyIds_(std::move(availableDifficultyIds))
    , previewDifficultyId_(initialPreviewDifficultyId)
{
    if (availableDifficultyIds_.contains(initialPreviewDifficultyId)) {
        selectedDifficultyIds_.append(initialPreviewDifficultyId);
    }
}

QList<int> BatchExportSelectionState::selectedDifficultyIds() const
{
    return selectedDifficultyIds_;
}

void BatchExportSelectionState::setSelectedDifficultyIds(const QList<int>& difficultyIds)
{
    selectedDifficultyIds_.clear();
    QSet<int> seen;
    for (int difficultyId : difficultyIds) {
        if (availableDifficultyIds_.contains(difficultyId) && !seen.contains(difficultyId)) {
            selectedDifficultyIds_.append(difficultyId);
            seen.insert(difficultyId);
        }
    }
}

int BatchExportSelectionState::previewDifficultyId() const
{
    return previewDifficultyId_;
}

void BatchExportSelectionState::updatePreviewDifficulty(int difficultyId)
{
    if (availableDifficultyIds_.contains(difficultyId)) {
        previewDifficultyId_ = difficultyId;
    }
}

}  // namespace miacode::video_export
