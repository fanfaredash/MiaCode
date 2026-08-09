#include "PreviewAudioWorker.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/Mmcss.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace miacode::preview_audio {

namespace {

using SteadyClock = std::chrono::steady_clock;
thread_local PreviewAudioWorker* currentPreviewAudioWorker = nullptr;
constexpr std::size_t kNonGuiCompletionRetentionCapacity = 128;

qint64 steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        SteadyClock::now().time_since_epoch()).count();
}

quint64 currentThreadId()
{
    quint64 value = static_cast<quint64>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    if (value == 0) {
        value = 1;
    }
    return value;
}

QString exceptionDetail(const std::exception& error)
{
    return QString::fromUtf8(error.what());
}

bool runtimeAudioDebugEnabled()
{
    return miacode::debug_options::audioDebugOutputEnabled();
}

void appendAudioDebugLog(const QString& message)
{
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Audio, QString(), message);
}

}  // namespace

struct PreviewAudioWorker::BackendSnapshot {
    QString backendId;
    bool ready = false;
    bool backgroundTrackAvailable = false;
    bool backgroundTrackRunning = false;
    double preparedSecond = 0.0;
    double authoritativeSecond = 0.0;
    double backgroundSecond = 0.0;
    RetainedPlaybackMode retainedMode = RetainedPlaybackMode::None;
    RetainedBgmState retainedBgmState = RetainedBgmState::NoneLoaded;
};

struct PreviewAudioWorker::RuntimeState {
    QString chartPath;
    QString trackPath;
    QString sfxDirectory;
    quint64 warmupAssetGeneration = 0;
    bool hasWarmupPaths = false;
    QString activeChartPath;
    bool hasActiveChartPath = false;
    quint64 pauseBarrierGeneration = 0;
    health::StallTracker healthStallTracker;
    double lastHealthLogSecond = -1.0;
};

PreviewAudioWorker::PreviewAudioWorker(
    PreviewAudioBackendFactory factory,
    CompletionCallback completionCallback,
    std::thread::id facadeOwningThreadId,
    SnapshotCallback snapshotCallback)
    : factory_(std::move(factory))
    , completionCallback_(std::move(completionCallback))
    , snapshotCallback_(std::move(snapshotCallback))
    , facadeOwningThreadId_(facadeOwningThreadId)
{
    thread_ = std::thread([this] { run(); });
}

PreviewAudioWorker::~PreviewAudioWorker()
{
    shutdownAndJoin();
}

WorkerPostResult PreviewAudioWorker::post(PreviewAudioCommand command)
{
    const quint64 sequence = nextCommandSequence_.fetch_add(1, std::memory_order_relaxed);
    command.identity.sequence = sequence;
    command.enqueuedAtNs = steadyNowNs();

    const CommandPolicy policy = commandPolicy(command.kind);
    const quint64 assetGeneration = command.identity.assetGeneration;

    if (!acceptingPosts_.load(std::memory_order_acquire)) {
        return {false, false, false, CommandError::ShuttingDown, sequence};
    }

    EnqueueResult result;
    {
        std::lock_guard wakeLock(wakeMutex_);
        if (!acceptingPosts_.load(std::memory_order_acquire)) {
            return {false, false, false, CommandError::ShuttingDown, sequence};
        }
        if (policy.usesAssetGeneration) {
            std::lock_guard assetLock(assetGenerationMutex_);
            result = queue_.enqueue(std::move(command));
            if (result.accepted) {
                latestAssetGeneration_ = std::max(latestAssetGeneration_, assetGeneration);
            }
        } else {
            result = queue_.enqueue(std::move(command));
        }
    }
    if (result.retiredSequence != 0) {
        markCompletionRetiredForNonGui(result.retiredSequence);
    }
    if (result.accepted) {
        wakeCv_.notify_one();
    }
    return {result.accepted, result.replaced, result.coalesced, result.error, sequence};
}

WorkerPostResult PreviewAudioWorker::postDeviceChangePauseBarrier(PreviewAudioCommand command)
{
    const quint64 sequence = nextCommandSequence_.fetch_add(1, std::memory_order_relaxed);
    command.identity.sequence = sequence;
    command.enqueuedAtNs = steadyNowNs();
    if (command.kind != CommandKind::DeviceChangePause) {
        return {false, false, false, CommandError::BackendFailure, sequence};
    }
    if (!acceptingPosts_.load(std::memory_order_acquire)) {
        return {false, false, false, CommandError::ShuttingDown, sequence};
    }

    EnqueueResult result;
    {
        std::lock_guard wakeLock(wakeMutex_);
        if (!acceptingPosts_.load(std::memory_order_acquire)) {
            return {false, false, false, CommandError::ShuttingDown, sequence};
        }
        result = queue_.enqueueDeviceChangePauseBarrier(std::move(command));
        if (!result.invalidatedCommands.empty()) {
            std::lock_guard invalidatedLock(deferredDevicePauseInvalidationsMutex_);
            for (PreviewAudioCommand& invalidated : result.invalidatedCommands) {
                deferredDevicePauseInvalidations_.push_back(std::move(invalidated));
            }
        }
    }
    if (result.retiredSequence != 0) {
        markCompletionRetiredForNonGui(result.retiredSequence);
    }
    if (result.accepted || !result.invalidatedCommands.empty()) {
        wakeCv_.notify_one();
    }
    return {result.accepted, result.replaced, result.coalesced, result.error, sequence};
}

PreviewAudioSnapshot PreviewAudioWorker::snapshot() const
{
    std::lock_guard lock(snapshotMutex_);
    return snapshot_;
}

NonGuiBarrierWaitStatus PreviewAudioWorker::waitForReadyForNonGui(
    std::chrono::milliseconds timeout)
{
    if (std::this_thread::get_id() == facadeOwningThreadId_) {
        return NonGuiBarrierWaitStatus::FacadeOwningThread;
    }

    std::unique_lock lock(nonGuiBarrierMutex_);
    if (nonGuiLifecycle_ != WorkerLifecycle::Ready && !nonGuiShuttingDown_
        && nonGuiWaitEnrollmentObserverForTest_) {
        nonGuiWaitEnrollmentObserverForTest_();
    }
    if (!nonGuiBarrierCv_.wait_for(lock, timeout, [this] {
            return nonGuiLifecycle_ == WorkerLifecycle::Ready || nonGuiShuttingDown_;
        })) {
        return NonGuiBarrierWaitStatus::Timeout;
    }
    return nonGuiShuttingDown_ ? NonGuiBarrierWaitStatus::ShuttingDown
                               : NonGuiBarrierWaitStatus::Ready;
}

NonGuiBarrierWaitStatus PreviewAudioWorker::waitForCompletionForNonGui(
    quint64 sequence,
    std::chrono::milliseconds timeout)
{
    if (std::this_thread::get_id() == facadeOwningThreadId_) {
        return NonGuiBarrierWaitStatus::FacadeOwningThread;
    }

    std::unique_lock lock(nonGuiBarrierMutex_);
    if (!nonGuiCompletions_.contains(sequence)
        && !retiredNonGuiCompletions_.contains(sequence)
        && !nonGuiShuttingDown_
        && nonGuiWaitEnrollmentObserverForTest_) {
        nonGuiWaitEnrollmentObserverForTest_();
    }
    if (!nonGuiBarrierCv_.wait_for(lock, timeout, [this, sequence] {
            return nonGuiCompletions_.contains(sequence)
                || retiredNonGuiCompletions_.contains(sequence)
                || nonGuiShuttingDown_;
        })) {
        return NonGuiBarrierWaitStatus::Timeout;
    }

    if (nonGuiShuttingDown_) {
        return NonGuiBarrierWaitStatus::ShuttingDown;
    }
    if (nonGuiCompletions_.contains(sequence)) {
        return NonGuiBarrierWaitStatus::Completed;
    }
    if (retiredNonGuiCompletions_.contains(sequence)) {
        return NonGuiBarrierWaitStatus::CompletionRetired;
    }
    return NonGuiBarrierWaitStatus::ShuttingDown;
}

void PreviewAudioWorker::shutdownAndJoin()
{
    if (currentPreviewAudioWorker == this) {
        throw std::logic_error(
            "PreviewAudioWorker::shutdownAndJoin must not be called from its worker thread");
    }

    std::unique_lock shutdownLock(shutdownMutex_);
    if (!thread_.joinable()) {
        return;
    }

    // Close producers before contending for wakeMutex_. A producer may post in a
    // tight loop while shutdown is trying to acquire that lock; waiting to close the
    // gate until after the lock lets it starve shutdown and accepts stale audio work.
    acceptingPosts_.store(false, std::memory_order_release);

    {
        std::scoped_lock gateLock(callbackMutex_, wakeMutex_);
        callbackDeliveryEnabled_ = false;
        {
            std::lock_guard barrierLock(nonGuiBarrierMutex_);
            nonGuiShuttingDown_ = true;
        }
        PreviewAudioCommand shutdown = makeHigh(CommandKind::Shutdown);
        shutdown.identity.sequence = nextCommandSequence_.fetch_add(1, std::memory_order_relaxed);
        shutdown.enqueuedAtNs = steadyNowNs();
        queue_.beginShutdown(std::move(shutdown));
    }
    nonGuiBarrierCv_.notify_all();
    wakeCv_.notify_one();
    {
        std::unique_lock callbackLock(callbackMutex_);
        callbackCv_.wait(callbackLock, [this] { return callbacksInFlight_ == 0; });
    }
    thread_.join();
}

void PreviewAudioWorker::run()
{
    currentPreviewAudioWorker = this;
    workerThreadId_.store(currentThreadId(), std::memory_order_release);
    publishLifecycle(WorkerLifecycle::Constructing);

    RuntimeState state;
    std::unique_ptr<PreviewAudioBackend> backend;
    try {
        backend = factory_ ? factory_() : nullptr;
        if (backend == nullptr) {
            publishLifecycle(
                WorkerLifecycle::Degraded,
                CommandError::BackendUnavailable,
                QStringLiteral("preview audio backend factory returned null"));
        } else {
            const BackendSnapshot backendState = captureBackendSnapshot(*backend);
            if (backendState.ready) {
                publishBackendLifecycle(WorkerLifecycle::Ready, backendState);
            } else {
                publishLifecycle(
                    WorkerLifecycle::Degraded,
                    CommandError::BackendUnavailable,
                    QStringLiteral("preview audio backend is not initialized"),
                    nullptr,
                    backend->nativeErrorCode());
            }
        }
    } catch (const std::exception& error) {
        backend.reset();
        publishLifecycle(WorkerLifecycle::Degraded, CommandError::BackendUnavailable, exceptionDetail(error));
    } catch (...) {
        backend.reset();
        publishLifecycle(
            WorkerLifecycle::Degraded,
            CommandError::BackendUnavailable,
            QStringLiteral("unknown preview audio backend factory failure"));
    }

    PreviewAudioHealthSampleSchedule healthSchedule(SteadyClock::now());
    for (;;) {
        std::optional<PreviewAudioCommand> command;
        std::optional<PreviewAudioCommand> invalidatedCommand;
        {
            std::unique_lock wakeLock(wakeMutex_);
            wakeCv_.wait_until(wakeLock, healthSchedule.deadline(), [this] {
                return !queue_.empty() || hasDeferredDevicePauseInvalidations();
            });
            if (state.pauseBarrierGeneration != 0) {
                invalidatedCommand = takeDeferredDevicePauseInvalidation();
            }
            if (!invalidatedCommand) {
                command = queue_.takeNext();
            }
        }
        if (invalidatedCommand) {
            deliverDevicePauseBarrierStale(std::move(*invalidatedCommand));
            continue;
        }
        const SteadyClock::time_point now = SteadyClock::now();
        if (!command) {
            if (backend != nullptr && healthSchedule.isDue(now)) {
                sampleHealth(*backend, state);
                healthSchedule.markSampled(SteadyClock::now());
            }
            continue;
        }
        if (command->kind == CommandKind::Shutdown) {
            rejectQueuedCommands(CommandError::ShuttingDown);
            break;
        }
        execute(std::move(*command), backend, state);
        const SteadyClock::time_point afterCommand = SteadyClock::now();
        if (backend != nullptr && healthSchedule.isDue(afterCommand)) {
            sampleHealth(*backend, state);
            healthSchedule.markSampled(SteadyClock::now());
        }
    }

    publishLifecycle(WorkerLifecycle::ShuttingDown, CommandError::ShuttingDown);
    CommandError shutdownError = CommandError::None;
    QString shutdownDetail;
    int shutdownNativeErrorCode = 0;
    if (backend != nullptr) {
        backend->clearNativeErrorCode();
        try {
            backend->prepareForShutdown();
        } catch (const std::exception& error) {
            shutdownError = CommandError::BackendFailure;
            shutdownDetail = exceptionDetail(error);
            shutdownNativeErrorCode = backend->nativeErrorCode();
            publishLifecycle(
                WorkerLifecycle::ShuttingDown,
                shutdownError,
                shutdownDetail,
                nullptr,
                shutdownNativeErrorCode);
        } catch (...) {
            shutdownError = CommandError::BackendFailure;
            shutdownDetail = QStringLiteral("unknown backend shutdown failure");
            shutdownNativeErrorCode = backend->nativeErrorCode();
            publishLifecycle(
                WorkerLifecycle::ShuttingDown,
                shutdownError,
                shutdownDetail,
                nullptr,
                shutdownNativeErrorCode);
        }
    }
    backend.reset();
    rejectQueuedCommands(CommandError::ShuttingDown);
    publishLifecycle(
        WorkerLifecycle::Stopped,
        shutdownError,
        shutdownDetail,
        nullptr,
        shutdownNativeErrorCode);
    currentPreviewAudioWorker = nullptr;
}

void PreviewAudioWorker::execute(
    PreviewAudioCommand command,
    std::unique_ptr<PreviewAudioBackend>& backend,
    RuntimeState& state)
{
    const qint64 startedAtNs = steadyNowNs();
    PreviewAudioCompletion completion;
    completion.kind = command.kind;
    completion.identity = command.identity;
    completion.workerThreadId = workerThreadId_.load(std::memory_order_acquire);
    completion.queueDelayNs = std::max<qint64>(0, startedAtNs - command.enqueuedAtNs);

    const CommandPolicy policy = commandPolicy(command.kind);
    const auto isAssetStale = [&] {
        return policy.usesAssetGeneration
            && !isCurrentAssetGeneration(command.identity.assetGeneration);
    };
    if (command.kind != CommandKind::ReloadAssets && isAssetStale()) {
        completion.error = CommandError::Stale;
        completion.success = false;
        completion.detail = QStringLiteral("asset generation was superseded before execution");
        completion.executionDurationNs = steadyNowNs() - startedAtNs;
        deliverCompletion(completion);
        return;
    }

    if (command.kind == CommandKind::DeviceChangePause) {
        state.pauseBarrierGeneration = std::max(
            state.pauseBarrierGeneration,
            command.identity.generation);
    } else if (policy.invalidatedByPlaybackBoundary
               && state.pauseBarrierGeneration != 0
               && command.identity.generation < state.pauseBarrierGeneration) {
        completion.error = CommandError::Stale;
        completion.success = false;
        completion.detail = QStringLiteral("device pause barrier superseded queued playback command");
        completion.executionDurationNs = steadyNowNs() - startedAtNs;
        deliverCompletion(completion);
        return;
    }

    if (command.kind == CommandKind::SetWarmupResolvedPaths) {
        state.chartPath = command.chartPath;
        state.trackPath = command.trackPath;
        state.sfxDirectory = command.sfxDirectory;
        state.warmupAssetGeneration = command.identity.assetGeneration;
        state.hasWarmupPaths = true;
        try {
            if (backend != nullptr) {
                backend->clearNativeErrorCode();
                backend->setWarmupResolvedPaths(state.chartPath, state.trackPath, state.sfxDirectory);
            }
            if (backend != nullptr && snapshot().lifecycle != WorkerLifecycle::Degraded) {
                const BackendSnapshot backendState = captureBackendSnapshot(*backend);
                completion.executionDurationNs = steadyNowNs() - startedAtNs;
                finishCompletion(completion, &backendState, std::nullopt, true);
            } else {
                completion.executionDurationNs = steadyNowNs() - startedAtNs;
                finishCompletion(completion, nullptr, std::nullopt, true);
            }
        } catch (const std::exception& error) {
            completion.success = false;
            completion.error = CommandError::BackendFailure;
            completion.detail = exceptionDetail(error);
            completion.nativeErrorCode = backend != nullptr ? backend->nativeErrorCode() : 0;
            completion.executionDurationNs = steadyNowNs() - startedAtNs;
            finishCompletion(completion, nullptr, std::nullopt, true);
        } catch (...) {
            completion.success = false;
            completion.error = CommandError::BackendFailure;
            completion.detail = QStringLiteral("unknown backend warmup failure");
            completion.nativeErrorCode = backend != nullptr ? backend->nativeErrorCode() : 0;
            completion.executionDurationNs = steadyNowNs() - startedAtNs;
            finishCompletion(completion, nullptr, std::nullopt, true);
        }
        return;
    }

    if (command.kind == CommandKind::ReloadAssets) {
        executeReload(command, backend, state, completion);
        if (completion.error == CommandError::Stale) {
            completion.executionDurationNs = steadyNowNs() - startedAtNs;
            deliverCompletion(completion);
        } else if (completion.success && backend != nullptr) {
            try {
                const BackendSnapshot backendState = captureBackendSnapshot(*backend);
                completion.executionDurationNs = steadyNowNs() - startedAtNs;
                finishCompletion(completion, &backendState, WorkerLifecycle::Ready, true);
            } catch (const std::exception& error) {
                completion.success = false;
                completion.error = CommandError::BackendFailure;
                completion.detail = exceptionDetail(error);
                completion.nativeErrorCode = backend->nativeErrorCode();
                completion.executionDurationNs = steadyNowNs() - startedAtNs;
                finishCompletion(completion, nullptr, WorkerLifecycle::Degraded, true);
            } catch (...) {
                completion.success = false;
                completion.error = CommandError::BackendFailure;
                completion.detail = QStringLiteral("unknown backend failure during reload snapshot capture");
                completion.nativeErrorCode = backend->nativeErrorCode();
                completion.executionDurationNs = steadyNowNs() - startedAtNs;
                finishCompletion(completion, nullptr, WorkerLifecycle::Degraded, true);
            }
        } else {
            completion.executionDurationNs = steadyNowNs() - startedAtNs;
            finishCompletion(completion, nullptr, WorkerLifecycle::Degraded, true);
        }
        return;
    }

    if (backend == nullptr || snapshot().lifecycle == WorkerLifecycle::Degraded) {
        completion.error = CommandError::BackendUnavailable;
        completion.success = false;
        completion.detail = QStringLiteral("preview audio backend is unavailable");
        completion.nativeErrorCode = backend != nullptr ? backend->nativeErrorCode() : 0;
        completion.executionDurationNs = steadyNowNs() - startedAtNs;
        finishCompletion(completion, nullptr, std::nullopt, policy.usesAssetGeneration);
        return;
    }

    backend->clearNativeErrorCode();

    const auto recordFirstBackendFailure = [&](const QString& detail, int nativeErrorCode) {
        if (completion.error != CommandError::None) {
            return;
        }
        completion.error = CommandError::BackendFailure;
        completion.success = false;
        completion.detail = detail;
        completion.nativeErrorCode = nativeErrorCode;
    };
    const auto attemptDevicePauseStep = [&](auto&& operation) {
        try {
            operation();
        } catch (const std::exception& error) {
            const int nativeErrorCode = backend->nativeErrorCode();
            recordFirstBackendFailure(exceptionDetail(error), nativeErrorCode);
        } catch (...) {
            const int nativeErrorCode = backend->nativeErrorCode();
            recordFirstBackendFailure(
                QStringLiteral("unknown backend command failure"),
                nativeErrorCode);
        }
    };

    try {
        if (policy.requiresTransaction && command.kind != CommandKind::DeviceChangePause) {
            backend->setPlaybackTransactionId(command.identity.transactionId);
        }
        switch (command.kind) {
        case CommandKind::Shutdown:
            completion.error = CommandError::ShuttingDown;
            completion.success = false;
            break;
        case CommandKind::DeviceChangePause: {
            if (!command.deviceRouteInvalidationOnly) {
                attemptDevicePauseStep([&] {
                    backend->setPlaybackTransactionId(command.identity.transactionId);
                });
                attemptDevicePauseStep([&] {
                    PausePreviewResult result = backend->pausePreviewPlaybackTransaction();
                    // The native callback captured the one authoritative cutoff time.
                    // Cleanup can run later, but must never publish a second clock sample.
                    result.pauseSecond = command.second;
                    completion.pauseResult = result;
                    completion.value = command.second;
                });
            }
            attemptDevicePauseStep([&] { backend->pauseBackgroundTrack(); });
            attemptDevicePauseStep([&] { backend->pauseTouchholdVoices(); });
            attemptDevicePauseStep([&] { backend->stopSfxVoices(); });
            if (!command.deviceRouteInvalidationOnly) {
                attemptDevicePauseStep([&] { backend->resetCursor(command.second, false); });
            }
            // Do not retain a BASS stream after a physical route change.  Releasing the
            // old endpoint here makes the next user-initiated play rebuild the assets on
            // the new endpoint; it cannot resume buffers that were cut mid-sample.
            attemptDevicePauseStep([&] { backend->invalidateOutputDevice(); });
            break;
        }
        case CommandKind::ManualPause: {
            const PausePreviewResult result = backend->pausePreviewPlaybackTransaction();
            completion.pauseResult = result;
            completion.value = result.pauseSecond;
            break;
        }
        case CommandKind::StopAll:
            backend->stopAll();
            break;
        case CommandKind::SetWarmupResolvedPaths:
        case CommandKind::ReloadAssets:
            completion.error = CommandError::BackendFailure;
            completion.success = false;
            completion.detail = QStringLiteral("worker asset command dispatch invariant failed");
            break;
        case CommandKind::SetChartPath:
            state.activeChartPath = command.chartPath;
            state.hasActiveChartPath = true;
            backend->setChartPath(command.chartPath);
            break;
        case CommandKind::SetBackgroundOffset:
            backend->setBackgroundTrackOffsetSeconds(command.value);
            break;
        case CommandKind::SetBackgroundRate:
            backend->setBackgroundTrackPlaybackRate(command.rate);
            break;
        case CommandKind::ApplyRateAtSecond:
            backend->applyPlaybackRateAtChartSecond(command.rate, command.second);
            break;
        case CommandKind::ApplyLevels:
            backend->applyLevels(command.settings);
            break;
        case CommandKind::ConfigureTimeline:
            backend->configureTimeline(command.noteMarkers, command.rate, command.timingSettings);
            break;
        case CommandKind::ClearTimeline:
            backend->clearTimeline();
            break;
        case CommandKind::ApplyPausedState:
            backend->applyPausedPreviewState(
                command.noteMarkers,
                command.option,
                command.second,
                command.rate,
                command.timingSettings);
            break;
        case CommandKind::Prepare:
            completion.value = backend->preparePreviewPlaybackTransaction(
                command.second,
                command.option,
                command.rate);
            if (!backend->audioEngineInitialized()) {
                completion.success = false;
                completion.error = CommandError::BackendFailure;
                completion.detail = QStringLiteral("audio engine unavailable after prepare");
                completion.nativeErrorCode = backend->nativeErrorCode();
            }
            break;
        case CommandKind::Commit:
            backend->commitPreparedPreviewPlayback();
            break;
        case CommandKind::Cancel:
            backend->cancelPreparedPreviewPlayback();
            break;
        case CommandKind::Start:
            completion.value = backend->startPreviewPlaybackTransaction(
                command.second,
                command.option,
                command.rate);
            if (!backend->audioEngineInitialized()) {
                completion.success = false;
                completion.error = CommandError::BackendFailure;
                completion.detail = QStringLiteral("audio engine unavailable after start");
                completion.nativeErrorCode = backend->nativeErrorCode();
            }
            break;
        case CommandKind::ResumeRetained:
            completion.value = backend->resumeRetainedPreviewPlaybackTransaction();
            if (!backend->audioEngineInitialized()) {
                completion.success = false;
                completion.error = CommandError::BackendFailure;
                completion.detail = QStringLiteral("audio engine unavailable after retained resume");
                completion.nativeErrorCode = backend->nativeErrorCode();
            }
            break;
        case CommandKind::SeekRetained:
            completion.value = backend->seekRetainedPreviewPlaybackTransaction(command.second, command.option);
            if (!backend->audioEngineInitialized()) {
                completion.success = false;
                completion.error = CommandError::BackendFailure;
                completion.detail = QStringLiteral("audio engine unavailable after retained seek");
                completion.nativeErrorCode = backend->nativeErrorCode();
            }
            break;
        case CommandKind::ResetRetained:
            backend->resetRetainedPreviewPlaybackTransaction(command.second);
            break;
        case CommandKind::ClearRetained:
            backend->clearRetainedPreviewPlaybackTransaction();
            break;
        case CommandKind::ResetCursor:
            backend->resetCursor(command.second, command.option);
            break;
        case CommandKind::PauseTouchhold:
            backend->pauseTouchholdVoices();
            break;
        case CommandKind::RestoreTouchhold:
            backend->restoreTouchholdVoices(command.second);
            break;
        case CommandKind::StartBackground:
            backend->startBackgroundTrack(command.second);
            break;
        case CommandKind::SeekBackground:
            backend->seekBackgroundTrack(command.second);
            break;
        case CommandKind::PauseBackground:
            backend->pauseBackgroundTrack();
            break;
        case CommandKind::StopSfxVoices:
            backend->stopSfxVoices();
            break;
        case CommandKind::SyncBackgroundTrack:
            backend->syncBackgroundTrack(command.second);
            break;
        case CommandKind::DrainEvents:
            backend->drainEvents(command.second);
            break;
        case CommandKind::Audition:
            completion.value = backend->audition(command.auditionKind, command.gain) ? 1.0 : 0.0;
            completion.success = completion.value != 0.0;
            if (!completion.success) {
                completion.error = CommandError::BackendFailure;
                completion.detail = QStringLiteral("backend rejected audition");
                completion.nativeErrorCode = backend->nativeErrorCode();
            }
            break;
        }

        if (isAssetStale()) {
            completion.error = CommandError::Stale;
            completion.success = false;
            completion.detail = QStringLiteral("asset generation was superseded during execution");
            completion.executionDurationNs = steadyNowNs() - startedAtNs;
            deliverCompletion(completion);
            return;
        }
        const BackendSnapshot backendState = captureBackendSnapshot(*backend);
        completion.executionDurationNs = steadyNowNs() - startedAtNs;
        finishCompletion(completion, &backendState, std::nullopt, policy.usesAssetGeneration);
    } catch (const std::exception& error) {
        const int nativeErrorCode = backend->nativeErrorCode();
        recordFirstBackendFailure(exceptionDetail(error), nativeErrorCode);
        completion.executionDurationNs = steadyNowNs() - startedAtNs;
        finishCompletion(completion, nullptr, std::nullopt, policy.usesAssetGeneration);
    } catch (...) {
        const int nativeErrorCode = backend->nativeErrorCode();
        recordFirstBackendFailure(
            QStringLiteral("unknown backend command failure"),
            nativeErrorCode);
        completion.executionDurationNs = steadyNowNs() - startedAtNs;
        finishCompletion(completion, nullptr, std::nullopt, policy.usesAssetGeneration);
    }
}

void PreviewAudioWorker::executeReload(
    const PreviewAudioCommand& command,
    std::unique_ptr<PreviewAudioBackend>& backend,
    RuntimeState& state,
    PreviewAudioCompletion& completion)
{
    if (command.applyChartPathBeforeReload) {
        state.activeChartPath = command.chartPath;
        state.hasActiveChartPath = true;
    }
    if (!publishAssetLifecycleIfCurrent(WorkerLifecycle::Loading, command.identity)) {
        completion.success = false;
        completion.error = CommandError::Stale;
        completion.detail = QStringLiteral("asset generation was superseded before loading publication");
        return;
    }
    try {
        const bool createdBackend = backend == nullptr;
        if (createdBackend) {
            backend = factory_ ? factory_() : nullptr;
            if (backend == nullptr) {
                completion.success = false;
                completion.error = CommandError::BackendUnavailable;
                completion.detail = QStringLiteral("preview audio backend factory returned null on reload");
                return;
            }
            backend->clearNativeErrorCode();
            if (state.hasWarmupPaths) {
                backend->setWarmupResolvedPaths(state.chartPath, state.trackPath, state.sfxDirectory);
            }
        } else {
            backend->clearNativeErrorCode();
        }
        if (command.applyChartPathBeforeReload
            || (createdBackend && state.hasActiveChartPath)) {
            backend->setChartPath(state.activeChartPath);
        }
        backend->reloadAssets(command.settings);
        if (!backend->audioEngineInitialized()) {
            completion.success = false;
            completion.error = CommandError::BackendFailure;
            completion.detail = QStringLiteral("preview audio backend was not initialized after reload");
            completion.nativeErrorCode = backend->nativeErrorCode();
            return;
        }
        if (!isCurrentAssetGeneration(command.identity.assetGeneration)) {
            completion.success = false;
            completion.error = CommandError::Stale;
            completion.detail = QStringLiteral("asset generation was superseded during reload");
            return;
        }
    } catch (const std::exception& error) {
        if (!isCurrentAssetGeneration(command.identity.assetGeneration)) {
            completion.success = false;
            completion.error = CommandError::Stale;
            completion.detail = QStringLiteral("superseded reload failed after newer asset state was posted");
            return;
        }
        completion.success = false;
        completion.error = backend != nullptr ? CommandError::BackendFailure : CommandError::BackendUnavailable;
        completion.detail = exceptionDetail(error);
        completion.nativeErrorCode = backend != nullptr ? backend->nativeErrorCode() : 0;
    } catch (...) {
        if (!isCurrentAssetGeneration(command.identity.assetGeneration)) {
            completion.success = false;
            completion.error = CommandError::Stale;
            completion.detail = QStringLiteral("superseded reload had an unknown failure");
            return;
        }
        completion.success = false;
        completion.error = backend != nullptr ? CommandError::BackendFailure : CommandError::BackendUnavailable;
        completion.detail = QStringLiteral("unknown backend reload failure");
        completion.nativeErrorCode = backend != nullptr ? backend->nativeErrorCode() : 0;
    }
}

PreviewAudioWorker::BackendSnapshot PreviewAudioWorker::captureBackendSnapshot(
    PreviewAudioBackend& backend) const
{
    BackendSnapshot state;
    state.backendId = backend.backendId();
    state.ready = backend.audioEngineInitialized();
    state.backgroundTrackAvailable = backend.hasBackgroundTrack();
    state.backgroundTrackRunning = backend.isBackgroundTrackRunning();
    state.preparedSecond = backend.preparedStartSecond();
    state.authoritativeSecond = backend.authoritativePlaybackSecond();
    state.backgroundSecond = backend.backgroundPlaybackSecond();
    state.retainedMode = backend.retainedPlaybackMode();
    state.retainedBgmState = backend.retainedBgmState();
    return state;
}

void PreviewAudioWorker::sampleHealth(PreviewAudioBackend& backend, RuntimeState& state)
{
    PreviewAudioHealthSample sample;
    try {
        sample = backend.sampleHealth();
    } catch (...) {
        return;
    }

    PreviewAudioSnapshot publishedSnapshot;
    {
        std::lock_guard lock(snapshotMutex_);
        sample.sequence = nextSnapshotSequence(snapshot_.healthSample.sequence);
        snapshot_.sequence = nextSnapshotSequence(snapshot_.sequence);
        snapshot_.healthSample = sample;
        snapshot_.workerThreadId = workerThreadId_.load(std::memory_order_acquire);
        publishedSnapshot = snapshot_;
    }
    stateCv_.notify_all();
    deliverSnapshot(publishedSnapshot);

    if (!runtimeAudioDebugEnabled()) {
        return;
    }

    const bool underrun = health::isUnderrun(sample.mixerActivity)
        || health::isUnderrun(sample.backgroundActivity);
    const double authoritativeSecond = publishedSnapshot.authoritativeSecond;
    const health::StallEdge edge = health::updateStall(
        &state.healthStallTracker,
        underrun,
        authoritativeSecond);
    if (edge != health::StallEdge::None) {
        appendAudioDebugLog(health::stallEdgePayload(
            edge,
            publishedSnapshot.identity.transactionId,
            authoritativeSecond,
            sample.mixerActivity,
            sample.backgroundActivity,
            state.healthStallTracker));
    }
    if (!health::shouldLogHealth(authoritativeSecond, state.lastHealthLogSecond)) {
        return;
    }
    state.lastHealthLogSecond = authoritativeSecond;
    const miacode::mmcss::LastRegistrationStatus mmcss =
        miacode::mmcss::lastRegistrationStatus();
    appendAudioDebugLog(health::healthPayload(
        publishedSnapshot.identity.transactionId,
        authoritativeSecond,
        sample.mixerActivity,
        sample.backgroundActivity,
        state.healthStallTracker,
        sample.buffer,
        /*mmcssRegisteredOnAudioThreads=*/false,
        mmcss.everRegistered ? mmcss.lastTaskClass : QString()));
}

void PreviewAudioWorker::publishLifecycle(
    WorkerLifecycle lifecycle,
    CommandError error,
    const QString& detail,
    const CommandIdentity* identity,
    int nativeErrorCode)
{
    const PreviewAudioSnapshot publishedSnapshot = updateLifecycleSnapshot(
        lifecycle,
        error,
        detail,
        identity,
        nativeErrorCode);
    stateCv_.notify_all();
    publishNonGuiLifecycle(lifecycle);
    deliverSnapshot(publishedSnapshot);
}

PreviewAudioSnapshot PreviewAudioWorker::updateLifecycleSnapshot(
    WorkerLifecycle lifecycle,
    CommandError error,
    const QString& detail,
    const CommandIdentity* identity,
    int nativeErrorCode)
{
    PreviewAudioSnapshot publishedSnapshot;
    {
        std::lock_guard lock(snapshotMutex_);
        snapshot_.sequence = nextSnapshotSequence(snapshot_.sequence);
        snapshot_.lifecycle = lifecycle;
        if (identity != nullptr) {
            snapshot_.identity = *identity;
        }
        snapshot_.lastError = error;
        snapshot_.lastSuccess = error == CommandError::None;
        snapshot_.backendReady = lifecycle == WorkerLifecycle::Ready;
        if (lifecycle == WorkerLifecycle::Degraded
            || lifecycle == WorkerLifecycle::ShuttingDown
            || lifecycle == WorkerLifecycle::Stopped) {
            snapshot_.backgroundTrackAvailable = false;
            snapshot_.backgroundTrackRunning = false;
        }
        snapshot_.detail = detail;
        snapshot_.nativeErrorCode = error == CommandError::None ? 0 : nativeErrorCode;
        snapshot_.workerThreadId = workerThreadId_.load(std::memory_order_acquire);
        publishedSnapshot = snapshot_;
    }
    return publishedSnapshot;
}

bool PreviewAudioWorker::publishAssetLifecycleIfCurrent(
    WorkerLifecycle lifecycle,
    const CommandIdentity& identity)
{
    PreviewAudioSnapshot publishedSnapshot;
    {
        std::lock_guard assetLock(assetGenerationMutex_);
        if (identity.assetGeneration != latestAssetGeneration_) {
            return false;
        }
        publishedSnapshot = updateLifecycleSnapshot(
            lifecycle,
            CommandError::None,
            {},
            &identity,
            0);
    }
    stateCv_.notify_all();
    publishNonGuiLifecycle(lifecycle);
    deliverSnapshot(publishedSnapshot);
    return true;
}

void PreviewAudioWorker::publishBackendLifecycle(
    WorkerLifecycle lifecycle,
    const BackendSnapshot& backendState,
    const CommandIdentity* identity)
{
    PreviewAudioSnapshot publishedSnapshot;
    {
        std::lock_guard lock(snapshotMutex_);
        snapshot_.sequence = nextSnapshotSequence(snapshot_.sequence);
        snapshot_.lifecycle = lifecycle;
        if (identity != nullptr) {
            snapshot_.identity = *identity;
        }
        snapshot_.lastError = CommandError::None;
        snapshot_.lastSuccess = true;
        snapshot_.backendReady = backendState.ready;
        snapshot_.backgroundTrackAvailable = backendState.backgroundTrackAvailable;
        snapshot_.backgroundTrackRunning = backendState.backgroundTrackRunning;
        snapshot_.preparedSecond = backendState.preparedSecond;
        snapshot_.authoritativeSecond = backendState.authoritativeSecond;
        snapshot_.backgroundPlaybackSecond = backendState.backgroundSecond;
        snapshot_.retainedPlaybackMode = backendState.retainedMode;
        snapshot_.retainedBgmState = backendState.retainedBgmState;
        snapshot_.backendId = backendState.backendId;
        snapshot_.detail.clear();
        snapshot_.nativeErrorCode = 0;
        snapshot_.workerThreadId = workerThreadId_.load(std::memory_order_acquire);
        publishedSnapshot = snapshot_;
    }
    stateCv_.notify_all();
    publishNonGuiLifecycle(lifecycle);
    deliverSnapshot(publishedSnapshot);
}

bool PreviewAudioWorker::publishCompletion(
    const PreviewAudioCompletion& completion,
    const BackendSnapshot* backendState,
    std::optional<WorkerLifecycle> lifecycle,
    bool requireCurrentAssetGeneration)
{
    PreviewAudioSnapshot publishedSnapshot;
    {
        std::unique_lock assetLock(assetGenerationMutex_, std::defer_lock);
        if (requireCurrentAssetGeneration) {
            assetLock.lock();
            if (completion.identity.assetGeneration != latestAssetGeneration_) {
                return false;
            }
        }
        std::lock_guard lock(snapshotMutex_);
        const bool preserveDegradedCause = snapshot_.lifecycle == WorkerLifecycle::Degraded
            && completion.success
            && !lifecycle.has_value()
            && backendState == nullptr;
        snapshot_.sequence = nextSnapshotSequence(snapshot_.sequence);
        snapshot_.identity = completion.identity;
        if (lifecycle) {
            snapshot_.lifecycle = *lifecycle;
            if (*lifecycle == WorkerLifecycle::Degraded
                || *lifecycle == WorkerLifecycle::ShuttingDown
                || *lifecycle == WorkerLifecycle::Stopped) {
                snapshot_.backendReady = false;
                snapshot_.backgroundTrackAvailable = false;
                snapshot_.backgroundTrackRunning = false;
            }
        }
        if (!preserveDegradedCause) {
            snapshot_.lastError = completion.error;
            snapshot_.lastSuccess = completion.success;
            snapshot_.detail = completion.detail;
            snapshot_.nativeErrorCode = completion.nativeErrorCode;
        }
        snapshot_.lastValue = completion.value;
        snapshot_.workerThreadId = completion.workerThreadId;
        if (backendState != nullptr) {
            snapshot_.backendId = backendState->backendId;
            snapshot_.backendReady = snapshot_.lifecycle == WorkerLifecycle::Ready
                && backendState->ready;
            snapshot_.backgroundTrackAvailable = backendState->backgroundTrackAvailable;
            snapshot_.backgroundTrackRunning = backendState->backgroundTrackRunning;
            snapshot_.preparedSecond = backendState->preparedSecond;
            snapshot_.authoritativeSecond = backendState->authoritativeSecond;
            snapshot_.backgroundPlaybackSecond = backendState->backgroundSecond;
            snapshot_.retainedPlaybackMode = backendState->retainedMode;
            snapshot_.retainedBgmState = backendState->retainedBgmState;
        }
        publishedSnapshot = snapshot_;
    }
    stateCv_.notify_all();
    if (lifecycle) {
        publishNonGuiLifecycle(*lifecycle);
    }
    deliverSnapshot(publishedSnapshot);
    deliverCompletion(completion);
    return true;
}

void PreviewAudioWorker::finishCompletion(
    PreviewAudioCompletion completion,
    const BackendSnapshot* backendState,
    std::optional<WorkerLifecycle> lifecycle,
    bool requireCurrentAssetGeneration)
{
    if (publishCompletion(
            completion,
            backendState,
            lifecycle,
            requireCurrentAssetGeneration)) {
        return;
    }
    completion.error = CommandError::Stale;
    completion.success = false;
    completion.detail = QStringLiteral("asset generation was superseded before final publication");
    completion.nativeErrorCode = 0;
    deliverCompletion(completion);
}

void PreviewAudioWorker::publishNonGuiLifecycle(WorkerLifecycle lifecycle)
{
    {
        std::lock_guard lock(nonGuiBarrierMutex_);
        nonGuiLifecycle_ = lifecycle;
        if (lifecycle == WorkerLifecycle::ShuttingDown || lifecycle == WorkerLifecycle::Stopped) {
            nonGuiShuttingDown_ = true;
        }
    }
    nonGuiBarrierCv_.notify_all();
}

void PreviewAudioWorker::setNonGuiWaitEnrollmentObserverForTest(std::function<void()> observer)
{
    std::lock_guard lock(nonGuiBarrierMutex_);
    nonGuiWaitEnrollmentObserverForTest_ = std::move(observer);
}

void PreviewAudioWorker::markCompletionRetiredForNonGui(quint64 sequence)
{
    {
        std::lock_guard lock(nonGuiBarrierMutex_);
        rememberRetiredCompletionForNonGuiLocked(sequence);
    }
    nonGuiBarrierCv_.notify_all();
}

void PreviewAudioWorker::rememberRetiredCompletionForNonGuiLocked(quint64 sequence)
{
    if (sequence == 0 || !retiredNonGuiCompletions_.insert(sequence).second) {
        return;
    }
    retiredNonGuiCompletionOrder_.push_back(sequence);
    while (retiredNonGuiCompletionOrder_.size() > kNonGuiCompletionRetentionCapacity) {
        retiredNonGuiCompletions_.erase(retiredNonGuiCompletionOrder_.front());
        retiredNonGuiCompletionOrder_.pop_front();
    }
}

void PreviewAudioWorker::retainCompletionForNonGui(const PreviewAudioCompletion& completion)
{
    std::lock_guard lock(nonGuiBarrierMutex_);
    const auto [_, inserted] = nonGuiCompletions_.insert_or_assign(
        completion.identity.sequence,
        completion);
    if (inserted) {
        nonGuiCompletionOrder_.push_back(completion.identity.sequence);
    }
    while (nonGuiCompletionOrder_.size() > kNonGuiCompletionRetentionCapacity) {
        const quint64 retiredSequence = nonGuiCompletionOrder_.front();
        nonGuiCompletionOrder_.pop_front();
        nonGuiCompletions_.erase(retiredSequence);
        rememberRetiredCompletionForNonGuiLocked(retiredSequence);
    }
    nonGuiBarrierCv_.notify_all();
}

void PreviewAudioWorker::deliverCompletion(const PreviewAudioCompletion& completion)
{
    retainCompletionForNonGui(completion);
    {
        std::lock_guard lock(callbackMutex_);
        if (!callbackDeliveryEnabled_ || !completionCallback_) {
            stateCv_.notify_all();
            return;
        }
        ++callbacksInFlight_;
    }
    try {
        completionCallback_(completion);
    } catch (...) {
    }
    {
        std::lock_guard lock(callbackMutex_);
        --callbacksInFlight_;
    }
    callbackCv_.notify_all();
    stateCv_.notify_all();
}

void PreviewAudioWorker::deliverSnapshot(const PreviewAudioSnapshot& snapshot)
{
    {
        std::lock_guard lock(callbackMutex_);
        if (!callbackDeliveryEnabled_ || !snapshotCallback_) {
            return;
        }
        ++callbacksInFlight_;
    }
    try {
        snapshotCallback_(snapshot);
    } catch (...) {
    }
    {
        std::lock_guard lock(callbackMutex_);
        --callbacksInFlight_;
    }
    callbackCv_.notify_all();
}

void PreviewAudioWorker::rejectQueuedCommands(CommandError error)
{
    while (std::optional<PreviewAudioCommand> command = takeDeferredDevicePauseInvalidation()) {
        deliverDevicePauseBarrierStale(std::move(*command));
    }
    while (std::optional<PreviewAudioCommand> command = queue_.takeNext()) {
        PreviewAudioCompletion completion;
        completion.kind = command->kind;
        completion.identity = command->identity;
        completion.error = error;
        completion.success = false;
        completion.detail = QStringLiteral("preview audio worker is shutting down");
        completion.workerThreadId = workerThreadId_.load(std::memory_order_acquire);
        completion.queueDelayNs = std::max<qint64>(0, steadyNowNs() - command->enqueuedAtNs);
        deliverCompletion(completion);
    }
    stateCv_.notify_all();
    nonGuiBarrierCv_.notify_all();
}

bool PreviewAudioWorker::hasDeferredDevicePauseInvalidations() const
{
    std::lock_guard lock(deferredDevicePauseInvalidationsMutex_);
    return !deferredDevicePauseInvalidations_.empty();
}

std::optional<PreviewAudioCommand> PreviewAudioWorker::takeDeferredDevicePauseInvalidation()
{
    std::lock_guard lock(deferredDevicePauseInvalidationsMutex_);
    if (deferredDevicePauseInvalidations_.empty()) {
        return std::nullopt;
    }
    PreviewAudioCommand command = std::move(deferredDevicePauseInvalidations_.front());
    deferredDevicePauseInvalidations_.pop_front();
    return command;
}

void PreviewAudioWorker::deliverDevicePauseBarrierStale(PreviewAudioCommand command)
{
    const qint64 nowNs = steadyNowNs();
    PreviewAudioCompletion completion;
    completion.kind = command.kind;
    completion.identity = command.identity;
    completion.error = CommandError::Stale;
    completion.success = false;
    completion.detail = QStringLiteral("device pause barrier removed queued playback command");
    completion.workerThreadId = workerThreadId_.load(std::memory_order_acquire);
    completion.queueDelayNs = std::max<qint64>(0, nowNs - command.enqueuedAtNs);
    completion.executionDurationNs = 0;
    deliverCompletion(completion);
}

bool PreviewAudioWorker::isCurrentAssetGeneration(quint64 generation) const
{
    std::lock_guard lock(assetGenerationMutex_);
    return generation == latestAssetGeneration_;
}

}  // namespace miacode::preview_audio
