#include "PlaybackIdentityGate.h"

namespace miacode::runtime {

PlaybackIdentityGate::PlaybackIdentityGate(quint64 sessionGeneration)
    : sessionGeneration_(sessionGeneration != 0
                             ? sessionGeneration
                             : miacode::v2::nextSessionGeneration())
{
}

quint64 PlaybackIdentityGate::advanceSequence()
{
    if (!active_) {
        return playbackSequence_;
    }
    return ++playbackSequence_;
}

void PlaybackIdentityGate::invalidate()
{
    if (!active_) {
        return;
    }
    active_ = false;
    ++sessionGeneration_;
    ++playbackSequence_;
}

miacode::v2::PlaybackCallbackStamp PlaybackIdentityGate::currentStamp() const
{
    return {sessionGeneration_, documentRevision_, playbackSequence_};
}

bool PlaybackIdentityGate::accepts(
    const miacode::v2::PlaybackCallbackStamp& stamp) const
{
    return active_ && stamp == currentStamp();
}

}  // namespace miacode::runtime
