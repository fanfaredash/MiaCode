#pragma once

#include <memory>

#include <QObject>
#include <QHash>
#include <QMutex>

#include "PreviewAudioBackend.h"

class BassPreviewAudioBackend final : public QObject, public miacode::preview_audio::PreviewAudioBackend
{
public:
    using PausePreviewResult = miacode::preview_audio::PausePreviewResult;
    using Event = miacode::preview_sfx_timeline::Event;
    using TouchholdSpan = miacode::preview_sfx_timeline::TouchholdSpan;
    using CollapsedEventGroup = miacode::preview_sfx_timeline::CollapsedEventGroup;

    explicit BassPreviewAudioBackend(QObject* parent = nullptr);
    ~BassPreviewAudioBackend() override;

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
    struct Sample;

    struct WarmupPathState {
        QString chartPath;
        QString trackPath;
        QString sfxDir;
    };

    struct PreparedAssetState {
        QString chartPath;
        QString trackPath;
        QString sfxDir;
    };

    struct TimelineProgramState {
        QVector<Event> events;
        QVector<TouchholdSpan> touchholdSpans;
        QVector<TimelineNoteMarker> sourceNoteMarkers;
    };

    struct PreparedPlaybackState {
        bool pending = false;
        bool resumeFromPause = false;
        double startSecond = 0.0;
    };

    struct PlaybackSessionState {
        int eventGroupIndex = 0;
        bool masterRunning = false;
        bool backgroundTrackRunning = false;
        bool backgroundTrackPendingStart = false;
        double backgroundTrackPendingStartSecond = 0.0;
        double backgroundTrackOffsetSeconds = 0.0;
        double backgroundTrackPlaybackRate = 1.0;
        double sessionStartSecond = 0.0;
        double sessionPlaybackRate = 1.0;
        double lastAuthoritativeSecond = 0.0;
        double lastStatusLogSecond = -1.0;
        double lastTriggeredGroupSecond = -1.0;
        int lastTriggeredGroupIndex = -1;
        int triggeredGroupCount = 0;
    };

    QString resolveTrackPath(const QString& chartPath) const;
    QString resolveSfxDir() const;
    bool runtimeLibrariesPresent() const;
    bool initializeAudioEngine();
    bool ensureBassFxLoaded();
    void unloadBassFx();
    void loadOptionalPlugins();
    void unloadOptionalPlugins();
    void initializeAssets();
    void resetAssets();
    void clearPreparedTimeline();
    void rebuildPreparedTimeline(
        const QVector<TimelineNoteMarker>& noteMarkers,
        double playbackRate,
        const PreviewTimingSettings& timingSettings);
    void rebuildPreparedGroups();
    void refreshPreparedAssets();
    void applySampleLevels();
    Sample* sampleForKind(const QString& kind) const;
    void setBackgroundTrackSampleSpeed(double rate);
    void resetMasterMixerClock(double startSecond);
    void stopAllSamples();
    void stopPlaybackSession();
    double authoritativeSecond() const;
    void configureBackgroundTrackForSecond(double second);
    bool maybeStartPendingBackgroundTrack(double second);
    bool playKindInternal(const QString& kind, double gain = 1.0);
    void triggerGroup(const CollapsedEventGroup& group);
    void clearScheduledGroupSync();
    void armNextGroupSync(double currentSecond);
    void logPlaybackStatus(double authoritativeSecond, double fallbackSecond);
    void logPreparedEventWindow(double startSecond) const;
    QString groupSignature(const CollapsedEventGroup& group) const;
#ifdef Q_OS_WIN
    static void onMixerGroupSync(quint32 handle, quint32 channel, quint32 data, void* user);
    void handleMixerGroupSync(quint32 handle);
#endif

    PreviewAudioSettings settings_;
    PreviewTimingSettings timingSettings_;
    WarmupPathState warmupPaths_;
    PreparedAssetState preparedAssets_;
    TimelineProgramState preparedTimeline_;
    QVector<CollapsedEventGroup> preparedGroups_;
    PreparedPlaybackState preparedPlayback_;
    PlaybackSessionState playbackSession_;
    quint64 playbackTransactionId_ = 0;
    quint32 deviceSampleRate_ = 48000;
    double preparedTimelinePlaybackRate_ = 1.0;
    bool engineInitialized_ = false;
    quint32 masterMixer_ = 0;
    quint32 pluginAac_ = 0;
    quint32 pluginOpus_ = 0;
    void* bassFxModule_ = nullptr;
    void* bassFxTempoCreate_ = nullptr;
    QMutex schedulerMutex_;
    quint32 scheduledGroupSync_ = 0;
    int scheduledGroupIndex_ = -1;
    QHash<QString, Sample*> samplesByKind_;
    Sample* backgroundTrackSample_ = nullptr;
    Sample* touchholdSample_ = nullptr;
    std::unique_ptr<Sample> answerSample_;
    std::unique_ptr<Sample> judgeSample_;
    std::unique_ptr<Sample> judgeBreakSample_;
    std::unique_ptr<Sample> slideSample_;
    std::unique_ptr<Sample> breakSample_;
    std::unique_ptr<Sample> breakSlideStartSample_;
    std::unique_ptr<Sample> breakSlideFinishSample_;
    std::unique_ptr<Sample> judgeBreakSlideSample_;
    std::unique_ptr<Sample> exSample_;
    std::unique_ptr<Sample> touchSample_;
    std::unique_ptr<Sample> touchholdSampleOwner_;
    std::unique_ptr<Sample> fireworkSample_;
    std::unique_ptr<Sample> backgroundTrackSampleOwner_;
};
