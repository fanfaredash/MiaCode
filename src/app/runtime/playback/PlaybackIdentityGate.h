#pragma once

#include "app/v2/PlaybackControl.h"

namespace miacode::runtime {

// Small, UI-independent identity gate for playback callbacks and commands.
// Keeping this state separate makes the generation/revision/sequence rules
// testable without constructing the Session or its native runtime objects.
class PlaybackIdentityGate final
{
public:
    explicit PlaybackIdentityGate(quint64 sessionGeneration = 0);

    quint64 sessionGeneration() const { return sessionGeneration_; }
    quint64 documentRevision() const { return documentRevision_; }
    quint64 playbackSequence() const { return playbackSequence_; }
    bool active() const { return active_; }

    void setDocumentRevision(quint64 revision) { documentRevision_ = revision; }
    quint64 advanceSequence();
    void invalidate();

    miacode::v2::PlaybackCallbackStamp currentStamp() const;
    bool accepts(const miacode::v2::PlaybackCallbackStamp& stamp) const;

private:
    quint64 sessionGeneration_ = 0;
    quint64 documentRevision_ = 0;
    quint64 playbackSequence_ = 0;
    bool active_ = true;
};

}  // namespace miacode::runtime
