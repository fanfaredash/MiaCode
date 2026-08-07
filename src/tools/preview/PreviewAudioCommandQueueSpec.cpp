#include <QTextStream>

#include <optional>

#include "audio/PreviewAudioCommandQueue.h"

namespace {

using miacode::preview_audio::CommandError;
using miacode::preview_audio::CommandKind;
using miacode::preview_audio::PreviewAudioCommand;
using miacode::preview_audio::PreviewAudioCommandQueue;
using miacode::preview_audio::makeAudition;
using miacode::preview_audio::makeHigh;
using miacode::preview_audio::makeLatest;
using miacode::preview_audio::makeOrdered;

bool expect(bool condition, const char* message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

PreviewAudioCommand numbered(PreviewAudioCommand command, quint64 sequence)
{
    command.identity.sequence = sequence;
    return command;
}

PreviewAudioCommand devicePause(quint64 generation, quint64 transactionId, quint64 pauseToken)
{
    PreviewAudioCommand command = makeHigh(CommandKind::DeviceChangePause, generation, transactionId);
    command.identity.pauseToken = pauseToken;
    return command;
}

bool verifyFifoWithinClasses(QTextStream& err)
{
    bool ok = true;

    PreviewAudioCommandQueue high;
    ok &= expect(high.enqueue(numbered(makeHigh(CommandKind::StopAll, 1), 1)).accepted,
                 "first high command accepted", err);
    ok &= expect(high.enqueue(numbered(makeHigh(CommandKind::StopAll, 2), 2)).accepted,
                 "second high command accepted", err);
    ok &= expect(high.takeNext()->identity.sequence == 1, "high queue is FIFO (first)", err);
    ok &= expect(high.takeNext()->identity.sequence == 2, "high queue is FIFO (second)", err);

    PreviewAudioCommandQueue ordered;
    ok &= expect(ordered.enqueue(numbered(makeOrdered(CommandKind::Start, 1), 3)).accepted,
                 "first ordered command accepted", err);
    ok &= expect(ordered.enqueue(numbered(makeOrdered(CommandKind::SeekRetained, 1), 4)).accepted,
                 "second ordered command accepted", err);
    ok &= expect(ordered.takeNext()->identity.sequence == 3, "ordered queue is FIFO (first)", err);
    ok &= expect(ordered.takeNext()->identity.sequence == 4, "ordered queue is FIFO (second)", err);

    PreviewAudioCommandQueue audition;
    ok &= expect(audition.enqueue(numbered(makeAudition(7, QStringLiteral("answer")), 5)).accepted,
                 "first audition accepted", err);
    ok &= expect(audition.enqueue(numbered(makeAudition(7, QStringLiteral("slide")), 6)).accepted,
                 "second audition accepted", err);
    ok &= expect(audition.takeNext()->identity.sequence == 5, "audition queue is FIFO (first)", err);
    ok &= expect(audition.takeNext()->identity.sequence == 6, "audition queue is FIFO (second)", err);
    return ok;
}

bool verifyCapacitiesAndReservedSlots(QTextStream& err)
{
    bool ok = true;
    ok &= expect(PreviewAudioCommandQueue::kHighCapacity == 16,
                 "ordinary high capacity is exactly 16", err);
    ok &= expect(PreviewAudioCommandQueue::kOrderedCapacity == 128,
                 "ordered capacity is exactly 128", err);
    ok &= expect(PreviewAudioCommandQueue::kAuditionCapacity == 32,
                 "audition capacity is exactly 32", err);

    PreviewAudioCommandQueue high;
    for (qsizetype i = 0; i < PreviewAudioCommandQueue::kHighCapacity; ++i) {
        ok &= expect(high.enqueue(numbered(makeHigh(CommandKind::StopAll, 1), quint64(i + 1))).accepted,
                     "ordinary high slot accepted", err);
    }
    const auto highOverflow = high.enqueue(makeHigh(CommandKind::StopAll, 1));
    ok &= expect(!highOverflow.accepted && highOverflow.error == CommandError::QueueFull,
                 "ordinary high overflow is explicit", err);

    const auto firstPause = high.enqueue(devicePause(5, 17, 41));
    const auto duplicatePause = high.enqueue(devicePause(5, 17, 42));
    ok &= expect(firstPause.accepted, "reserved device pause accepted when ordinary high is full", err);
    ok &= expect(duplicatePause.accepted && duplicatePause.coalesced && !duplicatePause.replaced,
                 "duplicate device pause coalesces", err);
    const auto firstManual = high.enqueue(
        numbered(makeHigh(CommandKind::ManualPause, 5, 17), 90));
    const auto secondManual = high.enqueue(
        numbered(makeHigh(CommandKind::ManualPause, 6, 18), 91));
    ok &= expect(firstManual.accepted,
                 "reserved manual pause accepted when ordinary high is full", err);
    ok &= expect(!secondManual.accepted && secondManual.error == CommandError::QueueFull,
                 "second distinct manual pause is full when reserve and ordinary high are full", err);

    PreviewAudioCommand shutdown = makeHigh(CommandKind::Shutdown);
    shutdown.identity.sequence = 99;
    ok &= expect(high.beginShutdown(shutdown).accepted,
                 "reserved shutdown accepted when ordinary high is full", err);
    const auto rejected = high.enqueue(makeOrdered(CommandKind::Start, 6, 18));
    ok &= expect(!rejected.accepted && rejected.error == CommandError::ShuttingDown,
                 "post-shutdown enqueue is explicit", err);

    bool foundDevicePause = false;
    bool foundShutdown = false;
    QVector<quint64> manualSequences;
    while (const std::optional<PreviewAudioCommand> command = high.takeNext()) {
        if (command->kind == CommandKind::DeviceChangePause) {
            foundDevicePause = true;
            ok &= expect(command->identity.pauseToken == 41,
                         "coalescing preserves the first immutable pause token", err);
        }
        if (command->kind == CommandKind::Shutdown) {
            foundShutdown = true;
            ok &= expect(command->identity.sequence == 99, "shutdown command is preserved", err);
        }
        if (command->kind == CommandKind::ManualPause) {
            manualSequences.append(command->identity.sequence);
        }
    }
    ok &= expect(foundDevicePause, "reserved device pause can be taken", err);
    ok &= expect(foundShutdown, "reserved shutdown can be taken", err);
    ok &= expect(manualSequences == QVector<quint64>({90}),
                 "rejected manual pause does not replace the reserved command", err);

    PreviewAudioCommandQueue ordered;
    for (qsizetype i = 0; i < PreviewAudioCommandQueue::kOrderedCapacity; ++i) {
        ok &= expect(ordered.enqueue(numbered(makeOrdered(CommandKind::Start, 1), quint64(i))).accepted,
                     "ordered slot accepted", err);
    }
    const auto orderedOverflow = ordered.enqueue(makeOrdered(CommandKind::Start, 1));
    ok &= expect(!orderedOverflow.accepted && orderedOverflow.error == CommandError::QueueFull,
                 "ordered capacity is 128", err);

    PreviewAudioCommandQueue audition;
    for (qsizetype i = 0; i < PreviewAudioCommandQueue::kAuditionCapacity; ++i) {
        ok &= expect(audition.enqueue(makeAudition(1, QString::number(i))).accepted,
                     "audition slot accepted", err);
    }
    const auto auditionOverflow = audition.enqueue(makeAudition(1, QStringLiteral("overflow")));
    ok &= expect(!auditionOverflow.accepted && auditionOverflow.error == CommandError::QueueFull,
                 "audition capacity is 32", err);
    return ok;
}

bool verifyManualPauseReserveAndOrdinarySpill(QTextStream& err)
{
    bool ok = true;
    PreviewAudioCommandQueue queue;

    const auto reserved = queue.enqueue(
        numbered(makeHigh(CommandKind::ManualPause, 5, 17), 1));
    const auto ordinaryBeforeSpill = queue.enqueue(
        numbered(makeHigh(CommandKind::StopAll, 5), 2));
    const auto spilled = queue.enqueue(
        numbered(makeHigh(CommandKind::ManualPause, 6, 18), 3));
    ok &= expect(reserved.accepted, "first manual pause uses its reserved slot", err);
    ok &= expect(ordinaryBeforeSpill.accepted, "ordinary high command accepted", err);
    ok &= expect(spilled.accepted && !spilled.coalesced,
                 "second distinct manual pause uses ordinary high capacity", err);

    for (qsizetype i = 0; i < PreviewAudioCommandQueue::kHighCapacity - 2; ++i) {
        ok &= expect(queue.enqueue(numbered(
                         makeHigh(CommandKind::StopAll, 6), quint64(i + 4))).accepted,
                     "remaining ordinary high slot accepted", err);
    }
    const auto overflow = queue.enqueue(makeHigh(CommandKind::StopAll, 6));
    ok &= expect(!overflow.accepted && overflow.error == CommandError::QueueFull,
                 "spilled manual pause consumes exactly one ordinary high slot", err);

    const auto first = queue.takeNext();
    const auto second = queue.takeNext();
    const auto third = queue.takeNext();
    ok &= expect(first && first->identity.sequence == 1,
                 "reserved manual pause keeps global high FIFO position", err);
    ok &= expect(second && second->identity.sequence == 2,
                 "ordinary command keeps global high FIFO position", err);
    ok &= expect(third && third->identity.sequence == 3,
                 "spilled manual pause keeps global high FIFO position", err);
    return ok;
}

bool verifyPriorityAndLatestReplacement(QTextStream& err)
{
    bool ok = true;
    PreviewAudioCommandQueue queue;

    ok &= expect(queue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 1.0)).accepted,
                 "sync tick accepted", err);
    ok &= expect(queue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 2.0)).replaced,
                 "same-generation sync tick replaced", err);
    ok &= expect(queue.enqueue(makeLatest(CommandKind::DrainEvents, 4, 3.0)).accepted,
                 "drain tick has an independent latest slot", err);
    ok &= expect(queue.enqueue(numbered(makeOrdered(CommandKind::Start, 4, 7), 7)).accepted,
                 "ordered start accepted", err);
    ok &= expect(queue.enqueue(numbered(devicePause(4, 7, 9), 8)).accepted,
                 "reserved high pause accepted", err);

    const auto first = queue.takeNext();
    const auto second = queue.takeNext();
    const auto third = queue.takeNext();
    const auto fourth = queue.takeNext();
    ok &= expect(first && first->kind == CommandKind::DeviceChangePause,
                 "high priority precedes ordered and latest", err);
    ok &= expect(second && second->kind == CommandKind::Start,
                 "ordered work precedes latest-only work", err);
    ok &= expect(third && third->kind == CommandKind::SyncBackgroundTrack
                     && third->value == 2.0,
                 "latest sync exposes only the replacement value", err);
    ok &= expect(fourth && fourth->kind == CommandKind::DrainEvents
                     && fourth->value == 3.0,
                 "latest drain retains its own value", err);
    ok &= expect(!queue.takeNext(), "replaced sync value disappeared", err);
    return ok;
}

bool verifyLatestGenerationOrdering(QTextStream& err)
{
    bool ok = true;

    PreviewAudioCommandQueue syncSameGeneration;
    syncSameGeneration.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 1.0));
    ok &= expect(syncSameGeneration.enqueue(
                     makeLatest(CommandKind::SyncBackgroundTrack, 4, 2.0)).replaced,
                 "same-generation sync replaces", err);
    ok &= expect(syncSameGeneration.takeNext()->value == 2.0,
                 "same-generation sync keeps newest value", err);

    PreviewAudioCommandQueue syncNewerGeneration;
    syncNewerGeneration.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 1.0));
    ok &= expect(syncNewerGeneration.enqueue(
                     makeLatest(CommandKind::SyncBackgroundTrack, 5, 2.0)).replaced,
                 "newer-generation sync replaces older generation", err);
    const auto newerSync = syncNewerGeneration.takeNext();
    ok &= expect(newerSync && newerSync->identity.generation == 5 && newerSync->value == 2.0,
                 "newer-generation sync remains queued", err);

    PreviewAudioCommandQueue syncOlderGeneration;
    syncOlderGeneration.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 5, 2.0));
    const auto staleSync = syncOlderGeneration.enqueue(
        makeLatest(CommandKind::SyncBackgroundTrack, 4, 1.0));
    ok &= expect(!staleSync.accepted && staleSync.error == CommandError::Stale,
                 "older-generation sync is explicitly stale", err);
    const auto retainedSync = syncOlderGeneration.takeNext();
    ok &= expect(retainedSync && retainedSync->identity.generation == 5 && retainedSync->value == 2.0,
                 "older sync cannot overwrite newer slot", err);

    PreviewAudioCommandQueue drainSameGeneration;
    drainSameGeneration.enqueue(makeLatest(CommandKind::DrainEvents, 4, 1.0));
    ok &= expect(drainSameGeneration.enqueue(
                     makeLatest(CommandKind::DrainEvents, 4, 2.0)).replaced,
                 "same-generation drain replaces", err);
    ok &= expect(drainSameGeneration.takeNext()->value == 2.0,
                 "same-generation drain keeps newest value", err);

    PreviewAudioCommandQueue drainNewerGeneration;
    drainNewerGeneration.enqueue(makeLatest(CommandKind::DrainEvents, 4, 1.0));
    ok &= expect(drainNewerGeneration.enqueue(
                     makeLatest(CommandKind::DrainEvents, 5, 2.0)).replaced,
                 "newer-generation drain replaces older generation", err);
    const auto newerDrain = drainNewerGeneration.takeNext();
    ok &= expect(newerDrain && newerDrain->identity.generation == 5 && newerDrain->value == 2.0,
                 "newer-generation drain remains queued", err);

    PreviewAudioCommandQueue drainOlderGeneration;
    drainOlderGeneration.enqueue(makeLatest(CommandKind::DrainEvents, 5, 2.0));
    const auto staleDrain = drainOlderGeneration.enqueue(
        makeLatest(CommandKind::DrainEvents, 4, 1.0));
    ok &= expect(!staleDrain.accepted && staleDrain.error == CommandError::Stale,
                 "older-generation drain is explicitly stale", err);
    const auto retainedDrain = drainOlderGeneration.takeNext();
    ok &= expect(retainedDrain && retainedDrain->identity.generation == 5 && retainedDrain->value == 2.0,
                 "older drain cannot overwrite newer slot", err);
    return ok;
}

bool verifyLatestReplacementRefreshesFifoOrder(QTextStream& err)
{
    bool ok = true;
    PreviewAudioCommandQueue queue;
    queue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 1.0));
    queue.enqueue(makeLatest(CommandKind::DrainEvents, 4, 2.0));
    ok &= expect(queue.enqueue(
                     makeLatest(CommandKind::SyncBackgroundTrack, 4, 3.0)).replaced,
                 "later sync replaces its occupied slot", err);

    const auto first = queue.takeNext();
    const auto second = queue.takeNext();
    ok &= expect(first && first->kind == CommandKind::DrainEvents && first->value == 2.0,
                 "unreplaced drain keeps its earlier latest-class FIFO position", err);
    ok &= expect(second && second->kind == CommandKind::SyncBackgroundTrack && second->value == 3.0,
                 "replacement sync receives its later latest-class FIFO position", err);
    return ok;
}

bool verifyInvalidationWatermark(QTextStream& err)
{
    bool ok = true;
    PreviewAudioCommandQueue orderedQueue;
    orderedQueue.invalidateBefore(5);

    const auto oldStart = orderedQueue.enqueue(makeOrdered(CommandKind::Start, 4, 2));
    ok &= expect(!oldStart.accepted && oldStart.error == CommandError::Stale,
                 "old ordered playback is stale after invalidation", err);
    ok &= expect(orderedQueue.enqueue(makeOrdered(CommandKind::Start, 5, 3)).accepted,
                 "generation at watermark survives", err);

    PreviewAudioCommandQueue latestQueue;
    latestQueue.invalidateBefore(5);
    ok &= expect(latestQueue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 5, 1.0)).accepted,
                 "threshold sync is accepted", err);
    ok &= expect(latestQueue.takeNext()->kind == CommandKind::SyncBackgroundTrack,
                 "threshold sync can be taken", err);
    const auto oldSync = latestQueue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 2.0));
    ok &= expect(!oldSync.accepted && oldSync.error == CommandError::Stale,
                 "old sync stays stale after its slot was taken", err);
    if (oldSync.accepted) {
        latestQueue.takeNext();
    }

    ok &= expect(latestQueue.enqueue(makeLatest(CommandKind::DrainEvents, 5, 1.0)).accepted,
                 "threshold drain is accepted", err);
    ok &= expect(latestQueue.takeNext()->kind == CommandKind::DrainEvents,
                 "threshold drain can be taken", err);
    const auto oldDrain = latestQueue.enqueue(makeLatest(CommandKind::DrainEvents, 4, 2.0));
    ok &= expect(!oldDrain.accepted && oldDrain.error == CommandError::Stale,
                 "old drain stays stale after its slot was taken", err);

    PreviewAudioCommandQueue policyQueue;
    policyQueue.invalidateBefore(5);
    const auto oldPausedState = policyQueue.enqueue(makeOrdered(CommandKind::ApplyPausedState, 4));
    ok &= expect(!oldPausedState.accepted && oldPausedState.error == CommandError::Stale,
                 "old paused state is playback-boundary stale", err);

    PreviewAudioCommand reload = makeOrdered(CommandKind::ReloadAssets, 4);
    reload.identity.assetGeneration = 9;
    PreviewAudioCommand levels = makeOrdered(CommandKind::ApplyLevels, 4);
    levels.identity.assetGeneration = 9;
    ok &= expect(policyQueue.enqueue(reload).accepted,
                 "old-generation asset work bypasses playback watermark", err);
    ok &= expect(policyQueue.enqueue(levels).accepted,
                 "old-generation settings work bypasses playback watermark", err);

    PreviewAudioCommandQueue monotonicQueue;
    monotonicQueue.invalidateBefore(7);
    monotonicQueue.invalidateBefore(6);
    const auto belowMaximum = monotonicQueue.enqueue(makeOrdered(CommandKind::Start, 6, 4));
    ok &= expect(!belowMaximum.accepted && belowMaximum.error == CommandError::Stale,
                 "invalidation watermark never moves backward", err);
    ok &= expect(monotonicQueue.enqueue(makeOrdered(CommandKind::Start, 7, 5)).accepted,
                 "generation at maximum watermark survives", err);

    PreviewAudioCommandQueue safetyQueue;
    safetyQueue.enqueue(numbered(devicePause(4, 8, 22), 1));
    safetyQueue.enqueue(numbered(makeHigh(CommandKind::ManualPause, 4, 8), 2));
    safetyQueue.enqueue(numbered(makeHigh(CommandKind::StopAll, 4), 3));
    safetyQueue.invalidateBefore(5);
    const auto device = safetyQueue.takeNext();
    const auto manual = safetyQueue.takeNext();
    const auto stop = safetyQueue.takeNext();
    ok &= expect(device && device->kind == CommandKind::DeviceChangePause
                     && device->identity.sequence == 1,
                 "device pause safety barrier survives playback invalidation", err);
    ok &= expect(manual && manual->kind == CommandKind::ManualPause
                     && manual->identity.sequence == 2,
                 "manual pause safety control survives playback invalidation", err);
    ok &= expect(stop && stop->kind == CommandKind::StopAll
                     && stop->identity.sequence == 3,
                 "stop-all safety control survives playback invalidation", err);
    return ok;
}

bool verifyPlaybackInvalidation(QTextStream& err)
{
    bool ok = true;
    PreviewAudioCommandQueue queue;

    PreviewAudioCommand reload = makeOrdered(CommandKind::ReloadAssets, 4);
    reload.identity.assetGeneration = 12;
    PreviewAudioCommand levels = makeOrdered(CommandKind::ApplyLevels, 4);
    levels.identity.assetGeneration = 12;
    ok &= expect(queue.enqueue(makeOrdered(CommandKind::Start, 4, 2)).accepted,
                 "old playback command accepted", err);
    ok &= expect(queue.enqueue(makeLatest(CommandKind::SyncBackgroundTrack, 4, 1.0)).accepted,
                 "old playback tick accepted", err);
    ok &= expect(queue.enqueue(reload).accepted, "old-generation asset work accepted", err);
    ok &= expect(queue.enqueue(levels).accepted, "old-generation settings work accepted", err);
    ok &= expect(queue.enqueue(makeAudition(12, QStringLiteral("answer"))).accepted,
                 "asset-scoped audition accepted", err);
    ok &= expect(queue.enqueue(makeOrdered(CommandKind::Start, 5, 3)).accepted,
                 "current playback command accepted", err);

    queue.invalidateBefore(5);
    ok &= expect(!queue.containsInvalidatablePlaybackGeneration(4),
                 "old invalidatable playback generation removed", err);
    ok &= expect(queue.containsInvalidatablePlaybackGeneration(5),
                 "current invalidatable playback generation retained", err);

    bool foundReload = false;
    bool foundLevels = false;
    bool foundAudition = false;
    bool foundCurrentStart = false;
    while (const std::optional<PreviewAudioCommand> command = queue.takeNext()) {
        foundReload |= command->kind == CommandKind::ReloadAssets;
        foundLevels |= command->kind == CommandKind::ApplyLevels;
        foundAudition |= command->kind == CommandKind::Audition;
        foundCurrentStart |= command->kind == CommandKind::Start
            && command->identity.generation == 5;
    }
    ok &= expect(foundReload && foundLevels, "asset and settings work survives playback invalidation", err);
    ok &= expect(foundAudition, "asset-scoped audition survives playback invalidation", err);
    ok &= expect(foundCurrentStart, "generation at threshold survives invalidation", err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;
    ok &= verifyFifoWithinClasses(err);
    ok &= verifyCapacitiesAndReservedSlots(err);
    ok &= verifyManualPauseReserveAndOrdinarySpill(err);
    ok &= verifyPriorityAndLatestReplacement(err);
    ok &= verifyLatestGenerationOrdering(err);
    ok &= verifyLatestReplacementRefreshesFifoOrder(err);
    ok &= verifyPlaybackInvalidation(err);
    ok &= verifyInvalidationWatermark(err);
    if (ok) {
        out << "preview_audio_command_queue_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
