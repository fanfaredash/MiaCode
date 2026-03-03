#include "PreviewMediaController.h"

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioOutput>
#endif
#include <QDir>
#include <QFileInfo>
#include <QImage>
#ifdef HAVE_QT_MULTIMEDIA
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#endif
#include <QUrl>

namespace {
QString normalizedLocalPath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}
}

PreviewMediaController::PreviewMediaController(QObject* parent)
    : QObject(parent)
#ifdef HAVE_QT_MULTIMEDIA
    , player_(new QMediaPlayer(this))
    , audioOutput_(new QAudioOutput(this))
    , videoSink_(new QVideoSink(this))
#endif
{
#ifdef HAVE_QT_MULTIMEDIA
    audioOutput_->setMuted(true);
    audioOutput_->setVolume(0.0f);
    player_->setAudioOutput(audioOutput_);
    player_->setVideoSink(videoSink_);
    connect(videoSink_, &QVideoSink::videoFrameChanged, this, &PreviewMediaController::onVideoFrameChanged);
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 positionMs) {
        if (mediaKind_ != MediaKind::Video) {
            return;
        }
        emit playbackPositionChanged(static_cast<double>(positionMs) / 1000.0);
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::EndOfMedia) {
            return;
        }
        videoPlaybackActive_ = false;
        emit playbackFinished();
    });
#endif
}

PreviewMediaController::~PreviewMediaController() = default;

bool PreviewMediaController::hasVideoMedia() const
{
    return mediaKind_ == MediaKind::Video;
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

void PreviewMediaController::setPlayheadSeconds(double seconds)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(seconds);
    return;
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return;
    }

    const qint64 targetMs = qMax<qint64>(0, qRound64(seconds * 1000.0));
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
    const qint64 targetMs = qMax<qint64>(0, qRound64(seconds * 1000.0));
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    player_->play();
    videoPlaybackActive_ = true;
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
    }
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
    return static_cast<double>(player_->position()) / 1000.0;
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
#ifdef HAVE_QT_MULTIMEDIA
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        player_->pause();
        player_->setPosition(0);
    }
#endif
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
    mediaKind_ = MediaKind::None;
    mediaPath_.clear();
#ifdef HAVE_QT_MULTIMEDIA
    if (player_ != nullptr) {
        player_->stop();
        player_->setSource(QUrl());
    }
#endif
    publishFrame(QImage());
}

void PreviewMediaController::publishFrame(const QImage& frame)
{
    emit frameChanged(frame);
}

void PreviewMediaController::loadImageMedia(const QString& path)
{
    const QImage image(path);
    if (image.isNull()) {
        return;
    }
    mediaKind_ = MediaKind::Image;
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
    const QImage image = frame.toImage();
    if (image.isNull()) {
        return;
    }
    publishFrame(image);
#endif
}
