#pragma once

#include "PreviewAudioCommandQueue.h"
#include "PreviewAudioWorkerFactory.h"
#include "PreviewAudioWorkerProtocol.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace miacode::preview_audio {

class PreviewAudioWorker
{
public:
    // Invoked synchronously on the worker thread. The callback must not call
    // shutdownAndJoin() or destroy its PreviewAudioWorker.
    using CompletionCallback = std::function<void(const PreviewAudioCompletion&)>;

    explicit PreviewAudioWorker(
        PreviewAudioBackendFactory factory = productionPreviewAudioBackendFactory(),
        CompletionCallback completionCallback = {});
    ~PreviewAudioWorker();

    PreviewAudioWorker(const PreviewAudioWorker&) = delete;
    PreviewAudioWorker& operator=(const PreviewAudioWorker&) = delete;

    WorkerPostResult post(PreviewAudioCommand command);
    PreviewAudioSnapshot snapshot() const;
    void shutdownAndJoin();

private:
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
    void deliverCompletion(const PreviewAudioCompletion& completion);
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
    bool callbackDeliveryEnabled_ = true;
    std::size_t callbacksInFlight_ = 0;

    std::mutex shutdownMutex_;
    std::condition_variable stateCv_;

    std::atomic_bool acceptingPosts_{true};
    std::atomic<quint64> nextCommandSequence_{1};
    std::atomic<quint64> workerThreadId_{0};
    std::thread thread_;
};

}  // namespace miacode::preview_audio
