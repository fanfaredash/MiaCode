#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "common/PreviewSfxTimeline.h"
#include "PreviewAudioSettings.h"

class QtPreviewSfxRuntime : public QObject
{
    Q_OBJECT

public:
    struct PausePreviewResult {
        bool usedBackgroundTrack = false;
        double pauseSecond = 0.0;
    };

    explicit QtPreviewSfxRuntime(QObject* parent = nullptr);
    ~QtPreviewSfxRuntime() override;

    void setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir);
    void reloadAssets(const PreviewAudioSettings& settings);
    bool audioEngineInitialized() const;
    void setChartPath(const QString& chartPath);
    void setBackgroundTrackOffsetSeconds(double seconds);
    void setBackgroundTrackPlaybackRate(double rate);
    void applyLevels(const PreviewAudioSettings& settings);
    void configureTimeline(const QVector<TimelineNoteMarker>& noteMarkers);
    void clearTimeline();
    void setPlaybackTransactionId(quint64 transactionId);
    double preparePreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate);
    void commitPreparedPreviewPlayback();
    void cancelPreparedPreviewPlayback();
    double preparedStartSecond() const;
    void applyPausedPreviewState(
        const QVector<TimelineNoteMarker>& noteMarkers,
        bool noteMarkersChanged,
        double pauseSecond);
    double startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate);
    PausePreviewResult capturePausedPreviewTransaction();
    double syncPreviewPlaybackClockTransaction(double fallbackSecond);
    void resetCursor(double second, bool includeCurrentSecond);
    void drainEvents(double second);
    void pauseTouchholdVoices();
    void restoreTouchholdVoices(double second);
    void syncBackgroundTrack(double timelineSecond);
    bool hasBackgroundTrack() const;
    bool isBackgroundTrackRunning() const;
    void startBackgroundTrack(double second);
    void seekBackgroundTrack(double second);
    void pauseBackgroundTrack();
    double backgroundPlaybackSecond() const;
    bool audition(const QString& kind, double gain = 1.0);
    void stopAll();

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
    };

    struct PlaybackSessionState {
        int eventIndex = 0;
        bool backgroundTrackRunning = false;
        bool backgroundTrackPendingStart = false;
        double backgroundTrackOffsetSeconds = 0.0;
        double backgroundTrackPlaybackRate = 1.0;
        double backgroundTrackLastTimelineSecond = 0.0;
    };

    struct PreparedPlaybackState {
        bool pending = false;
        bool resumeFromPause = false;
        double startSecond = 0.0;
    };

    QString resolveTrackPath(const QString& chartPath) const;
    QString resolveSfxDir() const;
    void rebuildPreparedTimeline(const QVector<TimelineNoteMarker>& noteMarkers);
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
    void applyVolumes();
    bool playKindInternal(const QString& kind, double gain = 1.0);
    void startTouchholdSpan(int spanIndex, double offsetSeconds);
    void stopTouchholdSpan(int spanIndex);
    bool playTouchholdAudition();

    PreviewAudioSettings settings_;
    WarmupPathState warmupPaths_;
    PreparedAssetState preparedAssets_;
    TimelineProgramState preparedTimeline_;
    PlaybackSessionState playbackSession_;
    PreparedPlaybackState preparedPlayback_;
    quint64 playbackTransactionId_ = 0;
    quint64 touchholdSoundLengthFrames_ = 0;
    quint32 deviceSampleRate_ = 48000;
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
