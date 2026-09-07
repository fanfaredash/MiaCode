#include "QtPreviewSfxRuntime.h"

#include "PreviewBassEmergencyPause.h"

#include <QMetaObject>

#include <utility>

using namespace miacode::preview_audio;

QtPreviewSfxRuntime::QtPreviewSfxRuntime(QObject* parent)
    : QtPreviewSfxRuntime(productionPreviewAudioBackendFactory(), parent)
{
}

QtPreviewSfxRuntime::QtPreviewSfxRuntime(PreviewAudioBackendFactory factory, QObject* parent)
    : QObject(parent)
    , callbackState_(std::make_shared<CallbackState>())
{
    const std::shared_ptr<CallbackState> callbackState = callbackState_;
    worker_ = std::make_unique<PreviewAudioWorker>(
        std::move(factory),
        [this, callbackState](const PreviewAudioCompletion& completion) {
            if (!callbackState->deliveryEnabled.load(std::memory_order_acquire)) {
                return;
            }
            if (completion.kind == CommandKind::DeviceChangePause) {
                std::lock_guard lock(completedDeviceCutoffMutex_);
                completedDeviceCutoff_ = completion;
            }
            QMetaObject::invokeMethod(
                this,
                [this, callbackState, completion] {
                    if (callbackState->deliveryEnabled.load(std::memory_order_acquire)) {
                        handleCompletion(completion);
                    }
                },
                Qt::QueuedConnection);
        },
        std::this_thread::get_id(),
        [this, callbackState](const PreviewAudioSnapshot& snapshot) {
            if (!callbackState->deliveryEnabled.load(std::memory_order_acquire)) {
                return;
            }
            QMetaObject::invokeMethod(
                this,
                [this, callbackState, snapshot] {
                    if (callbackState->deliveryEnabled.load(std::memory_order_acquire)) {
                        handleSnapshot(snapshot);
                    }
                },
                Qt::QueuedConnection);
        });
}

QtPreviewSfxRuntime::~QtPreviewSfxRuntime()
{
    shutdownWorker();
}

QtPreviewSfxRuntime::AssetSubmission QtPreviewSfxRuntime::setWarmupResolvedPaths(
    const QString& chartPath,
    const QString& trackPath,
    const QString& sfxDir)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SetWarmupResolvedPaths);
    command.identity.assetGeneration = assetGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    command.chartPath = chartPath;
    command.trackPath = trackPath;
    command.sfxDirectory = sfxDir;
    AssetSubmission submission;
    submission.identity = command.identity;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

QtPreviewSfxRuntime::AssetSubmission QtPreviewSfxRuntime::reloadAssets(const PreviewAudioSettings& settings)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ReloadAssets);
    command.identity.assetGeneration = assetGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    command.settings = settings;
    AssetSubmission submission;
    submission.identity = command.identity;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

QtPreviewSfxRuntime::AssetSubmission QtPreviewSfxRuntime::reloadAssetsForChart(
    const QString& chartPath,
    const PreviewAudioSettings& settings)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ReloadAssets);
    command.identity.assetGeneration = assetGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    command.chartPath = chartPath;
    command.applyChartPathBeforeReload = true;
    command.settings = settings;
    AssetSubmission submission;
    submission.identity = command.identity;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

QtPreviewSfxRuntime::AssetSubmission QtPreviewSfxRuntime::reloadAssetsForChartWithWarmupPaths(
    const QString& chartPath,
    const QString& trackPath,
    const QString& sfxDir,
    const PreviewAudioSettings& settings)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ReloadAssets);
    command.identity.assetGeneration = assetGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    command.chartPath = chartPath;
    command.trackPath = trackPath;
    command.sfxDirectory = sfxDir;
    command.applyWarmupPathsBeforeReload = true;
    command.applyChartPathBeforeReload = true;
    command.settings = settings;
    AssetSubmission submission;
    submission.identity = command.identity;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

bool QtPreviewSfxRuntime::audioEngineInitialized() const
{
    const PreviewAudioSnapshot snapshot = lastSnapshot();
    return snapshot.backendReady
        && snapshot.identity.assetGeneration == assetGeneration_.load(std::memory_order_acquire);
}

QtPreviewSfxRuntime::AssetSubmission QtPreviewSfxRuntime::setChartPath(const QString& chartPath)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SetChartPath);
    command.identity.assetGeneration = assetGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    command.chartPath = chartPath;
    AssetSubmission submission;
    submission.identity = command.identity;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

void QtPreviewSfxRuntime::setBackgroundTrackOffsetSeconds(double seconds)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SetBackgroundOffset);
    command.value = seconds;
    post(std::move(command));
}

void QtPreviewSfxRuntime::setBackgroundTrackPlaybackRate(double rate)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SetBackgroundRate);
    command.rate = rate;
    post(std::move(command));
}

void QtPreviewSfxRuntime::applyPlaybackRateAtChartSecond(double rate, double chartSecond)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ApplyRateAtSecond);
    command.rate = rate;
    command.second = chartSecond;
    post(std::move(command));
}

void QtPreviewSfxRuntime::applyLevels(const PreviewAudioSettings& settings)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ApplyLevels);
    command.settings = settings;
    post(std::move(command));
}

void QtPreviewSfxRuntime::configureTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ConfigureTimeline);
    command.noteMarkers = noteMarkers;
    command.rate = playbackRate;
    command.timingSettings = timingSettings;
    post(std::move(command));
}

void QtPreviewSfxRuntime::clearTimeline()
{
    post(makeCommand(CommandKind::ClearTimeline));
}

void QtPreviewSfxRuntime::setPlaybackTransactionId(quint64 transactionId)
{
    transactionId_.store(transactionId, std::memory_order_release);
}

QtPreviewSfxRuntime::PlaybackSubmission QtPreviewSfxRuntime::preparePreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    PreviewAudioCommand command = makeCommand(CommandKind::Prepare);
    command.identity.generation = advancePlaybackGeneration();
    command.second = startSecond;
    command.option = resumeFromPause;
    command.rate = playbackRate;
    const PreviewAudioSnapshot snapshot = lastSnapshot();
    PlaybackSubmission submission;
    submission.identity = command.identity;
    submission.fallbackSecond = snapshot.preparedSecond != 0.0 ? snapshot.preparedSecond : startSecond;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

void QtPreviewSfxRuntime::commitPreparedPreviewPlayback()
{
    post(makeCommand(CommandKind::Commit));
}

void QtPreviewSfxRuntime::cancelPreparedPreviewPlayback()
{
    post(makeCommand(CommandKind::Cancel));
}

double QtPreviewSfxRuntime::preparedStartSecond() const
{
    return lastSnapshot().preparedSecond;
}

void QtPreviewSfxRuntime::applyPausedPreviewState(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool noteMarkersChanged,
    double pauseSecond,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ApplyPausedState);
    command.noteMarkers = noteMarkers;
    command.option = noteMarkersChanged;
    command.second = pauseSecond;
    command.rate = playbackRate;
    command.timingSettings = timingSettings;
    post(std::move(command));
}

double QtPreviewSfxRuntime::startPreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    PreviewAudioCommand command = makeCommand(CommandKind::Start);
    command.identity.generation = advancePlaybackGeneration();
    command.second = startSecond;
    command.option = resumeFromPause;
    command.rate = playbackRate;
    post(std::move(command));
    return startSecond;
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::capturePausedPreviewTransaction()
{
    PreviewAudioCommand command = makeCommand(CommandKind::ManualPause);
    command.identity.generation = advancePlaybackGeneration();
    post(std::move(command));
    return lastPauseResult();
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::pausePreviewPlaybackTransaction()
{
    PreviewAudioCommand command = makeCommand(CommandKind::ManualPause);
    command.identity.generation = advancePlaybackGeneration();
    post(std::move(command));
    return lastPauseResult();
}

QtPreviewSfxRuntime::PauseSubmission QtPreviewSfxRuntime::requestManualPause(
    quint64 transactionId,
    double wallSecond)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ManualPause);
    command.identity.generation = advancePlaybackGeneration();
    command.identity.transactionId = transactionId;
    command.second = wallSecond;
    PauseSubmission submission;
    submission.identity = command.identity;
    submission.visualFallbackSecond = wallSecond;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

QtPreviewSfxRuntime::DevicePauseRequest QtPreviewSfxRuntime::requestDeviceChangePause(
    quint64 transactionId,
    quint64 deviceSequence,
    quint64 pauseToken,
    double pauseSecond)
{
    quint64 observedDeviceSequence = deviceSequence_.load(std::memory_order_acquire);
    while (observedDeviceSequence < deviceSequence
           && !deviceSequence_.compare_exchange_weak(
               observedDeviceSequence,
               deviceSequence,
               std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    PreviewAudioCommand command = makeCommand(CommandKind::DeviceChangePause);
    command.identity.generation = advancePlaybackGeneration();
    command.identity.transactionId = transactionId;
    command.identity.deviceSequence = deviceSequence_.load(std::memory_order_acquire);
    command.identity.pauseToken = pauseToken;
    command.second = pauseSecond;

    DevicePauseRequest request;
    request.identity = command.identity;
    request.post = postDeviceChangePauseBarrier(std::move(command));
    request.identity.sequence = request.post.sequence;
    return request;
}

void QtPreviewSfxRuntime::armDeviceChangeCutoffClock(
    double startSecond,
    double playbackRate,
    quint64 transactionId)
{
    std::lock_guard lock(deviceChangeCutoffClockMutex_);
    deviceChangeCutoffClock_.armed = true;
    deviceChangeCutoffClock_.anchoredAt = std::chrono::steady_clock::now();
    deviceChangeCutoffClock_.startSecond = startSecond;
    deviceChangeCutoffClock_.playbackRate = playbackRate;
    deviceChangeCutoffClock_.transactionId = transactionId;
}

void QtPreviewSfxRuntime::disarmDeviceChangeCutoffClock()
{
    std::lock_guard lock(deviceChangeCutoffClockMutex_);
    deviceChangeCutoffClock_.armed = false;
}

miacode::preview_audio::PreviewAudioDeviceCutoff QtPreviewSfxRuntime::requestDeviceChangeCutoff()
{
    using miacode::preview_audio::PreviewAudioDeviceCutoff;

    PreviewAudioDeviceCutoff cutoff;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    quint64 transactionId = 0;
    bool playbackWasArmed = false;
    {
        std::lock_guard lock(deviceChangeCutoffClockMutex_);
        playbackWasArmed = deviceChangeCutoffClock_.armed;
        if (playbackWasArmed) {
            deviceChangeCutoffClock_.armed = false;
            const auto elapsed = now - deviceChangeCutoffClock_.anchoredAt;
            cutoff.cutoffSecond = deviceChangeCutoffClock_.startSecond
                + std::chrono::duration<double>(elapsed).count()
                    * deviceChangeCutoffClock_.playbackRate;
            transactionId = deviceChangeCutoffClock_.transactionId;
        }
    }

    cutoff.armedPlaybackWasCut = playbackWasArmed;
    cutoff.outputRouteInvalidationOnly = !playbackWasArmed;
    cutoff.eventMonotonicNs = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    // This closes normal playback submission before creating the barrier.  The same
    // gate is also required while paused: the next explicit Play must cold-prepare on
    // the new endpoint instead of retaining a stream bound to the previous device.
    deviceCutoffActive_.store(true, std::memory_order_release);
    if (playbackWasArmed) {
        const BassEmergencyPauseResult emergencyPause =
            PreviewBassEmergencyPause::pauseActiveOutput();
        cutoff.emergencyPauseStartedNs = emergencyPause.startedMonotonicNs;
        cutoff.emergencyPauseFinishedNs = emergencyPause.finishedMonotonicNs;
        cutoff.emergencyPauseDeviceIndex = emergencyPause.outputDeviceIndex;
        cutoff.emergencyPauseError = emergencyPause.nativeErrorCode;
        cutoff.emergencyPauseAttempted = emergencyPause.attempted;
        cutoff.emergencyPauseSucceeded = emergencyPause.paused;
    }

    PreviewAudioCommand command;
    command.kind = CommandKind::DeviceChangePause;
    command.identity.generation = advancePlaybackGeneration();
    command.identity.assetGeneration = assetGeneration_.load(std::memory_order_acquire);
    command.identity.transactionId = transactionId;
    command.identity.deviceSequence = deviceSequence_.fetch_add(1, std::memory_order_acq_rel) + 1;
    command.identity.pauseToken = command.identity.deviceSequence;
    command.second = cutoff.cutoffSecond;
    command.deviceRouteInvalidationOnly = cutoff.outputRouteInvalidationOnly;
    cutoff.identity = command.identity;
    if (!acceptingCommands_.load(std::memory_order_acquire)) {
        cutoff.post = {false, false, false, CommandError::ShuttingDown, 0};
        return cutoff;
    }
    std::lock_guard workerLock(workerLifecycleMutex_);
    if (!acceptingCommands_.load(std::memory_order_acquire) || worker_ == nullptr) {
        cutoff.post = {false, false, false, CommandError::ShuttingDown, 0};
        return cutoff;
    }
    cutoff.post = worker_->postDeviceChangePauseBarrier(std::move(command));
    cutoff.identity.sequence = cutoff.post.sequence;
    return cutoff;
}

bool QtPreviewSfxRuntime::beginManualPlaybackAfterDeviceCutoff()
{
    return deviceCutoffActive_.exchange(false, std::memory_order_acq_rel);
}

bool QtPreviewSfxRuntime::isDeviceCutoffActive() const noexcept
{
    return deviceCutoffActive_.load(std::memory_order_acquire);
}

std::optional<QtPreviewSfxRuntime::Completion>
QtPreviewSfxRuntime::takeCompletedDeviceChangeCutoff(quint64 sequence)
{
    std::lock_guard lock(completedDeviceCutoffMutex_);
    if (!completedDeviceCutoff_
        || completedDeviceCutoff_->identity.sequence != sequence) {
        return std::nullopt;
    }
    return std::exchange(completedDeviceCutoff_, std::nullopt);
}

QtPreviewSfxRuntime::PlaybackSubmission QtPreviewSfxRuntime::resumeRetainedPreviewPlaybackTransaction()
{
    PreviewAudioCommand command = makeCommand(CommandKind::ResumeRetained);
    command.identity.generation = advancePlaybackGeneration();
    PlaybackSubmission submission;
    submission.identity = command.identity;
    submission.fallbackSecond = authoritativePlaybackSecond();
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

QtPreviewSfxRuntime::PlaybackSubmission QtPreviewSfxRuntime::seekRetainedPreviewPlaybackTransaction(
    double targetSecond,
    bool continuePlaying)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SeekRetained);
    command.identity.generation = advancePlaybackGeneration();
    command.second = targetSecond;
    command.option = continuePlaying;
    PlaybackSubmission submission;
    submission.identity = command.identity;
    submission.fallbackSecond = targetSecond;
    submission.post = post(std::move(command));
    submission.identity.sequence = submission.post.sequence;
    return submission;
}

void QtPreviewSfxRuntime::resetRetainedPreviewPlaybackTransaction(double targetSecond)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ResetRetained);
    command.identity.generation = advancePlaybackGeneration();
    command.second = targetSecond;
    post(std::move(command));
}

void QtPreviewSfxRuntime::clearRetainedPreviewPlaybackTransaction()
{
    post(makeCommand(CommandKind::ClearRetained));
}

QtPreviewSfxRuntime::RetainedPlaybackMode QtPreviewSfxRuntime::retainedPlaybackMode() const
{
    return lastSnapshot().retainedPlaybackMode;
}

QtPreviewSfxRuntime::RetainedBgmState QtPreviewSfxRuntime::retainedBgmState() const
{
    return lastSnapshot().retainedBgmState;
}

quint64 QtPreviewSfxRuntime::playbackGeneration() const noexcept
{
    return playbackGeneration_.load(std::memory_order_acquire);
}

quint64 QtPreviewSfxRuntime::assetGeneration() const noexcept
{
    return assetGeneration_.load(std::memory_order_acquire);
}

double QtPreviewSfxRuntime::authoritativePlaybackSecond() const
{
    return lastSnapshot().authoritativeSecond;
}

void QtPreviewSfxRuntime::stopSfxVoices()
{
    post(makeCommand(CommandKind::StopSfxVoices));
}

double QtPreviewSfxRuntime::syncPreviewPlaybackClockTransaction(double fallbackSecond)
{
    const PreviewAudioSnapshot snapshot = lastSnapshot();
    return snapshot.sequence != 0 ? snapshot.authoritativeSecond : fallbackSecond;
}

void QtPreviewSfxRuntime::resetCursor(double second, bool includeCurrentSecond)
{
    PreviewAudioCommand command = makeCommand(CommandKind::ResetCursor);
    command.second = second;
    command.option = includeCurrentSecond;
    post(std::move(command));
}

void QtPreviewSfxRuntime::drainEvents(double second)
{
    PreviewAudioCommand command = makeCommand(CommandKind::DrainEvents);
    command.second = second;
    post(std::move(command));
}

void QtPreviewSfxRuntime::pauseTouchholdVoices()
{
    post(makeCommand(CommandKind::PauseTouchhold));
}

void QtPreviewSfxRuntime::restoreTouchholdVoices(double second)
{
    PreviewAudioCommand command = makeCommand(CommandKind::RestoreTouchhold);
    command.second = second;
    post(std::move(command));
}

void QtPreviewSfxRuntime::syncBackgroundTrack(double timelineSecond)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SyncBackgroundTrack);
    command.second = timelineSecond;
    post(std::move(command));
}

bool QtPreviewSfxRuntime::hasBackgroundTrack() const
{
    return lastSnapshot().backgroundTrackAvailable;
}

bool QtPreviewSfxRuntime::isBackgroundTrackRunning() const
{
    return lastSnapshot().backgroundTrackRunning;
}

WorkerPostResult QtPreviewSfxRuntime::startBackgroundTrack(double second)
{
    PreviewAudioCommand command = makeCommand(CommandKind::StartBackground);
    command.second = second;
    return post(std::move(command));
}

void QtPreviewSfxRuntime::seekBackgroundTrack(double second)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SeekBackground);
    command.second = second;
    post(std::move(command));
}

void QtPreviewSfxRuntime::pauseBackgroundTrack()
{
    post(makeCommand(CommandKind::PauseBackground));
}

double QtPreviewSfxRuntime::backgroundPlaybackSecond() const
{
    return lastSnapshot().backgroundPlaybackSecond;
}

bool QtPreviewSfxRuntime::audition(const QString& kind, double gain)
{
    const PreviewAudioSnapshot snapshot = lastSnapshot();
    if (!snapshot.backendReady
        || snapshot.identity.assetGeneration != assetGeneration_.load(std::memory_order_acquire)) {
        return false;
    }
    PreviewAudioCommand command = makeCommand(CommandKind::Audition);
    command.auditionKind = kind;
    command.gain = gain;
    return post(std::move(command)).accepted;
}

void QtPreviewSfxRuntime::stopAll()
{
    PreviewAudioCommand command = makeCommand(CommandKind::StopAll);
    command.identity.generation = advancePlaybackGeneration();
    post(std::move(command));
}

void QtPreviewSfxRuntime::prepareForShutdown()
{
    shutdownWorker();
}

NonGuiBarrierWaitStatus QtPreviewSfxRuntime::waitForReadyForNonGui(std::chrono::milliseconds timeout)
{
    return worker_ != nullptr
        ? worker_->waitForReadyForNonGui(timeout)
        : NonGuiBarrierWaitStatus::ShuttingDown;
}

NonGuiBarrierWaitStatus QtPreviewSfxRuntime::waitForCompletionForNonGui(
    quint64 sequence,
    std::chrono::milliseconds timeout)
{
    return worker_ != nullptr
        ? worker_->waitForCompletionForNonGui(sequence, timeout)
        : NonGuiBarrierWaitStatus::ShuttingDown;
}

WorkerPostResult QtPreviewSfxRuntime::post(PreviewAudioCommand command)
{
    const CommandKind kind = command.kind;
    const CommandIdentity identity = command.identity;
    if (deviceCutoffActive_.load(std::memory_order_acquire)
        && commandPolicy(kind).invalidatedByPlaybackBoundary) {
        return {false, false, false, CommandError::Stale, 0};
    }
    if (!acceptingCommands_.load(std::memory_order_acquire) || worker_ == nullptr) {
        return {false, false, false, CommandError::ShuttingDown, 0};
    }
    WorkerPostResult result = worker_->post(std::move(command));
    if (!result.accepted) {
        PreviewAudioCompletion completion;
        completion.kind = kind;
        completion.identity = identity;
        completion.identity.sequence = result.sequence;
        completion.error = result.error;
        completion.success = false;
        completion.detail = QStringLiteral("preview audio facade rejected command before worker execution");
        handleCompletion(completion);
    }
    return result;
}

WorkerPostResult QtPreviewSfxRuntime::postDeviceChangePauseBarrier(PreviewAudioCommand command)
{
    const CommandKind kind = command.kind;
    const CommandIdentity identity = command.identity;
    if (!acceptingCommands_.load(std::memory_order_acquire) || worker_ == nullptr) {
        return {false, false, false, CommandError::ShuttingDown, 0};
    }
    WorkerPostResult result = worker_->postDeviceChangePauseBarrier(std::move(command));
    if (!result.accepted) {
        PreviewAudioCompletion completion;
        completion.kind = kind;
        completion.identity = identity;
        completion.identity.sequence = result.sequence;
        completion.error = result.error;
        completion.success = false;
        completion.detail = QStringLiteral("preview audio facade rejected device pause before worker execution");
        handleCompletion(completion);
    }
    return result;
}

PreviewAudioCommand QtPreviewSfxRuntime::makeCommand(CommandKind kind) const
{
    PreviewAudioCommand command;
    command.kind = kind;
    command.identity.generation = playbackGeneration_.load(std::memory_order_acquire);
    command.identity.assetGeneration = assetGeneration_.load(std::memory_order_acquire);
    command.identity.transactionId = transactionId_.load(std::memory_order_acquire);
    command.identity.deviceSequence = deviceSequence_.load(std::memory_order_acquire);
    return command;
}

quint64 QtPreviewSfxRuntime::advancePlaybackGeneration()
{
    return playbackGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void QtPreviewSfxRuntime::handleCompletion(const Completion& completion)
{
    if (worker_ != nullptr) {
        handleSnapshot(worker_->snapshot());
    }
    emit commandCompleted(completion);
    switch (completion.kind) {
    case CommandKind::Prepare:
        emit previewPrepared(completion);
        break;
    case CommandKind::Commit:
    case CommandKind::Start:
        emit previewPlaybackStarted(completion);
        break;
    case CommandKind::ResumeRetained:
    case CommandKind::SeekRetained:
        emit retainedPlaybackCompleted(completion);
        break;
    case CommandKind::ManualPause:
    case CommandKind::DeviceChangePause:
        emit previewPlaybackPaused(completion);
        break;
    case CommandKind::ResetRetained:
    case CommandKind::ClearRetained:
        emit retainedPlaybackCompleted(completion);
        break;
    case CommandKind::Audition:
        emit auditionCompleted(completion);
        break;
    default:
        break;
    }
}

void QtPreviewSfxRuntime::handleSnapshot(const PreviewAudioSnapshot& snapshot)
{
    bool readyChanged = false;
    {
        std::lock_guard lock(snapshotMutex_);
        readyChanged = lastSnapshot_.backendReady != snapshot.backendReady;
        lastSnapshot_ = snapshot;
    }
    if (readyChanged) {
        emit backendReadyChanged(snapshot.backendReady);
    }
}

PreviewAudioSnapshot QtPreviewSfxRuntime::lastSnapshot() const
{
    std::lock_guard lock(snapshotMutex_);
    return lastSnapshot_;
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::lastPauseResult() const
{
    const PreviewAudioSnapshot snapshot = lastSnapshot();
    return {
        snapshot.backgroundTrackAvailable,
        snapshot.authoritativeSecond,
        snapshot.retainedPlaybackMode,
        snapshot.retainedBgmState,
    };
}

void QtPreviewSfxRuntime::shutdownWorker()
{
    acceptingCommands_.store(false, std::memory_order_release);
    if (callbackState_ != nullptr) {
        callbackState_->deliveryEnabled.store(false, std::memory_order_release);
    }
    std::lock_guard workerLock(workerLifecycleMutex_);
    if (worker_ != nullptr) {
        worker_->shutdownAndJoin();
        worker_.reset();
    }
}
