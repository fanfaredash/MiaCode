#include "MainWindow.PreviewSection.h"
#include "../../MainWindowShared.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <cstdio>

using namespace miacode::mainwindow::shared;

MainWindow::PreviewSection::PreviewSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::PreviewSection::applyPreviewStageMediaRouteVisualSettings()
{
    // While the export-preview dialog is up, PV/BG stays visible regardless of the
    // pause-hide option so the user previews exactly what the exported video shows.
    // Holding Alt while paused inverts the pause-hide option (same effective
    // flag as effectivePreviewOutlineVariant) so judge area ⇄ PV/BG flip together.
    const bool forceJudgeAreaWhenPaused =
        state_.previewForceLabeledJudgeLineWhenPaused_ != state_.pauseDisplayAltHoldActive_;
    const bool mediaVisible = !forceJudgeAreaWhenPaused
        || state_.qtPreviewPlaying_
        || state_.exportPreviewActive_;
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setBackgroundScaleMode(state_.previewBackgroundScaleMode_);
        state_.previewStageMediaHost_->setLayoutSquareScale(state_.previewLayoutSquareScale_);
        state_.previewStageMediaHost_->setMediaVisible(mediaVisible);
    }
    if (state_.previewCanvas_ != nullptr) {
        const bool stageMediaVisible =
            mediaVisible
            && state_.previewStageMediaHost_ != nullptr
            && state_.previewStageMediaHost_->hasResolvedMedia();
        state_.previewCanvas_->setStageMediaAvailable(stageMediaVisible);
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
    return false;
}

QWindow* MainWindow::PreviewSection::quickShellPreviewCompositeWindow() const
{
    return state_.quickShellPreviewCompositeSurface_ != nullptr
        ? state_.quickShellPreviewCompositeSurface_->hostWindow()
        : nullptr;
}

bool MainWindow::PreviewSection::shouldDeferQuickShellStartupStageMediaLoad() const
{
    return previewUsesStageMediaHostRoute() && state_.quickShellStartupStageMediaLoadDeferred_;
}

void MainWindow::PreviewSection::noteQuickShellStartupUiReady()
{
    if (!previewUsesStageMediaHostRoute()) {
        return;
    }
    state_.quickShellStartupUiReady_ = true;
    scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::PreviewSection::scheduleDeferredQuickShellStartupStageMediaLoadIfReady()
{
    if (!shouldDeferQuickShellStartupStageMediaLoad()
        || !state_.quickShellStartupUiReady_
        || state_.startupRestorePending_
        || state_.deferredQuickShellStartupStageMediaFlushScheduled_) {
        return;
    }

    state_.deferredQuickShellStartupStageMediaFlushScheduled_ = true;
    QTimer::singleShot(0, &owner_, [this]() {
        state_.deferredQuickShellStartupStageMediaFlushScheduled_ = false;
        if (!shouldDeferQuickShellStartupStageMediaLoad()
            || !state_.quickShellStartupUiReady_
            || state_.startupRestorePending_) {
            return;
        }

        state_.quickShellStartupStageMediaLoadDeferred_ = false;
        if (!state_.deferredQuickShellStartupStageMediaPending_ || state_.previewStageMediaHost_ == nullptr) {
            return;
        }

        state_.deferredQuickShellStartupStageMediaPending_ = false;
        // Phase 4c — use the cached override threaded through the
        // deferred-load path (set in syncPreviewStageMediaRouteChartPath).
        state_.previewStageMediaHost_->setChartPath(
            state_.deferredQuickShellStartupStageMediaChartPath_,
            state_.deferredQuickShellStartupStageMediaVideoOverride_);
        state_.previewStageMediaHost_->setPlayheadSeconds(state_.deferredQuickShellStartupStageMediaPausedSecond_);
        refreshPreviewStageMediaRouteDebugState(false);
    });
}

void MainWindow::PreviewSection::updatePreviewStageMediaPresentationMode(bool requestUpdate)
{
    if (state_.previewCanvas_ == nullptr) {
        return;
    }
    state_.previewCanvas_->setStageMediaPresentationMode(
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
    double pausedSecond,
    const QString& chartVideoOverridePath)
{
    Q_UNUSED(trackPath);
    const double clampedPausedSecond = qMax(0.0, pausedSecond);
    updatePreviewStageMediaPresentationMode(false);

    ensurePreviewStageMediaHostInitialized();
    state_.deferredQuickShellStartupStageMediaChartPath_ = chartPath;
    state_.deferredQuickShellStartupStageMediaPausedSecond_ = clampedPausedSecond;
    state_.deferredQuickShellStartupStageMediaPending_ = true;
    // Phase 4c — cache the override on the section so the
    // deferred-load completion in onPreviewStageMediaDeferredLoadReady
    // (around line 113) can apply it when the actual setChartPath fires.
    state_.deferredQuickShellStartupStageMediaVideoOverride_ = chartVideoOverridePath;
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        state_.deferredQuickShellStartupStageMediaPending_ = false;
        state_.previewStageMediaHost_->setChartPath(chartPath, chartVideoOverridePath);
        state_.previewStageMediaHost_->setPlayheadSeconds(clampedPausedSecond);
    }

    applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "sync_chart_path");
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::clearPreviewStageMediaRoute()
{
    state_.deferredQuickShellStartupStageMediaPending_ = false;
    state_.deferredQuickShellStartupStageMediaChartPath_.clear();
    state_.deferredQuickShellStartupStageMediaPausedSecond_ = 0.0;
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setChartPath(QString());
    }

    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::releasePreviewStageMediaDecoderForFileOperation()
{
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->releaseDecoderForFileReplace();
    }
}

void MainWindow::PreviewSection::applyPreviewMediaWarmupToStageMediaRoute(
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath)
{
    ensurePreviewStageMediaHostInitialized();
    state_.previewStageMediaHost_->setWarmupResolvedMediaPath(chartPath, resolvedMediaPath);
    state_.deferredQuickShellStartupStageMediaChartPath_ = chartPath;
    state_.deferredQuickShellStartupStageMediaPending_ = true;
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        state_.deferredQuickShellStartupStageMediaPending_ = false;
        state_.previewStageMediaHost_->setChartPath(chartPath);
    }

    applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "media_warmup_result");
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::resetPreviewStageMediaRouteTimelineOffset()
{
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setTimelineOffsetSeconds(0.0);
    }
}

void MainWindow::PreviewSection::applyPreviewStageMediaRoutePlaybackRate(double rate, const char* site)
{
    char buf[260];
    std::snprintf(buf, sizeof(buf),
        "preview/rate/route_apply tid=0x%llx site=%s rate=%.3f host=%d has_video=%d",
        static_cast<unsigned long long>(reinterpret_cast<quintptr>(QThread::currentThreadId())),
        site != nullptr ? site : "(unspecified)",
        rate,
        state_.previewStageMediaHost_ != nullptr ? 1 : 0,
        state_.previewStageMediaHost_ != nullptr && state_.previewStageMediaHost_->hasVideoMedia() ? 1 : 0);
    miacode::oplog::appendStartupBeaconLine(buf);
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setPlaybackRate(rate);
    }
}

bool MainWindow::PreviewSection::previewStageMediaRouteHasVideo() const
{
    return state_.previewStageMediaHost_ != nullptr && state_.previewStageMediaHost_->hasVideoMedia();
}

double MainWindow::PreviewSection::previewStageMediaRouteCurrentPlaybackSecond() const
{
    return state_.previewStageMediaHost_ != nullptr ? state_.previewStageMediaHost_->currentPlaybackSecond() : 0.0;
}

void MainWindow::PreviewSection::startPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->startPlayback(second);
    }
}

void MainWindow::PreviewSection::syncPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->syncPlayback(second);
    }
}

void MainWindow::PreviewSection::pausePreviewStageMediaRoutePlayback()
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->pausePlayback();
    }
}

void MainWindow::PreviewSection::seekPreviewStageMediaRouteWhilePaused(double second)
{
    const double clampedSecond = qBound(0.0, second, owner_.previewDurationSeconds());
    if (!previewStageMediaRouteHasVideo()) {
        state_.pausedPreviewMediaSeekPending_ = false;
        return;
    }

    state_.pausedPreviewMediaSeekPending_ = true;
    state_.pausedPreviewMediaSeekSecond_ = clampedSecond;
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setPlayheadSeconds(clampedSecond);
    }
}

void MainWindow::PreviewSection::submitPreviewStageMediaRoutePausedSeek(double second, quint64 generation)
{
    const double clampedSecond = qBound(0.0, second, owner_.previewDurationSeconds());
    if (!previewStageMediaRouteHasVideo()) {
        state_.pausedPreviewMediaSeekPending_ = false;
        return;
    }

    state_.pausedPreviewMediaSeekPending_ = true;
    state_.pausedPreviewMediaSeekSecond_ = clampedSecond;
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->submitPausedSeek(clampedSecond, generation);
    }
}

void MainWindow::PreviewSection::setPreviewStageMediaRouteObservedPlayheadSecond(double second)
{
    if (!previewUsesStageMediaHostRoute() || state_.previewStageMediaHost_ == nullptr) {
        return;
    }
    state_.previewStageMediaHost_->setObservedPlayheadSecond(second);
}

void MainWindow::PreviewSection::ensureQuickShellPreviewCompositeSurfaceInitialized()
{
    if (state_.quickShellPreviewCompositeSurface_ == nullptr) {
        state_.quickShellPreviewCompositeSurface_ = new QuickShellPreviewCompositeSurface(&owner_);
    }
    state_.quickShellPreviewCompositeSurface_->setRuntime(state_.previewCanvas_);
    state_.quickShellPreviewCompositeSurface_->setMediaHost(state_.previewStageMediaHost_);
}

void MainWindow::PreviewSection::refreshQuickShellPreviewCompositeSurfaceState()
{
    ensureQuickShellPreviewCompositeSurfaceInitialized();
    if (state_.quickShellPreviewCompositeSurface_ == nullptr) {
        return;
    }

    const bool nextActive = quickShellPreviewUsesSeparateSurface();
    state_.quickShellPreviewCompositeSurface_->setRuntime(state_.previewCanvas_);
    state_.quickShellPreviewCompositeSurface_->setMediaHost(state_.previewStageMediaHost_);
    state_.quickShellPreviewCompositeSurface_->setActive(nextActive);

    if (state_.quickShellPreviewCompositeSurfaceActive_ == nextActive) {
        return;
    }

    state_.quickShellPreviewCompositeSurfaceActive_ = nextActive;
    if (state_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/stage_media"),
            QString("action=presentation_mode mode=%1")
                .arg(nextActive ? QStringLiteral("separate_surface") : QStringLiteral("inline")));
    }
}

void MainWindow::PreviewSection::ensurePreviewStageMediaHostInitialized()
{
    if (state_.previewStageMediaHost_ != nullptr) {
        ensureQuickShellPreviewCompositeSurfaceInitialized();
        refreshQuickShellPreviewCompositeSurfaceState();
        return;
    }

    state_.previewStageMediaHost_ = new PreviewStageMediaHost(&owner_);
    state_.previewStageMediaHost_->setBackgroundScaleMode(state_.previewBackgroundScaleMode_);
    state_.previewStageMediaHost_->setLayoutSquareScale(state_.previewLayoutSquareScale_);
    owner_.setPreviewStageMediaFrameRateMode(state_.previewStageMediaFrameRateMode_, false);
    // Push the persisted video decode-mode preference (硬件渲染 / 软件渲染) once,
    // before any PV is resolved, so the user's choice applies on the first load.
    // Done directly (not via owner_.setVideoDecodePrefersSoftware) because that
    // setter early-returns when the value is unchanged, which would skip the
    // initial host hand-off when the cached value equals the default (false).
    state_.previewStageMediaHost_->setVideoDecodePreference(owner_.currentVideoDecodePrefersSoftware());
    connect(state_.previewStageMediaHost_, &PreviewStageMediaHost::mediaStateChanged, &owner_, [this]() {
        applyPreviewStageMediaRouteVisualSettings();
        refreshQuickShellPreviewCompositeSurfaceState();
        refreshPreviewStageMediaRouteDebugState(false);
    });
    connect(state_.previewStageMediaHost_, &PreviewStageMediaHost::playbackPositionChanged, &owner_, [this](double second) {
        if (state_.qtPreviewPlaying_) {
            return;
        }
        if (state_.pausedSeekMediaPending_) {
            if (state_.runtimeDebugOutputEnabled_) {
                miacode::debug_log::appendLine(
                    miacode::debug_log::Channel::Audio,
                    QStringLiteral("preview/stage_media"),
                    QString("action=paused_seek_media_drop second=%1 reason=pending_generation")
                        .arg(second, 0, 'f', 6));
            }
            refreshPreviewStageMediaRouteDebugState(false);
            return;
        }
        state_.qtPreviewStartSecond_ = second;
        state_.qtPreviewElapsed_.restart();
        refreshPreviewStageMediaRouteDebugState(false);
    });
    connect(
        state_.previewStageMediaHost_,
        &PreviewStageMediaHost::playbackStartPrepared,
        &owner_,
        [this](double second, quint64 transactionId) {
            owner_.timelineSection_->handlePreviewStartupVideoPrepared(second, transactionId);
        }
    );
    connect(
        state_.previewStageMediaHost_,
        &PreviewStageMediaHost::pausedSeekCompleted,
        &owner_,
        [this](double second, quint64 generation) {
            owner_.handlePausedPreviewMediaSeekCompleted(second, generation);
        }
    );
    connect(state_.previewStageMediaHost_, &PreviewStageMediaHost::diagnosticsChanged, &owner_, [this]() {
        refreshPreviewStageMediaRouteDebugState(!state_.qtPreviewPlaying_);
    });
    ensureQuickShellPreviewCompositeSurfaceInitialized();
    state_.previewStageMediaHost_->setWarmupResolvedMediaPath(state_.previewMediaWarmupChartPath_, state_.previewMediaWarmupResolvedPath_);
    state_.deferredQuickShellStartupStageMediaChartPath_ = state_.currentFilePath_;
    // Phase 4c — pick up the &video= override from the parsed document
    // so the lazy host-init path (this code) honours it on first load.
    state_.deferredQuickShellStartupStageMediaVideoOverride_ = state_.document_.videoPath;
    state_.deferredQuickShellStartupStageMediaPausedSecond_ = qMax(0.0, state_.qtPreviewPauseSecond_);
    state_.deferredQuickShellStartupStageMediaPending_ = !state_.currentFilePath_.isEmpty();
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        state_.deferredQuickShellStartupStageMediaPending_ = false;
        state_.previewStageMediaHost_->setChartPath(
            state_.currentFilePath_,
            state_.deferredQuickShellStartupStageMediaVideoOverride_);
        state_.previewStageMediaHost_->setPlayheadSeconds(state_.qtPreviewPauseSecond_);
    }
    refreshQuickShellPreviewCompositeSurfaceState();
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::PreviewSection::shutdownPreviewStageMediaHost()
{
    if (state_.previewStageMediaHost_ == nullptr) {
        return;
    }
    if (state_.quickShellPreviewCompositeSurface_ != nullptr) {
        state_.quickShellPreviewCompositeSurface_->setMediaHost(nullptr);
        state_.quickShellPreviewCompositeSurface_->setActive(false);
    }
    state_.quickShellPreviewCompositeSurfaceActive_ = false;
    delete state_.previewStageMediaHost_;
    state_.previewStageMediaHost_ = nullptr;
}

void MainWindow::PreviewSection::refreshPreviewStageMediaRouteDebugState(bool requestUpdate)
{
    if (state_.previewCanvas_ == nullptr) {
        return;
    }
    miacode::preview::scene::PreviewExternalStageMediaType mediaType =
        miacode::preview::scene::PreviewExternalStageMediaType::None;
    bool videoPlaybackActive = false;
    double playbackSecond = 0.0;
    double clockDeltaSeconds = 0.0;
    qint64 videoFrameAgeMs = -1;
    qint64 videoFrameCountTotal = 0;
    double videoFrameRate = 0.0;
    double videoFrameIntervalAvgMs = 0.0;
    double videoFrameIntervalMaxMs = 0.0;
    qint64 videoFrameStallCount = 0;
    bool videoFrameStalled = false;
    bool hasResolvedMedia = false;
    bool hasVideoMedia = false;
    QString mediaTypeName = QStringLiteral("none");
    if (previewUsesStageMediaHostRoute() && state_.previewStageMediaHost_ != nullptr) {
        hasResolvedMedia = state_.previewStageMediaHost_->hasResolvedMedia();
        hasVideoMedia = state_.previewStageMediaHost_->hasVideoMedia();
        if (state_.previewStageMediaHost_->hasVideoMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Video;
            mediaTypeName = QStringLiteral("video");
        } else if (state_.previewStageMediaHost_->hasResolvedMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Image;
            mediaTypeName = QStringLiteral("image");
        }
        videoPlaybackActive = state_.previewStageMediaHost_->videoPlaybackActive();
        playbackSecond = state_.previewStageMediaHost_->currentPlaybackSecond();
        clockDeltaSeconds = state_.previewStageMediaHost_->clockDeltaSeconds();
        videoFrameAgeMs = state_.previewStageMediaHost_->videoFrameAgeMs();
        videoFrameCountTotal = state_.previewStageMediaHost_->videoFrameCountTotal();
        videoFrameRate = state_.previewStageMediaHost_->videoFrameRateEstimate();
        videoFrameIntervalAvgMs = state_.previewStageMediaHost_->videoFrameIntervalAvgMs();
        videoFrameIntervalMaxMs = state_.previewStageMediaHost_->videoFrameIntervalMaxMs();
        videoFrameStallCount = state_.previewStageMediaHost_->videoFrameStallCount();
        videoFrameStalled = state_.previewStageMediaHost_->videoFrameStalled();
    }
    state_.previewCanvas_->setExternalStageMediaProfileSummary(
        quickShellPreviewUsesSeparateSurface(),
        hasResolvedMedia,
        hasVideoMedia,
        mediaTypeName,
        videoFrameCountTotal,
        videoFrameRate,
        videoFrameIntervalAvgMs,
        videoFrameIntervalMaxMs,
        videoFrameStallCount
    );
    state_.previewCanvas_->setExternalStageMediaDebugState(
        mediaType,
        videoPlaybackActive,
        playbackSecond,
        clockDeltaSeconds,
        videoFrameAgeMs,
        videoFrameStalled,
        requestUpdate
    );
}

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

PreviewStageMediaHost* MainWindow::previewStageMediaHost() const
{
    // Phase 4c — non-owning. Returns nullptr until the host has been
    // lazily created (first chart-load triggers it inside
    // PreviewSection::ensurePreviewStageMediaHostInitialized).
    return state_.previewStageMediaHost_;
}

void MainWindow::syncPreviewStageMediaRouteChartPath(const QString& chartPath, const QString& trackPath, double pausedSecond, const QString& chartVideoOverridePath)
{
    previewSection_->syncPreviewStageMediaRouteChartPath(chartPath, trackPath, pausedSecond, chartVideoOverridePath);
}

void MainWindow::clearPreviewStageMediaRoute()
{
    previewSection_->clearPreviewStageMediaRoute();
}

void MainWindow::releasePreviewStageMediaDecoderForFileOperation()
{
    previewSection_->releasePreviewStageMediaDecoderForFileOperation();
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

void MainWindow::applyPreviewStageMediaRoutePlaybackRate(double rate, const char* site)
{
    previewSection_->applyPreviewStageMediaRoutePlaybackRate(rate, site);
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

void MainWindow::submitPreviewStageMediaRoutePausedSeek(double second, quint64 generation)
{
    previewSection_->submitPreviewStageMediaRoutePausedSeek(second, generation);
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
