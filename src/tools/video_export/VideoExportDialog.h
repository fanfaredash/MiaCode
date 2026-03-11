#pragma once

#include <QDialog>
#include <QElapsedTimer>

#include <functional>

#include "VideoExportController.h"

class QCheckBox;
class QComboBox;
class QCloseEvent;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;
class PreviewCanvas;

class VideoExportDialog : public QDialog
{
    Q_OBJECT

public:
    using SeekPreviewCallback = std::function<void(double second)>;

    VideoExportDialog(
        const VideoExportTask& baseTask,
        PreviewCanvas* sourceCanvas,
        SeekPreviewCallback seekPreviewCallback = {},
        QWidget* parent = nullptr
    );

private:
    void browseOutputPath();
    void startExport();
    bool applyUiToTask(VideoExportTask* task, QString* errorMessage) const;
    void onRangeSpinChanged();
    void onPreviewSliderChanged(int sliderValue);
    void setRangeStartFromPreview();
    void setRangeEndFromPreview();
    void toggleRangePreview();
    void stopRangePreview();
    void onRangePreviewTick();
    void syncRangeUi();
    void seekPreview(double second);
    double rangeStartSeconds() const;
    double rangeEndSeconds() const;
    double leadInStartSeconds() const;
    QString formatSecond(double second) const;

    void closeEvent(QCloseEvent* event) override;

    VideoExportTask baseTask_;
    PreviewCanvas* sourceCanvas_ = nullptr;
    SeekPreviewCallback seekPreviewCallback_;
    double totalDurationSeconds_ = 0.0;
    bool syncingRangeUi_ = false;
    double previewCursorSecond_ = 0.0;
    QLineEdit* outputPathEdit_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QCheckBox* showTimestampCheck_ = nullptr;
    QDoubleSpinBox* startSecondSpin_ = nullptr;
    QDoubleSpinBox* endSecondSpin_ = nullptr;
    QSlider* previewSlider_ = nullptr;
    QLabel* previewTimeLabel_ = nullptr;
    QLabel* leadInHintLabel_ = nullptr;
    QPushButton* previewRangeButton_ = nullptr;
    QPushButton* stopPreviewButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    QElapsedTimer previewElapsedTimer_;
};
