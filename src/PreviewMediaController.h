#pragma once

#include <QObject>
#include <QString>

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

    bool hasVideoMedia() const;
    bool isPlaybackActive() const;
    void setChartPath(const QString& chartPath);
    void setPlayheadSeconds(double seconds);
    void startPlayback(double seconds);
    void pausePlayback();
    double currentPlaybackSecond() const;
    void setBackgroundBrightness(double brightness);
    void reset();

signals:
    void frameChanged(const QImage& frame);
    void backgroundBrightnessChanged(double brightness);
    void playbackPositionChanged(double seconds);
    void playbackFinished();

private:
    enum class MediaKind {
        None,
        Image,
        Video,
    };

    QString resolveMediaPath(const QString& chartPath) const;
    void clearMedia();
    void publishFrame(const QImage& frame);
    void loadImageMedia(const QString& path);
    void loadVideoMedia(const QString& path);
    void onVideoFrameChanged(const QVideoFrame& frame);

    MediaKind mediaKind_ = MediaKind::None;
    QString chartPath_;
    QString mediaPath_;
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QVideoSink* videoSink_ = nullptr;
    double backgroundBrightness_ = 0.2;
    qint64 lastSeekMs_ = -1;
    bool videoPlaybackActive_ = false;
};
