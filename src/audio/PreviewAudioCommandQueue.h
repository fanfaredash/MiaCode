#pragma once

#include "PreviewAudioWorkerProtocol.h"

#include <deque>
#include <mutex>
#include <optional>

namespace miacode::preview_audio {

struct EnqueueResult {
    bool accepted = false;
    bool replaced = false;
    bool coalesced = false;
    CommandError error = CommandError::None;
};

class PreviewAudioCommandQueue
{
public:
    static constexpr qsizetype kHighCapacity = 16;
    static constexpr qsizetype kOrderedCapacity = 128;
    static constexpr qsizetype kAuditionCapacity = 32;

    EnqueueResult enqueue(PreviewAudioCommand command);
    EnqueueResult beginShutdown(
        PreviewAudioCommand shutdown = makeHigh(CommandKind::Shutdown));
    std::optional<PreviewAudioCommand> takeNext();

    void invalidateBefore(quint64 generation);
    bool containsPlaybackGeneration(quint64 generation) const;
    bool empty() const;
    qsizetype size() const;
    bool isShuttingDown() const;

private:
    struct Entry {
        PreviewAudioCommand command;
        quint64 order = 0;
    };

    Entry makeEntry(PreviewAudioCommand command);
    std::optional<PreviewAudioCommand> takeHigh();

    mutable std::mutex mutex_;
    std::deque<Entry> high_;
    std::deque<Entry> ordered_;
    std::deque<Entry> auditions_;
    std::deque<Entry> manualPauses_;
    std::optional<Entry> shutdown_;
    std::optional<Entry> devicePause_;
    std::optional<Entry> syncBackgroundTrack_;
    std::optional<Entry> drainEvents_;
    quint64 nextOrder_ = 1;
    bool shuttingDown_ = false;
};

}  // namespace miacode::preview_audio
