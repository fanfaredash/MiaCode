#pragma once

#include <QDialog>
#include <QList>
#include <QSize>
#include <QStringList>

#include <functional>

#include "VideoExportController.h"

class QCheckBox;
class QLineEdit;
class QListWidget;
class QMenu;
class QSlider;
class QLabel;
class QToolButton;
class QWidget;
class QCheckBox;

class BatchVideoExportDialog : public QDialog
{
    Q_OBJECT

public:
    using SharedSettingsChangedCallback = std::function<void(const VideoExportTask& task)>;

    explicit BatchVideoExportDialog(
        const VideoExportTask& baseTask,
        const QString& difficultyLabel,
        SharedSettingsChangedCallback sharedSettingsChangedCallback = {},
        QWidget* parent = nullptr
    );

    bool exportRequested() const { return exportRequested_; }
    VideoExportTask requestedTaskTemplate() const { return requestedTask_; }
    QStringList selectedChartDirectories() const;
    QString outputDirectory() const;
    QList<int> selectedDifficultyIds() const;

private:
    void addChartDirectories(const QStringList& directories);
    void browseChartDirectories();
    void browseOutputDirectory();
    void removeSelectedChartDirectories();
    void clearChartDirectories();
    void startExport();
    bool applyUiToTask(VideoExportTask* task, QString* errorMessage) const;
    void loadPersistedSettings();
    void savePersistedSettings(const VideoExportTask& task) const;
    void persistExportOnlySettings() const;
    void notifySharedSettingsChanged();
    VideoExportTask currentSharedSettingsTask() const;
    QSize selectedResolution() const;
    QString lastChartBrowseDirectory() const;
    QString lastOutputBrowseDirectory() const;
    void saveLastChartBrowseDirectory(const QString& directory) const;
    void saveLastOutputBrowseDirectory(const QString& directory) const;

    VideoExportTask baseTask_;
    QString difficultyLabel_;
    SharedSettingsChangedCallback sharedSettingsChangedCallback_;
    bool exportRequested_ = false;
    VideoExportTask requestedTask_;

    QListWidget* chartDirectoryList_ = nullptr;
    QLineEdit* outputDirectoryEdit_ = nullptr;
    QToolButton* resolutionButton_ = nullptr;
    QMenu* resolutionMenu_ = nullptr;
    QSize selectedResolution_ = QSize();
    QToolButton* fpsButton_ = nullptr;
    QMenu* fpsMenu_ = nullptr;
    int selectedFps_ = 60;
    QToolButton* presetButton_ = nullptr;
    QMenu* presetMenu_ = nullptr;
    VideoExportPreset selectedPreset_ = VideoExportPreset::Fast;
    QCheckBox* showTimestampCheck_ = nullptr;
    QCheckBox* showObjectStatsCheck_ = nullptr;
    QCheckBox* smoothBrightnessCheck_ = nullptr;
    QToolButton* backgroundScaleModeButton_ = nullptr;
    QMenu* backgroundScaleModeMenu_ = nullptr;
    PreviewBackgroundScaleMode selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    QLineEdit* flowSpeedEdit_ = nullptr;
    double selectedFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    QSlider* brightnessOuterSlider_ = nullptr;
    QSlider* brightnessInnerSlider_ = nullptr;
    QSlider* layoutSquareScaleSlider_ = nullptr;
    QLabel* brightnessOuterValueLabel_ = nullptr;
    QLabel* brightnessInnerValueLabel_ = nullptr;
    QLabel* layoutSquareScaleValueLabel_ = nullptr;
    QList<QCheckBox*> difficultyChecks_;
    QList<int> difficultyIds_;
};
