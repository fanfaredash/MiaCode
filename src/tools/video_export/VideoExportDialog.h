#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QSize>

#include <functional>

#include "VideoExportController.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QWidget;
class PreviewCanvas;

class VideoExportDialog : public QDialog
{
    Q_OBJECT

public:
    using SeekPreviewCallback = std::function<void(double second)>;
    using PlayPreviewCallback = std::function<void(double second)>;
    using PausePreviewCallback = std::function<void()>;
    using IsPreviewPlayingCallback = std::function<bool()>;
    using CurrentPreviewSecondCallback = std::function<double()>;
    using PreviewAspectRatioCallback = std::function<void(double ratio)>;
    using PreviewBrightnessCallback = std::function<void(double outer, double inner)>;

    VideoExportDialog(
        const VideoExportTask& baseTask,
        PreviewCanvas* sourceCanvas,
        SeekPreviewCallback seekPreviewCallback = {},
        PlayPreviewCallback playPreviewCallback = {},
        PausePreviewCallback pausePreviewCallback = {},
        IsPreviewPlayingCallback isPreviewPlayingCallback = {},
        CurrentPreviewSecondCallback currentPreviewSecondCallback = {},
        PreviewAspectRatioCallback previewAspectRatioCallback = {},
        PreviewBrightnessCallback previewBrightnessCallback = {},
        QWidget* parent = nullptr
    );
    bool exportSucceeded() const { return exportSucceeded_; }
    bool previewAspectChangedByDialog() const { return previewAspectChangedByDialog_; }

private:
    void browseOutputPath();
    void startExport();
    bool applyUiToTask(VideoExportTask* task, QString* errorMessage) const;
    void refreshDialogGeometry();
    void syncLivePreviewTimestampVisibility();
    void restoreLivePreviewState();
    void applySelectedAspectRatioToPreview(bool markChanged);

    void onRangeSpinChanged();
    void onPreviewSliderChanged(int sliderValue);
    void setRangeStartFromPreview();
    void setRangeEndFromPreview();
    void toggleRangePreview();
    void stopRangePreview(bool seekToCurrent);
    void stopRangePreviewToLeadIn();
    void updatePreviewPlayPauseUi();
    void onRangePreviewTick();
    void syncRangeUi();
    void seekPreview(double second);
    void playPreview(double second);
    void pausePreview();
    bool isPreviewPlaying() const;
    double currentPreviewSecond() const;
    QSize selectedResolution() const;
    double selectedResolutionAspectRatio() const;
    double rangeStartSeconds() const;
    double rangeEndSeconds() const;
    double leadInStartSeconds() const;
    QString formatSecond(double second) const;
    QWidget* buildCollapsibleSection(const QString& title, QWidget* content, bool expanded, QToolButton** toggleOut);
    void updateSectionToggle(QToolButton* toggle, QWidget* content, bool expanded);

    void closeEvent(QCloseEvent* event) override;
    void done(int result) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    VideoExportTask baseTask_;
    PreviewCanvas* sourceCanvas_ = nullptr;
    SeekPreviewCallback seekPreviewCallback_;
    PlayPreviewCallback playPreviewCallback_;
    PausePreviewCallback pausePreviewCallback_;
    IsPreviewPlayingCallback isPreviewPlayingCallback_;
    CurrentPreviewSecondCallback currentPreviewSecondCallback_;
    PreviewAspectRatioCallback previewAspectRatioCallback_;
    PreviewBrightnessCallback previewBrightnessCallback_;

    double totalDurationSeconds_ = 0.0;
    double previewCursorSecond_ = 0.0;
    double initialResolutionAspectRatio_ = 1.0;
    bool exportSucceeded_ = false;
    bool syncingRangeUi_ = false;
    bool rangePreviewPlaying_ = false;
    bool previewAspectChangedByDialog_ = false;
    bool previewStateRestored_ = false;
    int previewSeekHeldArrowKey_ = 0;
    QElapsedTimer previewSeekHeldArrowElapsed_;

    QLineEdit* outputPathEdit_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QCheckBox* showTimestampCheck_ = nullptr;
    QSlider* brightnessOuterSlider_ = nullptr;
    QSlider* brightnessInnerSlider_ = nullptr;
    QLabel* brightnessOuterValueLabel_ = nullptr;
    QLabel* brightnessInnerValueLabel_ = nullptr;
    QDoubleSpinBox* startSecondSpin_ = nullptr;
    QDoubleSpinBox* endSecondSpin_ = nullptr;
    QSlider* previewSlider_ = nullptr;
    QLabel* previewTimeLabel_ = nullptr;
    QWidget* optionsContent_ = nullptr;
    QWidget* rangeContent_ = nullptr;
    QToolButton* optionsToggle_ = nullptr;
    QToolButton* rangeToggle_ = nullptr;
    QToolButton* previewRangeButton_ = nullptr;
    QToolButton* stopPreviewButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QTimer* previewTimer_ = nullptr;
};
