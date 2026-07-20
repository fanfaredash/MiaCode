#pragma once

#include "../../MainWindow.h"

class MainWindow::PreviewSection {
public:
    PreviewSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void ensurePreviewSfxRuntimePrepared();
    void schedulePreviewSubsystemWarmup();
    void schedulePreviewMediaWarmup(quint64 generation, const QString& chartPathSnapshot, const QString& trackPathSnapshot, const QString& chartVideoOverrideSnapshot = QString());
    void schedulePreviewSfxWarmup(
        quint64 generation,
        const QString& chartPathSnapshot,
        const QString& trackPathSnapshot,
        const PreviewAudioSettings& audioSettingsSnapshot,
        double playbackRateSnapshot
    );
    void applyPreviewMediaWarmupResult(
        quint64 generation,
        const QString& chartPath,
        const QString& resolvedMediaPath,
        const QString& trackPath,
        qint64 workerElapsedMs
    );
    void applyPreviewSfxWarmupResult(
        quint64 generation,
        const QString& chartPath,
        const QString& trackPath,
        const QString& sfxDir,
        qint64 workerElapsedMs
    );
    MainWindow::PreviewStageMediaRoute previewStageMediaRoute() const;
    void applyPreviewStageMediaRouteVisualSettings();
    bool previewUsesStageMediaHostRoute() const;
    bool quickShellPreviewUsesSeparateSurface() const;
    QWindow* quickShellPreviewCompositeWindow() const;
    bool shouldDeferQuickShellStartupStageMediaLoad() const;
    void noteQuickShellStartupUiReady();
    void scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
    void updatePreviewStageMediaPresentationMode(bool requestUpdate = true);
    void ensurePreviewStageMediaRouteInitialized();
    void syncPreviewStageMediaRouteChartPath(const QString& chartPath, const QString& trackPath, double pausedSecond, const QString& chartVideoOverridePath = QString());
    void clearPreviewStageMediaRoute();
    void applyPreviewMediaWarmupToStageMediaRoute(
        const QString& chartPath,
        const QString& resolvedMediaPath,
        const QString& trackPath
    );
    void resetPreviewStageMediaRouteTimelineOffset();
    void applyPreviewStageMediaRoutePlaybackRate(double rate, const char* site = nullptr);
    bool previewStageMediaRouteHasVideo() const;
    double previewStageMediaRouteCurrentPlaybackSecond() const;
    void startPreviewStageMediaRoutePlayback(double second);
    void syncPreviewStageMediaRoutePlayback(double second);
    void pausePreviewStageMediaRoutePlayback();
    void seekPreviewStageMediaRouteWhilePaused(double second);
    void submitPreviewStageMediaRoutePausedSeek(double second, quint64 generation);
    void setPreviewStageMediaRouteObservedPlayheadSecond(double second);
    void ensureQuickShellPreviewCompositeSurfaceInitialized();
    void refreshQuickShellPreviewCompositeSurfaceState();
    void ensurePreviewStageMediaHostInitialized();
    void shutdownPreviewStageMediaHost();
    void refreshPreviewStageMediaRouteDebugState(bool requestUpdate = true);
    QString resolveDefaultTrackPath() const;
    PreviewOutlineVariant previewOutlineVariantFromStorageValue(const QString& value) const;
    QString previewOutlineVariantStorageValue() const;
    PreviewOutlineVariant autoPreviewOutlineVariantForChart(const QString& chartPath) const;
    PreviewOutlineVariant effectivePreviewOutlineVariant() const;
    void applyEffectivePreviewOutlineVariantToCanvas();
    void setPauseDisplayAltHoldActive(bool active);
    void setTouchPadAuthoringCtrlHoldActive(bool active);
    void applyPreviewOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection, bool persistState);
    QString resolvePreviewCustomOutlineDir() const;
    QString resolvePreviewCustomOutlinePath() const;
    QString effectivePreviewCustomOutlinePath() const;
    QStringList availablePreviewCustomOutlineFileNames() const;
    void applyPreviewCustomOutlineFileName(const QString& fileName, bool persistState);
    MainWindow::PreviewSkinVariant previewSkinVariantFromStorageValue(const QString& value) const;
    QString previewSkinVariantStorageValue() const;
    QStringList availablePreviewSkinDirectoryNames() const;
    QString previewSkinDisplayName(const QString& directoryName) const;
    QString resolvePreviewSkinDir() const;
    QString resolvePreviewSkinRootDir() const;
    void applyPreviewAudioSettingsToRuntime();

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
