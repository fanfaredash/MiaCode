#pragma once

#include "runtime/Session.h"

namespace miacode::runtime {

class StageMediaHost {
public:
    StageMediaHost(Session& session, Session::HostUi& ui, Session::HostState& state);

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
    Session::PreviewStageMediaRoute previewStageMediaRoute() const;
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
    // Hard-close the stage-media decoder's file handle before an in-app media
    // tool renames/replaces the pv/bg video (Windows file-lock; see
    // PreviewStageMediaHost::releaseDecoderForFileReplace).
    void releasePreviewStageMediaDecoderForFileOperation();
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
    Session::PreviewSkinVariant previewSkinVariantFromStorageValue(const QString& value) const;
    QString previewSkinVariantStorageValue() const;
    QStringList availablePreviewSkinDirectoryNames() const;
    QString previewSkinDisplayName(const QString& directoryName) const;
    QString resolvePreviewSkinDir() const;
    QString resolvePreviewSkinRootDir() const;
    void applyPreviewAudioSettingsToRuntime();
    // Per-project mixer persistence (<chartDir>/.miacode/preferences.json,
    // key "preview_audio"). The app-level local preset only seeds a project
    // that has never stored a mixer of its own.
    void loadProjectAudioPreferences();
    void saveProjectAudioPreferences() const;

private:
    Session& session_;
    Session::HostUi& ui_;
    Session::HostState& state_;
    // The preview appearance settings are owned by the application assembly,
    // not by the window; this is the same single copy Session binds to.
    miacode::v2::PreviewAppearanceState::Values& previewAppearanceValues_;
};

}  // namespace miacode::runtime
