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
        miacode::debug_log::Channel::Runtime,
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
        emit playbackPositionChanged(lastTimelineSecond_);
        emit diagnosticsChanged();
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::EndOfMedia) {
            return;
        }
        videoPlaybackActive_ = false;
        videoPlaybackPendingStart_ = false;
        updateClockDelta();
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
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
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
    } else {
        player_->play();
        videoPlaybackActive_ = true;
        videoPlaybackPendingStart_ = false;
        if (playerPlaybackState(player_) != QMediaPlayer::PlayingState) {
            QMetaObject::invokeMethod(player_, [this]() {
                if (mediaKind_ == MediaKind::Video && videoPlaybackActive_) {
                    player_->play();
                }
            }, Qt::QueuedConnection);
        }
    }
    updateClockDelta();
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
        emit diagnosticsChanged();
        return;
    }

    const double rawSecond = seconds + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
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
    updateClockDelta();
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
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    updateClockDelta();
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
    if (!hasVideoMedia() || !videoFrameElapsed_.isValid()) {
        return -1;
    }
    return videoFrameElapsed_.elapsed();
}

qint64 PreviewStageMediaHost::videoFrameCountTotal() const
{
    return videoFrameCountTotal_;
}

void PreviewStageMediaHost::setObservedPlayheadSecond(double second)
{
    observedPlayheadSecond_ = qMax(0.0, second);
    updateClockDelta();
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
    mediaPath_.clear();
    imageSource_ = QUrl();
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    observedPlayheadSecond_ = 0.0;
    clockDeltaSeconds_ = 0.0;
    videoFrameElapsed_.invalidate();
    videoFrameCountTotal_ = 0;
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
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoFrameElapsed_.invalidate();
    videoFrameCountTotal_ = 0;
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
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoFrameElapsed_.invalidate();
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
    const bool firstFrame = !videoFrameElapsed_.isValid();
    videoFrameElapsed_.restart();
    Q_UNUSED(firstFrame);
    ++videoFrameCountTotal_;
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
    emit diagnosticsChanged();
#endif
}
