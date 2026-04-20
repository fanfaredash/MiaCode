#include "preview/runtime/PreviewStageMediaHost.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#endif

#include <QDir>
#include <QFileInfo>
#include <QVariant>
#include <QtMath>

namespace {

constexpr qint64 kPausedSeekAckToleranceMs = 80;
constexpr int kVideoFrameIntervalWindowSize = 120;
constexpr qint64 kVideoFrameStallMinMs = 120;
constexpr double kVideoFrameStallMultiplier = 3.5;

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

PreviewStageMediaHost::~PreviewStageMediaHost() = default;

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
    player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
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
        if (status != QMediaPlayer::EndOfMedia) {
            return;
        }
        videoPlaybackActive_ = false;
        videoPlaybackPendingStart_ = false;
        videoPlaybackActiveElapsed_.invalidate();
        updateClockDelta();
        updateVideoFrameStallState(true);
        emit diagnosticsChanged();
        emit playbackFinished();
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
    emit mediaVisibilityChanged();
}

QUrl PreviewStageMediaHost::imageSource() const
{
    return imageSource_;
}

int PreviewStageMediaHost::backgroundScaleMode() const
{
    return static_cast<int>(backgroundScaleMode_);
}

void PreviewStageMediaHost::setBackgroundScaleModeValue(int mode)
{
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

void PreviewStageMediaHost::setChartPath(const QString& chartPath)
{
    const QString normalizedChartPath = normalizedLocalPath(chartPath);
    if (normalizedChartPath == chartPath_) {
        return;
    }

    chartPath_ = normalizedChartPath;
    clearMedia();
    if (chartPath_.isEmpty()) {
        appendPreviewStageMediaLog(QStringLiteral("set_chart_path"), QStringLiteral("chart=(empty) kind=none"));
        return;
    }

    const QString resolvedPath = resolveMediaPath(chartPath_);
    if (resolvedPath.isEmpty()) {
        appendPreviewStageMediaLog(
            QStringLiteral("set_chart_path"),
            QStringLiteral("chart=%1 kind=none").arg(chartPath_)
        );
        return;
    }

    mediaPath_ = resolvedPath;
    const QString suffix = QFileInfo(mediaPath_).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("mp4")) {
        loadVideoMedia(mediaPath_);
    } else {
        loadImageMedia(mediaPath_);
    }

    appendPreviewStageMediaLog(
        QStringLiteral("set_chart_path"),
        QStringLiteral("chart=%1 media=%2 kind=%3")
            .arg(chartPath_)
            .arg(mediaPath_)
            .arg(debugMediaTypeName())
    );
}

void PreviewStageMediaHost::setPlaybackRate(double rate)
{
    playbackRate_ = qMax(0.05, rate);
#ifdef HAVE_QT_MULTIMEDIA
    if (player_ != nullptr) {
        player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
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
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
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
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
#else
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();
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

    lastTimelineSecond_ = qMax(0.0, seconds);
    const double rawSecond = seconds + timelineOffsetSeconds_;
    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    if (rawSecond < 0.0) {
        player_->pause();
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
        player_->play();
        videoPlaybackActive_ = true;
        videoPlaybackPendingStart_ = false;
        videoPlaybackActiveElapsed_.restart();
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
        if (videoPlaybackActive_ && playerPlaybackState(player_) != QMediaPlayer::PlayingState) {
            player_->play();
        }
        updateClockDelta();
        updateVideoFrameStallState(true);
        emit diagnosticsChanged();
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
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
    appendPreviewStageMediaLog(
        QStringLiteral("sync_playback_started"),
        QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
            .arg(playbackTransactionId_)
            .arg(seconds, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(targetMs));
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
#endif
}

void PreviewStageMediaHost::pausePlayback()
{
#ifndef HAVE_QT_MULTIMEDIA
    return;
#else
    observedPlayheadSecond_ = currentPlaybackSecond();
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        player_->pause();
    }
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
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
    if (player_ != nullptr) {
        player_->stop();
        player_->setSource(QUrl());
    }
#endif
    mediaKind_ = MediaKind::None;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    mediaPath_.clear();
    imageSource_ = QUrl();
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    observedPlayheadSecond_ = 0.0;
    clockDeltaSeconds_ = 0.0;
    resetVideoFrameDiagnostics();
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
    mediaKind_ = MediaKind::Image;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    resetVideoFrameDiagnostics();
    updateClockDelta();
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
}

void PreviewStageMediaHost::loadVideoMedia(const QString& path)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(path);
#else
    initializeBackendObjects();
    if (player_ == nullptr) {
        return;
    }

    imageSource_ = QUrl();
    mediaKind_ = MediaKind::Video;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    resetVideoFrameDiagnostics();
    player_->setSource(QUrl::fromLocalFile(path));
    player_->pause();
    player_->setPosition(0);
    bindVideoOutput();
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
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=0 sink=0"));
        return;
    }

    player_->setVideoOutput(videoOutputObject_);

    QObject* sinkObject = nullptr;
    const QVariant sinkVariant = videoOutputObject_->property("videoSink");
    if (sinkVariant.isValid()) {
        sinkObject = sinkVariant.value<QObject*>();
    }
    videoSink_ = qobject_cast<QVideoSink*>(sinkObject);
    if (videoSink_ != nullptr) {
        videoSinkFrameConnection_ = QObject::connect(videoSink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
            noteVideoFrameArrived(frame);
        });
        appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=1 sink=1"));
        return;
    }

    appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=1 sink=0"));
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

void PreviewStageMediaHost::noteVideoFrameArrived(const QVideoFrame& frame)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(frame);
#else
    if (!frame.isValid()) {
        return;
    }
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
}

void PreviewStageMediaHost::updateVideoFrameStallState(bool logTransition)
{
    const qint64 ageMs = currentVideoFrameAgeForDiagnosticsMs();
    const bool stalled =
        hasVideoMedia()
        && videoPlaybackActive_
        && ageMs >= 0
        && ageMs >= videoFrameStallThresholdMs();
    if (videoFrameStalled_ == stalled) {
        return;
    }
    videoFrameStalled_ = stalled;
    if (videoFrameStalled_) {
        videoFrameStallCount_ += 1;
    }
    if (!logTransition) {
        return;
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
