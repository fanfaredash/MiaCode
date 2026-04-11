#pragma once

#include "../../MainWindow.h"

class MainWindow::PreviewSection {
public:
    PreviewSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

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
    void syncPreviewStageMediaRouteChartPath(const QString& chartPath, const QString& trackPath, double pausedSecond);
    void clearPreviewStageMediaRoute();
    void applyPreviewMediaWarmupToStageMediaRoute(
        const QString& chartPath,
        const QString& resolvedMediaPath,
        const QString& trackPath
    );
    void resetPreviewStageMediaRouteTimelineOffset();
    void applyPreviewStageMediaRoutePlaybackRate(double rate);
    bool previewStageMediaRouteHasVideo() const;
    double previewStageMediaRouteCurrentPlaybackSecond() const;
    void startPreviewStageMediaRoutePlayback(double second);
    void syncPreviewStageMediaRoutePlayback(double second);
    void pausePreviewStageMediaRoutePlayback();
    void seekPreviewStageMediaRouteWhilePaused(double second);
    void setPreviewStageMediaRouteObservedPlayheadSecond(double second);
    void ensureQuickShellPreviewCompositeSurfaceInitialized();
    void refreshQuickShellPreviewCompositeSurfaceState();
    void ensurePreviewStageMediaHostInitialized();
    void shutdownPreviewStageMediaHost();
    void refreshPreviewStageMediaRouteDebugState(bool requestUpdate = true);

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
