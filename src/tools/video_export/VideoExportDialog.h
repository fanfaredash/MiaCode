#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QSize>

#include <functional>

#include "VideoExportController.h"

class QCheckBox;
class QCloseEvent;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QWheelEvent;
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
    using PreviewTimestampCallback = std::function<void(bool showTimestamp)>;
    using PreviewAspectRatioCallback = std::function<void(double ratio)>;
    using PreviewBrightnessCallback = std::function<void(double outer, double inner)>;
    using PreviewLayoutScaleCallback = std::function<void(double scale)>;
    using PreviewSmoothBrightnessCallback = std::function<void(bool smooth)>;
    using PreviewScaleModeCallback = std::function<void(PreviewBackgroundScaleMode mode)>;
    using PreviewFlowSpeedCallback = std::function<void(double flowSpeed)>;

    VideoExportDialog(
        const VideoExportTask& baseTask,
        PreviewCanvas* sourceCanvas,
        SeekPreviewCallback seekPreviewCallback = {},
        PlayPreviewCallback playPreviewCallback = {},
        PausePreviewCallback pausePreviewCallback = {},
        IsPreviewPlayingCallback isPreviewPlayingCallback = {},
        CurrentPreviewSecondCallback currentPreviewSecondCallback = {},
        PreviewTimestampCallback previewTimestampCallback = {},
        PreviewAspectRatioCallback previewAspectRatioCallback = {},
        PreviewBrightnessCallback previewBrightnessCallback = {},
        PreviewLayoutScaleCallback previewLayoutScaleCallback = {},
        PreviewSmoothBrightnessCallback previewSmoothBrightnessCallback = {},
        PreviewScaleModeCallback previewScaleModeCallback = {},
        PreviewFlowSpeedCallback previewFlowSpeedCallback = {},
        QWidget* parent = nullptr
    );
    bool exportSucceeded() const { return exportSucceeded_; }
    bool exportRequested() const { return exportRequested_; }
    VideoExportTask requestedExportTask() const { return requestedExportTask_; }
    bool previewAspectChangedByDialog() const { return previewAspectChangedByDialog_; }

private:
    void browseOutputPath();
    void startExport();
    bool applyUiToTask(VideoExportTask* task, QString* errorMessage) const;
    void refreshDialogGeometry();
    void syncLivePreviewTimestampVisibility();
    void restoreLivePreviewState();
    void loadPersistedSettings();
    void savePersistedSettings(const VideoExportTask& task) const;
    void persistExportOnlySettings() const;
    void applySelectedAspectRatioToPreview(bool markChanged);
    bool stepPreviewSliderBySeconds(double deltaSeconds);
    bool handlePreviewSliderWheel(QWheelEvent* event);
    void beginPreviewHeldSeek(int direction, int key);
    void stopPreviewHeldSeek(int key = 0);
    void applyPreviewHeldSeekTick();

    void onRangeSpinChanged();
    void onPreviewSliderChanged(int sliderValue);
    void setRangeStartFromPreview();
    void setRangeEndFromPreview();
    void toggleRangePreview();
    void stopRangePreview(bool seekToCurrent);
    void stopRangePreviewToStart();
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
    PreviewTimestampCallback previewTimestampCallback_;
    PreviewAspectRatioCallback previewAspectRatioCallback_;
    PreviewBrightnessCallback previewBrightnessCallback_;
    PreviewLayoutScaleCallback previewLayoutScaleCallback_;
    PreviewSmoothBrightnessCallback previewSmoothBrightnessCallback_;
    PreviewScaleModeCallback previewScaleModeCallback_;
    PreviewFlowSpeedCallback previewFlowSpeedCallback_;

    double totalDurationSeconds_ = 0.0;
    double previewCursorSecond_ = 0.0;
    double initialResolutionAspectRatio_ = 1.0;
    bool exportSucceeded_ = false;
    bool exportRequested_ = false;
    bool syncingRangeUi_ = false;
    bool rangePreviewPlaying_ = false;
    bool previewAspectChangedByDialog_ = false;
    bool previewStateRestored_ = false;
    bool initialShowTimestamp_ = true;
    bool initialShowObjectStatsHud_ = false;
    int previewHeldSeekDirection_ = 0;
    int previewSeekHeldArrowKey_ = 0;
    int previewSeekHeldArrowLastElapsedMs_ = 0;
    QElapsedTimer previewSeekHeldArrowElapsed_;
    QElapsedTimer previewScrubRenderElapsed_;
    VideoExportTask requestedExportTask_;

    QLineEdit* outputPathEdit_ = nullptr;
    QToolButton* resolutionButton_ = nullptr;
    QMenu* resolutionMenu_ = nullptr;
    QSize selectedResolution_ = QSize();
    QToolButton* fpsButton_ = nullptr;
    QMenu* fpsMenu_ = nullptr;
    int selectedFps_ = 60;
    QCheckBox* showTimestampCheck_ = nullptr;
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
    QDoubleSpinBox* startSecondSpin_ = nullptr;
    QDoubleSpinBox* endSecondSpin_ = nullptr;
    QLineEdit* startCurrentTimeEdit_ = nullptr;
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
    QTimer* previewHeldSeekTimer_ = nullptr;
};
