#pragma once

#include "PreviewAudioCommandQueue.h"
#include "PreviewAudioWorkerFactory.h"
#include "PreviewAudioWorkerProtocol.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace miacode::preview_audio {

class PreviewAudioNonGuiBarrierSpecAccess;

class PreviewAudioHealthSampleSchedule
{
public:
    using Clock = std::chrono::steady_clock;

    explicit PreviewAudioHealthSampleSchedule(Clock::time_point startedAt)
        : deadline_(startedAt + kInterval)
    {
    }

    bool isDue(Clock::time_point now) const { return now >= deadline_; }
    Clock::time_point deadline() const { return deadline_; }
    void markSampled(Clock::time_point sampledAt) { deadline_ = sampledAt + kInterval; }

private:
    static constexpr auto kInterval = std::chrono::seconds(1);
    Clock::time_point deadline_;
};

class PreviewAudioWorker
{
public:
    // Invoked synchronously on the worker thread. The callback must not call
    // shutdownAndJoin() or destroy its PreviewAudioWorker.
    using CompletionCallback = std::function<void(const PreviewAudioCompletion&)>;
    // Invoked synchronously on the worker thread after a snapshot publication.
    // GUI facades must queue delivery to their QObject receiver.
    using SnapshotCallback = std::function<void(const PreviewAudioSnapshot&)>;

    explicit PreviewAudioWorker(
        PreviewAudioBackendFactory factory = productionPreviewAudioBackendFactory(),
        CompletionCallback completionCallback = {},
        std::thread::id facadeOwningThreadId = std::this_thread::get_id(),
        SnapshotCallback snapshotCallback = {});
    ~PreviewAudioWorker();

    PreviewAudioWorker(const PreviewAudioWorker&) = delete;
    PreviewAudioWorker& operator=(const PreviewAudioWorker&) = delete;

    WorkerPostResult post(PreviewAudioCommand command);
    PreviewAudioSnapshot snapshot() const;

    // These barriers are for non-GUI callers only. Calling them from the facade-owning
    // thread is a contract violation and returns FacadeOwningThread without waiting.
    // Completed and CompletionRetired are non-consuming: every caller observes the
    // same status while its bounded entry is retained (up to 128 completed and 128
    // retired sequences). Callers that need a result must wait before that window
    // is exhausted.
    NonGuiBarrierWaitStatus waitForReadyForNonGui(std::chrono::milliseconds timeout);
    NonGuiBarrierWaitStatus waitForCompletionForNonGui(
        quint64 sequence,
        std::chrono::milliseconds timeout);
    void shutdownAndJoin();

private:
    friend class PreviewAudioNonGuiBarrierSpecAccess;

    struct BackendSnapshot;
    struct RuntimeState;

    void run();
    void execute(
        PreviewAudioCommand command,
        std::unique_ptr<PreviewAudioBackend>& backend,
        RuntimeState& state);
    void executeReload(
        const PreviewAudioCommand& command,
        std::unique_ptr<PreviewAudioBackend>& backend,
        RuntimeState& state,
        PreviewAudioCompletion& completion);
    BackendSnapshot captureBackendSnapshot(PreviewAudioBackend& backend) const;
    void publishLifecycle(
        WorkerLifecycle lifecycle,
        CommandError error = CommandError::None,
        const QString& detail = {},
        const CommandIdentity* identity = nullptr,
        int nativeErrorCode = 0);
    PreviewAudioSnapshot updateLifecycleSnapshot(
        WorkerLifecycle lifecycle,
        CommandError error,
        const QString& detail,
        const CommandIdentity* identity,
        int nativeErrorCode);
    bool publishAssetLifecycleIfCurrent(
        WorkerLifecycle lifecycle,
        const CommandIdentity& identity);
    void publishBackendLifecycle(
        WorkerLifecycle lifecycle,
        const BackendSnapshot& backendSnapshot,
        const CommandIdentity* identity = nullptr);
    bool publishCompletion(
        const PreviewAudioCompletion& completion,
        const BackendSnapshot* backendSnapshot = nullptr,
        std::optional<WorkerLifecycle> lifecycle = std::nullopt,
        bool requireCurrentAssetGeneration = false);
    void finishCompletion(
        PreviewAudioCompletion completion,
        const BackendSnapshot* backendSnapshot = nullptr,
        std::optional<WorkerLifecycle> lifecycle = std::nullopt,
        bool requireCurrentAssetGeneration = false);
    void publishNonGuiLifecycle(WorkerLifecycle lifecycle);
    void setNonGuiWaitEnrollmentObserverForTest(std::function<void()> observer);
    void markCompletionRetiredForNonGui(quint64 sequence);
    void rememberRetiredCompletionForNonGuiLocked(quint64 sequence);
    void retainCompletionForNonGui(const PreviewAudioCompletion& completion);
    void deliverCompletion(const PreviewAudioCompletion& completion);
    void deliverSnapshot(const PreviewAudioSnapshot& snapshot);
    void sampleHealth(
        PreviewAudioBackend& backend,
        RuntimeState& state);
    void rejectQueuedCommands(CommandError error);
    bool isCurrentAssetGeneration(quint64 generation) const;

    PreviewAudioBackendFactory factory_;
    PreviewAudioCommandQueue queue_;

    mutable std::mutex snapshotMutex_;
    PreviewAudioSnapshot snapshot_;

    mutable std::mutex assetGenerationMutex_;
    quint64 latestAssetGeneration_ = 0;

    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;

    std::mutex callbackMutex_;
    std::condition_variable callbackCv_;
    CompletionCallback completionCallback_;
    SnapshotCallback snapshotCallback_;
    bool callbackDeliveryEnabled_ = true;
    std::size_t callbacksInFlight_ = 0;

    std::mutex shutdownMutex_;
    std::condition_variable stateCv_;

    std::mutex nonGuiBarrierMutex_;
    std::condition_variable nonGuiBarrierCv_;
    std::unordered_map<quint64, PreviewAudioCompletion> nonGuiCompletions_;
    std::deque<quint64> nonGuiCompletionOrder_;
    std::unordered_set<quint64> retiredNonGuiCompletions_;
    std::deque<quint64> retiredNonGuiCompletionOrder_;
    WorkerLifecycle nonGuiLifecycle_ = WorkerLifecycle::Constructing;
    bool nonGuiShuttingDown_ = false;
    std::function<void()> nonGuiWaitEnrollmentObserverForTest_;

    std::atomic_bool acceptingPosts_{true};
    std::atomic<quint64> nextCommandSequence_{1};
    std::atomic<quint64> workerThreadId_{0};
    const std::thread::id facadeOwningThreadId_;
    std::thread thread_;
};

}  // namespace miacode::preview_audio
