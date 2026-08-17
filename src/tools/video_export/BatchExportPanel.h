#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

#include <functional>

#include "tools/video_export/VideoExportController.h"

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class VideoExportDialog;

namespace miacode::video_export {

// Separates batch-export scope from the badge-selected preview target. The
// badge initializes the selection once; user changes are never overwritten by
// later preview-target updates.
class BatchExportSelectionState
{
public:
    BatchExportSelectionState(QList<int> availableDifficultyIds, int initialPreviewDifficultyId);

    QList<int> selectedDifficultyIds() const;
    void setSelectedDifficultyIds(const QList<int>& difficultyIds);
    int previewDifficultyId() const;
    void updatePreviewDifficulty(int difficultyId);

private:
    QList<int> availableDifficultyIds_;
    QList<int> selectedDifficultyIds_;
    int previewDifficultyId_ = 0;
};

class BatchExportPanel : public QWidget
{
    Q_OBJECT

public:
    BatchExportPanel(
        const VideoExportTask& baseTask,
        QList<int> availableDifficultyIds,
        int initialPreviewDifficultyId,
        QWidget* parent = nullptr);

    // Called once by ExportSection after it has created a fully owner-wired
    // VideoExportDialog. The panel owns the dialog through the QWidget tree.
    void installSharedSettingsPanel(VideoExportDialog* settingsPanel);

    QStringList chartDirectories() const;
    QString outputDirectory() const;
    QList<int> selectedDifficultyIds() const;
    VideoExportTask requestedTaskTemplate() const { return requestedTaskTemplate_; }
    bool prepareRequestedTask(QString* errorMessage = nullptr);

    // Retargets the preview/audition at another difficulty of the SAME chart.
    // `retargetedTask` is that difficulty's freshly-built seed task; it re-seeds
    // the shared settings panel's chart payload (片头 banner fields, markers,
    // duration) while every user-tuned setting — including the batch queue —
    // stays put.
    void updatePreviewDifficulty(int difficultyId, const VideoExportTask& retargetedTask);
    int previewDifficultyId() const;
    bool isClockCountEnabledForPreview() const;
    bool isAddIntroActiveForPreview() const;
    IntroBannerSpec previewIntroSpec() const;
    void setBatchExportRunning(bool running);
    void applyThemeStyles();

signals:
    void batchExportConfirmed();
    void clockCountEnabledChanged(bool enabled);
    void introPreviewSettingsChanged();

private:
    QWidget* buildBatchOutputControls();
    void addChartDirectories(const QStringList& directories);
    void browseChartDirectories();
    void browseOutputDirectory();
    void removeSelectedChartDirectories();
    void clearChartDirectories();
    void refreshChartDirectoryNumbering();
    QString lastChartBrowseDirectory() const;
    QString lastOutputBrowseDirectory() const;
    void saveLastChartBrowseDirectory(const QString& directory) const;
    void saveLastOutputBrowseDirectory(const QString& directory) const;
    void onStartExportClicked();

    VideoExportTask baseTask_;
    BatchExportSelectionState selectionState_;
    QList<int> availableDifficultyIds_;
    VideoExportDialog* settingsPanel_ = nullptr;
    QWidget* settingsHost_ = nullptr;
    QVBoxLayout* rootLayout_ = nullptr;
    QWidget* batchOutputControls_ = nullptr;
    QListWidget* chartDirectoryList_ = nullptr;
    QLineEdit* outputDirectoryEdit_ = nullptr;
    QList<QCheckBox*> difficultyChecks_;
    QPushButton* startExportButton_ = nullptr;
    VideoExportTask requestedTaskTemplate_;
};

}  // namespace miacode::video_export
