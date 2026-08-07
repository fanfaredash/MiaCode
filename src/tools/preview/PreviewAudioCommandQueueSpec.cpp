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
    ok &= expect(high.enqueue(makeHigh(CommandKind::ManualPause, 5, 17)).accepted,
                 "reserved manual pause accepted when ordinary high is full", err);

    PreviewAudioCommand shutdown = makeHigh(CommandKind::Shutdown);
    shutdown.identity.sequence = 99;
    ok &= expect(high.beginShutdown(shutdown).accepted,
                 "reserved shutdown accepted when ordinary high is full", err);
    const auto rejected = high.enqueue(makeOrdered(CommandKind::Start, 6, 18));
    ok &= expect(!rejected.accepted && rejected.error == CommandError::ShuttingDown,
                 "post-shutdown enqueue is explicit", err);

    bool foundDevicePause = false;
    bool foundShutdown = false;
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
    }
    ok &= expect(foundDevicePause, "reserved device pause can be taken", err);
    ok &= expect(foundShutdown, "reserved shutdown can be taken", err);

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
    ok &= expect(!queue.containsPlaybackGeneration(4), "old playback generation invalidated", err);
    ok &= expect(queue.containsPlaybackGeneration(5), "current playback generation retained", err);

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
    ok &= verifyPriorityAndLatestReplacement(err);
    ok &= verifyPlaybackInvalidation(err);
    if (ok) {
        out << "preview_audio_command_queue_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
