#pragma once

#include "common/LogEmissionPolicy.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "core/video/PreviewRenderSettings.h"
#include "preview/runtime/PvMemoryDiagnostics.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QUrl>
#include <QString>

class QVideoSink;
#ifdef MIACODE_USE_QTAVPLAYER
// Preview decode backend on Windows: FFmpeg via QtAVPlayer (vendored under
// third_party/QtAVPlayer).
// QVideoFrame is included (not just forward-declared) because lastVideoFrame_
// is held by value for frame-replay when a VideoOutput attaches late.
#include <QVideoFrame>
class QAVPlayer;
#else
class QAudioOutput;
class QMediaPlayer;
class QVideoFrame;
#endif

class PreviewStageMediaHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasResolvedMedia READ hasResolvedMedia NOTIFY mediaStateChanged)
    Q_PROPERTY(bool hasVideoMedia READ hasVideoMedia NOTIFY mediaStateChanged)
    Q_PROPERTY(bool mediaVisible READ mediaVisible WRITE setMediaVisible NOTIFY mediaVisibilityChanged)
    Q_PROPERTY(QUrl imageSource READ imageSource NOTIFY imageSourceChanged)
    Q_PROPERTY(int backgroundScaleMode READ backgroundScaleMode WRITE setBackgroundScaleModeValue NOTIFY backgroundScaleModeChanged)
    Q_PROPERTY(double layoutSquareScale READ layoutSquareScale WRITE setLayoutSquareScale NOTIFY layoutSquareScaleChanged)
    Q_PROPERTY(bool videoPlaybackActive READ videoPlaybackActive NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool hasVideoFrame READ hasVideoFrame NOTIFY diagnosticsChanged)
    Q_PROPERTY(double currentPlaybackSecond READ currentPlaybackSecond NOTIFY diagnosticsChanged)
    Q_PROPERTY(double clockDeltaSeconds READ clockDeltaSeconds NOTIFY diagnosticsChanged)
    Q_PROPERTY(qint64 videoFrameAgeMs READ videoFrameAgeMs NOTIFY diagnosticsChanged)

public:
    enum class MediaKind {
        None,
        Image,
        Video,    // FFmpeg/QtAVPlayer-backed on Windows; QMediaPlayer elsewhere
    };
    Q_ENUM(MediaKind)

    explicit PreviewStageMediaHost(QObject* parent = nullptr);
    ~PreviewStageMediaHost() override;

    void initializeBackendObjects();
    void setWarmupResolvedMediaPath(const QString& chartPath, const QString& mediaPath);
    Q_INVOKABLE void attachVideoOutputObject(QObject* videoOutputObject);
    Q_INVOKABLE void attachVideoOutputObjects(QObject* videoOutputObject, QObject* innerVideoOutputObject);
    Q_INVOKABLE void detachVideoOutputObject(QObject* videoOutputObject);
    Q_INVOKABLE void detachVideoOutputObjects(QObject* videoOutputObject, QObject* innerVideoOutputObject);

    bool hasResolvedMedia() const;
    bool hasVideoMedia() const;
    bool mediaVisible() const;
    void setMediaVisible(bool visible);
    QUrl imageSource() const;
    // Resolved video file path. Empty when not in Video mode. Returned
    // as a local file path (not a QUrl) since the only consumer is the
    // out-of-process worker projector which converts to a SerialString
    // for IPC.
    QString resolvedVideoPath() const;
    int backgroundScaleMode() const;
    void setBackgroundScaleModeValue(int mode);
    void setBackgroundScaleMode(PreviewBackgroundScaleMode mode);
    double layoutSquareScale() const;
    void setLayoutSquareScale(double scale);

    // Phase 4c — `chartVideoOverridePath` is the raw `&video=` value
    // from `SimaiDocument::videoPath`. When non-empty, it overrides
    // the sibling-filename heuristic (bg.mp4 / pv.mp4) for video
    // resolution. Empty string preserves legacy behaviour.
    void setChartPath(const QString& chartPath,
                      const QString& chartVideoOverridePath = QString());

    // Phase 4c-8 / 4d — fallback path for legacy LWA composition mode
    // (when MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA=0): returns a copy
    // of the currently-loaded image-mode background. With per-pixel
    // alpha (the default), QML's PreviewStageMediaItem renders the
    // bg natively and this getter returns null. The CPU detour
    // exists only for the env-disabled-NRB fallback case.
    QImage currentBackgroundImage() const;

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

    // Deterministically close the decoder's OS file handle on the currently
    // loaded pv/bg video, so an in-app media tool can rename/replace that very
    // file on Windows (where a file held open for read by FFmpeg's avio cannot
    // be renamed/removed -> "pv占用" / ERROR_SHARING_VIOLATION). A plain
    // clearMedia() is NOT enough: it drops only the outer sink's frame and
    // unloads the demuxer ASYNCHRONOUSLY (the QAVPlayer survives, still holding
    // its QAVFormatContext ref), so the handle can still be open when the caller
    // renames the file. This drops every retained decoded frame (both sinks +
    // lastVideoFrame_) and DESTROYS the player so ~QAVPlayer joins its
    // demux/decode threads and releases the format context synchronously —
    // avformat_close_input has run by the time this returns. The backend is
    // rebuilt lazily on the next load (initializeBackendObjects), exactly like
    // recoverVideoBackend. See project_pv_file_lock_release.
    void releaseDecoderForFileReplace();

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
    void setVideoFrameToImageMaxFps(double fps);
    void setObservedPlayheadSecond(double second);
    QString debugMediaTypeName() const;

    // Video decode-mode preference (硬件渲染 / 软件渲染 toggle). false = hardware
    // (D3D11VA, the default), true = software (FFmpeg CPU). Hot-switchable at
    // RUNTIME with no app restart: when a PV is loaded this reloads it in place on
    // the same QAVPlayer (reusing the sink, restoring position + play state);
    // otherwise it just takes effect on the next load. The persisted user
    // preference is owned by MainWindow and pushed here; the dev env override
    // MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO still wins on top.
    void setVideoDecodePreference(bool preferSoftware);
    bool videoDecodePrefersSoftware() const { return videoDecodePreferSoftware_; }

signals:
    void mediaStateChanged();
    void mediaVisibilityChanged();
    void imageSourceChanged();
    void backgroundScaleModeChanged();
    void layoutSquareScaleChanged();
    void playbackPositionChanged(double seconds);
    void playbackStartPrepared(double seconds, quint64 transactionId);
    void pausedSeekCompleted(double seconds, quint64 generation);
    void diagnosticsChanged();

private:
    using PvMemoryBoundary = miacode::preview::pv_memory::BoundaryReason;

    void clearMedia();
    QString resolveMediaPath(const QString& chartPath) const;
    void loadImageMedia(const QString& path);
    void loadVideoMedia(const QString& path);
    void bindVideoOutput();
    bool recoverVideoBackend(const QString& reason, double targetSecond, bool resumePlayback);
    void schedulePreparedPlaybackTimeout(quint64 transactionId, qint64 targetMs);
    void schedulePausedSeekTimeout(quint64 generation, qint64 targetMs);
    void scheduleVideoPlaybackWatchdog(const QString& reason);
    bool trySoftRecoverVideoPlayback(const QString& reason,
                                     double targetSecond,
                                     qint64 initialFrameCount,
                                     qint64 ageMs);
    void updateClockDelta();
    void noteVideoFrameArrived(const QVideoFrame& frame, quint64 sourceGeneration);
    // The inner-circle VideoOutput is only rendered in InnerCircleFitOuterFill
    // (background scale mode 3); in every other mode it is bound but invisible,
    // so feeding it decoded frames buys nothing and retains a decode-pool
    // surface. True only when a distinct inner sink exists AND that mode is on.
    bool innerVideoSinkActive() const;
    // What refreshInnerVideoSinkForScaleMode() actually did, so the scale-mode log
    // line can state the outcome instead of the intent — "entered mode 3 but there
    // was no retained frame to prime" and "primed with the current frame" are the
    // two halves of the "wrong first frame after switching" report, and only the
    // callee can tell them apart.
    enum class InnerVideoSinkRefresh {
        None,     // no distinct inner sink, or entering the mode with no retained frame
        Primed,   // retained frame pushed, so the mode shows the current frame at once
        Cleared,  // frame released, so the sink stops pinning a decode-pool surface
    };
    // Push the retained frame into the inner sink so a mid-playback switch into
    // InnerCircleFitOuterFill shows the current frame without waiting for the
    // next decode; clear it when leaving the mode so nothing stays pinned.
    InnerVideoSinkRefresh refreshInnerVideoSinkForScaleMode();
#ifdef MIACODE_USE_QTAVPLAYER
    // QtAVPlayer frame path: a decoded QAVVideoFrame (already converted to a
    // QVideoFrame and tagged with its presentation pts in seconds) is pushed
    // to the QML sink here and used to settle the paused-seek /
    // prepared-start handshakes by pts.
    void handleDecodedVideoFrame(const QVideoFrame& frame, double ptsSeconds, double durationSeconds, quint64 sourceGeneration);
    // Settle the paused-seek / prepared-start acks once the decoded media time
    // [start,end] (frame pts..pts+dur, or the seeked() position as a point)
    // reaches the pending seek target. Mirrors the QMediaPlayer path's
    // frame-covers-target / position-ack logic, keyed on pts instead of µs.
    void settlePendingSeekAcks(double mediaSecondStart, double mediaSecondEnd);
    // One-shot fallback: if hardware (D3D11VA) decode reports InvalidMedia,
    // re-open the source forcing FFmpeg software decode before giving up.
    void maybeRetryWithSoftwareDecode();
    // Hot-switch the currently-loaded PV's decode mode in place on the same
    // QAVPlayer (stop -> setInputVideoCodec -> reload -> seek -> restore play
    // state). Same in-place reload as the software fallback, but driven by the
    // user's hardware/software preference and bidirectional. No app restart.
    void reloadVideoDecodeInPlace();
#endif
    // HW-decode diagnostics:
    // drain the QtAVPlayer copy-path cumulative counters into one runtime-log line on a
    // low-frequency cadence (seek / end-of-media). No-op off the QtAVPlayer/Windows path.
    void emitHwDecodeDiagSummary(const char* reason);
    void resetVideoFrameDiagnostics();
    bool updateVideoFrameStallState(bool logTransition);
    qint64 currentVideoFrameAgeForDiagnosticsMs() const;
    qint64 videoFrameStallThresholdMs() const;
    miacode::preview::pv_memory::Observation pvMemoryObservation(bool includeProcess) const;
    void emitPvMemoryRecords(const QVector<miacode::preview::pv_memory::Record>& records);
    void beginPvMemorySource();
    void observePvMemoryFrame(const QVideoFrame& frame,
                              const miacode::preview::pv_memory::ImageConversionFact& conversion);
    void recordPvMemoryBoundary(PvMemoryBoundary reason);
    void clearPvMemorySource();
    void latePvMemoryNoMedia();
    void postClearPvMemoryCheckpoint(quint64 clearEpoch, qint64 delayMs);
    void schedulePvMemoryPeriodicSample();
    void destroyPvMemorySource();

    MediaKind mediaKind_ = MediaKind::None;
    // 硬件/软件渲染 preference: false = hardware D3D11VA decode (default), true =
    // software. Set by MainWindow from the persisted user preference; read in
    // initializeBackendObjects (initial/rebuild decode choice) and applied live by
    // setVideoDecodePreference / reloadVideoDecodeInPlace.
    bool videoDecodePreferSoftware_ = false;
    QString chartPath_;
    QString mediaPath_;
    // Phase 4c — last-set raw `&video=` value. Part of the setChartPath skip key.
    QString chartVideoOverridePath_;
    // Content stamp (size:mtime) of the resolved media, so setChartPath skips a
    // re-decode only when the path, the override AND the file content are all
    // unchanged — a same-named clip with new bytes still forces a reload.
    QString mediaStamp_;
    QUrl imageSource_;
    // Phase 4c-8 — cached QImage of the loaded image-mode background.
    // Populated in loadImageMedia(); cleared in clearMedia/loadVideoMedia/
    // loadDcompVideoMedia. Surface reads this via currentBackgroundImage()
    // and pushes into PreviewBuildContext for StageBackgroundSource.
    QImage loadedBackgroundImage_;
    // Phase 4c-9 throttle — caps QVideoFrame::toImage() conversions
    // to ~30Hz to keep the GUI thread under per-tick budget. Each
    // conversion is ~5–10ms (YUV→RGB + 2.5+ MB allocation) for a
    // 1080p source; without throttling, a 30fps video produces
    // 30 conversions/sec on the GUI thread, which pushes the
    // 60Hz chart-preview tick into stutter territory (HUD stutter
    // count climbed to ~20 during ECHO playback before throttling).
    QElapsedTimer videoFrameToImageThrottle_;
    double videoFrameToImageMaxFps_ = 30.0;
    bool mediaVisible_ = true;
    PreviewBackgroundScaleMode backgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    double layoutSquareScale_ = miacode::preview_video::kLayoutSquareScaleDefault;
#ifdef MIACODE_USE_QTAVPLAYER
    // FFmpeg decode backend. setSpeed() runs inside QtAVPlayer's own decode
    // loop (no Qt converter rebuild) so rate changes never race the QSG
    // texture sampler — the class of crash the QMediaPlayer scaffolding below
    // existed to paper over. No QAudioOutput: the video's own audio track is
    // intentionally never played (song audio is BASS-owned).
    QAVPlayer* player_ = nullptr;
    QMetaObject::Connection videoFrameConnection_;
    QMetaObject::Connection seekedConnection_;
    bool videoBackendLoaded_ = false;
    bool softwareDecodeFallbackTried_ = false;
    double lastFramePtsSeconds_ = -1.0;
    // Latest decoded frame, replayed into the QML sink when a VideoOutput
    // attaches after decoding has already produced frames (e.g. paused bg) —
    // the push model has no continuous source to re-pull from like
    // QMediaPlayer::setVideoOutput did.
    QVideoFrame lastVideoFrame_;
#else
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QMetaObject::Connection videoSinkFrameConnection_;
#endif
    QPointer<QObject> videoOutputObject_;
    QPointer<QObject> innerVideoOutputObject_;
    QPointer<QVideoSink> videoSink_;
    QPointer<QVideoSink> innerVideoSink_;
    quint64 videoSourceGeneration_ = 0;
    double timelineOffsetSeconds_ = 0.0;
    double playbackRate_ = 1.0;
    miacode::diagnostics::PlaybackRateLogGate playbackRateLogGate_;
    int syncVideoFrameBeaconBudget_ = 0;
    int syncMediaStatusBeaconBudget_ = 0;
    // G2 Commit 1: Qt 6.8 FFmpeg's QMediaPlayer::setPlaybackRate has a race
    // when the player is in a transient mediaStatus (LoadingMedia /
    // BufferingMedia / StalledMedia / InvalidMedia) — the rate write either
    // gets silently dropped or fights with the buffer-fill loop. When
    // setPlaybackRate is called in those states we cache the requested rate
    // here and re-apply it from mediaStatusChanged once the player lands in
    // a stable state. Matches the deferred-apply pattern outlined in
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §6.2.
    bool pendingPlaybackRateApply_ = false;
    quint64 playbackTransactionId_ = 0;
    qint64 preparedPlaybackTargetMs_ = -1;
    double preparedPlaybackTargetSecond_ = 0.0;
    quint64 preparedPlaybackTransaction_ = 0;
    bool preparedPlaybackPending_ = false;
    bool preparedPlaybackReady_ = false;
    double lastTimelineSecond_ = 0.0;
    qint64 lastSeekMs_ = -1;
    // HW-decode diag: seek-landing latency clock + one-shot flag, read at the first
    // displayed frame after QAVPlayer::seeked to emit the preview/seek_landing line.
    QElapsedTimer seekCatchupTimer_;
    bool seekLatencyPending_ = false;
    qint64 pausedSeekTargetMs_ = -1;
    double pausedSeekTargetSecond_ = 0.0;
    quint64 pausedSeekGeneration_ = 0;
    bool pausedSeekCompletionPending_ = false;
    quint64 preparedPlaybackTimeoutSerial_ = 0;
    quint64 pausedSeekTimeoutSerial_ = 0;
    quint64 videoPlaybackWatchdogSerial_ = 0;
    bool recoveringVideoBackend_ = false;
    int consecutiveVideoBackendRecoveryCount_ = 0;
    int consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
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
    miacode::preview::pv_memory::Diagnostics pvMemoryDiagnostics_;
    QElapsedTimer pvMemoryElapsed_;
    bool pvMemoryPeriodicTimerArmed_ = false;
    quint64 pvMemoryPeriodicTimerEpoch_ = 0;
    bool shuttingDown_ = false;
};
