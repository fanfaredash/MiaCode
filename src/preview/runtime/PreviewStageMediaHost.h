#pragma once

#include "preview/video/PreviewRenderSettings.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QUrl>
#include <QString>

class QAudioOutput;
class QMediaPlayer;
class QVideoFrame;
class QVideoSink;

class PreviewStageMediaHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasResolvedMedia READ hasResolvedMedia NOTIFY mediaStateChanged)
    Q_PROPERTY(bool hasVideoMedia READ hasVideoMedia NOTIFY mediaStateChanged)
    Q_PROPERTY(bool mediaVisible READ mediaVisible WRITE setMediaVisible NOTIFY mediaVisibilityChanged)
    Q_PROPERTY(QUrl imageSource READ imageSource NOTIFY imageSourceChanged)
    Q_PROPERTY(int backgroundScaleMode READ backgroundScaleMode WRITE setBackgroundScaleModeValue NOTIFY backgroundScaleModeChanged)
    Q_PROPERTY(bool videoPlaybackActive READ videoPlaybackActive NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool hasVideoFrame READ hasVideoFrame NOTIFY diagnosticsChanged)
    Q_PROPERTY(double currentPlaybackSecond READ currentPlaybackSecond NOTIFY diagnosticsChanged)
    Q_PROPERTY(double clockDeltaSeconds READ clockDeltaSeconds NOTIFY diagnosticsChanged)
    Q_PROPERTY(qint64 videoFrameAgeMs READ videoFrameAgeMs NOTIFY diagnosticsChanged)

public:
    enum class MediaKind {
        None,
        Image,
        Video,
    };
    Q_ENUM(MediaKind)

    explicit PreviewStageMediaHost(QObject* parent = nullptr);
    ~PreviewStageMediaHost() override;

    void initializeBackendObjects();
    void setWarmupResolvedMediaPath(const QString& chartPath, const QString& mediaPath);
    Q_INVOKABLE void attachVideoOutputObject(QObject* videoOutputObject);
    Q_INVOKABLE void detachVideoOutputObject(QObject* videoOutputObject);

    bool hasResolvedMedia() const;
    bool hasVideoMedia() const;
    bool mediaVisible() const;
    void setMediaVisible(bool visible);
    QUrl imageSource() const;
    int backgroundScaleMode() const;
    void setBackgroundScaleModeValue(int mode);
    void setBackgroundScaleMode(PreviewBackgroundScaleMode mode);

    void setChartPath(const QString& chartPath);
    void setPlaybackRate(double rate);
    void setTimelineOffsetSeconds(double seconds);
    void setPlaybackTransactionId(quint64 transactionId);
    void preparePlaybackStart(double seconds, quint64 transactionId);
    void commitPreparedPlaybackStart(double currentTimelineSecond);
    void cancelPreparedPlaybackStart(quint64 transactionId = 0);
    bool hasPreparedPlaybackStart(quint64 transactionId) const;
    void setPlayheadSeconds(double seconds);
    void submitPausedSeek(double seconds, quint64 generation);
    void startPlayback(double seconds);
    void syncPlayback(double seconds);
    void pausePlayback();
    void shutdownForAppExit();

    double currentPlaybackSecond() const;
    bool videoPlaybackActive() const;
    bool hasVideoFrame() const;
    double clockDeltaSeconds() const;
    qint64 videoFrameAgeMs() const;
    qint64 videoFrameCountTotal() const;
    double videoFrameRateEstimate() const;
    double videoFrameIntervalAvgMs() const;
    double videoFrameIntervalMaxMs() const;
    qint64 videoFrameStallCount() const;
    bool videoFrameStalled() const;
    void setObservedPlayheadSecond(double second);
    QString debugMediaTypeName() const;

signals:
    void mediaStateChanged();
    void mediaVisibilityChanged();
    void imageSourceChanged();
    void backgroundScaleModeChanged();
    void playbackPositionChanged(double seconds);
    void playbackStartPrepared(double seconds, quint64 transactionId);
    void pausedSeekCompleted(double seconds, quint64 generation);
    void playbackFinished();
    void diagnosticsChanged();

private:
    void clearMedia();
    QString resolveMediaPath(const QString& chartPath) const;
    void loadImageMedia(const QString& path);
    void loadVideoMedia(const QString& path);
    void bindVideoOutput();
    void updateClockDelta();
    void noteVideoFrameArrived(const QVideoFrame& frame);
    void resetVideoFrameDiagnostics();
    void updateVideoFrameStallState(bool logTransition);
    qint64 currentVideoFrameAgeForDiagnosticsMs() const;
    qint64 videoFrameStallThresholdMs() const;

    MediaKind mediaKind_ = MediaKind::None;
    QString chartPath_;
    QString mediaPath_;
    QString warmupChartPath_;
    QString warmupMediaPath_;
    QUrl imageSource_;
    bool mediaVisible_ = true;
    PreviewBackgroundScaleMode backgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QPointer<QObject> videoOutputObject_;
    QPointer<QVideoSink> videoSink_;
    QMetaObject::Connection videoSinkFrameConnection_;
    double timelineOffsetSeconds_ = 0.0;
    double playbackRate_ = 1.0;
    quint64 playbackTransactionId_ = 0;
    qint64 preparedPlaybackTargetMs_ = -1;
    double preparedPlaybackTargetSecond_ = 0.0;
    quint64 preparedPlaybackTransaction_ = 0;
    bool preparedPlaybackPending_ = false;
    bool preparedPlaybackReady_ = false;
    double lastTimelineSecond_ = 0.0;
    qint64 lastSeekMs_ = -1;
    qint64 pausedSeekTargetMs_ = -1;
    double pausedSeekTargetSecond_ = 0.0;
    quint64 pausedSeekGeneration_ = 0;
    bool pausedSeekCompletionPending_ = false;
    bool videoPlaybackActive_ = false;
    bool videoPlaybackPendingStart_ = false;
    double observedPlayheadSecond_ = 0.0;
    double clockDeltaSeconds_ = 0.0;
    QElapsedTimer videoFrameElapsed_;
    QElapsedTimer videoPlaybackActiveElapsed_;
    QVector<double> videoFrameIntervalsMs_;
    int videoFrameIntervalWriteIndex_ = 0;
    int videoFrameIntervalCount_ = 0;
    double videoFrameIntervalSumMs_ = 0.0;
    double videoFrameIntervalMaxMs_ = 0.0;
    qint64 videoFrameIntervalSampleCount_ = 0;
    qint64 videoFrameCountTotal_ = 0;
    qint64 videoFrameStallCount_ = 0;
    bool videoFrameStalled_ = false;
    bool shuttingDown_ = false;
};
