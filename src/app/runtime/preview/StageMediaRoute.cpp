#include "runtime/preview/StageMediaHost.h"
#include "runtime/Shared.h"
#include "runtime/playback/PlaybackHost.h"
#include "runtime/shell/ShellHost.h"

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

using namespace miacode::runtime::shared;

miacode::runtime::StageMediaHost::StageMediaHost(
    Session& session,
    Session::HostUi& ui,
    Session::HostState& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
    , previewAppearanceValues_(session.previewAppearanceValues_)
{}

void miacode::runtime::StageMediaHost::applyPreviewStageMediaRouteVisualSettings()
{
    // While the export-preview dialog is up, PV/BG stays visible regardless of the
    // pause-hide option so the user previews exactly what the exported video shows.
    // Holding Alt while paused inverts the pause-hide option (same effective
    // flag as effectivePreviewOutlineVariant) so judge area ⇄ PV/BG flip together.
    const bool forceJudgeAreaWhenPaused =
        state_.previewForceLabeledJudgeLineWhenPaused_ != state_.pauseDisplayAltHoldActive_;
    const bool mediaVisible = !forceJudgeAreaWhenPaused
        || state_.playing_
        || state_.exportPreviewActive_;
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setBackgroundScaleMode(state_.previewBackgroundScaleMode_);
        state_.previewStageMediaHost_->setLayoutSquareScale(state_.previewLayoutSquareScale_);
        state_.previewStageMediaHost_->setMediaVisible(mediaVisible);
    }
    if (state_.scene_ != nullptr) {
        const bool stageMediaVisible =
            mediaVisible
            && state_.previewStageMediaHost_ != nullptr
            && state_.previewStageMediaHost_->hasResolvedMedia();
        state_.scene_->setStageMediaAvailable(stageMediaVisible);
    }
}

Session::PreviewStageMediaRoute miacode::runtime::StageMediaHost::previewStageMediaRoute() const
{
    return Session::PreviewStageMediaRoute::QuickShellStageHost;
}

bool miacode::runtime::StageMediaHost::previewUsesStageMediaHostRoute() const
{
    return true;
}

bool miacode::runtime::StageMediaHost::quickShellPreviewUsesSeparateSurface() const
{
    return false;
}

QWindow* miacode::runtime::StageMediaHost::quickShellPreviewCompositeWindow() const
{
    return state_.quickShellPreviewCompositeSurface_ != nullptr
        ? state_.quickShellPreviewCompositeSurface_->hostWindow()
        : nullptr;
}

bool miacode::runtime::StageMediaHost::shouldDeferQuickShellStartupStageMediaLoad() const
{
    return previewUsesStageMediaHostRoute() && state_.quickShellStartupStageMediaLoadDeferred_;
}

void miacode::runtime::StageMediaHost::noteQuickShellStartupUiReady()
{
    if (!previewUsesStageMediaHostRoute()) {
        return;
    }
    state_.quickShellStartupUiReady_ = true;
    scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void miacode::runtime::StageMediaHost::scheduleDeferredQuickShellStartupStageMediaLoadIfReady()
{
    if (!shouldDeferQuickShellStartupStageMediaLoad()
        || !state_.quickShellStartupUiReady_
        || state_.startupRestorePending_
        || state_.deferredQuickShellStartupStageMediaFlushScheduled_) {
        return;
    }

    state_.deferredQuickShellStartupStageMediaFlushScheduled_ = true;
    QTimer::singleShot(0, &session_, [this]() {
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

void miacode::runtime::StageMediaHost::updatePreviewStageMediaPresentationMode(bool requestUpdate)
{
    if (state_.scene_ == nullptr) {
        return;
    }
    state_.scene_->setStageMediaPresentationMode(
        miacode::preview::scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem,
        requestUpdate
    );
}

void miacode::runtime::StageMediaHost::ensurePreviewStageMediaRouteInitialized()
{
    ensurePreviewStageMediaHostInitialized();
}

void miacode::runtime::StageMediaHost::syncPreviewStageMediaRouteChartPath(
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

void miacode::runtime::StageMediaHost::clearPreviewStageMediaRoute()
{
    state_.deferredQuickShellStartupStageMediaPending_ = false;
    state_.deferredQuickShellStartupStageMediaChartPath_.clear();
    state_.deferredQuickShellStartupStageMediaPausedSecond_ = 0.0;
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setChartPath(QString());
    }

    refreshPreviewStageMediaRouteDebugState(false);
}

void miacode::runtime::StageMediaHost::releasePreviewStageMediaDecoderForFileOperation()
{
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->releaseDecoderForFileReplace();
    }
}

void miacode::runtime::StageMediaHost::applyPreviewMediaWarmupToStageMediaRoute(
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

void miacode::runtime::StageMediaHost::resetPreviewStageMediaRouteTimelineOffset()
{
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setTimelineOffsetSeconds(0.0);
    }
}

void miacode::runtime::StageMediaHost::applyPreviewStageMediaRoutePlaybackRate(double rate, const char* site)
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

bool miacode::runtime::StageMediaHost::previewStageMediaRouteHasVideo() const
{
    return state_.previewStageMediaHost_ != nullptr && state_.previewStageMediaHost_->hasVideoMedia();
}

double miacode::runtime::StageMediaHost::previewStageMediaRouteCurrentPlaybackSecond() const
{
    return state_.previewStageMediaHost_ != nullptr ? state_.previewStageMediaHost_->currentPlaybackSecond() : 0.0;
}

void miacode::runtime::StageMediaHost::startPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->startPlayback(second);
    }
}

void miacode::runtime::StageMediaHost::syncPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->syncPlayback(second);
    }
}

void miacode::runtime::StageMediaHost::pausePreviewStageMediaRoutePlayback()
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->pausePlayback();
    }
}

void miacode::runtime::StageMediaHost::seekPreviewStageMediaRouteWhilePaused(double second)
{
    const double clampedSecond = qBound(0.0, second, session_.previewDurationSeconds());
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

void miacode::runtime::StageMediaHost::submitPreviewStageMediaRoutePausedSeek(double second, quint64 generation)
{
    const double clampedSecond = qBound(0.0, second, session_.previewDurationSeconds());
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

void miacode::runtime::StageMediaHost::setPreviewStageMediaRouteObservedPlayheadSecond(double second)
{
    if (!previewUsesStageMediaHostRoute() || state_.previewStageMediaHost_ == nullptr) {
        return;
    }
    state_.previewStageMediaHost_->setObservedPlayheadSecond(second);
}

void miacode::runtime::StageMediaHost::ensureQuickShellPreviewCompositeSurfaceInitialized()
{
    if (state_.quickShellPreviewCompositeSurface_ == nullptr) {
        state_.quickShellPreviewCompositeSurface_ = new QuickShellPreviewCompositeSurface(&session_);
    }
    state_.quickShellPreviewCompositeSurface_->setRuntime(state_.scene_);
    state_.quickShellPreviewCompositeSurface_->setMediaHost(state_.previewStageMediaHost_);
}

void miacode::runtime::StageMediaHost::refreshQuickShellPreviewCompositeSurfaceState()
{
    ensureQuickShellPreviewCompositeSurfaceInitialized();
    if (state_.quickShellPreviewCompositeSurface_ == nullptr) {
        return;
    }

    const bool nextActive = quickShellPreviewUsesSeparateSurface();
    state_.quickShellPreviewCompositeSurface_->setRuntime(state_.scene_);
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

void miacode::runtime::StageMediaHost::ensurePreviewStageMediaHostInitialized()
{
    if (state_.previewStageMediaHost_ != nullptr) {
        ensureQuickShellPreviewCompositeSurfaceInitialized();
        refreshQuickShellPreviewCompositeSurfaceState();
        return;
    }

    state_.previewStageMediaHost_ = new PreviewStageMediaHost(&session_);
    state_.previewStageMediaHost_->setBackgroundScaleMode(state_.previewBackgroundScaleMode_);
    state_.previewStageMediaHost_->setLayoutSquareScale(state_.previewLayoutSquareScale_);
    session_.playback_->setPreviewStageMediaFrameRateMode(state_.previewStageMediaFrameRateMode_, false);
    // Push the persisted video decode-mode preference (硬件渲染 / 软件渲染) once,
    // before any PV is resolved, so the user's choice applies on the first load.
    // Done directly (not via session_.setVideoDecodePrefersSoftware) because that
    // setter early-returns when the value is unchanged, which would skip the
    // initial host hand-off when the cached value equals the default (false).
    state_.previewStageMediaHost_->setVideoDecodePreference(session_.currentVideoDecodePrefersSoftware());
    QObject::connect(state_.previewStageMediaHost_, &PreviewStageMediaHost::mediaStateChanged, &session_, [this]() {
        applyPreviewStageMediaRouteVisualSettings();
        refreshQuickShellPreviewCompositeSurfaceState();
        refreshPreviewStageMediaRouteDebugState(false);
    });
    QObject::connect(state_.previewStageMediaHost_, &PreviewStageMediaHost::playbackPositionChanged, &session_, [this](double second) {
        if (state_.playing_) {
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
    QObject::connect(
        state_.previewStageMediaHost_,
        &PreviewStageMediaHost::playbackStartPrepared,
        &session_,
        [this](double second, quint64 transactionId) {
            session_.playback_->handlePreviewStartupVideoPrepared(second, transactionId);
        }
    );
    QObject::connect(
        state_.previewStageMediaHost_,
        &PreviewStageMediaHost::pausedSeekCompleted,
        &session_,
        [this](double second, quint64 generation) {
            session_.handlePausedPreviewMediaSeekCompleted(second, generation);
        }
    );
    QObject::connect(state_.previewStageMediaHost_, &PreviewStageMediaHost::diagnosticsChanged, &session_, [this]() {
        refreshPreviewStageMediaRouteDebugState(!state_.playing_);
    });
    ensureQuickShellPreviewCompositeSurfaceInitialized();
    state_.previewStageMediaHost_->setWarmupResolvedMediaPath(state_.previewMediaWarmupChartPath_, state_.previewMediaWarmupResolvedPath_);
    state_.deferredQuickShellStartupStageMediaChartPath_ = state_.currentFilePath_;
    // Phase 4c — pick up the &video= override from the parsed document
    // so the lazy host-init path (this code) honours it on first load.
    state_.deferredQuickShellStartupStageMediaVideoOverride_ = session_.applicationServices_.workspace().document().videoPath;
    state_.deferredQuickShellStartupStageMediaPausedSecond_ = qMax(0.0, state_.pauseSecond_);
    state_.deferredQuickShellStartupStageMediaPending_ = !state_.currentFilePath_.isEmpty();
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        state_.deferredQuickShellStartupStageMediaPending_ = false;
        state_.previewStageMediaHost_->setChartPath(
            state_.currentFilePath_,
            state_.deferredQuickShellStartupStageMediaVideoOverride_);
        state_.previewStageMediaHost_->setPlayheadSeconds(state_.pauseSecond_);
    }
    refreshQuickShellPreviewCompositeSurfaceState();
    refreshPreviewStageMediaRouteDebugState(false);
}

void miacode::runtime::StageMediaHost::shutdownPreviewStageMediaHost()
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

void miacode::runtime::StageMediaHost::refreshPreviewStageMediaRouteDebugState(bool requestUpdate)
{
    if (state_.scene_ == nullptr) {
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
    state_.scene_->setExternalStageMediaProfileSummary(
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
    state_.scene_->setExternalStageMediaDebugState(
        mediaType,
        videoPlaybackActive,
        playbackSecond,
        clockDeltaSeconds,
        videoFrameAgeMs,
        videoFrameStalled,
        requestUpdate
    );
}

void Session::applyPreviewStageMediaRouteVisualSettings()
{
    stageMedia_->applyPreviewStageMediaRouteVisualSettings();
}

Session::PreviewStageMediaRoute Session::previewStageMediaRoute() const
{
    return stageMedia_->previewStageMediaRoute();
}

bool Session::previewUsesStageMediaHostRoute() const
{
    return stageMedia_->previewUsesStageMediaHostRoute();
}

bool Session::quickShellPreviewUsesSeparateSurface() const
{
    return stageMedia_->quickShellPreviewUsesSeparateSurface();
}

QWindow* Session::quickShellPreviewCompositeWindow() const
{
    return stageMedia_->quickShellPreviewCompositeWindow();
}

bool Session::shouldDeferQuickShellStartupStageMediaLoad() const
{
    return stageMedia_->shouldDeferQuickShellStartupStageMediaLoad();
}

void Session::noteQuickShellStartupUiReady()
{
    stageMedia_->noteQuickShellStartupUiReady();
}

void Session::scheduleDeferredQuickShellStartupStageMediaLoadIfReady()
{
    stageMedia_->scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void Session::updatePreviewStageMediaPresentationMode(bool requestUpdate)
{
    stageMedia_->updatePreviewStageMediaPresentationMode(requestUpdate);
}

void Session::ensurePreviewStageMediaRouteInitialized()
{
    stageMedia_->ensurePreviewStageMediaRouteInitialized();
}

PreviewStageMediaHost* Session::previewStageMediaHost() const
{
    // Phase 4c — non-owning. Returns nullptr until the host has been
    // lazily created (first chart-load triggers it inside
    // PreviewSection::ensurePreviewStageMediaHostInitialized).
    return state_.previewStageMediaHost_;
}

void Session::syncPreviewStageMediaRouteChartPath(const QString& chartPath, const QString& trackPath, double pausedSecond, const QString& chartVideoOverridePath)
{
    stageMedia_->syncPreviewStageMediaRouteChartPath(chartPath, trackPath, pausedSecond, chartVideoOverridePath);
}

void Session::clearPreviewStageMediaRoute()
{
    stageMedia_->clearPreviewStageMediaRoute();
}

void Session::releasePreviewStageMediaDecoderForFileOperation()
{
    stageMedia_->releasePreviewStageMediaDecoderForFileOperation();
}

void Session::applyPreviewMediaWarmupToStageMediaRoute(
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath)
{
    stageMedia_->applyPreviewMediaWarmupToStageMediaRoute(chartPath, resolvedMediaPath, trackPath);
}

void Session::resetPreviewStageMediaRouteTimelineOffset()
{
    stageMedia_->resetPreviewStageMediaRouteTimelineOffset();
}

void Session::applyPreviewStageMediaRoutePlaybackRate(double rate, const char* site)
{
    stageMedia_->applyPreviewStageMediaRoutePlaybackRate(rate, site);
}

bool Session::previewStageMediaRouteHasVideo() const
{
    return stageMedia_->previewStageMediaRouteHasVideo();
}

double Session::previewStageMediaRouteCurrentPlaybackSecond() const
{
    return stageMedia_->previewStageMediaRouteCurrentPlaybackSecond();
}

void Session::startPreviewStageMediaRoutePlayback(double second)
{
    stageMedia_->startPreviewStageMediaRoutePlayback(second);
}

void Session::syncPreviewStageMediaRoutePlayback(double second)
{
    stageMedia_->syncPreviewStageMediaRoutePlayback(second);
}

void Session::pausePreviewStageMediaRoutePlayback()
{
    stageMedia_->pausePreviewStageMediaRoutePlayback();
}

void Session::seekPreviewStageMediaRouteWhilePaused(double second)
{
    stageMedia_->seekPreviewStageMediaRouteWhilePaused(second);
}

void Session::submitPreviewStageMediaRoutePausedSeek(double second, quint64 generation)
{
    stageMedia_->submitPreviewStageMediaRoutePausedSeek(second, generation);
}

void Session::setPreviewStageMediaRouteObservedPlayheadSecond(double second)
{
    stageMedia_->setPreviewStageMediaRouteObservedPlayheadSecond(second);
}

void Session::ensureQuickShellPreviewCompositeSurfaceInitialized()
{
    stageMedia_->ensureQuickShellPreviewCompositeSurfaceInitialized();
}

void Session::refreshQuickShellPreviewCompositeSurfaceState()
{
    stageMedia_->refreshQuickShellPreviewCompositeSurfaceState();
}

void Session::ensurePreviewStageMediaHostInitialized()
{
    stageMedia_->ensurePreviewStageMediaHostInitialized();
}

void Session::shutdownPreviewStageMediaHost()
{
    stageMedia_->shutdownPreviewStageMediaHost();
}

void Session::refreshPreviewStageMediaRouteDebugState(bool requestUpdate)
{
    stageMedia_->refreshPreviewStageMediaRouteDebugState(requestUpdate);
}
