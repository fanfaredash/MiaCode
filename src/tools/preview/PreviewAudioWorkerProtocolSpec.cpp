#include <QTextStream>

#include <array>
#include <type_traits>

#include "audio/PreviewAudioWorkerProtocol.h"

namespace {

using namespace miacode::preview_audio;

bool expect(bool condition, const char* message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyVocabularyAndValueSemantics(QTextStream& err)
{
    const std::array allKinds{
        CommandKind::Shutdown,
        CommandKind::DeviceChangePause,
        CommandKind::ManualPause,
        CommandKind::StopAll,
        CommandKind::SetWarmupResolvedPaths,
        CommandKind::ReloadAssets,
        CommandKind::SetChartPath,
        CommandKind::SetBackgroundOffset,
        CommandKind::SetBackgroundRate,
        CommandKind::ApplyRateAtSecond,
        CommandKind::ApplyLevels,
        CommandKind::ConfigureTimeline,
        CommandKind::ClearTimeline,
        CommandKind::ApplyPausedState,
        CommandKind::Prepare,
        CommandKind::Commit,
        CommandKind::Cancel,
        CommandKind::Start,
        CommandKind::ResumeRetained,
        CommandKind::SeekRetained,
        CommandKind::ResetRetained,
        CommandKind::ClearRetained,
        CommandKind::ResetCursor,
        CommandKind::PauseTouchhold,
        CommandKind::RestoreTouchhold,
        CommandKind::StartBackground,
        CommandKind::SeekBackground,
        CommandKind::PauseBackground,
        CommandKind::StopSfxVoices,
        CommandKind::SyncBackgroundTrack,
        CommandKind::DrainEvents,
        CommandKind::Audition,
    };
    const std::array allErrors{
        CommandError::None,
        CommandError::QueueFull,
        CommandError::ShuttingDown,
        CommandError::BackendUnavailable,
        CommandError::BackendFailure,
        CommandError::Stale,
    };
    const std::array lifecycles{
        WorkerLifecycle::Constructing,
        WorkerLifecycle::Ready,
        WorkerLifecycle::Loading,
        WorkerLifecycle::Degraded,
        WorkerLifecycle::ShuttingDown,
        WorkerLifecycle::Stopped,
    };

    bool ok = true;
    ok &= expect(allKinds.size() == 32, "complete command vocabulary", err);
    ok &= expect(allErrors.size() == 6, "complete error vocabulary", err);
    ok &= expect(lifecycles.size() == 6, "complete lifecycle vocabulary", err);
    ok &= expect(std::is_copy_constructible_v<PreviewAudioCommand>, "commands are owned value types", err);
    ok &= expect(std::is_copy_constructible_v<PreviewAudioCompletion>, "completions are value types", err);
    ok &= expect(std::is_copy_constructible_v<PreviewAudioSnapshot>, "snapshots are value types", err);

    CommandIdentity identity;
    identity.sequence = 1;
    identity.generation = 2;
    identity.assetGeneration = 3;
    identity.transactionId = 4;
    identity.deviceSequence = 5;
    identity.pauseToken = 6;
    PreviewAudioCommand command;
    command.identity = identity;
    PreviewAudioCompletion completion;
    completion.identity = command.identity;
    PreviewAudioSnapshot snapshot;
    snapshot.identity = completion.identity;
    ok &= expect(snapshot.identity.sequence == 1 && snapshot.identity.generation == 2
                     && snapshot.identity.assetGeneration == 3
                     && snapshot.identity.transactionId == 4
                     && snapshot.identity.deviceSequence == 5
                     && snapshot.identity.pauseToken == 6,
                 "identity is carried through command completion and snapshot", err);
    return ok;
}

bool verifyClassificationAndCoalescing(QTextStream& err)
{
    bool ok = true;
    ok &= expect(commandClass(CommandKind::Shutdown) == CommandClass::High,
                 "shutdown is high priority", err);
    ok &= expect(commandClass(CommandKind::Start) == CommandClass::Ordered,
                 "start is ordered", err);
    ok &= expect(commandClass(CommandKind::SyncBackgroundTrack) == CommandClass::Latest,
                 "sync is latest-only", err);
    ok &= expect(commandClass(CommandKind::DrainEvents) == CommandClass::Latest,
                 "drain is latest-only", err);
    ok &= expect(commandClass(CommandKind::Audition) == CommandClass::Audition,
                 "audition has its own bounded class", err);
    ok &= expect(isPlaybackCommand(CommandKind::Start)
                     && isPlaybackCommand(CommandKind::SyncBackgroundTrack),
                 "transport and ticks are playback commands", err);
    ok &= expect(!isPlaybackCommand(CommandKind::ReloadAssets)
                     && !isPlaybackCommand(CommandKind::ApplyLevels)
                     && !isPlaybackCommand(CommandKind::Audition),
                 "asset settings and audition are not playback invalidation targets", err);

    const PreviewAudioCommand firstPause = [] {
        PreviewAudioCommand command = makeHigh(CommandKind::DeviceChangePause, 8, 21);
        command.identity.pauseToken = 11;
        return command;
    }();
    PreviewAudioCommand duplicatePause = firstPause;
    duplicatePause.identity.deviceSequence = 12;
    duplicatePause.identity.pauseToken = 12;
    ok &= expect(canCoalesce(firstPause, duplicatePause), "device pause duplicates coalesce", err);

    const PreviewAudioCommand firstTick = makeLatest(CommandKind::SyncBackgroundTrack, 8, 1.0);
    ok &= expect(canCoalesce(firstTick, makeLatest(CommandKind::SyncBackgroundTrack, 8, 2.0)),
                 "latest tick coalesces within its generation", err);
    ok &= expect(!canCoalesce(firstTick, makeLatest(CommandKind::SyncBackgroundTrack, 9, 2.0)),
                 "latest tick coalescing is generation-scoped", err);
    ok &= expect(!canCoalesce(firstTick, makeLatest(CommandKind::DrainEvents, 8, 2.0)),
                 "latest slots are kind-scoped", err);
    return ok;
}

bool verifySnapshotSequence(QTextStream& err)
{
    bool ok = true;
    const quint64 first = nextSnapshotSequence(0);
    const quint64 second = nextSnapshotSequence(first);
    ok &= expect(first == 1 && second == 2 && isNewerSnapshotSequence(second, first),
                 "snapshot sequence helper advances monotonically", err);
    ok &= expect(!isNewerSnapshotSequence(first, second),
                 "older snapshot sequence is rejected", err);
    return ok;
}

bool verifyCompletionAcceptance(QTextStream& err)
{
    bool ok = true;
    PreviewAudioCompletion started;
    started.kind = CommandKind::Start;
    started.identity.generation = 4;
    started.identity.transactionId = 19;
    ok &= expect(acceptsPlaybackCompletion(4, 19, started), "matching playback completion accepted", err);
    ok &= expect(!acceptsPlaybackCompletion(5, 19, started), "old playback generation rejected", err);
    ok &= expect(!acceptsPlaybackCompletion(4, 20, started), "wrong transaction rejected", err);

    PreviewAudioCompletion zeroTransaction = started;
    zeroTransaction.identity.transactionId = 0;
    ok &= expect(!acceptsPlaybackCompletion(4, 0, zeroTransaction),
                 "transactional operation requires a nonzero transaction", err);
    PreviewAudioCompletion drain;
    drain.kind = CommandKind::DrainEvents;
    drain.identity.generation = 4;
    ok &= expect(acceptsPlaybackCompletion(4, 0, drain),
                 "zero transaction is allowed for non-transactional playback work", err);
    drain.identity.transactionId = 19;
    ok &= expect(!acceptsPlaybackCompletion(4, 19, drain),
                 "non-transactional operation cannot impersonate a transaction", err);
    PreviewAudioCompletion reloadAsPlayback;
    reloadAsPlayback.kind = CommandKind::ReloadAssets;
    reloadAsPlayback.identity.generation = 4;
    ok &= expect(!acceptsPlaybackCompletion(4, 0, reloadAsPlayback),
                 "asset completion is not accepted through the playback predicate", err);

    PreviewAudioCompletion paused;
    paused.kind = CommandKind::DeviceChangePause;
    paused.identity.generation = 7;
    paused.identity.transactionId = 31;
    paused.identity.deviceSequence = 44;
    paused.identity.pauseToken = 43;
    const quint64 latestDeviceSequence = 45;
    Q_UNUSED(latestDeviceSequence);
    ok &= expect(acceptsDevicePauseCompletion(7, 31, 43, paused),
                 "pause completion matches immutable token despite a newer device sequence", err);
    ok &= expect(!acceptsDevicePauseCompletion(8, 31, 43, paused),
                 "pause completion requires current generation", err);
    ok &= expect(!acceptsDevicePauseCompletion(7, 32, 43, paused),
                 "pause completion requires pending transaction", err);
    ok &= expect(!acceptsDevicePauseCompletion(7, 31, 44, paused),
                 "pause completion requires immutable pause token", err);

    PreviewAudioCompletion reload;
    reload.kind = CommandKind::ReloadAssets;
    reload.identity.generation = 7;
    reload.identity.assetGeneration = 10;
    ok &= expect(!acceptsAssetCompletion(11, reload),
                 "older asset completion rejected despite matching playback generation", err);
    reload.identity.assetGeneration = 11;
    ok &= expect(acceptsAssetCompletion(11, reload), "latest asset completion accepted", err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;
    ok &= verifyVocabularyAndValueSemantics(err);
    ok &= verifyClassificationAndCoalescing(err);
    ok &= verifySnapshotSequence(err);
    ok &= verifyCompletionAcceptance(err);
    if (ok) {
        out << "preview_audio_worker_protocol_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
