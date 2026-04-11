#include "MainWindow.PreviewSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

MainWindow::PreviewSection::PreviewSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

#define previewStageMediaHost_ state_.previewStageMediaHost_
#define previewCanvas_ state_.previewCanvas_
#define quickShellPreviewCompositeSurface_ state_.quickShellPreviewCompositeSurface_
#define quickShellStartupStageMediaLoadDeferred_ state_.quickShellStartupStageMediaLoadDeferred_
#define quickShellStartupUiReady_ state_.quickShellStartupUiReady_
#define startupRestorePending_ state_.startupRestorePending_
#define deferredQuickShellStartupStageMediaFlushScheduled_ state_.deferredQuickShellStartupStageMediaFlushScheduled_
#define deferredQuickShellStartupStageMediaPending_ state_.deferredQuickShellStartupStageMediaPending_
#define deferredQuickShellStartupStageMediaChartPath_ state_.deferredQuickShellStartupStageMediaChartPath_
#define deferredQuickShellStartupStageMediaPausedSecond_ state_.deferredQuickShellStartupStageMediaPausedSecond_
#define previewBackgroundScaleMode_ state_.previewBackgroundScaleMode_
#define previewPlaybackRate_ state_.previewPlaybackRate_
#define pausedPreviewMediaSeekPending_ state_.pausedPreviewMediaSeekPending_
#define pausedPreviewMediaSeekSecond_ state_.pausedPreviewMediaSeekSecond_
#define currentFilePath_ state_.currentFilePath_
#define qtPreviewPauseSecond_ state_.qtPreviewPauseSecond_
#define previewMediaWarmupChartPath_ state_.previewMediaWarmupChartPath_
#define previewMediaWarmupResolvedPath_ state_.previewMediaWarmupResolvedPath_
#define qtPreviewPlaying_ state_.qtPreviewPlaying_
#define qtPreviewStartSecond_ state_.qtPreviewStartSecond_
#define qtPreviewElapsed_ state_.qtPreviewElapsed_
#define quickShellPreviewCompositeSurfaceActive_ state_.quickShellPreviewCompositeSurfaceActive_
#define runtimeDebugOutputEnabled_ state_.runtimeDebugOutputEnabled_

void MainWindow::PreviewSection::applyPreviewStageMediaRouteVisualSettings()
{
    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->setBackgroundScaleMode(previewBackgroundScaleMode_);
    }
}

MainWindow::PreviewStageMediaRoute MainWindow::PreviewSection::previewStageMediaRoute() const
{
    return PreviewStageMediaRoute::QuickShellStageHost;
}

bool MainWindow::PreviewSection::previewUsesStageMediaHostRoute() const
{
    return true;
}

bool MainWindow::PreviewSection::quickShellPreviewUsesSeparateSurface() const
{
    return miacode::quick_shell::shouldUseSeparatePreviewSurface(
        true,
        previewStageMediaHost_ != nullptr && previewStageMediaHost_->hasVideoMedia()
    );
}

QWindow* MainWindow::PreviewSection::quickShellPreviewCompositeWindow() const
{
    return quickShellPreviewCompositeSurface_ != nullptr
        ? quickShellPreviewCompositeSurface_->hostWindow()
        : nullptr;
}

bool MainWindow::PreviewSection::shouldDeferQuickShellStartupStageMediaLoad() const
{
    return previewUsesStageMediaHostRoute() && quickShellStartupStageMediaLoadDeferred_;
}

void MainWindow::PreviewSection::noteQuickShellStartupUiReady()
{
    if (!previewUsesStageMediaHostRoute()) {
        return;
    }
    quickShellStartupUiReady_ = true;
    scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::PreviewSection::scheduleDeferredQuickShellStartupStageMediaLoadIfReady()
{
    if (!shouldDeferQuickShellStartupStageMediaLoad()
        || !quickShellStartupUiReady_
        || startupRestorePending_
        || deferredQuickShellStartupStageMediaFlushScheduled_) {
        return;
    }

    deferredQuickShellStartupStageMediaFlushScheduled_ = true;
    QTimer::singleShot(0, &owner_, [this]() {
        deferredQuickShellStartupStageMediaFlushScheduled_ = false;
        if (!shouldDeferQuickShellStartupStageMediaLoad()
            || !quickShellStartupUiReady_
            || startupRestorePending_) {
            return;
        }

        quickShellStartupStageMediaLoadDeferred_ = false;
        if (!deferredQuickShellStartupStageMediaPending_ || previewStageMediaHost_ == nullptr) {
            return;
        }

        deferredQuickShellStartupStageMediaPending_ = false;
        previewStageMediaHost_->setChartPath(deferredQuickShellStartupStageMediaChartPath_);
        previewStageMediaHost_->setPlayheadSeconds(deferredQuickShellStartupStageMediaPausedSecond_);
        refreshPreviewStageMediaRouteDebugState(false);
    });
}

void MainWindow::PreviewSection::updatePreviewStageMediaPresentationMode(bool requestUpdate)
{
    if (previewCanvas_ == nullptr) {
        return;
    }
    previewCanvas_->setStageMediaPresentationMode(
        miacode::preview::scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem,
        requestUpdate
    );
}

void MainWindow::PreviewSection::ensurePreviewStageMediaRouteInitialized()
{
    ensurePreviewStageMediaHostInitialized();
}

void MainWindow::PreviewSection::syncPreviewStageMediaRouteChartPath(
    const QString& chartPath,
    const QString& trackPath,
    double pausedSecond)
{
    const double clampedPausedSecond = qMax(0.0, pausedSecond);
    updatePreviewStageMediaPresentationMode(false);

    ensurePreviewStageMediaHostInitialized();
    deferredQuickShellStartupStageMediaChartPath_ = chartPath;
    deferredQuickShellStartupStageMediaPausedSecond_ = clampedPausedSecond;
    deferredQuickShellStartupStageMediaPending_ = true;
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        deferredQuickShellStartupStageMediaPending_ = false;
        previewStageMediaHost_->setChartPath(chartPath);
        previewStageMediaHost_->setPlayheadSeconds(clampedPausedSecond);
    }

    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::clearPreviewStageMediaRoute()
{
    deferredQuickShellStartupStageMediaPending_ = false;
    deferredQuickShellStartupStageMediaChartPath_.clear();
    deferredQuickShellStartupStageMediaPausedSecond_ = 0.0;
    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->setChartPath(QString());
    }

    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::applyPreviewMediaWarmupToStageMediaRoute(
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath)
{
    ensurePreviewStageMediaHostInitialized();
    previewStageMediaHost_->setWarmupResolvedMediaPath(chartPath, resolvedMediaPath);
    deferredQuickShellStartupStageMediaChartPath_ = chartPath;
    deferredQuickShellStartupStageMediaPending_ = true;
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        deferredQuickShellStartupStageMediaPending_ = false;
        previewStageMediaHost_->setChartPath(chartPath);
    }

    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::resetPreviewStageMediaRouteTimelineOffset()
{
    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->setTimelineOffsetSeconds(0.0);
    }
}

void MainWindow::PreviewSection::applyPreviewStageMediaRoutePlaybackRate(double rate)
{
    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->setPlaybackRate(rate);
    }
}

bool MainWindow::PreviewSection::previewStageMediaRouteHasVideo() const
{
    return previewStageMediaHost_ != nullptr && previewStageMediaHost_->hasVideoMedia();
}

double MainWindow::PreviewSection::previewStageMediaRouteCurrentPlaybackSecond() const
{
    return previewStageMediaHost_ != nullptr ? previewStageMediaHost_->currentPlaybackSecond() : 0.0;
}

void MainWindow::PreviewSection::startPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->startPlayback(second);
    }
}

void MainWindow::PreviewSection::syncPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->syncPlayback(second);
    }
}

void MainWindow::PreviewSection::pausePreviewStageMediaRoutePlayback()
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->pausePlayback();
    }
}

void MainWindow::PreviewSection::seekPreviewStageMediaRouteWhilePaused(double second)
{
    const double clampedSecond = qBound(0.0, second, owner_.previewDurationSeconds());
    if (!previewStageMediaRouteHasVideo()) {
        pausedPreviewMediaSeekPending_ = false;
        return;
    }

    pausedPreviewMediaSeekPending_ = true;
    pausedPreviewMediaSeekSecond_ = clampedSecond;
    if (previewStageMediaHost_ != nullptr) {
        previewStageMediaHost_->setPlayheadSeconds(clampedSecond);
    }
}

void MainWindow::PreviewSection::setPreviewStageMediaRouteObservedPlayheadSecond(double second)
{
    if (!previewUsesStageMediaHostRoute() || previewStageMediaHost_ == nullptr) {
        return;
    }
    previewStageMediaHost_->setObservedPlayheadSecond(second);
}

void MainWindow::PreviewSection::ensureQuickShellPreviewCompositeSurfaceInitialized()
{
    if (quickShellPreviewCompositeSurface_ == nullptr) {
        quickShellPreviewCompositeSurface_ = new QuickShellPreviewCompositeSurface(&owner_);
    }
    quickShellPreviewCompositeSurface_->setRuntime(previewCanvas_);
    quickShellPreviewCompositeSurface_->setMediaHost(previewStageMediaHost_);
}

void MainWindow::PreviewSection::refreshQuickShellPreviewCompositeSurfaceState()
{
    ensureQuickShellPreviewCompositeSurfaceInitialized();
    if (quickShellPreviewCompositeSurface_ == nullptr) {
        return;
    }

    const bool nextActive = quickShellPreviewUsesSeparateSurface();
    quickShellPreviewCompositeSurface_->setRuntime(previewCanvas_);
    quickShellPreviewCompositeSurface_->setMediaHost(previewStageMediaHost_);
    quickShellPreviewCompositeSurface_->setActive(nextActive);

    if (quickShellPreviewCompositeSurfaceActive_ == nextActive) {
        return;
    }

    quickShellPreviewCompositeSurfaceActive_ = nextActive;
    if (runtimeDebugOutputEnabled_) {
        owner_.appendOutput(
            "preview/stage_media",
            QString("action=presentation_mode mode=%1")
                .arg(nextActive ? QStringLiteral("separate_surface") : QStringLiteral("inline"))
        );
    }
}

void MainWindow::PreviewSection::ensurePreviewStageMediaHostInitialized()
{
    if (previewStageMediaHost_ != nullptr) {
        ensureQuickShellPreviewCompositeSurfaceInitialized();
        refreshQuickShellPreviewCompositeSurfaceState();
        return;
    }

    previewStageMediaHost_ = new PreviewStageMediaHost(&owner_);
    previewStageMediaHost_->setBackgroundScaleMode(previewBackgroundScaleMode_);
    connect(previewStageMediaHost_, &PreviewStageMediaHost::mediaStateChanged, &owner_, [this]() {
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setStageMediaAvailable(previewStageMediaHost_->hasResolvedMedia());
        }
        refreshQuickShellPreviewCompositeSurfaceState();
        refreshPreviewStageMediaRouteDebugState(false);
    });
    connect(previewStageMediaHost_, &PreviewStageMediaHost::playbackPositionChanged, &owner_, [this](double second) {
        if (qtPreviewPlaying_) {
            return;
        }
        if (pausedPreviewMediaSeekPending_) {
            if (qAbs(second - pausedPreviewMediaSeekSecond_) > 0.05) {
                return;
            }
            second = pausedPreviewMediaSeekSecond_;
            pausedPreviewMediaSeekPending_ = false;
        }
        qtPreviewStartSecond_ = second;
        qtPreviewElapsed_.restart();
        owner_.applyQtPreviewPosition(second, true);
        refreshPreviewStageMediaRouteDebugState(false);
    });
    connect(previewStageMediaHost_, &PreviewStageMediaHost::playbackFinished, &owner_, [this]() {
        owner_.finishQtPreviewPlaybackAndReturnToEntry("Qt preview reached the end of current media.");
    });
    connect(previewStageMediaHost_, &PreviewStageMediaHost::diagnosticsChanged, &owner_, [this]() {
        refreshPreviewStageMediaRouteDebugState(!qtPreviewPlaying_);
    });
    ensureQuickShellPreviewCompositeSurfaceInitialized();
    previewStageMediaHost_->setWarmupResolvedMediaPath(previewMediaWarmupChartPath_, previewMediaWarmupResolvedPath_);
    deferredQuickShellStartupStageMediaChartPath_ = currentFilePath_;
    deferredQuickShellStartupStageMediaPausedSecond_ = qMax(0.0, qtPreviewPauseSecond_);
    deferredQuickShellStartupStageMediaPending_ = !currentFilePath_.isEmpty();
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        deferredQuickShellStartupStageMediaPending_ = false;
        previewStageMediaHost_->setChartPath(currentFilePath_);
        previewStageMediaHost_->setPlayheadSeconds(qtPreviewPauseSecond_);
    }
    refreshQuickShellPreviewCompositeSurfaceState();
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::shutdownPreviewStageMediaHost()
{
    if (previewStageMediaHost_ == nullptr) {
        return;
    }
    if (quickShellPreviewCompositeSurface_ != nullptr) {
        quickShellPreviewCompositeSurface_->setMediaHost(nullptr);
        quickShellPreviewCompositeSurface_->setActive(false);
    }
    quickShellPreviewCompositeSurfaceActive_ = false;
    delete previewStageMediaHost_;
    previewStageMediaHost_ = nullptr;
}

void MainWindow::PreviewSection::refreshPreviewStageMediaRouteDebugState(bool requestUpdate)
{
    if (previewCanvas_ == nullptr) {
        return;
    }
    miacode::preview::scene::PreviewExternalStageMediaType mediaType =
        miacode::preview::scene::PreviewExternalStageMediaType::None;
    bool videoPlaybackActive = false;
    double playbackSecond = 0.0;
    double clockDeltaSeconds = 0.0;
    qint64 videoFrameAgeMs = -1;
    bool hasResolvedMedia = false;
    bool hasVideoMedia = false;
    QString mediaTypeName = QStringLiteral("none");
    if (previewUsesStageMediaHostRoute() && previewStageMediaHost_ != nullptr) {
        hasResolvedMedia = previewStageMediaHost_->hasResolvedMedia();
        hasVideoMedia = previewStageMediaHost_->hasVideoMedia();
        if (previewStageMediaHost_->hasVideoMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Video;
            mediaTypeName = QStringLiteral("video");
        } else if (previewStageMediaHost_->hasResolvedMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Image;
            mediaTypeName = QStringLiteral("image");
        }
        videoPlaybackActive = previewStageMediaHost_->videoPlaybackActive();
        playbackSecond = previewStageMediaHost_->currentPlaybackSecond();
        clockDeltaSeconds = previewStageMediaHost_->clockDeltaSeconds();
        videoFrameAgeMs = previewStageMediaHost_->videoFrameAgeMs();
    }
    previewCanvas_->setExternalStageMediaProfileSummary(
        quickShellPreviewUsesSeparateSurface(),
        hasResolvedMedia,
        hasVideoMedia,
        mediaTypeName,
        previewStageMediaHost_ != nullptr ? previewStageMediaHost_->videoFrameCountTotal() : 0
    );
    previewCanvas_->setExternalStageMediaDebugState(
        mediaType,
        videoPlaybackActive,
        playbackSecond,
        clockDeltaSeconds,
        videoFrameAgeMs,
        requestUpdate
    );
}

#undef previewStageMediaHost_
#undef previewCanvas_
#undef quickShellPreviewCompositeSurface_
#undef quickShellStartupStageMediaLoadDeferred_
#undef quickShellStartupUiReady_
#undef startupRestorePending_
#undef deferredQuickShellStartupStageMediaFlushScheduled_
#undef deferredQuickShellStartupStageMediaPending_
#undef deferredQuickShellStartupStageMediaChartPath_
#undef deferredQuickShellStartupStageMediaPausedSecond_
#undef previewBackgroundScaleMode_
#undef previewPlaybackRate_
#undef pausedPreviewMediaSeekPending_
#undef pausedPreviewMediaSeekSecond_
#undef currentFilePath_
#undef qtPreviewPauseSecond_
#undef previewMediaWarmupChartPath_
#undef previewMediaWarmupResolvedPath_
#undef qtPreviewPlaying_
#undef qtPreviewStartSecond_
#undef qtPreviewElapsed_
#undef quickShellPreviewCompositeSurfaceActive_
#undef runtimeDebugOutputEnabled_

void MainWindow::applyPreviewStageMediaRouteVisualSettings()
{
    previewSection_->applyPreviewStageMediaRouteVisualSettings();
}

MainWindow::PreviewStageMediaRoute MainWindow::previewStageMediaRoute() const
{
    return previewSection_->previewStageMediaRoute();
}

bool MainWindow::previewUsesStageMediaHostRoute() const
{
    return previewSection_->previewUsesStageMediaHostRoute();
}

bool MainWindow::quickShellPreviewUsesSeparateSurface() const
{
    return previewSection_->quickShellPreviewUsesSeparateSurface();
}

QWindow* MainWindow::quickShellPreviewCompositeWindow() const
{
    return previewSection_->quickShellPreviewCompositeWindow();
}

bool MainWindow::shouldDeferQuickShellStartupStageMediaLoad() const
{
    return previewSection_->shouldDeferQuickShellStartupStageMediaLoad();
}

void MainWindow::noteQuickShellStartupUiReady()
{
    previewSection_->noteQuickShellStartupUiReady();
}

void MainWindow::scheduleDeferredQuickShellStartupStageMediaLoadIfReady()
{
    previewSection_->scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::updatePreviewStageMediaPresentationMode(bool requestUpdate)
{
    previewSection_->updatePreviewStageMediaPresentationMode(requestUpdate);
}

void MainWindow::ensurePreviewStageMediaRouteInitialized()
{
    previewSection_->ensurePreviewStageMediaRouteInitialized();
}

void MainWindow::syncPreviewStageMediaRouteChartPath(const QString& chartPath, const QString& trackPath, double pausedSecond)
{
    previewSection_->syncPreviewStageMediaRouteChartPath(chartPath, trackPath, pausedSecond);
}

void MainWindow::clearPreviewStageMediaRoute()
{
    previewSection_->clearPreviewStageMediaRoute();
}

void MainWindow::applyPreviewMediaWarmupToStageMediaRoute(
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath)
{
    previewSection_->applyPreviewMediaWarmupToStageMediaRoute(chartPath, resolvedMediaPath, trackPath);
}

void MainWindow::resetPreviewStageMediaRouteTimelineOffset()
{
    previewSection_->resetPreviewStageMediaRouteTimelineOffset();
}

void MainWindow::applyPreviewStageMediaRoutePlaybackRate(double rate)
{
    previewSection_->applyPreviewStageMediaRoutePlaybackRate(rate);
}

bool MainWindow::previewStageMediaRouteHasVideo() const
{
    return previewSection_->previewStageMediaRouteHasVideo();
}

double MainWindow::previewStageMediaRouteCurrentPlaybackSecond() const
{
    return previewSection_->previewStageMediaRouteCurrentPlaybackSecond();
}

void MainWindow::startPreviewStageMediaRoutePlayback(double second)
{
    previewSection_->startPreviewStageMediaRoutePlayback(second);
}

void MainWindow::syncPreviewStageMediaRoutePlayback(double second)
{
    previewSection_->syncPreviewStageMediaRoutePlayback(second);
}

void MainWindow::pausePreviewStageMediaRoutePlayback()
{
    previewSection_->pausePreviewStageMediaRoutePlayback();
}

void MainWindow::seekPreviewStageMediaRouteWhilePaused(double second)
{
    previewSection_->seekPreviewStageMediaRouteWhilePaused(second);
}

void MainWindow::setPreviewStageMediaRouteObservedPlayheadSecond(double second)
{
    previewSection_->setPreviewStageMediaRouteObservedPlayheadSecond(second);
}

void MainWindow::ensureQuickShellPreviewCompositeSurfaceInitialized()
{
    previewSection_->ensureQuickShellPreviewCompositeSurfaceInitialized();
}

void MainWindow::refreshQuickShellPreviewCompositeSurfaceState()
{
    previewSection_->refreshQuickShellPreviewCompositeSurfaceState();
}

void MainWindow::ensurePreviewStageMediaHostInitialized()
{
    previewSection_->ensurePreviewStageMediaHostInitialized();
}

void MainWindow::shutdownPreviewStageMediaHost()
{
    previewSection_->shutdownPreviewStageMediaHost();
}

void MainWindow::refreshPreviewStageMediaRouteDebugState(bool requestUpdate)
{
    previewSection_->refreshPreviewStageMediaRouteDebugState(requestUpdate);
}
