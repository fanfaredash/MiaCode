#include "preview/runtime/PreviewStageMediaHost.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"

#include <cstdio>  // G2 Diag: std::snprintf for sync rate-change beacon lines

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#endif

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QVariant>
#include <QtMath>

namespace {

constexpr qint64 kPausedSeekAckToleranceMs = 80;
constexpr int kMediaSeekPrepareTimeoutMs = 800;
constexpr int kVideoPlaybackWatchdogMs = 600;
constexpr int kVideoPlaybackSoftRecoveryMaxConsecutive = 2;
constexpr int kVideoBackendRecoveryMaxConsecutive = 3;
constexpr int kVideoFrameIntervalWindowSize = 120;
constexpr qint64 kVideoFrameStallMinMs = 120;
constexpr double kVideoFrameStallMultiplier = 3.5;

unsigned long currentBeaconTid() noexcept
{
#ifdef Q_OS_WIN
    return static_cast<unsigned long>(::GetCurrentThreadId());
#else
    return 0;
#endif
}

double averageOrZero(double total, qint64 count)
{
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

double fpsFromAverageMs(double averageMs)
{
    return averageMs > 1e-6 ? 1000.0 / averageMs : 0.0;
}

QString normalizedLocalPath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

void appendPreviewStageMediaLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/stage_media"),
        text
    );
}

#ifdef HAVE_QT_MULTIMEDIA
QString mediaStatusName(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::NoMedia:
        return QStringLiteral("NoMedia");
    case QMediaPlayer::LoadingMedia:
        return QStringLiteral("LoadingMedia");
    case QMediaPlayer::LoadedMedia:
        return QStringLiteral("LoadedMedia");
    case QMediaPlayer::StalledMedia:
        return QStringLiteral("StalledMedia");
    case QMediaPlayer::BufferingMedia:
        return QStringLiteral("BufferingMedia");
    case QMediaPlayer::BufferedMedia:
        return QStringLiteral("BufferedMedia");
    case QMediaPlayer::EndOfMedia:
        return QStringLiteral("EndOfMedia");
    case QMediaPlayer::InvalidMedia:
        return QStringLiteral("InvalidMedia");
    default:
        return QStringLiteral("UnknownMediaStatus");
    }
}

QString mediaErrorName(QMediaPlayer::Error error)
{
    switch (error) {
    case QMediaPlayer::NoError:
        return QStringLiteral("NoError");
    case QMediaPlayer::ResourceError:
        return QStringLiteral("ResourceError");
    case QMediaPlayer::FormatError:
        return QStringLiteral("FormatError");
    case QMediaPlayer::NetworkError:
        return QStringLiteral("NetworkError");
    case QMediaPlayer::AccessDeniedError:
        return QStringLiteral("AccessDeniedError");
    default:
        return QStringLiteral("UnknownError");
    }
}

QString playbackStateName(QMediaPlayer::PlaybackState state)
{
    switch (state) {
    case QMediaPlayer::StoppedState:
        return QStringLiteral("StoppedState");
    case QMediaPlayer::PlayingState:
        return QStringLiteral("PlayingState");
    case QMediaPlayer::PausedState:
        return QStringLiteral("PausedState");
    default:
        return QStringLiteral("UnknownState");
    }
}

QMediaPlayer::PlaybackState playerPlaybackState(const QMediaPlayer* player)
{
    if (player == nullptr) {
        return QMediaPlayer::StoppedState;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return player->playbackState();
#else
    return player->state();
#endif
}
#endif

}  // namespace

PreviewStageMediaHost::PreviewStageMediaHost(QObject* parent)
    : QObject(parent)
{
    videoFrameIntervalsMs_.resize(kVideoFrameIntervalWindowSize);
    videoFrameIntervalsMs_.fill(0.0);
}

PreviewStageMediaHost::~PreviewStageMediaHost()
{
    QElapsedTimer timer;
    timer.start();
    const QString mediaType = debugMediaTypeName();
    const bool hadPlayer = player_ != nullptr;
    shutdownForAppExit();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/stage_media_host"),
        QStringLiteral("destructor"),
        timer.elapsed(),
        QStringLiteral("had_player=%1 media_kind=%2")
            .arg(hadPlayer ? 1 : 0)
            .arg(mediaType)
    );
}

void PreviewStageMediaHost::shutdownForAppExit()
{
    shuttingDown_ = true;
    clearMedia();
}

void PreviewStageMediaHost::initializeBackendObjects()
{
#ifndef HAVE_QT_MULTIMEDIA
    return;
#else
    if (player_ != nullptr) {
        return;
    }

    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    audioOutput_->setMuted(true);
    audioOutput_->setVolume(0.0f);
    player_->setAudioOutput(audioOutput_);
    // G2 fix: setPlaybackRate is safe on a freshly constructed player
    // (no source attached yet → no demuxer/decoder worker running, so the
    // rate write can't race with the in-flight decode loop that fast-fails
    // at non-1.0 rates on Qt 6.8). This call is the load-bearing piece
    // of the new rate-change story: PreviewStageMediaHost::setPlaybackRate
    // forwards every rate != 1.0 / rate != oldRate change through
    // recoverVideoBackend(), which deletes this player and re-enters
    // initializeBackendObjects() with the desired rate already in
    // playbackRate_. The line below is where that desired rate lands.
    player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
    // One-shot diagnostic so the user-machine vs dev-machine PV-scaling
    // bug can be pinned down without a custom build. The QtMultimedia
    // backend (FFmpeg vs native WMF on Windows) is selected at runtime
    // based on QT_MEDIA_BACKEND + plugin availability + silent fallback;
    // it's never logged elsewhere, but the chosen backend can change how
    // a video frame's pixel size + rotation get reported (rotation
    // metadata in particular is applied by some backends and ignored by
    // others, which inverts the aspect of phone-captured PVs).
    appendPreviewStageMediaLog(
        QStringLiteral("media_backend"),
        QString("qt_runtime_version=%1 qt_media_backend_env=%2")
            .arg(QString::fromLatin1(qVersion()))
            .arg(qEnvironmentVariable(
                "QT_MEDIA_BACKEND",
                QStringLiteral("(unset:default)")))
    );
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 positionMs) {
        if (mediaKind_ != MediaKind::Video) {
            return;
        }
        lastTimelineSecond_ = qMax(0.0, static_cast<double>(positionMs) / 1000.0 - timelineOffsetSeconds_);
        updateClockDelta();
        if (pausedSeekCompletionPending_
            && pausedSeekTargetMs_ >= 0
            && videoSink_ == nullptr
            && qAbs(positionMs - pausedSeekTargetMs_) <= kPausedSeekAckToleranceMs) {
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_ack"),
                QString("generation=%1 second=%2 position_ms=%3 target_ms=%4 source=position_fallback")
                    .arg(pausedSeekGeneration_)
                    .arg(pausedSeekTargetSecond_, 0, 'f', 6)
                    .arg(positionMs)
                    .arg(pausedSeekTargetMs_)
            );
            pausedSeekCompletionPending_ = false;
            emit pausedSeekCompleted(pausedSeekTargetSecond_, pausedSeekGeneration_);
        }
        if (preparedPlaybackPending_
            && preparedPlaybackTargetMs_ >= 0
            && videoSink_ == nullptr
            && qAbs(positionMs - preparedPlaybackTargetMs_) <= kPausedSeekAckToleranceMs) {
            appendPreviewStageMediaLog(
                QStringLiteral("prepare_playback_ready"),
                QString("txn=%1 second=%2 position_ms=%3 target_ms=%4 source=position_fallback")
                    .arg(preparedPlaybackTransaction_)
                    .arg(preparedPlaybackTargetSecond_, 0, 'f', 6)
                    .arg(positionMs)
                    .arg(preparedPlaybackTargetMs_)
            );
            preparedPlaybackPending_ = false;
            preparedPlaybackReady_ = true;
            emit playbackStartPrepared(preparedPlaybackTargetSecond_, preparedPlaybackTransaction_);
        }
        emit playbackPositionChanged(lastTimelineSecond_);
        emit diagnosticsChanged();
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (syncMediaStatusBeaconBudget_ > 0) {
            --syncMediaStatusBeaconBudget_;
            char buf[240];
            std::snprintf(buf, sizeof(buf),
                "preview/media/status_changed tid=%lu status=%d state=%d rate=%.3f pos=%lld pending_rate=%d kind=%d",
                currentBeaconTid(),
                static_cast<int>(status),
                static_cast<int>(playerPlaybackState(player_)),
                player_ != nullptr ? static_cast<double>(player_->playbackRate()) : -1.0,
                player_ != nullptr ? static_cast<long long>(player_->position()) : -1ll,
                pendingPlaybackRateApply_ ? 1 : 0,
                static_cast<int>(mediaKind_));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        appendPreviewStageMediaLog(
            QStringLiteral("media_status"),
            QString("status=%1 playback_state=%2 position_ms=%3 kind=%4 recovering=%5")
                .arg(mediaStatusName(status))
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(debugMediaTypeName())
                .arg(recoveringVideoBackend_ ? 1 : 0)
        );
        // G2 Commit 1: flush a deferred setPlaybackRate write the moment the
        // player lands in a stable state. The original setPlaybackRate call
        // returned without touching the player and stashed pendingPlaybackRateApply_;
        // here is where the cached playbackRate_ actually reaches QMediaPlayer.
        if (pendingPlaybackRateApply_ && player_ != nullptr) {
            const bool stableNow = status == QMediaPlayer::NoMedia
                                || status == QMediaPlayer::LoadedMedia
                                || status == QMediaPlayer::BufferedMedia
                                || status == QMediaPlayer::EndOfMedia;
            if (stableNow) {
                pendingPlaybackRateApply_ = false;
                // Reachable only for rate==1.0 deferrals: setPlaybackRate
                // routes any rate != 1.0 through recoverVideoBackend() now,
                // which rebuilds the player and applies the rate inside
                // initializeBackendObjects (pre-setSource, safe). A
                // pending 1.0 flush is harmless even if the player ended
                // up rebuilt in between, because rebuilds always apply
                // the latest cached playbackRate_ themselves.
                {
                    char buf[180];
                    std::snprintf(buf, sizeof(buf),
                        "preview/rate/qt_host_flush_about_to_setrate tid=%lu rate=%.3f status=%d",
                        currentBeaconTid(),
                        playbackRate_,
                        static_cast<int>(status));
                    miacode::oplog::appendStartupBeaconLine(buf);
                }
                player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
                {
                    char buf[180];
                    std::snprintf(buf, sizeof(buf),
                        "preview/rate/qt_host_flush_setrate_done tid=%lu rate=%.3f player_rate=%.3f",
                        currentBeaconTid(),
                        playbackRate_,
                        player_ != nullptr ? static_cast<double>(player_->playbackRate()) : -1.0);
                    miacode::oplog::appendStartupBeaconLine(buf);
                }
                appendPreviewStageMediaLog(
                    QStringLiteral("playback_rate_flushed"),
                    QString("rate=%1 status=%2 kind=%3")
                        .arg(playbackRate_, 0, 'f', 3)
                        .arg(mediaStatusName(status))
                        .arg(debugMediaTypeName()));
            }
        }
        if (status == QMediaPlayer::EndOfMedia) {
            videoPlaybackActive_ = false;
            videoPlaybackPendingStart_ = false;
            videoPlaybackActiveElapsed_.invalidate();
            ++videoPlaybackWatchdogSerial_;
            updateClockDelta();
            updateVideoFrameStallState(true);
            emit diagnosticsChanged();
            emit playbackFinished();
            return;
        }
        if (status == QMediaPlayer::StalledMedia && mediaKind_ == MediaKind::Video && videoPlaybackActive_) {
            scheduleVideoPlaybackWatchdog(QStringLiteral("media_status_stalled"));
            return;
        }
        if (status != QMediaPlayer::InvalidMedia
            || mediaKind_ != MediaKind::Video
            || recoveringVideoBackend_) {
            return;
        }
        const double targetSecond = videoPlaybackActive_ ? observedPlayheadSecond_ : currentPlaybackSecond();
        QMetaObject::invokeMethod(
            this,
            [this, targetSecond]() {
                recoverVideoBackend(QStringLiteral("media_status_invalid"), targetSecond, videoPlaybackActive_);
            },
            Qt::QueuedConnection
        );
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString& errorString) {
        appendPreviewStageMediaLog(
            QStringLiteral("media_error"),
            QString("error=%1 text=\"%2\" playback_state=%3 position_ms=%4 kind=%5 recovering=%6")
                .arg(mediaErrorName(error))
                .arg(errorString)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(debugMediaTypeName())
                .arg(recoveringVideoBackend_ ? 1 : 0)
        );
        if (error == QMediaPlayer::NoError
            || mediaKind_ != MediaKind::Video
            || recoveringVideoBackend_) {
            return;
        }
        const QString reason = QStringLiteral("media_error_%1").arg(mediaErrorName(error));
        const double targetSecond = videoPlaybackActive_ ? observedPlayheadSecond_ : currentPlaybackSecond();
        QMetaObject::invokeMethod(
            this,
            [this, reason, targetSecond]() {
                recoverVideoBackend(reason, targetSecond, videoPlaybackActive_);
            },
            Qt::QueuedConnection
        );
    });
#endif
}

void PreviewStageMediaHost::setWarmupResolvedMediaPath(const QString& chartPath, const QString& mediaPath)
{
    warmupChartPath_ = normalizedLocalPath(chartPath);
    warmupMediaPath_ = normalizedLocalPath(mediaPath);
}

void PreviewStageMediaHost::attachVideoOutputObject(QObject* videoOutputObject)
{
    if (videoOutputObject_ == videoOutputObject) {
        return;
    }
    videoOutputObject_ = videoOutputObject;
    bindVideoOutput();
}

void PreviewStageMediaHost::detachVideoOutputObject(QObject* videoOutputObject)
{
    if (videoOutputObject_ == nullptr || videoOutputObject == nullptr) {
        return;
    }
    if (videoOutputObject_ != videoOutputObject) {
        return;
    }
    videoOutputObject_.clear();
    bindVideoOutput();
}

bool PreviewStageMediaHost::hasResolvedMedia() const
{
    return mediaKind_ != MediaKind::None;
}

bool PreviewStageMediaHost::hasVideoMedia() const
{
    return mediaKind_ == MediaKind::Video;
}

bool PreviewStageMediaHost::mediaVisible() const
{
    return mediaVisible_;
}

void PreviewStageMediaHost::setMediaVisible(bool visible)
{
    if (mediaVisible_ == visible) {
        return;
    }
    mediaVisible_ = visible;
    // Phase 4c-9 — invalidate the toImage() throttle so the first
    // QVideoSink frame after a visibility-on transition is captured
    // immediately (bg shouldn't ghost between hide→show with stale
    // content, especially at scrub time). Only used by the legacy
    // CPU detour; ignored under per-pixel alpha mode.
    if (visible) {
        videoFrameToImageThrottle_.invalidate();
    }
    emit mediaVisibilityChanged();
}

QImage PreviewStageMediaHost::currentBackgroundImage() const
{
    // Phase 4d — when per-pixel alpha is enabled the DComp HWND is
    // transparent at the OS level, so QML's PreviewStageMediaItem
    // (Image/VideoOutput) below shows through natively. The CPU
    // detour (StageBackgroundSource painting the bg) is no longer
    // needed and should be skipped to avoid duplicate paint cost.
    if (miacode::debug_options::previewDCompPerPixelAlphaEnabled()) {
        return QImage();
    }
    // Phase 4c-8 (image) + 4c-9 (video) — return the cached bg image
    // for both kinds. For Image: loadImageMedia() seeds it once at
    // chart-load. For Video: noteVideoFrameArrived() converts each
    // QVideoFrame to a QImage and updates it as new frames flow in.
    // The DComp surface reads this every snapshot tick; image-mode
    // returns the same content frame-to-frame (cheap), video-mode
    // returns the latest decoded frame.
    if (mediaKind_ != MediaKind::Image && mediaKind_ != MediaKind::Video) {
        return QImage();
    }
    return loadedBackgroundImage_;
}

QUrl PreviewStageMediaHost::imageSource() const
{
    return imageSource_;
}

QString PreviewStageMediaHost::resolvedVideoPath() const
{
    if (mediaKind_ != MediaKind::Video) {
        return QString();
    }
    return mediaPath_;
}

int PreviewStageMediaHost::backgroundScaleMode() const
{
    return static_cast<int>(backgroundScaleMode_);
}

void PreviewStageMediaHost::setBackgroundScaleModeValue(int mode)
{
    if (mode == static_cast<int>(PreviewBackgroundScaleMode::SquareFitContain)) {
        setBackgroundScaleMode(PreviewBackgroundScaleMode::SquareFitContain);
        return;
    }
    setBackgroundScaleMode(
        mode == static_cast<int>(PreviewBackgroundScaleMode::FitContain)
            ? PreviewBackgroundScaleMode::FitContain
            : PreviewBackgroundScaleMode::FillCrop
    );
}

void PreviewStageMediaHost::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    if (backgroundScaleMode_ == mode) {
        return;
    }
    backgroundScaleMode_ = mode;
    emit backgroundScaleModeChanged();
}

void PreviewStageMediaHost::setChartPath(const QString& chartPath,
                                         const QString& chartVideoOverridePath)
{
    const QString normalizedChartPath = normalizedLocalPath(chartPath);
    // Phase 4c — keys cache on (chart-path, video-override) jointly.
    // The user might re-select the same chart after editing &video=,
    // so chart-path equality alone isn't sufficient to skip the
    // refresh.
    if (normalizedChartPath == chartPath_
        && chartVideoOverridePath == chartVideoOverridePath_) {
        return;
    }

    chartPath_ = normalizedChartPath;
    chartVideoOverridePath_ = chartVideoOverridePath;
    clearMedia();
    if (chartPath_.isEmpty()) {
        appendPreviewStageMediaLog(QStringLiteral("set_chart_path"), QStringLiteral("chart=(empty) kind=none"));
        return;
    }

    // Phase 4c — unified resolution: explicit `&video=` override
    // beats the sibling-filename heuristic. Falls back to the legacy
    // `resolveMediaPath` path when no override is given AND no
    // sibling video exists, so still-image charts keep working.
    QString resolvedPath = miacode::chart_assets::resolveChartVideoPath(
        chartPath_, chartVideoOverridePath_);
    if (resolvedPath.isEmpty()) {
        // No video override AND no sibling video → fall through to
        // the legacy media-resolver, which also picks up image-only
        // backgrounds (bg.jpg / bg.png).
        resolvedPath = resolveMediaPath(chartPath_);
    }
    if (resolvedPath.isEmpty()) {
        appendPreviewStageMediaLog(
            QStringLiteral("set_chart_path"),
            QStringLiteral("chart=%1 kind=none").arg(chartPath_)
        );
        return;
    }

    mediaPath_ = resolvedPath;
    const bool isVideo = miacode::chart_assets::isVideoBackgroundPath(mediaPath_);
    if (isVideo) {
        loadVideoMedia(mediaPath_);
    } else {
        loadImageMedia(mediaPath_);
    }

    appendPreviewStageMediaLog(
        QStringLiteral("set_chart_path"),
        QStringLiteral("chart=%1 media=%2 kind=%3 override=%4")
            .arg(chartPath_)
            .arg(mediaPath_)
            .arg(debugMediaTypeName())
            .arg(chartVideoOverridePath_.isEmpty()
                    ? QStringLiteral("(none)")
                    : chartVideoOverridePath_)
    );
}

void PreviewStageMediaHost::setPlaybackRate(double rate)
{
    MC_OP("PreviewStageMediaHost::setPlaybackRate");
    const double oldRate = playbackRate_;
    playbackRate_ = qMax(0.05, rate);
#ifdef HAVE_QT_MULTIMEDIA
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        syncVideoFrameBeaconBudget_ = 24;
        syncMediaStatusBeaconBudget_ = 16;
    }
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/rate/qt_host_enter tid=%lu rate=%.3f old=%.3f player_null=%d kind=%d",
            currentBeaconTid(),
            playbackRate_,
            oldRate,
            player_ == nullptr ? 1 : 0,
            static_cast<int>(mediaKind_));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (player_ == nullptr) {
        // No player yet — playbackRate_ is the cached value that
        // initializeBackendObjects() will apply on a fresh player BEFORE
        // setSource. That ordering (rate-before-source) is verified safe
        // even at 0.5x / 1.5x (logs_24 startup line 134-161).
        return;
    }
    // beta55 diagnostic build: revert beta54's recoverVideoBackend rebuild
    // path so the 0.5x / 1.5x crash recurs on demand. This release ships
    // alongside Start_MiaCode_DisablePerPixelAlpha.bat and
    // Start_MiaCode_FFmpegBackend.bat — users compare default-launcher
    // behaviour (crash) against env-toggle behaviour (does it survive?)
    // to confirm hypothesis #1 (DComp per-pixel-alpha + QRhi D3D11
    // texture invalidation during Qt's setPlaybackRate-triggered
    // converter rebuild). beta54's rebuild fix is intentionally absent
    // here; if hypothesis #1 confirms, the proper fix targets the QML /
    // DComp surface layer instead. Sync beacon trail is preserved so
    // late tail-truncation in the async DebugLog doesn't lose the
    // breadcrumb when the crash recurs.
    const QMediaPlayer::MediaStatus status = player_->mediaStatus();
    const QMediaPlayer::PlaybackState playbackState = playerPlaybackState(player_);
    const qreal playerRateBefore = player_->playbackRate();
    const bool stable = status == QMediaPlayer::NoMedia
                     || status == QMediaPlayer::LoadedMedia
                     || status == QMediaPlayer::BufferedMedia
                     || status == QMediaPlayer::EndOfMedia;
    if (!stable) {
        pendingPlaybackRateApply_ = true;
        {
            char buf[240];
            std::snprintf(buf, sizeof(buf),
                "preview/rate/qt_host_deferred tid=%lu rate=%.3f status=%d state=%d player_rate=%.3f pos=%lld",
                currentBeaconTid(),
                playbackRate_,
                static_cast<int>(status),
                static_cast<int>(playbackState),
                static_cast<double>(playerRateBefore),
                static_cast<long long>(player_->position()));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        appendPreviewStageMediaLog(
            QStringLiteral("playback_rate_deferred"),
            QString("rate=%1 status=%2 kind=%3")
                .arg(playbackRate_, 0, 'f', 3)
                .arg(mediaStatusName(status))
                .arg(debugMediaTypeName()));
        return;
    }
    pendingPlaybackRateApply_ = false;
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/rate/qt_host_about_to_setrate tid=%lu rate=%.3f old=%.3f status=%d state=%d player_rate=%.3f pos=%lld",
            currentBeaconTid(),
            playbackRate_,
            oldRate,
            static_cast<int>(status),
            static_cast<int>(playbackState),
            static_cast<double>(playerRateBefore),
            static_cast<long long>(player_->position()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
    {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/rate/qt_host_setrate_done tid=%lu rate=%.3f player_rate=%.3f status=%d state=%d pos=%lld",
            currentBeaconTid(),
            playbackRate_,
            static_cast<double>(player_->playbackRate()),
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)),
            static_cast<long long>(player_->position()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif
}

void PreviewStageMediaHost::setPlaybackTransactionId(quint64 transactionId)
{
    playbackTransactionId_ = transactionId;
}

void PreviewStageMediaHost::preparePlaybackStart(double seconds, quint64 transactionId)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    Q_UNUSED(transactionId);
#else
    initializeBackendObjects();
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();
    preparedPlaybackTransaction_ = transactionId;
    preparedPlaybackTargetSecond_ = clampedSecond;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    appendPreviewStageMediaLog(
        QStringLiteral("prepare_playback_start"),
        QString("txn=%1 second=%2 rate=%3 offset=%4 has_video=%5")
            .arg(transactionId)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(playbackRate_, 0, 'f', 3)
            .arg(timelineOffsetSeconds_, 0, 'f', 6)
            .arg(mediaKind_ == MediaKind::Video && player_ != nullptr ? 1 : 0));
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        emit playbackStartPrepared(clampedSecond, transactionId);
        emit diagnosticsChanged();
        return;
    }

    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    preparedPlaybackTargetMs_ = targetMs;
    preparedPlaybackPending_ = true;
    player_->pause();
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < 40) {
        QMetaObject::invokeMethod(
            this,
            [this, transactionId, clampedSecond]() {
                if (!preparedPlaybackPending_ || preparedPlaybackTransaction_ != transactionId) {
                    appendPreviewStageMediaLog(
                        QStringLiteral("prepare_playback_drop"),
                        QString("txn=%1 current_txn=%2 reason=queued_ack_stale")
                            .arg(transactionId)
                            .arg(preparedPlaybackTransaction_)
                    );
                    return;
                }
                preparedPlaybackPending_ = false;
                preparedPlaybackReady_ = true;
                appendPreviewStageMediaLog(
                    QStringLiteral("prepare_playback_ready"),
                    QString("txn=%1 second=%2 position_ms=%3 target_ms=%4 source=queued")
                        .arg(transactionId)
                        .arg(clampedSecond, 0, 'f', 6)
                        .arg(lastSeekMs_)
                        .arg(preparedPlaybackTargetMs_)
                );
                emit playbackStartPrepared(clampedSecond, transactionId);
            },
            Qt::QueuedConnection
        );
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    schedulePreparedPlaybackTimeout(transactionId, targetMs);
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::commitPreparedPlaybackStart(double currentTimelineSecond)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(currentTimelineSecond);
#else
    initializeBackendObjects();
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return;
    }

    const double clampedSecond = qMax(0.0, currentTimelineSecond);
    observedPlayheadSecond_ = clampedSecond;
    lastTimelineSecond_ = clampedSecond;
    const double rawSecond = clampedSecond + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        player_->pause();
        videoPlaybackPendingStart_ = true;
        videoPlaybackActive_ = false;
        videoPlaybackActiveElapsed_.invalidate();
        appendPreviewStageMediaLog(
            QStringLiteral("commit_prepared_playback_pending"),
            QString("txn=%1 second=%2 raw_second=%3")
                .arg(playbackTransactionId_)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6));
        preparedPlaybackPending_ = false;
        preparedPlaybackReady_ = false;
        preparedPlaybackTargetMs_ = -1;
        preparedPlaybackTargetSecond_ = 0.0;
        preparedPlaybackTransaction_ = 0;
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    if (playerPlaybackState(player_) != QMediaPlayer::PlayingState) {
        QMetaObject::invokeMethod(player_, [this]() {
            if (mediaKind_ == MediaKind::Video && videoPlaybackActive_) {
                player_->play();
            }
        }, Qt::QueuedConnection);
    }
    appendPreviewStageMediaLog(
        QStringLiteral("commit_prepared_playback"),
        QString("txn=%1 second=%2 raw_second=%3 target_ms=%4 late=%5")
            .arg(playbackTransactionId_)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(qAbs(clampedSecond - preparedPlaybackTargetSecond_) > 0.0005 ? 1 : 0));
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    updateClockDelta();
    updateVideoFrameStallState(true);
    scheduleVideoPlaybackWatchdog(QStringLiteral("commit_prepared_playback"));
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::cancelPreparedPlaybackStart(quint64 transactionId)
{
    if (transactionId != 0 && preparedPlaybackTransaction_ != transactionId) {
        return;
    }
    if (!preparedPlaybackPending_ && !preparedPlaybackReady_ && preparedPlaybackTransaction_ == 0) {
        return;
    }
    appendPreviewStageMediaLog(
        QStringLiteral("prepare_playback_cancel"),
        QString("txn=%1 pending=%2 ready=%3")
            .arg(preparedPlaybackTransaction_)
            .arg(preparedPlaybackPending_ ? 1 : 0)
            .arg(preparedPlaybackReady_ ? 1 : 0));
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
}

bool PreviewStageMediaHost::hasPreparedPlaybackStart(quint64 transactionId) const
{
    return preparedPlaybackReady_ && preparedPlaybackTransaction_ == transactionId;
}

void PreviewStageMediaHost::setTimelineOffsetSeconds(double seconds)
{
    timelineOffsetSeconds_ = qIsFinite(seconds) ? seconds : 0.0;
    updateClockDelta();
    emit diagnosticsChanged();
}

void PreviewStageMediaHost::setPlayheadSeconds(double seconds)
{
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();

#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < 40) {
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::startPlayback(double seconds)
{
    MC_OP("PreviewStageMediaHost::startPlayback");
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
#else
    initializeBackendObjects();
    observedPlayheadSecond_ = qMax(0.0, seconds);
    appendPreviewStageMediaLog(
        QStringLiteral("start_playback"),
        QString("txn=%1 second=%2 raw_second=%3 rate=%4 offset=%5 has_video=%6")
            .arg(playbackTransactionId_)
            .arg(observedPlayheadSecond_, 0, 'f', 6)
            .arg(observedPlayheadSecond_ + timelineOffsetSeconds_, 0, 'f', 6)
            .arg(playbackRate_, 0, 'f', 3)
            .arg(timelineOffsetSeconds_, 0, 'f', 6)
            .arg(mediaKind_ == MediaKind::Video && player_ != nullptr ? 1 : 0));
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }
    syncVideoFrameBeaconBudget_ = qMax(syncVideoFrameBeaconBudget_, 24);
    syncMediaStatusBeaconBudget_ = qMax(syncMediaStatusBeaconBudget_, 16);

    lastTimelineSecond_ = qMax(0.0, seconds);
    const double rawSecond = seconds + timelineOffsetSeconds_;
    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/play/start_before_set_position tid=%lu txn=%llu target_ms=%lld rate=%.3f status=%d state=%d pos=%lld",
            currentBeaconTid(),
            static_cast<unsigned long long>(playbackTransactionId_),
            static_cast<long long>(targetMs),
            playbackRate_,
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)),
            static_cast<long long>(player_->position()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->setPosition(targetMs);
    {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/play/start_after_set_position tid=%lu txn=%llu pos=%lld status=%d state=%d",
            currentBeaconTid(),
            static_cast<unsigned long long>(playbackTransactionId_),
            static_cast<long long>(player_->position()),
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (rawSecond < 0.0) {
        miacode::oplog::appendStartupBeaconLine("preview/play/start_before_pause_negative_raw");
        player_->pause();
        miacode::oplog::appendStartupBeaconLine("preview/play/start_after_pause_negative_raw");
        videoPlaybackPendingStart_ = true;
        videoPlaybackActive_ = false;
        videoPlaybackActiveElapsed_.invalidate();
        appendPreviewStageMediaLog(
            QStringLiteral("start_playback_pending"),
            QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
                .arg(playbackTransactionId_)
                .arg(seconds, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(targetMs));
    } else {
        if (videoFrameElapsed_.isValid()) {
            videoFrameElapsed_.restart();
        }
        {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/play/start_before_play tid=%lu txn=%llu rate=%.3f status=%d state=%d",
                currentBeaconTid(),
                static_cast<unsigned long long>(playbackTransactionId_),
                playbackRate_,
                static_cast<int>(player_->mediaStatus()),
                static_cast<int>(playerPlaybackState(player_)));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        player_->play();
        {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/play/start_after_play tid=%lu txn=%llu status=%d state=%d pos=%lld",
                currentBeaconTid(),
                static_cast<unsigned long long>(playbackTransactionId_),
                static_cast<int>(player_->mediaStatus()),
                static_cast<int>(playerPlaybackState(player_)),
                static_cast<long long>(player_->position()));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        videoPlaybackActive_ = true;
        videoPlaybackPendingStart_ = false;
        videoPlaybackActiveElapsed_.restart();
        consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
        if (playerPlaybackState(player_) != QMediaPlayer::PlayingState) {
            QMetaObject::invokeMethod(player_, [this]() {
                if (mediaKind_ == MediaKind::Video && videoPlaybackActive_) {
                    player_->play();
                }
            }, Qt::QueuedConnection);
        }
        appendPreviewStageMediaLog(
            QStringLiteral("start_playback_started"),
            QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
                .arg(playbackTransactionId_)
                .arg(seconds, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(targetMs));
        scheduleVideoPlaybackWatchdog(QStringLiteral("start_playback"));
    }
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::submitPausedSeek(double seconds, quint64 generation)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    Q_UNUSED(generation);
#else
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        emit pausedSeekCompleted(clampedSecond, generation);
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    pausedSeekGeneration_ = generation;
    pausedSeekTargetMs_ = targetMs;
    pausedSeekTargetSecond_ = clampedSecond;
    pausedSeekCompletionPending_ = true;
    appendPreviewStageMediaLog(
        QStringLiteral("paused_seek_media_submit"),
        QString("generation=%1 second=%2 target_ms=%3")
            .arg(generation)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(targetMs)
    );
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < 40) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, clampedSecond]() {
                if (!pausedSeekCompletionPending_ || pausedSeekGeneration_ != generation) {
                    appendPreviewStageMediaLog(
                        QStringLiteral("paused_seek_media_drop"),
                        QString("generation=%1 current_generation=%2 reason=queued_ack_stale")
                            .arg(generation)
                            .arg(pausedSeekGeneration_)
                    );
                    return;
                }
                pausedSeekCompletionPending_ = false;
                appendPreviewStageMediaLog(
                    QStringLiteral("paused_seek_media_ack"),
                    QString("generation=%1 second=%2 position_ms=%3 target_ms=%4 source=queued")
                        .arg(generation)
                        .arg(clampedSecond, 0, 'f', 6)
                        .arg(lastSeekMs_)
                        .arg(pausedSeekTargetMs_)
                );
                emit pausedSeekCompleted(clampedSecond, generation);
            },
            Qt::QueuedConnection
        );
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    schedulePausedSeekTimeout(generation, targetMs);
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::syncPlayback(double seconds)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
#else
    initializeBackendObjects();
    observedPlayheadSecond_ = qMax(0.0, seconds);
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = qMax(0.0, seconds);
    if (!videoPlaybackPendingStart_) {
        const bool wasPlaying = playerPlaybackState(player_) == QMediaPlayer::PlayingState;
        bool transitioned = false;
        if (videoPlaybackActive_ && !wasPlaying) {
            player_->play();
            transitioned = true;
        }
        updateClockDelta();
        const bool stallStateChanged = updateVideoFrameStallState(true);
        // Steady-state syncPlayback is called every preview tick (~60/s). Emitting
        // diagnosticsChanged unconditionally here fans out to refreshPreviewStageMediaRouteDebugState
        // which re-pushes media stats into frameState on every tick even though values seldom change.
        // Limit the emission to actual state transitions; other paths (noteVideoFrameArrived,
        // pause, recover, stall transitions) continue to emit when something genuinely changed.
        if (transitioned || stallStateChanged) {
            emit diagnosticsChanged();
        }
        return;
    }

    const double rawSecond = seconds + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    appendPreviewStageMediaLog(
        QStringLiteral("sync_playback_started"),
        QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
            .arg(playbackTransactionId_)
            .arg(seconds, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(targetMs));
    scheduleVideoPlaybackWatchdog(QStringLiteral("sync_playback_started"));
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::pausePlayback()
{
    observedPlayheadSecond_ = currentPlaybackSecond();
#ifndef HAVE_QT_MULTIMEDIA
    return;
#else
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        player_->pause();
    }
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    ++videoPlaybackWatchdogSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
#endif
}

double PreviewStageMediaHost::currentPlaybackSecond() const
{
#ifndef HAVE_QT_MULTIMEDIA
    return 0.0;
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return 0.0;
    }
    if (!videoPlaybackActive_) {
        return qMax(0.0, lastTimelineSecond_);
    }
    return qMax(0.0, static_cast<double>(player_->position()) / 1000.0 - timelineOffsetSeconds_);
#endif
}

bool PreviewStageMediaHost::videoPlaybackActive() const
{
    return mediaKind_ == MediaKind::Video && videoPlaybackActive_;
}

bool PreviewStageMediaHost::hasVideoFrame() const
{
    return mediaKind_ == MediaKind::Video && videoFrameElapsed_.isValid();
}

double PreviewStageMediaHost::clockDeltaSeconds() const
{
    return clockDeltaSeconds_;
}

qint64 PreviewStageMediaHost::videoFrameAgeMs() const
{
    if (!hasVideoMedia()) {
        return -1;
    }
    return currentVideoFrameAgeForDiagnosticsMs();
}

qint64 PreviewStageMediaHost::videoFrameCountTotal() const
{
    return videoFrameCountTotal_;
}

double PreviewStageMediaHost::videoFrameRateEstimate() const
{
    return fpsFromAverageMs(videoFrameIntervalAvgMs());
}

double PreviewStageMediaHost::videoFrameIntervalAvgMs() const
{
    return averageOrZero(videoFrameIntervalSumMs_, videoFrameIntervalSampleCount_);
}

double PreviewStageMediaHost::videoFrameIntervalMaxMs() const
{
    return videoFrameIntervalMaxMs_;
}

qint64 PreviewStageMediaHost::videoFrameStallCount() const
{
    return videoFrameStallCount_;
}

bool PreviewStageMediaHost::videoFrameStalled() const
{
    return videoFrameStalled_;
}

void PreviewStageMediaHost::setVideoFrameToImageMaxFps(double fps)
{
    const double normalized = qBound(1.0, qIsFinite(fps) ? fps : 30.0, 1000.0);
    if (qAbs(videoFrameToImageMaxFps_ - normalized) <= 0.001) {
        return;
    }
    videoFrameToImageMaxFps_ = normalized;
    videoFrameToImageThrottle_.invalidate();
}

void PreviewStageMediaHost::setObservedPlayheadSecond(double second)
{
    observedPlayheadSecond_ = qMax(0.0, second);
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
}

QString PreviewStageMediaHost::debugMediaTypeName() const
{
    switch (mediaKind_) {
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::None:
    default:
        return QStringLiteral("none");
    }
}

void PreviewStageMediaHost::clearMedia()
{
#ifdef HAVE_QT_MULTIMEDIA
    if (videoSinkFrameConnection_) {
        QObject::disconnect(videoSinkFrameConnection_);
        videoSinkFrameConnection_ = QMetaObject::Connection();
    }
    videoSink_.clear();
    if (player_ != nullptr) {
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        player_->stop();
        player_->setSource(QUrl());
    }
#endif
    // Phase 4c-8 — clear the cached image so a chart switch from
    // image→video doesn't leave the old image visible behind the
    // new video.
    loadedBackgroundImage_ = QImage();
    ++videoSourceGeneration_;
    mediaKind_ = MediaKind::None;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    mediaPath_.clear();
    imageSource_ = QUrl();
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    ++videoPlaybackWatchdogSerial_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    observedPlayheadSecond_ = 0.0;
    clockDeltaSeconds_ = 0.0;
    resetVideoFrameDiagnostics();
    if (shuttingDown_) {
        return;
    }
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
}

QString PreviewStageMediaHost::resolveMediaPath(const QString& chartPath) const
{
    const QString normalizedChartPath = normalizedLocalPath(chartPath);
    if (normalizedChartPath == warmupChartPath_) {
        return warmupMediaPath_;
    }
    return miacode::chart_assets::resolveBackgroundMediaPath(chartPath);
}

void PreviewStageMediaHost::loadImageMedia(const QString& path)
{
    imageSource_ = QUrl::fromLocalFile(path);
    // Phase 4c-8 — also load the QImage so DComp's StageBackgroundSource
    // can render it. The QML PreviewStageMediaItem still binds to
    // imageSource_ but is occluded by the DComp HWND on top; the DComp
    // surface needs its own copy to actually paint the bg in the
    // chart-preview area.
    loadedBackgroundImage_ = QImage(path);
    if (loadedBackgroundImage_.isNull()) {
        appendPreviewStageMediaLog(
            QStringLiteral("load_image_failed"),
            QStringLiteral("path=%1").arg(path));
    }
    mediaKind_ = MediaKind::Image;
    ++videoSourceGeneration_;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    ++videoPlaybackWatchdogSerial_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    resetVideoFrameDiagnostics();
    updateClockDelta();
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
}

void PreviewStageMediaHost::loadVideoMedia(const QString& path)
{
    MC_OP("PreviewStageMediaHost::loadVideoMedia");
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(path);
#else
    initializeBackendObjects();
    if (player_ == nullptr) {
        return;
    }

    imageSource_ = QUrl();
    // Phase 4c-9 — clear stale image bg from a prior chart so the
    // DComp surface paints nothing until the first decoded video
    // frame arrives via noteVideoFrameArrived(). Also invalidate
    // the toImage() throttle so the very first frame after this
    // chart switch is captured immediately (otherwise a recently-
    // armed throttle from the previous chart could delay it up to
    // 33ms on top of the QMediaPlayer prepare latency).
    loadedBackgroundImage_ = QImage();
    videoFrameToImageThrottle_.invalidate();
    mediaKind_ = MediaKind::Video;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    ++videoPlaybackWatchdogSerial_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    resetVideoFrameDiagnostics();
    syncMediaStatusBeaconBudget_ = qMax(syncMediaStatusBeaconBudget_, 12);
    syncVideoFrameBeaconBudget_ = qMax(syncVideoFrameBeaconBudget_, 8);
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/load_video/before_bind tid=%lu rate=%.3f status=%d state=%d",
            currentBeaconTid(),
            playbackRate_,
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    bindVideoOutput();
    miacode::oplog::appendStartupBeaconLine("preview/load_video/after_bind");
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/load_video/before_set_source tid=%lu rate=%.3f path_len=%d",
            currentBeaconTid(),
            playbackRate_,
            static_cast<int>(path.size()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->setSource(QUrl::fromLocalFile(path));
    {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/load_video/after_set_source tid=%lu status=%d state=%d",
            currentBeaconTid(),
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->pause();
    miacode::oplog::appendStartupBeaconLine("preview/load_video/after_pause");
    player_->setPosition(0);
    miacode::oplog::appendStartupBeaconLine("preview/load_video/after_set_position_zero");
    updateClockDelta();
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::bindVideoOutput()
{
#ifndef HAVE_QT_MULTIMEDIA
    return;
#else
    if (videoSinkFrameConnection_) {
        QObject::disconnect(videoSinkFrameConnection_);
        videoSinkFrameConnection_ = QMetaObject::Connection();
    }
    videoSink_.clear();

    if (player_ == nullptr) {
        return;
    }

    if (videoOutputObject_ == nullptr) {
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/before_clear_output");
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/after_clear_output");
        appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=0 sink=0"));
        return;
    }

    miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/before_set_output_object");
    player_->setVideoOutput(videoOutputObject_);
    miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/after_set_output_object");

    QObject* sinkObject = nullptr;
    const QVariant sinkVariant = videoOutputObject_->property("videoSink");
    if (sinkVariant.isValid()) {
        sinkObject = sinkVariant.value<QObject*>();
    }
    videoSink_ = qobject_cast<QVideoSink*>(sinkObject);
    if (videoSink_ != nullptr) {
        const quint64 sourceGeneration = videoSourceGeneration_;
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/before_connect_sink");
        videoSinkFrameConnection_ = QObject::connect(videoSink_, &QVideoSink::videoFrameChanged, this, [this, sourceGeneration](const QVideoFrame& frame) {
            if (syncVideoFrameBeaconBudget_ > 0) {
                char buf[180];
                std::snprintf(buf, sizeof(buf),
                    "preview/frame/sink_signal tid=%lu source_gen=%llu",
                    currentBeaconTid(),
                    static_cast<unsigned long long>(sourceGeneration));
                miacode::oplog::appendStartupBeaconLine(buf);
            }
            noteVideoFrameArrived(frame, sourceGeneration);
        });
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/after_connect_sink");
        appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=1 sink=1"));
        return;
    }

    appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=1 sink=0"));
#endif
}

bool PreviewStageMediaHost::recoverVideoBackend(const QString& reason, double targetSecond, bool resumePlayback)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(reason);
    Q_UNUSED(targetSecond);
    Q_UNUSED(resumePlayback);
    return false;
#else
    if (recoveringVideoBackend_
        || shuttingDown_
        || mediaKind_ != MediaKind::Video
        || mediaPath_.isEmpty()) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_backend_recover_skip"),
            QString("reason=%1 recovering=%2 shutting_down=%3 kind=%4 has_path=%5")
                .arg(reason)
                .arg(recoveringVideoBackend_ ? 1 : 0)
                .arg(shuttingDown_ ? 1 : 0)
                .arg(debugMediaTypeName())
                .arg(mediaPath_.isEmpty() ? 0 : 1)
        );
        return false;
    }
    if (consecutiveVideoBackendRecoveryCount_ >= kVideoBackendRecoveryMaxConsecutive) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_backend_recover_skip"),
            QString("reason=%1 recovery_count=%2 limit=%3 frame_count=%4")
                .arg(reason)
                .arg(consecutiveVideoBackendRecoveryCount_)
                .arg(kVideoBackendRecoveryMaxConsecutive)
                .arg(videoFrameCountTotal_)
        );
        return false;
    }

    const QString path = mediaPath_;
    const double clampedTargetSecond = qMax(0.0, qIsFinite(targetSecond) ? targetSecond : currentPlaybackSecond());
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedTargetSecond + timelineOffsetSeconds_) * 1000.0));
    const bool completePausedSeek = pausedSeekCompletionPending_;
    const quint64 pausedSeekGeneration = pausedSeekGeneration_;
    const double pausedSeekSecond = pausedSeekTargetSecond_;
    const bool completePreparedPlayback = preparedPlaybackPending_;
    const quint64 preparedTransaction = preparedPlaybackTransaction_;
    const double preparedSecond = preparedPlaybackTargetSecond_;

    ++consecutiveVideoBackendRecoveryCount_;
    ++pausedSeekTimeoutSerial_;
    ++preparedPlaybackTimeoutSerial_;
    ++videoPlaybackWatchdogSerial_;
    pausedSeekCompletionPending_ = false;
    preparedPlaybackPending_ = false;
    if (completePreparedPlayback) {
        preparedPlaybackReady_ = true;
    }

    appendPreviewStageMediaLog(
        QStringLiteral("video_backend_recover_begin"),
        QString("reason=%1 recovery_count=%2 target_second=%3 target_ms=%4 resume=%5 frame_count=%6 age_ms=%7 playback_state=%8 position_ms=%9 sink=%10")
            .arg(reason)
            .arg(consecutiveVideoBackendRecoveryCount_)
            .arg(clampedTargetSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(resumePlayback ? 1 : 0)
            .arg(videoFrameCountTotal_)
            .arg(currentVideoFrameAgeForDiagnosticsMs())
            .arg(playbackStateName(playerPlaybackState(player_)))
            .arg(player_ != nullptr ? player_->position() : -1)
            .arg(videoSink_ != nullptr ? 1 : 0)
    );

    recoveringVideoBackend_ = true;
    if (videoSinkFrameConnection_) {
        QObject::disconnect(videoSinkFrameConnection_);
        videoSinkFrameConnection_ = QMetaObject::Connection();
    }
    videoSink_.clear();
    if (player_ != nullptr) {
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        player_->stop();
        player_->setSource(QUrl());
        player_->setAudioOutput(nullptr);
        delete player_;
        player_ = nullptr;
    }
    if (audioOutput_ != nullptr) {
        delete audioOutput_;
        audioOutput_ = nullptr;
    }

    initializeBackendObjects();
    if (player_ == nullptr) {
        recoveringVideoBackend_ = false;
        appendPreviewStageMediaLog(
            QStringLiteral("video_backend_recover_failed"),
            QString("reason=%1 target_second=%2")
                .arg(reason)
                .arg(clampedTargetSecond, 0, 'f', 6)
        );
        return false;
    }

    imageSource_ = QUrl();
    mediaKind_ = MediaKind::Video;
    ++videoSourceGeneration_;
    resetVideoFrameDiagnostics();
    observedPlayheadSecond_ = clampedTargetSecond;
    lastTimelineSecond_ = clampedTargetSecond;
    lastSeekMs_ = targetMs;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    bindVideoOutput();
    player_->setSource(QUrl::fromLocalFile(path));
    player_->pause();
    player_->setPosition(targetMs);
    if (resumePlayback) {
        if (videoFrameElapsed_.isValid()) {
            videoFrameElapsed_.restart();
        }
        player_->play();
        videoPlaybackActive_ = true;
        videoPlaybackActiveElapsed_.restart();
        consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
        scheduleVideoPlaybackWatchdog(QStringLiteral("backend_recover_resume"));
    }
    updateClockDelta();
    recoveringVideoBackend_ = false;

    appendPreviewStageMediaLog(
        QStringLiteral("video_backend_recover_end"),
        QString("reason=%1 target_second=%2 target_ms=%3 resume=%4 playback_state=%5 sink=%6")
            .arg(reason)
            .arg(clampedTargetSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(resumePlayback ? 1 : 0)
            .arg(playbackStateName(playerPlaybackState(player_)))
            .arg(videoSink_ != nullptr ? 1 : 0)
    );
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
    if (completePausedSeek) {
        appendPreviewStageMediaLog(
            QStringLiteral("paused_seek_media_ack"),
            QString("generation=%1 second=%2 target_ms=%3 source=recover")
                .arg(pausedSeekGeneration)
                .arg(pausedSeekSecond, 0, 'f', 6)
                .arg(pausedSeekTargetMs_)
        );
        emit pausedSeekCompleted(pausedSeekSecond, pausedSeekGeneration);
    }
    if (completePreparedPlayback) {
        appendPreviewStageMediaLog(
            QStringLiteral("prepare_playback_ready"),
            QString("txn=%1 second=%2 target_ms=%3 source=recover")
                .arg(preparedTransaction)
                .arg(preparedSecond, 0, 'f', 6)
                .arg(preparedPlaybackTargetMs_)
        );
        emit playbackStartPrepared(preparedSecond, preparedTransaction);
    }
    return true;
#endif
}

void PreviewStageMediaHost::schedulePreparedPlaybackTimeout(quint64 transactionId, qint64 targetMs)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(transactionId);
    Q_UNUSED(targetMs);
#else
    const quint64 serial = ++preparedPlaybackTimeoutSerial_;
    QTimer::singleShot(kMediaSeekPrepareTimeoutMs, this, [this, serial, transactionId, targetMs]() {
        if (serial != preparedPlaybackTimeoutSerial_
            || !preparedPlaybackPending_
            || preparedPlaybackTransaction_ != transactionId
            || preparedPlaybackTargetMs_ != targetMs) {
            return;
        }
        const double targetSecond = preparedPlaybackTargetSecond_;
        appendPreviewStageMediaLog(
            QStringLiteral("prepare_playback_timeout"),
            QString("txn=%1 second=%2 target_ms=%3 position_ms=%4 frame_count=%5 age_ms=%6 playback_state=%7")
                .arg(transactionId)
                .arg(targetSecond, 0, 'f', 6)
                .arg(targetMs)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(videoFrameCountTotal_)
                .arg(currentVideoFrameAgeForDiagnosticsMs())
                .arg(playbackStateName(playerPlaybackState(player_)))
        );
        preparedPlaybackPending_ = false;
        preparedPlaybackReady_ = true;
        ++preparedPlaybackTimeoutSerial_;
        recoverVideoBackend(QStringLiteral("prepare_playback_timeout"), targetSecond, false);
        if (preparedPlaybackReady_ && preparedPlaybackTransaction_ == transactionId) {
            emit playbackStartPrepared(targetSecond, transactionId);
        }
    });
#endif
}

void PreviewStageMediaHost::schedulePausedSeekTimeout(quint64 generation, qint64 targetMs)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(generation);
    Q_UNUSED(targetMs);
#else
    const quint64 serial = ++pausedSeekTimeoutSerial_;
    QTimer::singleShot(kMediaSeekPrepareTimeoutMs, this, [this, serial, generation, targetMs]() {
        if (serial != pausedSeekTimeoutSerial_
            || !pausedSeekCompletionPending_
            || pausedSeekGeneration_ != generation
            || pausedSeekTargetMs_ != targetMs) {
            return;
        }
        const double targetSecond = pausedSeekTargetSecond_;
        appendPreviewStageMediaLog(
            QStringLiteral("paused_seek_media_timeout"),
            QString("generation=%1 second=%2 target_ms=%3 position_ms=%4 frame_count=%5 age_ms=%6 playback_state=%7")
                .arg(generation)
                .arg(targetSecond, 0, 'f', 6)
                .arg(targetMs)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(videoFrameCountTotal_)
                .arg(currentVideoFrameAgeForDiagnosticsMs())
                .arg(playbackStateName(playerPlaybackState(player_)))
        );
        const bool recovered = recoverVideoBackend(QStringLiteral("paused_seek_timeout"), targetSecond, false);
        if (!recovered
            && pausedSeekCompletionPending_
            && pausedSeekGeneration_ == generation
            && pausedSeekTargetMs_ == targetMs) {
            pausedSeekCompletionPending_ = false;
            ++pausedSeekTimeoutSerial_;
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_ack"),
                QString("generation=%1 second=%2 target_ms=%3 source=timeout_no_recover")
                    .arg(generation)
                    .arg(targetSecond, 0, 'f', 6)
                    .arg(targetMs)
            );
            emit pausedSeekCompleted(targetSecond, generation);
        }
    });
#endif
}

void PreviewStageMediaHost::scheduleVideoPlaybackWatchdog(const QString& reason)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(reason);
#else
    if (mediaKind_ != MediaKind::Video || !videoPlaybackActive_) {
        return;
    }
    const quint64 serial = ++videoPlaybackWatchdogSerial_;
    const qint64 frameCount = videoFrameCountTotal_;
    const double targetSecond = observedPlayheadSecond_;
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/watchdog/schedule tid=%lu serial=%llu reason=%s frame_count=%lld age_ms=%lld rate=%.3f",
            currentBeaconTid(),
            static_cast<unsigned long long>(serial),
            reason.toUtf8().constData(),
            static_cast<long long>(frameCount),
            static_cast<long long>(currentVideoFrameAgeForDiagnosticsMs()),
            playbackRate_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    QTimer::singleShot(kVideoPlaybackWatchdogMs, this, [this, serial, frameCount, targetSecond, reason]() {
        if (serial != videoPlaybackWatchdogSerial_
            || mediaKind_ != MediaKind::Video
            || !videoPlaybackActive_) {
            miacode::oplog::appendStartupBeaconLine("preview/watchdog/skip_stale_or_inactive");
            return;
        }
        const qint64 ageMs = currentVideoFrameAgeForDiagnosticsMs();
        const bool noNewFrame = videoFrameCountTotal_ <= frameCount;
        const bool staleFrame = ageMs >= kVideoPlaybackWatchdogMs;
        const bool notPlaying = playerPlaybackState(player_) != QMediaPlayer::PlayingState;
        if (!noNewFrame && !staleFrame && !notPlaying) {
            consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
            miacode::oplog::appendStartupBeaconLine("preview/watchdog/healthy");
            return;
        }
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_watchdog_timeout"),
            QString("reason=%1 target_second=%2 observed_second=%3 no_new_frame=%4 stale_frame=%5 not_playing=%6 frame_count=%7 initial_frame_count=%8 age_ms=%9 playback_state=%10 soft_recovery_count=%11")
                .arg(reason)
                .arg(targetSecond, 0, 'f', 6)
                .arg(observedPlayheadSecond_, 0, 'f', 6)
                .arg(noNewFrame ? 1 : 0)
                .arg(staleFrame ? 1 : 0)
                .arg(notPlaying ? 1 : 0)
                .arg(videoFrameCountTotal_)
                .arg(frameCount)
                .arg(ageMs)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(consecutiveVideoPlaybackSoftRecoveryCount_)
        );
        trySoftRecoverVideoPlayback(
            QStringLiteral("playback_watchdog_%1").arg(reason),
            observedPlayheadSecond_,
            frameCount,
            ageMs);
    });
#endif
}

bool PreviewStageMediaHost::trySoftRecoverVideoPlayback(const QString& reason,
                                                        double targetSecond,
                                                        qint64 initialFrameCount,
                                                        qint64 ageMs)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(reason);
    Q_UNUSED(targetSecond);
    Q_UNUSED(initialFrameCount);
    Q_UNUSED(ageMs);
    return false;
#else
    if (shuttingDown_
        || mediaKind_ != MediaKind::Video
        || player_ == nullptr
        || !videoPlaybackActive_) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_soft_recover_skip"),
            QString("reason=%1 shutting_down=%2 kind=%3 has_player=%4 active=%5")
                .arg(reason)
                .arg(shuttingDown_ ? 1 : 0)
                .arg(debugMediaTypeName())
                .arg(player_ != nullptr ? 1 : 0)
                .arg(videoPlaybackActive_ ? 1 : 0));
        return false;
    }
    if (consecutiveVideoPlaybackSoftRecoveryCount_ >= kVideoPlaybackSoftRecoveryMaxConsecutive) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_soft_recover_exhausted"),
            QString("reason=%1 count=%2 limit=%3 frame_count=%4 initial_frame_count=%5 age_ms=%6 playback_state=%7 position_ms=%8")
                .arg(reason)
                .arg(consecutiveVideoPlaybackSoftRecoveryCount_)
                .arg(kVideoPlaybackSoftRecoveryMaxConsecutive)
                .arg(videoFrameCountTotal_)
                .arg(initialFrameCount)
                .arg(ageMs)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_->position()));
        miacode::oplog::appendStartupBeaconLine("preview/watchdog/soft_recover_exhausted");
        return false;
    }

    const int step = consecutiveVideoPlaybackSoftRecoveryCount_++;
    const QString strategy = step == 0
        ? QStringLiteral("seek_flush")
        : QStringLiteral("rebind_sink_seek_flush");
    const double clampedTargetSecond = qMax(0.0, qIsFinite(targetSecond) ? targetSecond : currentPlaybackSecond());
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedTargetSecond + timelineOffsetSeconds_) * 1000.0));
    const qint64 flushMs = targetMs + 1;
    ++videoPlaybackWatchdogSerial_;
    syncVideoFrameBeaconBudget_ = qMax(syncVideoFrameBeaconBudget_, 24);
    syncMediaStatusBeaconBudget_ = qMax(syncMediaStatusBeaconBudget_, 16);

    {
        char buf[300];
        std::snprintf(buf, sizeof(buf),
            "preview/watchdog/soft_recover_begin tid=%lu step=%d strategy=%s target_ms=%lld pos=%lld frame_count=%lld age_ms=%lld rate=%.3f",
            currentBeaconTid(),
            step,
            strategy.toUtf8().constData(),
            static_cast<long long>(targetMs),
            static_cast<long long>(player_->position()),
            static_cast<long long>(videoFrameCountTotal_),
            static_cast<long long>(ageMs),
            playbackRate_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    appendPreviewStageMediaLog(
        QStringLiteral("video_playback_soft_recover_begin"),
        QString("reason=%1 step=%2 strategy=%3 target_second=%4 target_ms=%5 flush_ms=%6 frame_count=%7 initial_frame_count=%8 age_ms=%9 playback_state=%10 position_ms=%11")
            .arg(reason)
            .arg(step)
            .arg(strategy)
            .arg(clampedTargetSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(flushMs)
            .arg(videoFrameCountTotal_)
            .arg(initialFrameCount)
            .arg(ageMs)
            .arg(playbackStateName(playerPlaybackState(player_)))
            .arg(player_->position()));

    if (step == 1) {
        if (videoSinkFrameConnection_) {
            QObject::disconnect(videoSinkFrameConnection_);
            videoSinkFrameConnection_ = QMetaObject::Connection();
        }
        videoSink_.clear();
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        bindVideoOutput();
    }

    player_->pause();
    player_->setPosition(flushMs);
    videoPlaybackActive_ = true;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.restart();
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }

    QTimer::singleShot(0, this, [this, targetMs, reason, strategy]() {
        if (shuttingDown_
            || mediaKind_ != MediaKind::Video
            || player_ == nullptr
            || !videoPlaybackActive_) {
            return;
        }
        player_->setPosition(targetMs);
        player_->play();
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_soft_recover_play"),
            QString("reason=%1 strategy=%2 target_ms=%3 playback_state=%4 position_ms=%5")
                .arg(reason)
                .arg(strategy)
                .arg(targetMs)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_->position()));
        miacode::oplog::appendStartupBeaconLine("preview/watchdog/soft_recover_play");
        scheduleVideoPlaybackWatchdog(QStringLiteral("soft_recover_%1").arg(strategy));
    });
    return true;
#endif
}

void PreviewStageMediaHost::updateClockDelta()
{
    if (!hasVideoMedia()) {
        clockDeltaSeconds_ = 0.0;
        return;
    }
    clockDeltaSeconds_ = currentPlaybackSecond() - observedPlayheadSecond_;
}

void PreviewStageMediaHost::noteVideoFrameArrived(const QVideoFrame& frame, quint64 sourceGeneration)
{
    MC_OP("PreviewStageMediaHost::noteVideoFrameArrived");
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(frame);
    Q_UNUSED(sourceGeneration);
#else
    const bool syncFrameBeacon = syncVideoFrameBeaconBudget_ > 0;
    if (syncFrameBeacon) {
        char buf[300];
        std::snprintf(buf, sizeof(buf),
            "preview/frame/arrived_enter tid=%lu valid=%d source_gen=%llu current_gen=%llu count=%lld start_us=%lld end_us=%lld active=%d rate=%.3f",
            currentBeaconTid(),
            frame.isValid() ? 1 : 0,
            static_cast<unsigned long long>(sourceGeneration),
            static_cast<unsigned long long>(videoSourceGeneration_),
            static_cast<long long>(videoFrameCountTotal_),
            static_cast<long long>(frame.startTime()),
            static_cast<long long>(frame.endTime()),
            videoPlaybackActive_ ? 1 : 0,
            playbackRate_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (mediaKind_ != MediaKind::Video || sourceGeneration != videoSourceGeneration_) {
        if (syncFrameBeacon) {
            --syncVideoFrameBeaconBudget_;
            miacode::oplog::appendStartupBeaconLine("preview/frame/arrived_drop_stale");
        }
        appendPreviewStageMediaLog(
            QStringLiteral("video_frame_drop"),
            QString("reason=stale_source source_generation=%1 current_generation=%2 kind=%3")
                .arg(sourceGeneration)
                .arg(videoSourceGeneration_)
                .arg(debugMediaTypeName())
        );
        return;
    }
    if (!frame.isValid()) {
        if (syncFrameBeacon) {
            --syncVideoFrameBeaconBudget_;
            miacode::oplog::appendStartupBeaconLine("preview/frame/arrived_drop_invalid");
        }
        return;
    }
    // Phase 4c-9 — convert the QVideoFrame to a QImage and stash it
    // for DComp's StageBackgroundSource. The QML VideoOutput
    // underneath the DComp HWND is occluded (WS_EX_LAYERED + LWA_ALPHA
    // is opaque per-window), so DComp has to paint the frame itself.
    // QVideoFrame::toImage() is GUI-thread safe (this slot runs on
    // GUI via the queued `videoFrameChanged` connection).
    //
    // Cost-mitigation gates (added because user reported the HUD's
    // stutter counter climbing to ~20 with video bg vs <5 with image):
    //   1. Skip when not visible — `mediaVisible_` gates the user's
    //      "Hide PV/BG while paused" setting; no point converting if
    //      the user has chosen to hide the bg.
    //   2. Throttle to ~30Hz max (33ms gap). For 30fps source video
    //      this means we capture roughly every other emission on
    //      avg; the rendered bg updates at ~15Hz, which is still
    //      visually fluid for a background and halves GUI thread
    //      cost. We always capture the FIRST frame after a
    //      visibility transition / chart switch (throttle not yet
    //      armed), so user actions don't get a stale frame.
    const qint64 videoFrameToImageThrottleNs =
        qMax<qint64>(1, qRound64(1000000000.0 / qMax(1.0, videoFrameToImageMaxFps_)));
    const bool throttledOut =
        videoFrameToImageThrottle_.isValid()
        && videoFrameToImageThrottle_.nsecsElapsed() < videoFrameToImageThrottleNs;
    // Phase 4d — when per-pixel alpha is on, QML's VideoOutput renders
    // the video natively (GPU-direct via QRhi), no CPU detour needed.
    // Skip the toImage() conversion entirely — that's the whole point
    // of per-pixel alpha: zero CPU cost for video bg.
    const bool skipForPerPixelAlpha =
        miacode::debug_options::previewDCompPerPixelAlphaEnabled();
    if (mediaVisible_ && !throttledOut && !skipForPerPixelAlpha) {
        if (syncFrameBeacon) {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/frame/before_to_image tid=%lu count=%lld pixel_format=%d",
                currentBeaconTid(),
                static_cast<long long>(videoFrameCountTotal_),
                static_cast<int>(frame.surfaceFormat().pixelFormat()));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        QImage decodedImage = frame.toImage();
        if (syncFrameBeacon) {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/frame/after_to_image tid=%lu null=%d size=%dx%d",
                currentBeaconTid(),
                decodedImage.isNull() ? 1 : 0,
                decodedImage.width(),
                decodedImage.height());
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        if (!decodedImage.isNull()) {
            loadedBackgroundImage_ = std::move(decodedImage);
            videoFrameToImageThrottle_.restart();
        }
    } else if (syncFrameBeacon) {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/frame/skip_to_image tid=%lu visible=%d throttled=%d per_pixel_alpha=%d",
            currentBeaconTid(),
            mediaVisible_ ? 1 : 0,
            throttledOut ? 1 : 0,
            skipForPerPixelAlpha ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    const bool firstFrameForSource = videoFrameCountTotal_ == 0;
    if (videoFrameElapsed_.isValid()) {
        const double intervalMs = static_cast<double>(videoFrameElapsed_.nsecsElapsed()) / 1000000.0;
        videoFrameIntervalSumMs_ += intervalMs;
        videoFrameIntervalMaxMs_ = qMax(videoFrameIntervalMaxMs_, intervalMs);
        videoFrameIntervalSampleCount_ += 1;
        if (!videoFrameIntervalsMs_.isEmpty()) {
            videoFrameIntervalsMs_[videoFrameIntervalWriteIndex_] = intervalMs;
            videoFrameIntervalWriteIndex_ = (videoFrameIntervalWriteIndex_ + 1) % videoFrameIntervalsMs_.size();
            videoFrameIntervalCount_ = qMin(videoFrameIntervalCount_ + 1, videoFrameIntervalsMs_.size());
        }
    }
    videoFrameElapsed_.restart();
    ++videoFrameCountTotal_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    if (firstFrameForSource) {
        // Frame size + surface format diagnostics are the missing piece for
        // the user-machine PV-scaling bug: mediaTargetRect / VideoOutput
        // fillMode use the source's reported pixel aspect, and that aspect
        // can flip portrait↔landscape between QtMultimedia backends when a
        // phone-captured PV carries rotation metadata (FFmpeg applies the
        // rotation, native backends often don't). frame.size() shows the
        // post-rotation (or raw) pixel size; surfaceFormat.frameSize() and
        // viewport() expose the underlying buffer + cropped region; the
        // rotation/mirrored flags expose what the backend reports the
        // frame still needs.
        const QVideoFrameFormat fmt = frame.surfaceFormat();
        const QSize frameSize = frame.size();
        const QSize fmtFrameSize = fmt.frameSize();
        const QRect viewport = fmt.viewport();
        const auto rotationDegrees = [](QtVideo::Rotation rotation) -> int {
            switch (rotation) {
            case QtVideo::Rotation::None: return 0;
            case QtVideo::Rotation::Clockwise90: return 90;
            case QtVideo::Rotation::Clockwise180: return 180;
            case QtVideo::Rotation::Clockwise270: return 270;
            }
            return -1;
        };
        appendPreviewStageMediaLog(
            QStringLiteral("video_frame_first"),
            QString(
                "frame_ms=%1 frame_end_ms=%2 player_position_ms=%3 target_seek_ms=%4 "
                "playback_active=%5 frame_size=%6x%7 fmt_size=%8x%9 "
                "viewport=%10,%11+%12x%13 pixel_format=%14 rotation_deg=%15 "
                "mirrored=%16"
            )
                .arg(frame.startTime() >= 0 ? frame.startTime() / 1000 : -1)
                .arg(frame.endTime() >= 0 ? frame.endTime() / 1000 : -1)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(lastSeekMs_)
                .arg(videoPlaybackActive_ ? 1 : 0)
                .arg(frameSize.width()).arg(frameSize.height())
                .arg(fmtFrameSize.width()).arg(fmtFrameSize.height())
                .arg(viewport.x()).arg(viewport.y())
                .arg(viewport.width()).arg(viewport.height())
                .arg(QVideoFrameFormat::pixelFormatToString(fmt.pixelFormat()))
                .arg(rotationDegrees(frame.rotation()))
                .arg(frame.mirrored() ? 1 : 0)
        );
    }
    if (videoPlaybackActive_ && !videoPlaybackActiveElapsed_.isValid()) {
        videoPlaybackActiveElapsed_.restart();
    }
    if (pausedSeekCompletionPending_ && pausedSeekTargetMs_ >= 0) {
        qint64 candidatePositionMs = -1;
        const qint64 frameStartUs = frame.startTime();
        const qint64 frameEndUs = frame.endTime();
        if (frameStartUs >= 0) {
            candidatePositionMs = frameStartUs / 1000;
        } else if (player_ != nullptr) {
            candidatePositionMs = player_->position();
        }
        const bool frameCoversTarget =
            frameStartUs >= 0
            && frameEndUs >= 0
            && (frameStartUs / 1000) <= pausedSeekTargetMs_
            && pausedSeekTargetMs_ <= (frameEndUs / 1000);
        const bool closeEnough =
            candidatePositionMs >= 0
            && qAbs(candidatePositionMs - pausedSeekTargetMs_) <= kPausedSeekAckToleranceMs;
        if (frameCoversTarget || closeEnough) {
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_ack"),
                QString("generation=%1 second=%2 target_ms=%3 frame_ms=%4 frame_end_ms=%5 source=frame")
                    .arg(pausedSeekGeneration_)
                    .arg(pausedSeekTargetSecond_, 0, 'f', 6)
                    .arg(pausedSeekTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
            pausedSeekCompletionPending_ = false;
            emit pausedSeekCompleted(pausedSeekTargetSecond_, pausedSeekGeneration_);
        } else {
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_wait_frame"),
                QString("generation=%1 target_ms=%2 frame_ms=%3 frame_end_ms=%4")
                    .arg(pausedSeekGeneration_)
                    .arg(pausedSeekTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
        }
    }
    if (preparedPlaybackPending_ && preparedPlaybackTargetMs_ >= 0) {
        qint64 candidatePositionMs = -1;
        const qint64 frameStartUs = frame.startTime();
        const qint64 frameEndUs = frame.endTime();
        if (frameStartUs >= 0) {
            candidatePositionMs = frameStartUs / 1000;
        } else if (player_ != nullptr) {
            candidatePositionMs = player_->position();
        }
        const bool frameCoversTarget =
            frameStartUs >= 0
            && frameEndUs >= 0
            && (frameStartUs / 1000) <= preparedPlaybackTargetMs_
            && preparedPlaybackTargetMs_ <= (frameEndUs / 1000);
        const bool closeEnough =
            candidatePositionMs >= 0
            && qAbs(candidatePositionMs - preparedPlaybackTargetMs_) <= kPausedSeekAckToleranceMs;
        if (frameCoversTarget || closeEnough) {
            appendPreviewStageMediaLog(
                QStringLiteral("prepare_playback_ready"),
                QString("txn=%1 second=%2 target_ms=%3 frame_ms=%4 frame_end_ms=%5 source=frame")
                    .arg(preparedPlaybackTransaction_)
                    .arg(preparedPlaybackTargetSecond_, 0, 'f', 6)
                    .arg(preparedPlaybackTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
            preparedPlaybackPending_ = false;
            preparedPlaybackReady_ = true;
            emit playbackStartPrepared(preparedPlaybackTargetSecond_, preparedPlaybackTransaction_);
        } else {
            appendPreviewStageMediaLog(
                QStringLiteral("prepare_playback_wait_frame"),
                QString("txn=%1 target_ms=%2 frame_ms=%3 frame_end_ms=%4")
                    .arg(preparedPlaybackTransaction_)
                    .arg(preparedPlaybackTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
        }
    }
    updateVideoFrameStallState(true);
    if (syncFrameBeacon) {
        --syncVideoFrameBeaconBudget_;
        char buf[280];
        std::snprintf(buf, sizeof(buf),
            "preview/frame/arrived_exit tid=%lu count=%lld player_pos=%lld clock_delta=%.6f status=%d state=%d remaining=%d",
            currentBeaconTid(),
            static_cast<long long>(videoFrameCountTotal_),
            player_ != nullptr ? static_cast<long long>(player_->position()) : -1ll,
            clockDeltaSeconds_,
            player_ != nullptr ? static_cast<int>(player_->mediaStatus()) : -1,
            player_ != nullptr ? static_cast<int>(playerPlaybackState(player_)) : -1,
            syncVideoFrameBeaconBudget_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::resetVideoFrameDiagnostics()
{
    videoFrameElapsed_.invalidate();
    videoPlaybackActiveElapsed_.invalidate();
    videoFrameIntervalsMs_.fill(0.0);
    videoFrameIntervalWriteIndex_ = 0;
    videoFrameIntervalCount_ = 0;
    videoFrameIntervalSumMs_ = 0.0;
    videoFrameIntervalMaxMs_ = 0.0;
    videoFrameIntervalSampleCount_ = 0;
    videoFrameCountTotal_ = 0;
    videoFrameStallCount_ = 0;
    videoFrameStalled_ = false;
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
}

bool PreviewStageMediaHost::updateVideoFrameStallState(bool logTransition)
{
    const qint64 ageMs = currentVideoFrameAgeForDiagnosticsMs();
    const bool stalled =
        hasVideoMedia()
        && videoPlaybackActive_
        && ageMs >= 0
        && ageMs >= videoFrameStallThresholdMs();
    if (videoFrameStalled_ == stalled) {
        return false;
    }
    videoFrameStalled_ = stalled;
    if (videoFrameStalled_) {
        videoFrameStallCount_ += 1;
    }
    if (!logTransition) {
        return true;
    }
    appendPreviewStageMediaLog(
        videoFrameStalled_ ? QStringLiteral("video_frame_stall_begin") : QStringLiteral("video_frame_stall_end"),
        QString("age_ms=%1 threshold_ms=%2 avg_interval_ms=%3 fps=%4 frame_count=%5 playback_second=%6 observed_second=%7 delta=%8")
            .arg(ageMs)
            .arg(videoFrameStallThresholdMs())
            .arg(videoFrameIntervalAvgMs(), 0, 'f', 3)
            .arg(videoFrameRateEstimate(), 0, 'f', 3)
            .arg(videoFrameCountTotal_)
            .arg(currentPlaybackSecond(), 0, 'f', 6)
            .arg(observedPlayheadSecond_, 0, 'f', 6)
            .arg(clockDeltaSeconds_, 0, 'f', 6)
    );
    if (videoFrameStalled_) {
        scheduleVideoPlaybackWatchdog(QStringLiteral("frame_stall"));
    }
    return true;
}

qint64 PreviewStageMediaHost::currentVideoFrameAgeForDiagnosticsMs() const
{
    if (videoFrameElapsed_.isValid()) {
        return videoFrameElapsed_.elapsed();
    }
    if (videoPlaybackActive_ && videoPlaybackActiveElapsed_.isValid()) {
        return videoPlaybackActiveElapsed_.elapsed();
    }
    return -1;
}

qint64 PreviewStageMediaHost::videoFrameStallThresholdMs() const
{
    const double baseIntervalMs = qMax(0.0, videoFrameIntervalAvgMs());
    const double scaledThresholdMs = qMax(
        static_cast<double>(kVideoFrameStallMinMs),
        qMax(33.0, baseIntervalMs) * kVideoFrameStallMultiplier
    );
    return qMax<qint64>(kVideoFrameStallMinMs, qRound64(scaledThresholdMs));
}
