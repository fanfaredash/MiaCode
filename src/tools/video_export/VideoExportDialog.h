#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QSize>

#include <functional>

#include "VideoExportController.h"
#include "tools/video_export/CardFontSettings.h"   // CardFontSelector

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFrame;
class QJsonObject;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;
class QVBoxLayout;
class QToolButton;
class QWheelEvent;
class QWidget;

class IntroPreviewWidget;

namespace miacode::ui {
class EditableValueLabel;
}

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
    using PreviewObjectStatsCallback = std::function<void(bool showObjectStatsHud)>;
    using PreviewChartInfoCallback = std::function<void(bool showChartInfoHud)>;
    using PreviewAspectRatioCallback = std::function<void(double ratio)>;
    using PreviewBrightnessCallback = std::function<void(double outer, double inner)>;
    using PreviewLayoutScaleCallback = std::function<void(double scale)>;
    using PreviewSmoothBrightnessCallback = std::function<void(bool smooth)>;
    using PreviewScaleModeCallback = std::function<void(PreviewBackgroundScaleMode mode)>;
    using PreviewTapFlowSpeedCallback = std::function<void(double flowSpeed)>;
    using PreviewTouchFlowSpeedCallback = std::function<void(double flowSpeed)>;
    using SharedSettingsSnapshotCallback = std::function<VideoExportTask()>;
    using OwnerWiredSettingsRefreshCallback = std::function<void()>;

    VideoExportDialog(
        const VideoExportTask& baseTask,
        SeekPreviewCallback seekPreviewCallback = {},
        PlayPreviewCallback playPreviewCallback = {},
        PausePreviewCallback pausePreviewCallback = {},
        IsPreviewPlayingCallback isPreviewPlayingCallback = {},
        CurrentPreviewSecondCallback currentPreviewSecondCallback = {},
        PreviewTimestampCallback previewTimestampCallback = {},
        PreviewObjectStatsCallback previewObjectStatsCallback = {},
        PreviewChartInfoCallback previewChartInfoCallback = {},
        PreviewAspectRatioCallback previewAspectRatioCallback = {},
        PreviewBrightnessCallback previewBrightnessCallback = {},
        PreviewLayoutScaleCallback previewLayoutScaleCallback = {},
        PreviewSmoothBrightnessCallback previewSmoothBrightnessCallback = {},
        PreviewScaleModeCallback previewScaleModeCallback = {},
        PreviewTapFlowSpeedCallback previewTapFlowSpeedCallback = {},
        PreviewTouchFlowSpeedCallback previewTouchFlowSpeedCallback = {},
        SharedSettingsSnapshotCallback sharedSettingsSnapshotCallback = {},
        QWidget* parent = nullptr
    );
    bool exportSucceeded() const { return exportSucceeded_; }
    bool exportRequested() const { return exportRequested_; }
    VideoExportTask requestedExportTask() const { return requestedExportTask_; }
    bool previewAspectChangedByDialog() const { return previewAspectChangedByDialog_; }
    // Export-page intro lead-in preview: the embedded audition reads these so it
    // can play the animated intro before the chart when 添加片头 is on (the
    // checkbox is greyed on a partial range, so isEnabled() matters).
    bool isAddIntroActiveForPreview() const;
    IntroBannerSpec previewIntroSpec() const { return currentIntroSpec(); }

    // Injects MainWindow-built, owner-wired settings widgets: videoExtras is
    // appended to the bottom of the Video tab; gameplayWidget to the Gameplay
    // tab (below the dialog's own flow-speed rows); skinWidget to the 皮肤 tab
    // (skin / judge line / HUD font). Call once after construction, before
    // exec().
    void injectOwnerWiredSettings(
        QWidget* videoExtras,
        QWidget* gameplayWidget,
        QWidget* skinWidget = nullptr,
        OwnerWiredSettingsRefreshCallback refreshCallback = {});

    // ---- Embedded panel mode (E-C, export-page phase 2) ----
    // The same dialog doubles as the Export hub page's in-page video panel:
    // call setEmbeddedPanelMode(true) immediately after construction (before
    // the widget is shown / added to a layout). It disables every
    // window-only behavior — modality, self-sizing height locks +
    // centering, Esc-reject (done() becomes a no-op), the Cancel button —
    // and reroutes the Export button: startExport() emits exportConfirmed()
    // instead of accept(), so the host launches the worker while the panel
    // stays open. The Tools-menu modal path is unchanged (flag off).
    void setEmbeddedPanelMode(bool embedded);
    bool embeddedPanelMode() const { return embeddedPanelMode_; }
    // While an embedded-launched export runs, the Export button doubles as
    // the cancel affordance (导出 → 取消导出); clicking it then emits
    // exportCancelRequested() instead of starting another export.
    void setEmbeddedExportRunning(bool running);
    // Host-side teardown for the embedded panel (the moral equivalent of the
    // modal path's done()): stops the range preview and restores the live
    // preview HUD/aspect state. Idempotent.
    void finalizeEmbeddedSession();

    // Re-apply every theme-baked stylesheet + icon on a light/dark switch.
    // buildUi() bakes these once at construction and Qt does NOT regenerate a
    // widget's literal stylesheet string on a palette change, so the embedded
    // export panel (a persistent page surface) would otherwise stay frozen at
    // the startup theme until it is destroyed + rebuilt. MainWindow's
    // applyUiTheme() forwards to the live embedded panel here; the modal path is
    // rebuilt per open so it never needs it, but calling it there is harmless.
    void applyThemeStyles();

signals:
    // Embedded mode only: the user confirmed the export (settings already
    // validated + persisted; requestedExportTask() carries the task).
    void exportConfirmed();
    // Embedded mode only: the user clicked the (cancel-mode) export button
    // while a worker run was active.
    void exportCancelRequested();
    // Embedded mode only: 添加片头 toggled or a 片头 setting changed — the host
    // refreshes the negative-time intro region (slider range + overlay).
    void introPreviewSettingsChanged();
    // Embedded mode only: the "Enable clock_count" checkbox toggled — the host
    // re-seeds the export-page audition's count-in so the preview matches what
    // will be exported (WYSIWYG).
    void clockCountEnabledChanged(bool enabled);

private:
    void browseOutputPath();
    void onExportButtonClicked();
    void startExport();
    bool applyUiToTask(VideoExportTask* task, QString* errorMessage) const;
    void refreshDialogGeometry();
    void syncLivePreviewTimestampVisibility();
    void syncLivePreviewObjectStatsVisibility();
    void syncLivePreviewChartInfoVisibility();
    void restoreLivePreviewState();
    void refreshSharedSettingsFromCallback();
    void refreshSharedSettingsFromTask(const VideoExportTask& task);
    void loadPersistedSettings();
    void savePersistedSettings(const VideoExportTask& task) const;
    void persistExportOnlySettings() const;
    // Shared by both save paths: add_intro + the "片头" tab styling keys.
    void appendIntroPersistedSettings(QJsonObject* settings) const;
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
    void handlePreviewPlayPauseShortcut();
    void handlePreviewStopOrPlayShortcut();
    void onRangePreviewTick();
    void syncRangeUi();
    // Updates the small "当前导出区间：[start, end]" caption from the current range.
    void refreshRangeSummaryLabel();
    // The export-range track (a custom QWidget): for handle drags, live-seek
    // the preview through the SAME single seek entry the right-side transport
    // uses — never a second transport.
    void scrubPreviewFromTrack(double second, bool live);
    // "Add intro" applies to exports that START at chart 0 (the bake gate in
    // applyUiToTask treats those as full-range); grey it when the range
    // starts mid-chart.
    void refreshAddIntroEnabledState();
    // ---- "片头" tab ----
    // Sub-control gating: background path row follows the combo, card
    // sub-options follow the card toggle, everything follows 添加片头.
    void syncIntroControlsEnabled();
    void browseIntroBackground();
    // Current intro spec = baseTask_.intro (chart payload) + the tab's styling
    // controls. The preview receives a resolved DX/Standard mode; the export
    // request may keep `mode=auto` so the launch snapshot can re-detect from the
    // live document immediately before crossing the worker boundary.
    IntroBannerSpec currentIntroSpec() const;
    IntroBannerSpec currentIntroSpecForExportTask() const;
    QString detectedIntroCardMode() const;
    QString selectedIntroCardMode(bool resolveAuto) const;
    void refreshIntroCardModeAutoLabel();
    void refreshIntroPreview();
    // Pin the read-only preview to the selected output aspect ratio.
    void resizeIntroPreviewToAspect();
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

    void closeEvent(QCloseEvent* event) override;
    void done(int result) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    VideoExportTask baseTask_;
    SeekPreviewCallback seekPreviewCallback_;
    PlayPreviewCallback playPreviewCallback_;
    PausePreviewCallback pausePreviewCallback_;
    IsPreviewPlayingCallback isPreviewPlayingCallback_;
    CurrentPreviewSecondCallback currentPreviewSecondCallback_;
    PreviewTimestampCallback previewTimestampCallback_;
    PreviewObjectStatsCallback previewObjectStatsCallback_;
    PreviewChartInfoCallback previewChartInfoCallback_;
    PreviewAspectRatioCallback previewAspectRatioCallback_;
    PreviewBrightnessCallback previewBrightnessCallback_;
    PreviewLayoutScaleCallback previewLayoutScaleCallback_;
    PreviewSmoothBrightnessCallback previewSmoothBrightnessCallback_;
    PreviewScaleModeCallback previewScaleModeCallback_;
    PreviewTapFlowSpeedCallback previewTapFlowSpeedCallback_;
    PreviewTouchFlowSpeedCallback previewTouchFlowSpeedCallback_;
    SharedSettingsSnapshotCallback sharedSettingsSnapshotCallback_;
    OwnerWiredSettingsRefreshCallback ownerWiredSettingsRefreshCallback_;

    double totalDurationSeconds_ = 0.0;
    double previewCursorSecond_ = 0.0;
    double initialResolutionAspectRatio_ = 1.0;
    bool embeddedPanelMode_ = false;
    bool embeddedExportRunning_ = false;
    bool exportSucceeded_ = false;
    bool exportRequested_ = false;
    bool syncingRangeUi_ = false;
    // Last emitted isAddIntroActiveForPreview() — refreshAddIntroEnabledState only
    // notifies the host (introPreviewSettingsChanged) when the range-driven intro
    // gate actually flips, so a stranded negative-time intro region gets torn down.
    bool introActiveForPreviewLast_ = false;
    bool rangePreviewPlaying_ = false;
    bool previewAspectChangedByDialog_ = false;
    bool previewStateRestored_ = false;
    bool initialShowTimestamp_ = true;
    bool initialShowObjectStats_ = false;
    bool initialShowChartInfo_ = false;
    int previewHeldSeekDirection_ = 0;
    int previewSeekHeldArrowKey_ = 0;
    int previewSeekHeldArrowLastElapsedMs_ = 0;
    QElapsedTimer previewSeekHeldArrowElapsed_;
    QElapsedTimer previewScrubRenderElapsed_;
    VideoExportTask requestedExportTask_;

    QLineEdit* outputPathEdit_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QSize selectedResolution_ = QSize();
    QComboBox* fpsCombo_ = nullptr;
    int selectedFps_ = 60;
    QComboBox* audioBitrateCombo_ = nullptr;
    int selectedAudioBitrateKbps_ = 192;
    QComboBox* presetCombo_ = nullptr;
    VideoExportPreset selectedPreset_ = VideoExportPreset::HighQuality;
    QCheckBox* showTimestampCheck_ = nullptr;
    QCheckBox* showObjectStatsCheck_ = nullptr;
    QCheckBox* showChartInfoCheck_ = nullptr;
    QCheckBox* clockCountCheck_ = nullptr;
    QCheckBox* addIntroCheck_ = nullptr;
    // ---- "片头" tab controls ----
    QComboBox* introBackgroundCombo_ = nullptr;
    QLineEdit* introBackgroundPathEdit_ = nullptr;
    QPushButton* introBackgroundBrowse_ = nullptr;
    QCheckBox* introBlurCheck_ = nullptr;
    QComboBox* introCardModeCombo_ = nullptr;
    QCheckBox* introCardShadowCheck_ = nullptr;
    QCheckBox* introLevelTextCheck_ = nullptr;
    // Difficulty-card custom fonts for the intro (empty path == bundled default).
    miacode::video_export::CardFontSelector introCardFontSelector_;
    IntroPreviewWidget* introPreview_ = nullptr;
    QCheckBox* smoothBrightnessCheck_ = nullptr;
    QComboBox* backgroundScaleModeCombo_ = nullptr;
    PreviewBackgroundScaleMode selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    QLineEdit* tapFlowSpeedEdit_ = nullptr;
    QLineEdit* touchFlowSpeedEdit_ = nullptr;
    double selectedTapFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    double selectedTouchFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    QSlider* brightnessOuterSlider_ = nullptr;
    QSlider* brightnessInnerSlider_ = nullptr;
    QSlider* layoutSquareScaleSlider_ = nullptr;
    miacode::ui::EditableValueLabel* brightnessOuterValueLabel_ = nullptr;
    miacode::ui::EditableValueLabel* brightnessInnerValueLabel_ = nullptr;
    miacode::ui::EditableValueLabel* layoutSquareScaleValueLabel_ = nullptr;
    QDoubleSpinBox* startSecondSpin_ = nullptr;
    QDoubleSpinBox* endSecondSpin_ = nullptr;
    // Held so applyThemeStyles() can re-apply their baked button stylesheets on
    // a theme switch (they are otherwise local widgets unreachable by pointer).
    QPushButton* outputBrowseButton_ = nullptr;
    QPushButton* setStartButton_ = nullptr;
    QPushButton* setEndButton_ = nullptr;
    // Stored as QWidget* (the concrete ExportRangeTrack is a file-local type in
    // the .cpp); cast where its API is needed.
    QWidget* rangeTrack_ = nullptr;
    // Small muted "当前导出区间：[start, end]" caption under the End row.
    QLabel* rangeSummaryLabel_ = nullptr;
    QSlider* previewSlider_ = nullptr;
    QLabel* previewTimeLabel_ = nullptr;
    QWidget* optionsContent_ = nullptr;
    QWidget* rangeContent_ = nullptr;
    QWidget* gameplayPage_ = nullptr;
    QWidget* skinPage_ = nullptr;
    QVBoxLayout* visualsPageLayout_ = nullptr;
    QVBoxLayout* gameplayPageLayout_ = nullptr;
    QVBoxLayout* skinPageLayout_ = nullptr;
    QTabWidget* settingsTabs_ = nullptr;
    QFrame* previewStrip_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
    QToolButton* previewRangeButton_ = nullptr;
    QToolButton* stopPreviewButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    QTimer* previewHeldSeekTimer_ = nullptr;
};
