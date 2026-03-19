#include "PreviewMediaController.h"

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioOutput>
#endif
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#ifdef HAVE_QT_MULTIMEDIA
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#endif
#include <QtMath>
#include <QUrl>

#include <algorithm>
#include <numeric>

namespace {
QString normalizedLocalPath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

struct SampleStats {
    bool hasValue = false;
    double avgMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
};

SampleStats computeSampleStats(const QVector<double>& samples)
{
    SampleStats stats;
    if (samples.isEmpty()) {
        return stats;
    }

    stats.hasValue = true;
    const double sum = std::accumulate(samples.cbegin(), samples.cend(), 0.0);
    stats.avgMs = sum / static_cast<double>(samples.size());
    QVector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    stats.maxMs = sorted.constLast();
    const int p95Index = qBound(0, static_cast<int>(qCeil(sorted.size() * 0.95)) - 1, sorted.size() - 1);
    stats.p95Ms = sorted.at(p95Index);
    return stats;
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
}

PreviewMediaController::PreviewMediaController(QObject* parent)
    : QObject(parent)
#ifdef HAVE_QT_MULTIMEDIA
    , player_(new QMediaPlayer(this))
    , audioOutput_(new QAudioOutput(this))
    , videoSink_(new QVideoSink(this))
    , bgmPlayer_(new QMediaPlayer(this))
    , bgmAudioOutput_(new QAudioOutput(this))
#endif
{
#ifdef HAVE_QT_MULTIMEDIA
    audioOutput_->setMuted(true);
    audioOutput_->setVolume(0.0f);
    player_->setAudioOutput(audioOutput_);
    player_->setVideoSink(videoSink_);
    bgmAudioOutput_->setMuted(false);
    bgmAudioOutput_->setVolume(1.0f);
    bgmPlayer_->setAudioOutput(bgmAudioOutput_);
    connect(videoSink_, &QVideoSink::videoFrameChanged, this, &PreviewMediaController::onVideoFrameChanged);
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 positionMs) {
        if (mediaKind_ != MediaKind::Video) {
            return;
        }
        const double timelineSecond = static_cast<double>(positionMs) / 1000.0 - timelineOffsetSeconds_;
        lastTimelineSecond_ = qMax(0.0, timelineSecond);
        emit playbackPositionChanged(lastTimelineSecond_);
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::EndOfMedia) {
            return;
        }
        videoPlaybackActive_ = false;
        videoPlaybackPendingStart_ = false;
        emit playbackFinished();
    });
    connect(bgmPlayer_, &QMediaPlayer::positionChanged, this, [this](qint64 positionMs) {
        const double timelineSecond = static_cast<double>(positionMs) / 1000.0 - timelineOffsetSeconds_;
        bgmLastTimelineSecond_ = qMax(0.0, timelineSecond);
    });
    connect(bgmPlayer_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::EndOfMedia) {
            return;
        }
        bgmPlaybackActive_ = false;
        bgmPlaybackPendingStart_ = false;
    });
#endif
}

PreviewMediaController::~PreviewMediaController() = default;

bool PreviewMediaController::hasResolvedMedia() const
{
    return mediaKind_ != MediaKind::None;
}

bool PreviewMediaController::hasVideoMedia() const
{
    return mediaKind_ == MediaKind::Video;
}

bool PreviewMediaController::hasBackgroundTrack() const
{
#ifndef HAVE_QT_MULTIMEDIA
    return false;
#else
    return bgmPlayer_ != nullptr && !bgmTrackPath_.isEmpty();
#endif
}

bool PreviewMediaController::isPlaybackActive() const
{
    return videoPlaybackActive_;
}

void PreviewMediaController::setChartPath(const QString& chartPath)
{
    const QString normalizedChartPath = normalizedLocalPath(chartPath);
    if (normalizedChartPath == chartPath_) {
        return;
    }

    chartPath_ = normalizedChartPath;
    clearMedia();
    if (chartPath_.isEmpty()) {
        return;
    }

    const QString resolvedPath = resolveMediaPath(chartPath_);
    if (resolvedPath.isEmpty()) {
        return;
    }

    mediaPath_ = resolvedPath;
    const QString suffix = QFileInfo(mediaPath_).suffix().toLower();
    if (suffix == "mp4") {
        loadVideoMedia(mediaPath_);
        return;
    }
    loadImageMedia(mediaPath_);
}

void PreviewMediaController::setBackgroundTrackPath(const QString& trackPath)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(trackPath);
    return;
#else
    const QString normalizedTrackPath = normalizedLocalPath(trackPath);
    if (normalizedTrackPath == bgmTrackPath_) {
        return;
    }
    bgmTrackPath_ = normalizedTrackPath;
    bgmLastSeekMs_ = -1;
    bgmPlaybackActive_ = false;
    bgmPlaybackPendingStart_ = false;
    bgmLastTimelineSecond_ = 0.0;
    if (bgmPlayer_ == nullptr) {
        return;
    }
    bgmPlayer_->stop();
    if (bgmTrackPath_.isEmpty()) {
        bgmPlayer_->setSource(QUrl());
        return;
    }
    bgmPlayer_->setSource(QUrl::fromLocalFile(bgmTrackPath_));
    bgmPlayer_->pause();
    bgmPlayer_->setPosition(0);
#endif
}

void PreviewMediaController::setBackgroundTrackVolume(double volume)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(volume);
    return;
#else
    bgmVolume_ = qBound(0.0, volume, 1.0);
    if (bgmAudioOutput_ != nullptr) {
        bgmAudioOutput_->setVolume(static_cast<float>(bgmVolume_));
    }
#endif
}

void PreviewMediaController::setBackgroundTrackPlaybackRate(double rate)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(rate);
    return;
#else
    if (bgmPlayer_ == nullptr) {
        return;
    }
    bgmPlayer_->setPlaybackRate(static_cast<qreal>(qMax(0.05, rate)));
#endif
}

void PreviewMediaController::setTimelineOffsetSeconds(double seconds)
{
    const double clamped = qIsFinite(seconds) ? seconds : 0.0;
    if (qFuzzyCompare(timelineOffsetSeconds_ + 1.0, clamped + 1.0)) {
        return;
    }
    timelineOffsetSeconds_ = clamped;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = false;
    lastTimelineSecond_ = 0.0;
#ifdef HAVE_QT_MULTIMEDIA
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        player_->pause();
        player_->setPosition(0);
        lastSeekMs_ = 0;
    }
#endif
}

void PreviewMediaController::setPlaybackRate(double rate)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(rate);
    return;
#else
    if (player_ == nullptr) {
        return;
    }
    const double clampedRate = qMax(0.05, rate);
    player_->setPlaybackRate(static_cast<qreal>(clampedRate));
#endif
}

void PreviewMediaController::setPlayheadSeconds(double seconds)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    return;
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return;
    }

    lastTimelineSecond_ = qMax(0.0, seconds);
    const qint64 targetMs = qMax<qint64>(0, qRound64((seconds + timelineOffsetSeconds_) * 1000.0));
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < 40) {
        return;
    }
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
#endif
}

void PreviewMediaController::startPlayback(double seconds)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    return;
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
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
        return;
    }
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
#endif
}

void PreviewMediaController::syncPlayback(double seconds)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    return;
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return;
    }
    lastTimelineSecond_ = qMax(0.0, seconds);
    if (!videoPlaybackPendingStart_) {
        if (videoPlaybackActive_ && playerPlaybackState(player_) != QMediaPlayer::PlayingState) {
            player_->play();
        }
        return;
    }
    const double rawSecond = seconds + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        return;
    }
    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
#endif
}

void PreviewMediaController::startBackgroundTrack(double seconds)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    return;
#else
    if (!hasBackgroundTrack() || bgmPlayer_ == nullptr) {
        return;
    }
    bgmLastTimelineSecond_ = qMax(0.0, seconds);
    const double rawSecond = seconds + timelineOffsetSeconds_;
    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    bgmLastSeekMs_ = targetMs;
    bgmPlayer_->setPosition(targetMs);
    if (rawSecond < 0.0) {
        bgmPlayer_->pause();
        bgmPlaybackPendingStart_ = true;
        bgmPlaybackActive_ = false;
        return;
    }
    bgmPlayer_->play();
    bgmPlaybackPendingStart_ = false;
    bgmPlaybackActive_ = true;
    if (playerPlaybackState(bgmPlayer_) != QMediaPlayer::PlayingState) {
        QMetaObject::invokeMethod(bgmPlayer_, [this]() {
            if (bgmPlaybackActive_ && bgmPlayer_ != nullptr) {
                bgmPlayer_->play();
            }
        }, Qt::QueuedConnection);
    }
#endif
}

void PreviewMediaController::syncBackgroundTrack(double seconds)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    return;
#else
    if (!hasBackgroundTrack() || bgmPlayer_ == nullptr) {
        return;
    }
    bgmLastTimelineSecond_ = qMax(0.0, seconds);
    if (!bgmPlaybackPendingStart_) {
        if (bgmPlaybackActive_ && playerPlaybackState(bgmPlayer_) != QMediaPlayer::PlayingState) {
            bgmPlayer_->play();
        }
        return;
    }
    const double rawSecond = seconds + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        return;
    }
    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    bgmLastSeekMs_ = targetMs;
    bgmPlayer_->setPosition(targetMs);
    bgmPlayer_->play();
    bgmPlaybackPendingStart_ = false;
    bgmPlaybackActive_ = true;
#endif
}

void PreviewMediaController::pausePlayback()
{
#ifndef HAVE_QT_MULTIMEDIA
    return;
#else
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        player_->pause();
        videoPlaybackActive_ = false;
        videoPlaybackPendingStart_ = false;
    }
#endif
}

void PreviewMediaController::pauseBackgroundTrack()
{
#ifndef HAVE_QT_MULTIMEDIA
    return;
#else
    if (bgmPlayer_ == nullptr) {
        return;
    }
    bgmPlayer_->pause();
    bgmPlaybackPendingStart_ = false;
    bgmPlaybackActive_ = false;
#endif
}

double PreviewMediaController::currentPlaybackSecond() const
{
#ifndef HAVE_QT_MULTIMEDIA
    return 0.0;
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return 0.0;
    }
    if (!videoPlaybackActive_) {
        return lastTimelineSecond_;
    }
    return qMax(0.0, static_cast<double>(player_->position()) / 1000.0 - timelineOffsetSeconds_);
#endif
}

double PreviewMediaController::currentBackgroundTrackSecond() const
{
#ifndef HAVE_QT_MULTIMEDIA
    return 0.0;
#else
    if (!hasBackgroundTrack() || bgmPlayer_ == nullptr) {
        return 0.0;
    }
    if (!bgmPlaybackActive_) {
        return qMax(0.0, bgmLastTimelineSecond_);
    }
    return qMax(0.0, static_cast<double>(bgmPlayer_->position()) / 1000.0 - timelineOffsetSeconds_);
#endif
}

void PreviewMediaController::setBackgroundBrightness(double brightness)
{
    const double clamped = qBound(0.0, brightness, 1.0);
    if (qFuzzyCompare(backgroundBrightness_ + 1.0, clamped + 1.0)) {
        return;
    }
    backgroundBrightness_ = clamped;
    emit backgroundBrightnessChanged(backgroundBrightness_);
}

void PreviewMediaController::reset()
{
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    lastTimelineSecond_ = 0.0;
    bgmLastSeekMs_ = -1;
    bgmPlaybackActive_ = false;
    bgmPlaybackPendingStart_ = false;
    bgmLastTimelineSecond_ = 0.0;
#ifdef HAVE_QT_MULTIMEDIA
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        player_->pause();
        player_->setPosition(0);
    }
    if (bgmPlayer_ != nullptr) {
        bgmPlayer_->pause();
        bgmPlayer_->setPosition(0);
    }
#endif
}

void PreviewMediaController::resetProfilingSession()
{
    profileVideoToImageTotalMs_ = 0.0;
    profileVideoToImageSampleCount_ = 0;
    profileVideoToImageSamplesMs_.clear();
}

QString PreviewMediaController::profilingSummaryLines() const
{
    const SampleStats stats = computeSampleStats(profileVideoToImageSamplesMs_);
    QString text;
    text += QString("video_frame_to_image_samples=%1\n").arg(profileVideoToImageSampleCount_);
    if (!stats.hasValue) {
        text += "video_frame_to_image_avg_ms=N/A\n";
        text += "video_frame_to_image_p95_ms=N/A\n";
        text += "video_frame_to_image_max_ms=N/A\n";
        return text;
    }
    text += QString("video_frame_to_image_avg_ms=%1\n").arg(QString::number(stats.avgMs, 'f', 4));
    text += QString("video_frame_to_image_p95_ms=%1\n").arg(QString::number(stats.p95Ms, 'f', 4));
    text += QString("video_frame_to_image_max_ms=%1\n").arg(QString::number(stats.maxMs, 'f', 4));
    return text;
}

QString PreviewMediaController::resolveMediaPath(const QString& chartPath) const
{
    const QFileInfo chartInfo(chartPath);
    const QDir chartDir(chartInfo.absolutePath());

    QStringList candidates;
#ifdef HAVE_QT_MULTIMEDIA
    candidates << "bg.mp4" << "pv.mp4";
#endif
    candidates << "bg.jpg" << "bg.png" << "bg.jpeg";
    for (const QString& name : candidates) {
        const QString path = chartDir.filePath(name);
        if (QFileInfo::exists(path)) {
            return QDir::cleanPath(path);
        }
    }
    return QString();
}

void PreviewMediaController::clearMedia()
{
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    lastTimelineSecond_ = 0.0;
    mediaKind_ = MediaKind::None;
    mediaPath_.clear();
#ifdef HAVE_QT_MULTIMEDIA
    if (player_ != nullptr) {
        player_->stop();
        player_->setSource(QUrl());
    }
    if (bgmPlayer_ != nullptr && bgmTrackPath_.isEmpty()) {
        bgmPlayer_->stop();
    }
#endif
    publishFrame(QImage());
#ifdef HAVE_QT_MULTIMEDIA
    publishVideoFrame(QVideoFrame());
#endif
}

void PreviewMediaController::publishFrame(const QImage& frame)
{
    emit frameChanged(frame);
}

void PreviewMediaController::publishVideoFrame(const QVideoFrame& frame)
{
    emit videoFrameChanged(frame);
}

void PreviewMediaController::loadImageMedia(const QString& path)
{
    const QImage image(path);
    if (image.isNull()) {
        return;
    }
    mediaKind_ = MediaKind::Image;
#ifdef HAVE_QT_MULTIMEDIA
    publishVideoFrame(QVideoFrame());
#endif
    publishFrame(image);
}

void PreviewMediaController::loadVideoMedia(const QString& path)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(path);
    return;
#else
    if (player_ == nullptr) {
        return;
    }
    mediaKind_ = MediaKind::Video;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    lastTimelineSecond_ = 0.0;
    player_->setSource(QUrl::fromLocalFile(path));
    player_->pause();
    player_->setPosition(0);
#endif
}

void PreviewMediaController::onVideoFrameChanged(const QVideoFrame& frame)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(frame);
    return;
#else
    if (mediaKind_ != MediaKind::Video) {
        return;
    }
    if (!frame.isValid()) {
        return;
    }
    publishVideoFrame(frame);
#endif
}
