#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "app/v2/UiRequestService.h"
#include "tools/video_export/VideoExportController.h"

class MainWindow;

// Pure-QML export settings session for the v2 shell. Does not host or modify
// VideoExportDialog / ExportLauncherPage. Drives audition + worker launch via
// MainWindow::ExportSection.
class QmlExportSession final : public QObject
{
    Q_OBJECT
    // File picking and messaging happen through this boundary so the session
    // itself never constructs a Widgets dialog.
    Q_PROPERTY(QObject* uiRequests READ uiRequests CONSTANT)
    Q_PROPERTY(bool pageSessionActive READ pageSessionActive NOTIFY pageSessionActiveChanged)
    Q_PROPERTY(int selectedDifficultyId READ selectedDifficultyId NOTIFY selectedDifficultyIdChanged)
    Q_PROPERTY(QString activeTab READ activeTab WRITE setActiveTab NOTIFY activeTabChanged)
    Q_PROPERTY(QString settingsTab READ settingsTab WRITE setSettingsTab NOTIFY settingsTabChanged)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY unavailableReasonChanged)
    Q_PROPERTY(QVariantList difficulties READ difficulties NOTIFY difficultiesChanged)
    Q_PROPERTY(bool exportRunning READ exportRunning NOTIFY exportRunningChanged)

    // Output
    Q_PROPERTY(QString outputPath READ outputPath WRITE setOutputPath NOTIFY outputChanged)
    Q_PROPERTY(int outputWidth READ outputWidth NOTIFY outputChanged)
    Q_PROPERTY(int outputHeight READ outputHeight NOTIFY outputChanged)
    Q_PROPERTY(int resolutionIndex READ resolutionIndex WRITE setResolutionIndex NOTIFY outputChanged)
    Q_PROPERTY(QVariantList resolutionOptions READ resolutionOptions CONSTANT)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY outputChanged)
    Q_PROPERTY(QVariantList fpsOptions READ fpsOptions CONSTANT)
    Q_PROPERTY(int audioBitrateKbps READ audioBitrateKbps WRITE setAudioBitrateKbps NOTIFY outputChanged)
    Q_PROPERTY(QVariantList audioBitrateOptions READ audioBitrateOptions CONSTANT)
    Q_PROPERTY(int presetIndex READ presetIndex WRITE setPresetIndex NOTIFY outputChanged)
    Q_PROPERTY(QVariantList presetOptions READ presetOptions CONSTANT)
    Q_PROPERTY(int sizePresetIndex READ sizePresetIndex WRITE setSizePresetIndex NOTIFY outputChanged)
    Q_PROPERTY(QVariantList sizePresetOptions READ sizePresetOptions CONSTANT)

    // Video
    Q_PROPERTY(double backgroundBrightnessOuter READ backgroundBrightnessOuter WRITE setBackgroundBrightnessOuter NOTIFY videoChanged)
    Q_PROPERTY(double backgroundBrightnessInner READ backgroundBrightnessInner WRITE setBackgroundBrightnessInner NOTIFY videoChanged)
    Q_PROPERTY(double layoutSquareScale READ layoutSquareScale WRITE setLayoutSquareScale NOTIFY videoChanged)
    Q_PROPERTY(int backgroundScaleModeIndex READ backgroundScaleModeIndex WRITE setBackgroundScaleModeIndex NOTIFY videoChanged)
    Q_PROPERTY(QVariantList backgroundScaleModeOptions READ backgroundScaleModeOptions CONSTANT)
    Q_PROPERTY(bool smoothBrightness READ smoothBrightness WRITE setSmoothBrightness NOTIFY videoChanged)
    Q_PROPERTY(bool showTimestamp READ showTimestamp WRITE setShowTimestamp NOTIFY videoChanged)
    Q_PROPERTY(bool showObjectStatsHud READ showObjectStatsHud WRITE setShowObjectStatsHud NOTIFY videoChanged)
    Q_PROPERTY(bool showChartInfoHud READ showChartInfoHud WRITE setShowChartInfoHud NOTIFY videoChanged)
    Q_PROPERTY(bool fixHudTextLayout READ fixHudTextLayout WRITE setFixHudTextLayout NOTIFY videoChanged)
    Q_PROPERTY(bool clockCountEnabled READ clockCountEnabled WRITE setClockCountEnabled NOTIFY videoChanged)

    // Gameplay (task-local)
    Q_PROPERTY(double tapFlowSpeed READ tapFlowSpeed WRITE setTapFlowSpeed NOTIFY gameplayChanged)
    Q_PROPERTY(double touchFlowSpeed READ touchFlowSpeed WRITE setTouchFlowSpeed NOTIFY gameplayChanged)

    // Skin / outline (owner-live preview settings)
    Q_PROPERTY(QVariantList skinOptions READ skinOptions NOTIFY skinChanged)
    Q_PROPERTY(int skinIndex READ skinIndex WRITE setSkinIndex NOTIFY skinChanged)
    Q_PROPERTY(QVariantList judgeEffectOptions READ judgeEffectOptions CONSTANT)
    Q_PROPERTY(int judgeEffectIndex READ judgeEffectIndex WRITE setJudgeEffectIndex NOTIFY skinChanged)
    Q_PROPERTY(QVariantList outlineOptions READ outlineOptions CONSTANT)
    Q_PROPERTY(int outlineIndex READ outlineIndex WRITE setOutlineIndex NOTIFY skinChanged)

    // Intro
    Q_PROPERTY(bool introEnabled READ introEnabled WRITE setIntroEnabled NOTIFY introChanged)
    Q_PROPERTY(int introBackgroundModeIndex READ introBackgroundModeIndex WRITE setIntroBackgroundModeIndex NOTIFY introChanged)
    Q_PROPERTY(QString introCustomBackgroundPath READ introCustomBackgroundPath WRITE setIntroCustomBackgroundPath NOTIFY introChanged)
    Q_PROPERTY(bool introBlurBackground READ introBlurBackground WRITE setIntroBlurBackground NOTIFY introChanged)
    Q_PROPERTY(int introModeIndex READ introModeIndex WRITE setIntroModeIndex NOTIFY introChanged)
    Q_PROPERTY(bool introCardShadow READ introCardShadow WRITE setIntroCardShadow NOTIFY introChanged)
    Q_PROPERTY(bool introLevelTextRender READ introLevelTextRender WRITE setIntroLevelTextRender NOTIFY introChanged)
    Q_PROPERTY(QVariantList introSoundOptions READ introSoundOptions NOTIFY introSoundOptionsChanged)
    Q_PROPERTY(int introSoundIndex READ introSoundIndex WRITE setIntroSoundIndex NOTIFY introChanged)
    Q_PROPERTY(QString introSoundFileName READ introSoundFileName WRITE setIntroSoundFileName NOTIFY introChanged)
    Q_PROPERTY(double introSoundVolume READ introSoundVolume WRITE setIntroSoundVolume NOTIFY introChanged)
    Q_PROPERTY(QString introSoundLabel READ introSoundLabel CONSTANT)
    Q_PROPERTY(QString introSoundVolumeLabel READ introSoundVolumeLabel CONSTANT)
    Q_PROPERTY(QString introSoundImportLabel READ introSoundImportLabel CONSTANT)

    // Range
    Q_PROPERTY(double exportStartSeconds READ exportStartSeconds WRITE setExportStartSeconds NOTIFY rangeChanged)
    Q_PROPERTY(double exportEndSeconds READ exportEndSeconds WRITE setExportEndSeconds NOTIFY rangeChanged)
    Q_PROPERTY(double contentDurationSeconds READ contentDurationSeconds NOTIFY rangeChanged)
    Q_PROPERTY(bool fullRangeExport READ fullRangeExport NOTIFY rangeChanged)

    // Batch
    Q_PROPERTY(QStringList chartDirectories READ chartDirectories NOTIFY batchChanged)
    Q_PROPERTY(QVariantList batchDifficultyChecks READ batchDifficultyChecks NOTIFY batchChanged)
    Q_PROPERTY(QString batchOutputDirectory READ batchOutputDirectory WRITE setBatchOutputDirectory NOTIFY batchChanged)

public:
    explicit QmlExportSession(MainWindow& backend, QObject* parent = nullptr);

    QObject* uiRequests() { return &uiRequests_; }
    bool pageSessionActive() const { return pageSessionActive_; }
    int selectedDifficultyId() const { return selectedDifficultyId_; }
    QString activeTab() const;
    QString settingsTab() const { return settingsTab_; }
    QString unavailableReason() const { return unavailableReason_; }
    QVariantList difficulties() const { return difficulties_; }
    bool exportRunning() const { return exportRunning_; }

    QString outputPath() const { return task_.outputPath; }
    int outputWidth() const { return task_.outputWidth; }
    int outputHeight() const { return task_.outputHeight; }
    int resolutionIndex() const { return resolutionIndex_; }
    QVariantList resolutionOptions() const;
    int fps() const { return task_.fps; }
    QVariantList fpsOptions() const;
    int audioBitrateKbps() const { return task_.audioBitrateKbps; }
    QVariantList audioBitrateOptions() const;
    int presetIndex() const;
    QVariantList presetOptions() const;
    int sizePresetIndex() const;
    QVariantList sizePresetOptions() const;

    double backgroundBrightnessOuter() const { return task_.backgroundBrightnessOuter; }
    double backgroundBrightnessInner() const { return task_.backgroundBrightnessInner; }
    double layoutSquareScale() const { return task_.layoutSquareScale; }
    int backgroundScaleModeIndex() const;
    QVariantList backgroundScaleModeOptions() const;
    bool smoothBrightness() const { return task_.smoothBrightness; }
    bool showTimestamp() const { return task_.showTimestamp; }
    bool showObjectStatsHud() const { return task_.showObjectStatsHud; }
    bool showChartInfoHud() const { return task_.showChartInfoHud; }
    bool fixHudTextLayout() const { return task_.fixHudTextLayout; }
    bool clockCountEnabled() const { return task_.clockCountEnabled; }

    double tapFlowSpeed() const { return task_.tapFlowSpeed; }
    double touchFlowSpeed() const { return task_.touchFlowSpeed; }

    QVariantList skinOptions() const;
    int skinIndex() const;
    QVariantList judgeEffectOptions() const;
    int judgeEffectIndex() const;
    QVariantList outlineOptions() const;
    int outlineIndex() const;

    bool introEnabled() const { return task_.intro.enabled; }
    int introBackgroundModeIndex() const;
    QString introCustomBackgroundPath() const { return task_.intro.customBackgroundPath; }
    bool introBlurBackground() const { return task_.intro.blurBackground; }
    int introModeIndex() const;
    bool introCardShadow() const { return task_.intro.cardShadow; }
    bool introLevelTextRender() const;
    QVariantList introSoundOptions() const;
    int introSoundIndex() const;
    QString introSoundFileName() const { return task_.introSoundFileName; }
    double introSoundVolume() const { return task_.introSoundVolume; }
    QString introSoundLabel() const;
    QString introSoundVolumeLabel() const;
    QString introSoundImportLabel() const;
    IntroBannerSpec previewIntroSpec() const;

    double exportStartSeconds() const { return task_.exportStartSeconds; }
    double exportEndSeconds() const;
    double contentDurationSeconds() const { return chartDurationSeconds_; }
    bool fullRangeExport() const { return task_.fullRangeExport; }

    QStringList chartDirectories() const { return chartDirectories_; }
    QVariantList batchDifficultyChecks() const;
    QString batchOutputDirectory() const { return batchOutputDirectory_; }

    void enter(int previousActiveDifficultyId);
    void leave();

    Q_INVOKABLE void selectDifficulty(int difficultyId);
    Q_INVOKABLE void setActiveTab(const QString& tabId);
    Q_INVOKABLE void setSettingsTab(const QString& tabId);
    Q_INVOKABLE void refreshFromDocument();
    Q_INVOKABLE void browseOutputPath();
    Q_INVOKABLE void browseIntroBackground();
    Q_INVOKABLE void importIntroSound();
    Q_INVOKABLE void browseBatchOutputDirectory();
    Q_INVOKABLE void addChartDirectories();
    Q_INVOKABLE void removeChartDirectory(int index);
    Q_INVOKABLE void clearChartDirectories();
    Q_INVOKABLE void setBatchDifficultyChecked(int difficultyId, bool checked);
    Q_INVOKABLE void setExportStartToCurrentPreview();
    Q_INVOKABLE void setExportEndToCurrentPreview();
    Q_INVOKABLE QString setExportStartText(const QString& text);
    Q_INVOKABLE QString setExportEndText(const QString& text);
    Q_INVOKABLE void startExport();
    Q_INVOKABLE void cancelExport();

    void setOutputPath(const QString& path);
    void setResolutionIndex(int index);
    void setFps(int fps);
    void setAudioBitrateKbps(int kbps);
    void setPresetIndex(int index);
    void setSizePresetIndex(int index);
    void setBackgroundBrightnessOuter(double value);
    void setBackgroundBrightnessInner(double value);
    void setLayoutSquareScale(double value);
    void setBackgroundScaleModeIndex(int index);
    void setSmoothBrightness(bool value);
    void setShowTimestamp(bool value);
    void setShowObjectStatsHud(bool value);
    void setShowChartInfoHud(bool value);
    void setFixHudTextLayout(bool value);
    void setClockCountEnabled(bool value);
    void setTapFlowSpeed(double value);
    void setTouchFlowSpeed(double value);
    void setSkinIndex(int index);
    void setJudgeEffectIndex(int index);
    void setOutlineIndex(int index);
    Q_INVOKABLE void openSkinDirectory();
    Q_INVOKABLE void openJudgeLineDirectory();
    void setIntroEnabled(bool value);
    void setIntroBackgroundModeIndex(int index);
    void setIntroCustomBackgroundPath(const QString& path);
    void setIntroBlurBackground(bool value);
    void setIntroModeIndex(int index);
    void setIntroCardShadow(bool value);
    void setIntroLevelTextRender(bool value);
    void setIntroSoundIndex(int index);
    void setIntroSoundFileName(const QString& fileName);
    void setIntroSoundVolume(double value);
    void setExportStartSeconds(double value);
    void setExportEndSeconds(double value);
    void setBatchOutputDirectory(const QString& path);

signals:
    void pageSessionActiveChanged();
    void selectedDifficultyIdChanged();
    void activeTabChanged();
    void settingsTabChanged();
    void unavailableReasonChanged();
    void difficultiesChanged();
    void exportRunningChanged();
    void outputChanged();
    void videoChanged();
    void gameplayChanged();
    void skinChanged();
    void introChanged();
    void introSoundOptionsChanged();
    void rangeChanged();
    void batchChanged();

private:
    void seedFromDifficulty(int difficultyId);
    void rebuildDifficultyList();
    void syncAudition();
    void applyLivePreviewSettings();
    void stopAudition();
    void applyPreferences();
    void savePreferences() const;
    void setUnavailableReason(const QString& reason);
    bool difficultyExists(int difficultyId) const;
    bool difficultyHasChartBody(int difficultyId) const;
    int resolveDefaultDifficultyId(int previousActiveDifficultyId) const;
    VideoExportTask buildRequestedTask() const;
    void applyOwnerLiveFields(VideoExportTask* task) const;
    void applyIntroSoundImport(const QString& selectedPath);
    void addChartDirectory(const QString& path);

    miacode::v2::UiRequestService uiRequests_;
    MainWindow* backend_ = nullptr;
    bool pageSessionActive_ = false;
    bool exportRunning_ = false;
    bool hasSeededTask_ = false;
    bool batchExportRunning_ = false;
    bool batchCancellationRequested_ = false;
    int selectedDifficultyId_ = 0;
    QString activeTab_ = QStringLiteral("export");
    QString settingsTab_ = QStringLiteral("output");
    QString unavailableReason_;
    QVariantList difficulties_;
    VideoExportTask task_;
    double chartDurationSeconds_ = 0.0;
    int resolutionIndex_ = 1;
    QStringList chartDirectories_;
    QList<int> batchSelectedDifficultyIds_;
    QString batchOutputDirectory_;
};
