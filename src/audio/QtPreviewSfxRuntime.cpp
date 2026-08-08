#include "QtPreviewSfxRuntime.h"

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
    command.identity.assetGeneration = ++assetGeneration_;
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
    command.identity.assetGeneration = ++assetGeneration_;
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
    return snapshot.backendReady && snapshot.identity.assetGeneration == assetGeneration_;
}

QtPreviewSfxRuntime::AssetSubmission QtPreviewSfxRuntime::setChartPath(const QString& chartPath)
{
    PreviewAudioCommand command = makeCommand(CommandKind::SetChartPath);
    command.identity.assetGeneration = ++assetGeneration_;
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
    transactionId_ = transactionId;
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
    deviceSequence_ = std::max(deviceSequence_, deviceSequence);
    PreviewAudioCommand command = makeCommand(CommandKind::DeviceChangePause);
    command.identity.generation = advancePlaybackGeneration();
    command.identity.transactionId = transactionId;
    command.identity.deviceSequence = deviceSequence_;
    command.identity.pauseToken = pauseToken;
    command.second = pauseSecond;

    DevicePauseRequest request;
    request.identity = command.identity;
    request.post = postDeviceChangePauseBarrier(std::move(command));
    request.identity.sequence = request.post.sequence;
    return request;
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
    return playbackGeneration_;
}

quint64 QtPreviewSfxRuntime::assetGeneration() const noexcept
{
    return assetGeneration_;
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
    if (!snapshot.backendReady || snapshot.identity.assetGeneration != assetGeneration_) {
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
    command.identity.generation = playbackGeneration_;
    command.identity.assetGeneration = assetGeneration_;
    command.identity.transactionId = transactionId_;
    command.identity.deviceSequence = deviceSequence_;
    return command;
}

quint64 QtPreviewSfxRuntime::advancePlaybackGeneration()
{
    ++playbackGeneration_;
    return playbackGeneration_;
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
    if (worker_ != nullptr) {
        worker_->shutdownAndJoin();
        worker_.reset();
    }
}
