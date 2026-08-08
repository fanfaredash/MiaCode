#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include <QObject>
#include <QHash>
#include <QMutex>

#include "common/PreviewAudioMixConfig.h"
#include "BassPreviewDebugLogRouting.h"
#include "BassPreviewSfxSchedulerPolicy.h"
#include "PreviewBassDeviceLease.h"
#include "PreviewAudioBackend.h"
#include "PreviewAudioHealth.h"

class BassPreviewAudioBackend final : public QObject, public miacode::preview_audio::PreviewAudioBackend
{
public:
    using PausePreviewResult = miacode::preview_audio::PausePreviewResult;
    using RetainedPlaybackMode = miacode::preview_audio::RetainedPlaybackMode;
    using RetainedBgmState = miacode::preview_audio::RetainedBgmState;
    using Event = miacode::preview_sfx_timeline::Event;
    using TouchholdSpan = miacode::preview_sfx_timeline::TouchholdSpan;
    using CollapsedEventGroup = miacode::preview_sfx_timeline::CollapsedEventGroup;

    explicit BassPreviewAudioBackend(QObject* parent = nullptr);
    ~BassPreviewAudioBackend() override;

    QString backendId() const override;
    bool canBePrimary(QString* reason = nullptr) const override;
    int nativeErrorCode() const noexcept override;
    void clearNativeErrorCode() noexcept override;

    void setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir) override;
    void reloadAssets(const PreviewAudioSettings& settings) override;
    bool audioEngineInitialized() const override;
    void setChartPath(const QString& chartPath) override;
    void setBackgroundTrackOffsetSeconds(double seconds) override;
    void setBackgroundTrackPlaybackRate(double rate) override;
    void applyPlaybackRateAtChartSecond(double rate, double chartSecond) override;
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
    PausePreviewResult pausePreviewPlaybackTransaction() override;
    double resumeRetainedPreviewPlaybackTransaction() override;
    double seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying) override;
    void resetRetainedPreviewPlaybackTransaction(double targetSecond) override;
    void clearRetainedPreviewPlaybackTransaction() override;
    RetainedPlaybackMode retainedPlaybackMode() const override;
    RetainedBgmState retainedBgmState() const override;
    double authoritativePlaybackSecond() const override;
    void stopSfxVoices() override;
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
    void prepareForShutdown() override;

private:
    struct Sample;

    enum class ScheduledMixerAction {
        None,
        SfxGroup,
        StartPendingBackgroundTrack,
        SfxGroupAndStartPendingBackgroundTrack,
    };

    struct PreparedAssetState {
        QString chartPath;
        QString trackPath;
        QString sfxDir;
        // Content stamp (size:mtime) of the resolved track, so setChartPath can
        // detect a same-path/new-content track and force a reload instead of
        // skipping on path equality alone.
        QString trackStamp;
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
        // Underrun / buffer-health probe state. Separate from lastStatusLogSecond so the
        // (much coarser) health cadence and the ~1 Hz bass_status cadence stay independent.
        double lastHealthLogSecond = -1.0;
        miacode::preview_audio::health::StallTracker stallTracker;
        double lastTriggeredGroupSecond = -1.0;
        int lastTriggeredGroupIndex = -1;
        int triggeredGroupCount = 0;
    };

    // Deferred report of armNextGroupSyncLocked's self-deactivation. That function runs
    // with schedulerMutex_ held and, through handleMixerGroupSync, on the BASS mixer
    // thread, so it records the failure here instead of logging it; the callers drain
    // this once they have released the lock.
    struct SfxSchedulerArmFailure {
        bool pending = false;
        int bassError = 0;
        double targetChartSecond = 0.0;
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
    double retainedTransportSecond() const;
    bool retainedSecondMatches(double targetSecond) const;
    void noteInitWindowOpened(const QString& reason);
    void noteTransportReady(const QString& reason);
    void appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation operation,
        const QString& payload = QString(),
        bool initWindowContext = false) const;
    void setBackgroundTrackSampleSpeed(double rate);
    void invalidateRetainedPlaybackState(const QString& reason);
    void updateRetainedBgmState();
    void logTrackFileMissingAfterLoadIfNeeded();
    void suspendPlaybackTransport();
    void anchorTransportToSecond(double targetSecond, const QString& reason);
    void clearResidualVoicesForPausedReposition();
    void repositionMasterTransportClock(double targetSecond);
    void repositionPausedTransportToSecond(double targetSecond, const QString& reason);
    void startTransportFromCurrentAnchor();
    void resetMasterMixerClock(double startSecond);
    // `reason` names the caller in the emitted `action=disarm` line. A disarm that is
    // never followed by a re-anchor silently drops live SFX to the GUI drainEvents
    // fallback, and the log only showed anchors, so the author of a missing re-anchor
    // could not be identified from a capture.
    void disarmSfxScheduler(const char* reason);
    void anchorSfxScheduler(double chartSecond);
    double currentSfxSchedulerChartSecond(double fallbackSecond) const;
    void armNextGroupSyncLocked();
    // Must be called with schedulerMutex_ released.
    void logSfxSchedulerArmFailure(const SfxSchedulerArmFailure& failure) const;
    void stopAllSamples();
    void stopPlaybackSession();
    double authoritativeSecond() const;
    void configureBackgroundTrackForSecond(
        double second,
        const QString& reason,
        miacode::preview_audio::bass::BassDebugRoute route);
    bool maybeStartPendingBackgroundTrack(double second);
    bool playKindInternal(
        const QString& kind,
        double gain = 1.0,
        int* nativeErrorCode = nullptr);
    // What reconcileTouchholdVoice() did, so the caller can log it after releasing
    // schedulerMutex_. The audio-thread path reaches this function from triggerGroup(),
    // i.e. under that lock, and a log write there is the stall the buffer-health probe
    // exists to catch. `changed` is false when the voice already belonged to the right
    // span, which is the common case and emits nothing.
    struct TouchholdTransition {
        bool changed = false;
        int owner = -1;
        int previousOwner = -1;
        double second = 0.0;
        double spanStartSecond = -1.0;
    };
    // Pass `out` when holding schedulerMutex_: the transition is recorded rather than
    // logged, and the caller emits it once the lock is gone. With `out` null the function
    // logs directly, which is correct only on the GUI path where no lock is held.
    void reconcileTouchholdVoice(double second, TouchholdTransition* out = nullptr);
    void logTouchholdTransition(const TouchholdTransition& transition) const;
    // `playedKindsOut`, when non-null, receives a compact "kind:gain" list of the
    // samples that ACTUALLY started. The group-level logs record a decision to
    // trigger; this records the sound. Collected rather than logged in place
    // because the mixer-sync caller runs under the scheduler mutex on the BASS
    // audio thread and must not log there.
    void triggerGroup(
        const CollapsedEventGroup& group,
        QString* playedKindsOut = nullptr,
        TouchholdTransition* touchholdOut = nullptr);
    // Stable bass_status token for the action the mixer sync is armed for. A member
    // rather than a neighbour of retainedPlaybackModeLabel in
    // BassPreviewAudioBackendImpl.h because ScheduledMixerAction is private here.
    static QString scheduledMixerActionLabel(ScheduledMixerAction action);
    void logPlaybackStatus(double authoritativeSecond, double fallbackSecond);
    // Underrun / buffer-level probe. Polled at tick rate (cheap BASS_ChannelIsActive
    // calls) so a stall edge is caught immediately; the fuller buffer-health line
    // self-throttles to health::kHealthSampleIntervalSeconds.
    void logAudioHealth(double authoritativeSecond);

    // ---- audio-health sampling: BASS queries, off the GUI thread ------------------
    //
    // BASS_ChannelIsActive / BASS_Mixer_ChannelIsActive / BASS_ChannelGetData all take
    // BASS's internal device lock. During a Windows audio endpoint switch BASS holds that
    // lock while it tears the old output device down and builds the new one, which on the
    // captures behind this change meant 2.0-4.5 s. logAudioHealth used to make those calls
    // ON THE GUI THREAD, at tick rate, so the GUI thread parked on that lock for the whole
    // switch -- and Qt's threaded render loop parks with it at the next sync, which is why
    // both visual threads died together while audio played on undisturbed.
    //
    // Bracketing all 27 stall episodes across five captures put the GUI thread's last log
    // line at this probe 18 times; at 3.9% of GUI-thread lines, chance predicts 1. So the
    // probe is moved off the GUI thread entirely rather than merely called less often:
    // a sampler thread owns every BASS query, and the GUI thread reads the published
    // snapshot. When BASS blocks now, only the sampler blocks, and it is the one thread
    // with nothing waiting on it.
    struct HealthSample {
        miacode::preview_audio::health::ChannelActivity mixerActivity =
            miacode::preview_audio::health::ChannelActivity::Unknown;
        miacode::preview_audio::health::ChannelActivity backgroundActivity =
            miacode::preview_audio::health::ChannelActivity::Unknown;
        miacode::preview_audio::health::BufferSnapshot buffer;
        // The BGM stream's own position. Sampled here rather than via
        // Sample::currentSec() on the GUI thread, because that is
        // BASS_Mixer_ChannelGetPosition + BASS_ChannelBytes2Seconds -- two more calls on
        // the same device lock, and logPlaybackStatus made them while holding
        // schedulerMutex_, so a blocked GUI thread also stalled the mixer callback.
        // -1.0 when there is no background track, matching the previous sentinel.
        double bgmRawSecond = -1.0;
        // Wall-clock age of this sample, so a reader can tell a fresh snapshot from one
        // frozen by a sampler that is itself stuck in BASS -- which is now the visible
        // signature of the endpoint switch instead of a frozen UI.
        qint64 sampledAtMs = 0;
        quint64 sequence = 0;
    };

    void startAudioHealthSampler();
    void stopAudioHealthSampler();
    void publishAudioHealthHandles();

    std::thread audioHealthSamplerThread_;
    std::atomic_bool audioHealthSamplerStop_{false};
    // Handles only, never C++ objects: a BASS handle is an integer, so a stale one makes
    // BASS return an error instead of dereferencing freed memory. This is what lets the
    // sampler run without any lifetime coupling to backgroundTrackSample_.
    std::atomic<quint32> audioHealthMixerHandle_{0};
    std::atomic<quint32> audioHealthSourceHandle_{0};
    // Gates the BASS queries on playback actually running. Without it the sampler polls
    // the device 10x a second for the whole process lifetime, including while idle --
    // strictly more contact with BASS than the per-tick probe it replaced, which at least
    // only ran while playing. A user hit a ~2 s hitch shortly after launch on the first
    // build of this sampler; idle polling is the part of it that was never wanted.
    std::atomic_bool audioHealthPlaybackRunning_{false};
    mutable QMutex audioHealthSampleMutex_;
    HealthSample audioHealthSample_;
    void logPreparedEventWindow(double startSecond) const;
    QString groupSignature(const CollapsedEventGroup& group) const;
    static void onMixerGroupSync(quint32 handle, quint32 channel, quint32 data, void* user);
    void handleMixerGroupSync(quint32 handle);

    PreviewAudioSettings settings_;
    PreviewTimingSettings timingSettings_;
    PreparedAssetState preparedAssets_;
    TimelineProgramState preparedTimeline_;
    QVector<CollapsedEventGroup> preparedGroups_;
    PreparedPlaybackState preparedPlayback_;
    PlaybackSessionState playbackSession_;
    quint64 playbackTransactionId_ = 0;
    quint32 deviceSampleRate_ = static_cast<quint32>(miacode::preview_audio::kMixSampleRate);
    double preparedTimelinePlaybackRate_ = 1.0;
    bool engineInitialized_ = false;
    int lastNativeErrorCode_ = 0;
    quint32 masterMixer_ = 0;
    quint32 pluginAac_ = 0;
    quint32 pluginOpus_ = 0;
    miacode::preview_audio::PreviewBassDeviceLease bassDeviceLease_;
    void* bassFxModule_ = nullptr;
    void* bassFxTempoCreate_ = nullptr;
    RetainedPlaybackMode retainedPlaybackMode_ = RetainedPlaybackMode::None;
    RetainedBgmState retainedBgmState_ = RetainedBgmState::NoneLoaded;
    bool initWindowActive_ = true;
    quint64 transportReadyGeneration_ = 0;
    bool trackMissingAfterLoadLogged_ = false;
    std::atomic_bool shuttingDown_ = false;
    // Held by the GUI thread AND by the BASS mixer callback thread (handleMixerGroupSync).
    // That makes it an audio-callback lock, so its rule is stricter than "guard the shared
    // fields":
    //
    //   While holding schedulerMutex_: NO I/O, NO logging, NO calls back into BASS.
    //
    // Time under this lock is time the mixer callback cannot trigger its next group of note
    // sounds; it turns directly into late notes and underruns. Calling into BASS under it
    // is worse than slow -- BASS_ChannelRemoveSync waits for the very sync callback that is
    // itself waiting on this mutex, an ABBA deadlock that presents as a frozen GUI thread
    // and is indistinguishable from the freezes this branch exists to diagnose.
    //
    // Snapshot into locals, release, then format / write / call out. Four sites do this:
    // handleMixerGroupSync, disarmSfxScheduler, logPlaybackStatus, and -- reached from
    // handleMixerGroupSync via triggerGroup, which is why it was the easiest to miss --
    // reconcileTouchholdVoice, whose row is deferred through TouchholdTransition.
    mutable QMutex schedulerMutex_;
    quint32 scheduledGroupSync_ = 0;
    int scheduledGroupIndex_ = -1;
    ScheduledMixerAction scheduledMixerAction_ = ScheduledMixerAction::None;
    bool sfxSchedulerActive_ = false;
    SfxSchedulerArmFailure sfxSchedulerArmFailure_;
    miacode::preview_audio::bass::SfxSchedulerAnchor sfxSchedulerAnchor_;
    quint64 sfxSchedulerAnchorDecodePosition_ = 0;
    QHash<QString, Sample*> samplesByKind_;
    Sample* backgroundTrackSample_ = nullptr;
    Sample* touchholdSample_ = nullptr;
    int touchholdOwnerSpanIndex_ = -1;
    std::unique_ptr<Sample> answerSample_;
    std::unique_ptr<Sample> judgeSample_;
    std::unique_ptr<Sample> judgeBreakSample_;
    std::unique_ptr<Sample> slideSample_;
    std::unique_ptr<Sample> breakSample_;
    std::unique_ptr<Sample> breakSlideStartSample_;
    std::unique_ptr<Sample> breakSlideFinishSample_;
    std::unique_ptr<Sample> breakSlideTailBreakSample_;
    std::unique_ptr<Sample> judgeBreakSlideSample_;
    std::unique_ptr<Sample> exSample_;
    std::unique_ptr<Sample> touchSample_;
    std::unique_ptr<Sample> touchholdSampleOwner_;
    std::unique_ptr<Sample> fireworkSample_;
    std::unique_ptr<Sample> clockSample_;  // clock_count count-in (audition only)
    std::unique_ptr<Sample> trackStartSample_;  // 片头 opening jingle (audition only)
    std::unique_ptr<Sample> backgroundTrackSampleOwner_;
};
