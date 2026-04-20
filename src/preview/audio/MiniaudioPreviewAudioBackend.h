#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "common/PreviewAudioMixConfig.h"
#include "PreviewAudioBackend.h"

class MiniaudioPreviewAudioBackend final : public QObject, public miacode::preview_audio::PreviewAudioBackend
{
public:
    using PausePreviewResult = miacode::preview_audio::PausePreviewResult;

    explicit MiniaudioPreviewAudioBackend(QObject* parent = nullptr);
    ~MiniaudioPreviewAudioBackend() override;

    QString backendId() const override;
    bool canBePrimary(QString* reason = nullptr) const override;

    void setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir) override;
    void reloadAssets(const PreviewAudioSettings& settings) override;
    bool audioEngineInitialized() const override;
    void setChartPath(const QString& chartPath) override;
    void setBackgroundTrackOffsetSeconds(double seconds) override;
    void setBackgroundTrackPlaybackRate(double rate) override;
    void applyLevels(const PreviewAudioSettings& settings) override;
    void configureTimeline(
        const QVector<TimelineNoteMarker>& noteMarkers,
        double playbackRate,
        const PreviewTimingSettings& timingSettings) override;
    void clearTimeline() override;
    void setPlaybackTransactionId(quint64 transactionId) override;
    double preparePreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate) override;
    void commitPreparedPreviewPlayback() override;
    void cancelPreparedPreviewPlayback() override;
    double preparedStartSecond() const override;
    void applyPausedPreviewState(
        const QVector<TimelineNoteMarker>& noteMarkers,
        bool noteMarkersChanged,
        double pauseSecond,
        double playbackRate,
        const PreviewTimingSettings& timingSettings) override;
    double startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate) override;
    PausePreviewResult capturePausedPreviewTransaction() override;
    double syncPreviewPlaybackClockTransaction(double fallbackSecond) override;
    void resetCursor(double second, bool includeCurrentSecond) override;
    void drainEvents(double second) override;
    void pauseTouchholdVoices() override;
    void restoreTouchholdVoices(double second) override;
    void syncBackgroundTrack(double timelineSecond) override;
    bool hasBackgroundTrack() const override;
    bool isBackgroundTrackRunning() const override;
    void startBackgroundTrack(double second) override;
    void seekBackgroundTrack(double second) override;
    void pauseBackgroundTrack() override;
    double backgroundPlaybackSecond() const override;
    bool audition(const QString& kind, double gain = 1.0) override;
    void stopAll() override;

private:
    using Event = miacode::preview_sfx_timeline::Event;
    using TouchholdSpan = miacode::preview_sfx_timeline::TouchholdSpan;

    struct EngineState;
    struct Voice;
    struct StretchedBackgroundState;

    struct SfxBank {
        QVector<Voice*> voices;
        int nextVoice = 0;
        bool configured = false;
    };

    struct WarmupPathState {
        QString chartPath;
        QString trackPath;
        QString sfxDir;
    };

    struct PreparedAssetState {
        QString chartPath;
        QString sfxDir;
        QString trackPath;
    };

    struct TimelineProgramState {
        QVector<Event> events;
        QVector<TouchholdSpan> touchholdSpans;
        QVector<TimelineNoteMarker> sourceNoteMarkers;
    };

    struct PlaybackSessionState {
        int eventIndex = 0;
        bool backgroundTrackRunning = false;
        bool backgroundTrackPendingStart = false;
        bool backgroundTrackClockArmed = false;
        double backgroundTrackOffsetSeconds = 0.0;
        double backgroundTrackPlaybackRate = 1.0;
        double backgroundTrackStartTimelineSecond = 0.0;
        double backgroundTrackClockPlaybackRate = 1.0;
        double backgroundTrackLastTimelineSecond = 0.0;
        quint64 backgroundTrackStartEngineFrame = 0;
    };

    struct PreparedPlaybackState {
        bool pending = false;
        bool resumeFromPause = false;
        double startSecond = 0.0;
    };

    QString resolveTrackPath(const QString& chartPath) const;
    QString resolveSfxDir() const;
    void rebuildPreparedTimeline(
        const QVector<TimelineNoteMarker>& noteMarkers,
        double playbackRate,
        const PreviewTimingSettings& timingSettings);
    void clearPreparedTimeline();
    void resetBackgroundTrackSessionState(double timelineSecond = 0.0);
    void resetBackgroundTrack();
    void resetStretchedBackgroundTrack();
    void resetBanks();
    bool initializeAudioEngine();
    void initializeAssets();
    void initializeBackgroundTrack();
    bool prepareStretchedBackgroundTrack(double timelineSecond);
    double stretchedBackgroundPlaybackSecond() const;
    bool stretchedBackgroundClockReady() const;
    void armBackgroundTrackClock(double timelineSecond);
    void clearBackgroundTrackClockAnchor();
    void applyVolumes();
    bool playKindInternal(const QString& kind, double gain = 1.0);
    void startTouchholdSpan(int spanIndex, double offsetSeconds);
    void stopTouchholdSpan(int spanIndex);
    bool playTouchholdAudition();

    PreviewAudioSettings settings_;
    PreviewTimingSettings timingSettings_;
    WarmupPathState warmupPaths_;
    PreparedAssetState preparedAssets_;
    TimelineProgramState preparedTimeline_;
    PlaybackSessionState playbackSession_;
    PreparedPlaybackState preparedPlayback_;
    quint64 playbackTransactionId_ = 0;
    quint64 touchholdSoundLengthFrames_ = 0;
    quint32 deviceSampleRate_ = static_cast<quint32>(miacode::preview_audio::kMixSampleRate);
    double preparedTimelinePlaybackRate_ = 1.0;
    double lastStretchedClockDriftLogSecond_ = -1.0;
    double lastStretchedClockDriftDeltaMs_ = 0.0;
    bool engineInitialized_ = false;
    Voice* touchholdVoice_ = nullptr;
    QVector<int> activeTouchholdSpanIndices_;
    Voice* backgroundTrackVoice_ = nullptr;
    bool backgroundTrackConfigured_ = false;
    StretchedBackgroundState* stretchedBackgroundState_ = nullptr;
    SfxBank answerSfx_;
    SfxBank judgeSfx_;
    SfxBank judgeBreakSfx_;
    SfxBank slideSfx_;
    SfxBank breakSfx_;
    SfxBank breakSlideStartSfx_;
    SfxBank breakSlideSfx_;
    SfxBank judgeBreakSlideSfx_;
    SfxBank exSfx_;
    SfxBank touchSfx_;
    SfxBank fireworkSfx_;
    EngineState* engineState_ = nullptr;
};
