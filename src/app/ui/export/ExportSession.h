#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "app/services/UiRequestService.h"
#include "app/services/JobProgressService.h"
#include "app/services/ExportEngine.h"
#include "app/services/PreviewAppearanceState.h"
#include "app/services/PreviewSurface.h"
#include "tools/video_export/VideoExportController.h"

#include "app/services/ShellNotifications.h"


// Pure-QML export settings session for the v2 shell. The only export UI there
// is; it drives audition + worker launch through miacode::ExportEngine and
// the live preview through miacode::PreviewSurface. The MainWindow& that
// remains carries one push signal and nothing else.
namespace miacode::ui {

class ExportSession final : public QObject
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
    Q_PROPERTY(QVariantList presetOptions READ presetOptions NOTIFY localeLabelsChanged)
    Q_PROPERTY(int sizePresetIndex READ sizePresetIndex WRITE setSizePresetIndex NOTIFY outputChanged)
    Q_PROPERTY(QVariantList sizePresetOptions READ sizePresetOptions NOTIFY localeLabelsChanged)

    Q_PROPERTY(bool showObjectStatsHud READ showObjectStatsHud WRITE setShowObjectStatsHud NOTIFY videoChanged)
    Q_PROPERTY(bool showChartInfoHud READ showChartInfoHud WRITE setShowChartInfoHud NOTIFY videoChanged)
    Q_PROPERTY(bool fixHudTextLayout READ fixHudTextLayout WRITE setFixHudTextLayout NOTIFY videoChanged)
    Q_PROPERTY(bool clockCountEnabled READ clockCountEnabled WRITE setClockCountEnabled NOTIFY videoChanged)

    // Shared portable font library for the intro difficulty card. File selection
    // remains QML-native through UiRequestService; no Widgets surface is used.
    Q_PROPERTY(QVariantList fontLibraryOptions READ fontLibraryOptions NOTIFY fontLibraryChanged)

    // Intro
    Q_PROPERTY(bool introEnabled READ introEnabled WRITE setIntroEnabled NOTIFY introChanged)
    Q_PROPERTY(int introBackgroundModeIndex READ introBackgroundModeIndex WRITE setIntroBackgroundModeIndex NOTIFY introChanged)
    Q_PROPERTY(QString introCustomBackgroundPath READ introCustomBackgroundPath WRITE setIntroCustomBackgroundPath NOTIFY introChanged)
    Q_PROPERTY(bool introBlurBackground READ introBlurBackground WRITE setIntroBlurBackground NOTIFY introChanged)
    Q_PROPERTY(int introModeIndex READ introModeIndex WRITE setIntroModeIndex NOTIFY introChanged)
    Q_PROPERTY(bool introCardShadow READ introCardShadow WRITE setIntroCardShadow NOTIFY introChanged)
    Q_PROPERTY(bool introLevelTextRender READ introLevelTextRender WRITE setIntroLevelTextRender NOTIFY introChanged)
    Q_PROPERTY(QString introFontDisplayPath READ introFontDisplayPath WRITE setIntroFontDisplayPath NOTIFY introChanged)
    Q_PROPERTY(QString introFontBodyPath READ introFontBodyPath WRITE setIntroFontBodyPath NOTIFY introChanged)
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
    Q_PROPERTY(double minimumExportRangeSeconds READ minimumExportRangeSeconds NOTIFY rangeChanged)
    Q_PROPERTY(bool fullRangeExport READ fullRangeExport NOTIFY rangeChanged)

    // Batch
    Q_PROPERTY(QStringList chartDirectories READ chartDirectories NOTIFY batchChanged)
    Q_PROPERTY(QVariantList batchDifficultyChecks READ batchDifficultyChecks NOTIFY batchChanged)
    Q_PROPERTY(QString batchOutputDirectory READ batchOutputDirectory WRITE setBatchOutputDirectory NOTIFY batchChanged)

public:
    ExportSession(miacode::ShellNotifications& notifications,
                     miacode::UiRequestService& uiRequests,
                     miacode::JobProgressService& jobProgress,
                     miacode::PreviewAppearanceState& appearance,
                     miacode::ExportEngine*& engineSlot,
                     miacode::PreviewSurface*& previewSlot,
                     QObject* parent = nullptr);

    QObject* uiRequests() { return uiRequests_; }
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

    bool showObjectStatsHud() const { return task_.showObjectStatsHud; }
    bool showChartInfoHud() const { return task_.showChartInfoHud; }
    bool fixHudTextLayout() const { return task_.fixHudTextLayout; }
    bool clockCountEnabled() const { return task_.clockCountEnabled; }

    QVariantList fontLibraryOptions() const;

    bool introEnabled() const { return task_.intro.enabled; }
    int introBackgroundModeIndex() const;
    QString introCustomBackgroundPath() const { return task_.intro.customBackgroundPath; }
    bool introBlurBackground() const { return task_.intro.blurBackground; }
    int introModeIndex() const;
    bool introCardShadow() const { return task_.intro.cardShadow; }
    bool introLevelTextRender() const;
    QString introFontDisplayPath() const { return task_.intro.fontDisplayPath; }
    QString introFontBodyPath() const { return task_.intro.fontBodyPath; }
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
    double minimumExportRangeSeconds() const;
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
    Q_INVOKABLE void importIntroFont();
    Q_INVOKABLE void resetIntroFonts();
    Q_INVOKABLE void browseBatchOutputDirectory();
    Q_INVOKABLE void addChartDirectories();
    Q_INVOKABLE void removeChartDirectory(int index);
    Q_INVOKABLE void clearChartDirectories();
    Q_INVOKABLE void setBatchDifficultyChecked(int difficultyId, bool checked);
    Q_INVOKABLE void setExportStartToCurrentPreview();
    Q_INVOKABLE void setExportEndToCurrentPreview();
    Q_INVOKABLE void setExportRangeSeconds(double start, double end);
    // Runtime-only entry point (not Q_INVOKABLE): a chart selection was
    // resolved to an export range before the page switch that will make this
    // session active. seedFromDifficulty() resets the whole task, including
    // the range, on every page entry/difficulty switch, so the range cannot be
    // applied here directly — it is staged and consumed once, right after the
    // next seed completes.
    void requestSelectionRangeExport(double startSecond, double endSecond);
    // Dropped when the page switch that was meant to consume the staged range
    // is rejected: requestPageSwitch() is asynchronous, so a failure surfaces
    // through navigationRejected() rather than a return value, and without
    // this the range would survive to ambush an unrelated later page entry.
    void clearPendingSelectionRangeExport();
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
    void setShowObjectStatsHud(bool value);
    void setShowChartInfoHud(bool value);
    void setFixHudTextLayout(bool value);
    void setClockCountEnabled(bool value);
    void setIntroEnabled(bool value);
    void setIntroBackgroundModeIndex(int index);
    void setIntroCustomBackgroundPath(const QString& path);
    void setIntroBlurBackground(bool value);
    void setIntroModeIndex(int index);
    void setIntroCardShadow(bool value);
    void setIntroLevelTextRender(bool value);
    void setIntroFontDisplayPath(const QString& path);
    void setIntroFontBodyPath(const QString& path);
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
    void localeLabelsChanged();
    void videoChanged();
    void fontLibraryChanged();
    void introChanged();
    void introSoundOptionsChanged();
    void rangeChanged();
    void batchChanged();

private:
    void seedFromDifficulty(int difficultyId);
    void applyPendingSelectionRangeExport();
    void rebuildDifficultyList();
    void syncAudition();
    void applyLivePreviewSettings();
    // Pulls the shared live render values into the task after someone else (Preview
    // Settings) wrote them while this page is open.
    void adoptPreviewRenderSettings();
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
    void applyFontImport(const QString& selectedPath);
    void addChartDirectory(const QString& path);

    miacode::UiRequestService* uiRequests_ = nullptr;
    miacode::JobProgressService* jobProgress_ = nullptr;
    // The appearance values' owner. The export page and the preview settings
    // page render the same skin, so they must read and write one copy.
    miacode::PreviewAppearanceState* appearance_ = nullptr;
    // Bound to the assembly's slot rather than to a snapshot of the pointer, so
    // the window withdrawing the engine during teardown is visible here at
    // once instead of leaving a dangling copy behind.
    miacode::ExportEngine** engineSlot_ = nullptr;
    miacode::PreviewSurface** previewSlot_ = nullptr;
    miacode::PreviewSurface* preview() const
    {
        return previewSlot_ != nullptr ? *previewSlot_ : nullptr;
    }
    miacode::ExportEngine* engine() const
    {
        return engineSlot_ != nullptr ? *engineSlot_ : nullptr;
    }
    miacode::ShellNotifications* notifications_ = nullptr;
    bool pageSessionActive_ = false;
    // Set while this session pushes its own task into the shared preview state, so the
    // echoed previewRenderSettingsChanged is not read back as someone else's write.
    bool pushingSharedSettings_ = false;
    bool exportRunning_ = false;
    bool hasSeededTask_ = false;
    // enter() queues seed one tick later; leave/selectDifficulty bump this
    // so a stale tick cannot land after the page is gone.
    quint64 pagePrepareGeneration_ = 0;
    bool batchExportRunning_ = false;
    bool batchCancellationRequested_ = false;
    mutable QVariantList fontLibraryOptionsCache_;
    mutable QString fontLibraryOptionsCacheDefaultLabel_;
    mutable bool fontLibraryOptionsCacheValid_ = false;
    int selectedDifficultyId_ = 0;
    QString activeTab_ = QStringLiteral("export");
    QString settingsTab_ = QStringLiteral("output");
    QString unavailableReason_;
    QVariantList difficulties_;
    VideoExportTask task_;
    double chartDurationSeconds_ = 0.0;
    // Staged by requestSelectionRangeExport(); consumed once by the next
    // seedFromDifficulty() and cleared, so a later plain page entry/switch
    // does not silently reapply a stale selection's range.
    bool hasPendingSelectionRangeExport_ = false;
    double pendingRangeStartSeconds_ = 0.0;
    double pendingRangeEndSeconds_ = 0.0;
    int resolutionIndex_ = 1;
    QStringList chartDirectories_;
    QList<int> batchSelectedDifficultyIds_;
    QString batchOutputDirectory_;
};
} // namespace miacode::ui
