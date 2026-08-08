#pragma once

#include "PreviewAudioWorker.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

#include <QObject>

class QtPreviewSfxRuntime : public QObject
{
    Q_OBJECT

public:
    using PausePreviewResult = miacode::preview_audio::PausePreviewResult;
    using RetainedPlaybackMode = miacode::preview_audio::RetainedPlaybackMode;
    using RetainedBgmState = miacode::preview_audio::RetainedBgmState;
    using Completion = miacode::preview_audio::PreviewAudioCompletion;

    struct DevicePauseRequest {
        miacode::preview_audio::CommandIdentity identity;
        miacode::preview_audio::WorkerPostResult post;
    };

    struct PauseSubmission {
        miacode::preview_audio::CommandIdentity identity;
        miacode::preview_audio::WorkerPostResult post;
        double visualFallbackSecond = 0.0;
    };

    struct AssetSubmission {
        miacode::preview_audio::CommandIdentity identity;
        miacode::preview_audio::WorkerPostResult post;
    };

    // The fallback is a snapshot only. The caller must wait for the completion
    // carrying this identity before treating the worker second as authoritative.
    struct PlaybackSubmission {
        miacode::preview_audio::CommandIdentity identity;
        miacode::preview_audio::WorkerPostResult post;
        double fallbackSecond = 0.0;
    };

    explicit QtPreviewSfxRuntime(QObject* parent = nullptr);
    explicit QtPreviewSfxRuntime(
        miacode::preview_audio::PreviewAudioBackendFactory factory,
        QObject* parent = nullptr);
    ~QtPreviewSfxRuntime() override;

    AssetSubmission setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir);
    AssetSubmission reloadAssets(const PreviewAudioSettings& settings);
    bool audioEngineInitialized() const;
    AssetSubmission setChartPath(const QString& chartPath);
    void setBackgroundTrackOffsetSeconds(double seconds);
    void setBackgroundTrackPlaybackRate(double rate);
    void applyPlaybackRateAtChartSecond(double rate, double chartSecond);
    void applyLevels(const PreviewAudioSettings& settings);
    void configureTimeline(
        const QVector<TimelineNoteMarker>& noteMarkers,
        double playbackRate,
        const PreviewTimingSettings& timingSettings);
    void clearTimeline();
    void setPlaybackTransactionId(quint64 transactionId);

    // These return the most recent worker snapshot until their asynchronous command
    // completion arrives. They never wait for, or directly enter, the audio backend.
    PlaybackSubmission preparePreviewPlaybackTransaction(
        double startSecond,
        bool resumeFromPause,
        double playbackRate);
    void commitPreparedPreviewPlayback();
    void cancelPreparedPreviewPlayback();
    double preparedStartSecond() const;
    void applyPausedPreviewState(
        const QVector<TimelineNoteMarker>& noteMarkers,
        bool noteMarkersChanged,
        double pauseSecond,
        double playbackRate,
        const PreviewTimingSettings& timingSettings);
    double startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate);
    PausePreviewResult capturePausedPreviewTransaction();
    PausePreviewResult pausePreviewPlaybackTransaction();
    PauseSubmission requestManualPause(quint64 transactionId, double wallSecond);
    DevicePauseRequest requestDeviceChangePause(
        quint64 transactionId,
        quint64 deviceSequence,
        quint64 pauseToken,
        double pauseSecond);
    PlaybackSubmission resumeRetainedPreviewPlaybackTransaction();
    PlaybackSubmission seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying);
    void resetRetainedPreviewPlaybackTransaction(double targetSecond);
    void clearRetainedPreviewPlaybackTransaction();
    RetainedPlaybackMode retainedPlaybackMode() const;
    RetainedBgmState retainedBgmState() const;
    quint64 playbackGeneration() const noexcept;
    quint64 assetGeneration() const noexcept;
    double authoritativePlaybackSecond() const;
    void stopSfxVoices();
    double syncPreviewPlaybackClockTransaction(double fallbackSecond);
    void resetCursor(double second, bool includeCurrentSecond);
    void drainEvents(double second);
    void pauseTouchholdVoices();
    void restoreTouchholdVoices(double second);
    void syncBackgroundTrack(double timelineSecond);
    bool hasBackgroundTrack() const;
    bool isBackgroundTrackRunning() const;
    miacode::preview_audio::WorkerPostResult startBackgroundTrack(double second);
    void seekBackgroundTrack(double second);
    void pauseBackgroundTrack();
    double backgroundPlaybackSecond() const;
    bool audition(const QString& kind, double gain = 1.0);
    void stopAll();

    // The only GUI-facing wait. Destructor shutdown uses the same sequence after
    // producer and callback delivery have been disabled.
    void prepareForShutdown();

    // These wrappers are intentionally available only for non-GUI command-line
    // tools. The worker rejects the facade-owning thread before it can wait.
    miacode::preview_audio::NonGuiBarrierWaitStatus waitForReadyForNonGui(
        std::chrono::milliseconds timeout);
    miacode::preview_audio::NonGuiBarrierWaitStatus waitForCompletionForNonGui(
        quint64 sequence,
        std::chrono::milliseconds timeout);

signals:
    void backendReadyChanged(bool ready);
    void commandCompleted(const QtPreviewSfxRuntime::Completion& completion);
    void previewPrepared(const QtPreviewSfxRuntime::Completion& completion);
    void previewPlaybackStarted(const QtPreviewSfxRuntime::Completion& completion);
    void previewPlaybackPaused(const QtPreviewSfxRuntime::Completion& completion);
    void retainedPlaybackCompleted(const QtPreviewSfxRuntime::Completion& completion);
    void auditionCompleted(const QtPreviewSfxRuntime::Completion& completion);

private:
    struct CallbackState {
        std::atomic_bool deliveryEnabled{true};
    };

    miacode::preview_audio::WorkerPostResult post(miacode::preview_audio::PreviewAudioCommand command);
    miacode::preview_audio::WorkerPostResult postDeviceChangePauseBarrier(
        miacode::preview_audio::PreviewAudioCommand command);
    miacode::preview_audio::PreviewAudioCommand makeCommand(
        miacode::preview_audio::CommandKind kind) const;
    quint64 advancePlaybackGeneration();
    void handleCompletion(const Completion& completion);
    void handleSnapshot(const miacode::preview_audio::PreviewAudioSnapshot& snapshot);
    miacode::preview_audio::PreviewAudioSnapshot lastSnapshot() const;
    PausePreviewResult lastPauseResult() const;
    void shutdownWorker();

    std::unique_ptr<miacode::preview_audio::PreviewAudioWorker> worker_;
    std::shared_ptr<CallbackState> callbackState_;
    mutable std::mutex snapshotMutex_;
    miacode::preview_audio::PreviewAudioSnapshot lastSnapshot_;
    std::atomic_bool acceptingCommands_{true};
    quint64 playbackGeneration_ = 1;
    quint64 assetGeneration_ = 1;
    quint64 transactionId_ = 0;
    quint64 deviceSequence_ = 0;
};

Q_DECLARE_METATYPE(QtPreviewSfxRuntime::Completion)
