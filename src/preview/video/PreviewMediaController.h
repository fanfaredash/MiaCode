#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QAudioOutput;
class QImage;
class QMediaPlayer;
class QVideoFrame;
class QVideoSink;

class PreviewMediaController : public QObject
{
    Q_OBJECT

public:
    explicit PreviewMediaController(QObject* parent = nullptr);
    ~PreviewMediaController() override;

    void initializeBackendObjects();
    void setWarmupResolvedMediaPath(const QString& chartPath, const QString& mediaPath);
    bool hasResolvedMedia() const;
    bool hasVideoMedia() const;
    bool hasBackgroundTrack() const;
    bool isPlaybackActive() const;
    void setChartPath(const QString& chartPath);
    void setBackgroundTrackPath(const QString& trackPath);
    void setBackgroundTrackVolume(double volume);
    void setBackgroundTrackPlaybackRate(double rate);
    void setTimelineOffsetSeconds(double seconds);
    void setPlaybackRate(double rate);
    void setPlayheadSeconds(double seconds);
    void startPlayback(double seconds);
    void syncPlayback(double seconds);
    void startBackgroundTrack(double seconds);
    void syncBackgroundTrack(double seconds);
    void pausePlayback();
    void pauseBackgroundTrack();
    double currentPlaybackSecond() const;
    double currentBackgroundTrackSecond() const;
    void setBackgroundBrightness(double brightness);
    void reset();
    void resetProfilingSession();
    QString profilingSummaryLines() const;

signals:
    void mediaStateChanged(bool hasResolvedMedia, bool hasVideoMedia);
    void frameChanged(const QImage& frame);
    void videoFrameChanged(const QVideoFrame& frame);
    void videoFallbackFrameChanged(const QImage& frame);
    void backgroundBrightnessChanged(double brightness);
    void playbackPositionChanged(double seconds);
    void playbackFinished();

private:
    enum class MediaKind {
        None,
        Image,
        Video,
    };

    void emitMediaStateChanged();
    QString resolveMediaPath(const QString& chartPath) const;
    void clearMedia();
    void publishFrame(const QImage& frame);
    void publishVideoFrame(const QVideoFrame& frame);
    void loadImageMedia(const QString& path);
    void loadVideoMedia(const QString& path);
    void onVideoFrameChanged(const QVideoFrame& frame);

    MediaKind mediaKind_ = MediaKind::None;
    QString chartPath_;
    QString mediaPath_;
    QString warmupChartPath_;
    QString warmupMediaPath_;
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QVideoSink* videoSink_ = nullptr;
    QMediaPlayer* bgmPlayer_ = nullptr;
    QAudioOutput* bgmAudioOutput_ = nullptr;
    double backgroundBrightness_ = 0.2;
    double timelineOffsetSeconds_ = 0.0;
    double lastTimelineSecond_ = 0.0;
    qint64 lastSeekMs_ = -1;
    bool videoPlaybackActive_ = false;
    bool videoPlaybackPendingStart_ = false;
    QString bgmTrackPath_;
    qint64 bgmLastSeekMs_ = -1;
    bool bgmPlaybackActive_ = false;
    bool bgmPlaybackPendingStart_ = false;
    double bgmLastTimelineSecond_ = 0.0;
    double bgmVolume_ = 1.0;
    double profileVideoToImageTotalMs_ = 0.0;
    quint64 profileVideoToImageSampleCount_ = 0;
    QVector<double> profileVideoToImageSamplesMs_;
};
