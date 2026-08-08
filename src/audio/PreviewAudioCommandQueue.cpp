#include "PreviewAudioCommandQueue.h"

#include <algorithm>
#include <utility>

namespace miacode::preview_audio {

namespace {

EnqueueResult accepted(
    bool replaced = false,
    bool coalesced = false,
    quint64 retiredSequence = 0)
{
    return {true, replaced, coalesced, CommandError::None, retiredSequence};
}

EnqueueResult rejected(CommandError error)
{
    return {false, false, false, error, 0};
}

template <typename Container>
void eraseStalePlayback(
    Container& entries,
    quint64 generation,
    std::vector<PreviewAudioCommand>* invalidatedCommands = nullptr)
{
    entries.erase(
        std::remove_if(
            entries.begin(),
            entries.end(),
            [generation, invalidatedCommands](auto& entry) {
                const bool stale = commandPolicy(entry.command.kind).invalidatedByPlaybackBoundary
                    && entry.command.identity.generation < generation;
                if (stale && invalidatedCommands != nullptr) {
                    invalidatedCommands->push_back(std::move(entry.command));
                }
                return stale;
            }),
        entries.end());
}

template <typename Entry>
void resetStalePlayback(
    std::optional<Entry>& entry,
    quint64 generation,
    std::vector<PreviewAudioCommand>* invalidatedCommands = nullptr)
{
    if (entry && commandPolicy(entry->command.kind).invalidatedByPlaybackBoundary
        && entry->command.identity.generation < generation) {
        if (invalidatedCommands != nullptr) {
            invalidatedCommands->push_back(std::move(entry->command));
        }
        entry.reset();
    }
}

template <typename Container>
bool containerContainsInvalidatablePlaybackGeneration(const Container& entries, quint64 generation)
{
    return std::any_of(
        entries.begin(),
        entries.end(),
        [generation](const auto& entry) {
            return commandPolicy(entry.command.kind).invalidatedByPlaybackBoundary
                && entry.command.identity.generation == generation;
        });
}

template <typename Entry>
bool optionalContainsInvalidatablePlaybackGeneration(
    const std::optional<Entry>& entry,
    quint64 generation)
{
    return entry && commandPolicy(entry->command.kind).invalidatedByPlaybackBoundary
        && entry->command.identity.generation == generation;
}

}  // namespace

PreviewAudioCommandQueue::Entry PreviewAudioCommandQueue::makeEntry(PreviewAudioCommand command)
{
    Entry entry;
    entry.command = std::move(command);
    entry.order = nextOrder_++;
    return entry;
}

EnqueueResult PreviewAudioCommandQueue::enqueue(PreviewAudioCommand command)
{
    if (command.kind == CommandKind::Shutdown) {
        return beginShutdown(std::move(command));
    }

    std::lock_guard lock(mutex_);
    if (shuttingDown_) {
        return rejected(CommandError::ShuttingDown);
    }
    const CommandPolicy policy = commandPolicy(command.kind);
    if (policy.invalidatedByPlaybackBoundary
        && command.identity.generation < minimumPlaybackGeneration_) {
        return rejected(CommandError::Stale);
    }

    if (command.kind == CommandKind::DeviceChangePause) {
        if (devicePause_) {
            if (canCoalesce(devicePause_->command, command)) {
                return accepted(false, true, command.identity.sequence);
            }
            return rejected(CommandError::QueueFull);
        }
        devicePause_ = makeEntry(std::move(command));
        return accepted();
    }

    if (command.kind == CommandKind::ManualPause) {
        if (!manualPause_) {
            manualPause_ = makeEntry(std::move(command));
            return accepted();
        }
        if (qsizetype(high_.size()) >= kHighCapacity) {
            return rejected(CommandError::QueueFull);
        }
        high_.push_back(makeEntry(std::move(command)));
        return accepted();
    }

    switch (commandClass(command.kind)) {
    case CommandClass::High:
        if (qsizetype(high_.size()) >= kHighCapacity) {
            return rejected(CommandError::QueueFull);
        }
        high_.push_back(makeEntry(std::move(command)));
        return accepted();
    case CommandClass::Ordered:
        if (qsizetype(ordered_.size()) >= kOrderedCapacity) {
            return rejected(CommandError::QueueFull);
        }
        ordered_.push_back(makeEntry(std::move(command)));
        return accepted();
    case CommandClass::Audition:
        if (qsizetype(auditions_.size()) >= kAuditionCapacity) {
            return rejected(CommandError::QueueFull);
        }
        auditions_.push_back(makeEntry(std::move(command)));
        return accepted();
    case CommandClass::Latest: {
        std::optional<Entry>& slot = command.kind == CommandKind::SyncBackgroundTrack
            ? syncBackgroundTrack_
            : drainEvents_;
        if (slot) {
            if (command.identity.generation < slot->command.identity.generation) {
                return rejected(CommandError::Stale);
            }
            const quint64 retiredSequence = slot->command.identity.sequence;
            slot = makeEntry(std::move(command));
            return accepted(true, false, retiredSequence);
        }
        slot = makeEntry(std::move(command));
        return accepted();
    }
    }
    return rejected(CommandError::BackendFailure);
}

EnqueueResult PreviewAudioCommandQueue::enqueueDeviceChangePauseBarrier(PreviewAudioCommand command)
{
    if (command.kind != CommandKind::DeviceChangePause) {
        return rejected(CommandError::BackendFailure);
    }

    std::lock_guard lock(mutex_);
    if (shuttingDown_) {
        return rejected(CommandError::ShuttingDown);
    }
    EnqueueResult result = accepted();
    minimumPlaybackGeneration_ = std::max(minimumPlaybackGeneration_, command.identity.generation);
    eraseStalePlayback(high_, minimumPlaybackGeneration_, &result.invalidatedCommands);
    eraseStalePlayback(ordered_, minimumPlaybackGeneration_, &result.invalidatedCommands);
    eraseStalePlayback(auditions_, minimumPlaybackGeneration_, &result.invalidatedCommands);
    resetStalePlayback(shutdown_, minimumPlaybackGeneration_, &result.invalidatedCommands);
    resetStalePlayback(devicePause_, minimumPlaybackGeneration_, &result.invalidatedCommands);
    resetStalePlayback(manualPause_, minimumPlaybackGeneration_, &result.invalidatedCommands);
    resetStalePlayback(syncBackgroundTrack_, minimumPlaybackGeneration_, &result.invalidatedCommands);
    resetStalePlayback(drainEvents_, minimumPlaybackGeneration_, &result.invalidatedCommands);

    if (devicePause_) {
        if (canCoalesce(devicePause_->command, command)) {
            result.coalesced = true;
            result.retiredSequence = command.identity.sequence;
            return result;
        }
        result.accepted = false;
        result.error = CommandError::QueueFull;
        return result;
    }
    devicePause_ = makeEntry(std::move(command));
    return result;
}

EnqueueResult PreviewAudioCommandQueue::beginShutdown(PreviewAudioCommand shutdown)
{
    std::lock_guard lock(mutex_);
    if (shuttingDown_) {
        return rejected(CommandError::ShuttingDown);
    }
    shutdown.kind = CommandKind::Shutdown;
    shutdown_ = makeEntry(std::move(shutdown));
    shuttingDown_ = true;
    return accepted();
}

std::optional<PreviewAudioCommand> PreviewAudioCommandQueue::takeHigh()
{
    enum class Source {
        None,
        Ordinary,
        Shutdown,
        DevicePause,
        ManualPause,
    };

    Source source = Source::None;
    quint64 firstOrder = std::numeric_limits<quint64>::max();
    const auto consider = [&source, &firstOrder](Source candidate, const Entry* entry) {
        if (entry != nullptr && entry->order < firstOrder) {
            source = candidate;
            firstOrder = entry->order;
        }
    };
    if (!high_.empty()) {
        firstOrder = high_.front().order;
        source = Source::Ordinary;
    }
    consider(Source::Shutdown, shutdown_ ? &*shutdown_ : nullptr);
    consider(Source::DevicePause, devicePause_ ? &*devicePause_ : nullptr);
    consider(Source::ManualPause, manualPause_ ? &*manualPause_ : nullptr);

    if (source == Source::None) {
        return std::nullopt;
    }
    if (source == Source::Ordinary) {
        PreviewAudioCommand command = std::move(high_.front().command);
        high_.pop_front();
        return command;
    }

    std::optional<Entry>* selected = nullptr;
    switch (source) {
    case Source::Shutdown:
        selected = &shutdown_;
        break;
    case Source::DevicePause:
        selected = &devicePause_;
        break;
    case Source::ManualPause:
        selected = &manualPause_;
        break;
    case Source::None:
    case Source::Ordinary:
        break;
    }
    PreviewAudioCommand command = std::move((*selected)->command);
    selected->reset();
    return command;
}

std::optional<PreviewAudioCommand> PreviewAudioCommandQueue::takeNext()
{
    std::lock_guard lock(mutex_);
    if (std::optional<PreviewAudioCommand> command = takeHigh()) {
        return command;
    }
    if (!ordered_.empty()) {
        PreviewAudioCommand command = std::move(ordered_.front().command);
        ordered_.pop_front();
        return command;
    }
    if (syncBackgroundTrack_ || drainEvents_) {
        std::optional<Entry>* selected = nullptr;
        if (!drainEvents_ || (syncBackgroundTrack_
                             && syncBackgroundTrack_->order < drainEvents_->order)) {
            selected = &syncBackgroundTrack_;
        } else {
            selected = &drainEvents_;
        }
        PreviewAudioCommand command = std::move((*selected)->command);
        selected->reset();
        return command;
    }
    if (!auditions_.empty()) {
        PreviewAudioCommand command = std::move(auditions_.front().command);
        auditions_.pop_front();
        return command;
    }
    return std::nullopt;
}

void PreviewAudioCommandQueue::invalidateBefore(quint64 generation)
{
    std::lock_guard lock(mutex_);
    minimumPlaybackGeneration_ = std::max(minimumPlaybackGeneration_, generation);
    eraseStalePlayback(high_, minimumPlaybackGeneration_);
    eraseStalePlayback(ordered_, minimumPlaybackGeneration_);
    eraseStalePlayback(auditions_, minimumPlaybackGeneration_);
    resetStalePlayback(shutdown_, minimumPlaybackGeneration_);
    resetStalePlayback(devicePause_, minimumPlaybackGeneration_);
    resetStalePlayback(manualPause_, minimumPlaybackGeneration_);
    resetStalePlayback(syncBackgroundTrack_, minimumPlaybackGeneration_);
    resetStalePlayback(drainEvents_, minimumPlaybackGeneration_);
}

bool PreviewAudioCommandQueue::containsInvalidatablePlaybackGeneration(quint64 generation) const
{
    std::lock_guard lock(mutex_);
    return containerContainsInvalidatablePlaybackGeneration(high_, generation)
        || containerContainsInvalidatablePlaybackGeneration(ordered_, generation)
        || containerContainsInvalidatablePlaybackGeneration(auditions_, generation)
        || optionalContainsInvalidatablePlaybackGeneration(shutdown_, generation)
        || optionalContainsInvalidatablePlaybackGeneration(devicePause_, generation)
        || optionalContainsInvalidatablePlaybackGeneration(manualPause_, generation)
        || optionalContainsInvalidatablePlaybackGeneration(syncBackgroundTrack_, generation)
        || optionalContainsInvalidatablePlaybackGeneration(drainEvents_, generation);
}

bool PreviewAudioCommandQueue::empty() const
{
    return size() == 0;
}

qsizetype PreviewAudioCommandQueue::size() const
{
    std::lock_guard lock(mutex_);
    return qsizetype(high_.size() + ordered_.size() + auditions_.size())
        + (shutdown_ ? 1 : 0)
        + (devicePause_ ? 1 : 0)
        + (manualPause_ ? 1 : 0)
        + (syncBackgroundTrack_ ? 1 : 0)
        + (drainEvents_ ? 1 : 0);
}

bool PreviewAudioCommandQueue::isShuttingDown() const
{
    std::lock_guard lock(mutex_);
    return shuttingDown_;
}

}  // namespace miacode::preview_audio
