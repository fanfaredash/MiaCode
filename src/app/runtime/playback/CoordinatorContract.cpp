#include "runtime/playback/PlaybackCoordinator.h"

namespace miacode::runtime {

void PlaybackCoordinator::setDocumentRevision(quint64 revision)
{
    identity_.setDocumentRevision(revision);
}

void PlaybackCoordinator::invalidateSession()
{
    identity_.invalidate();
}

miacode::v2::PlaybackSnapshot PlaybackCoordinator::playbackSnapshot() const
{
    if (!identity_.active()) {
        const miacode::v2::PlaybackCallbackStamp stamp = identity_.currentStamp();
        return {
            stamp.sessionGeneration,
            stamp.documentRevision,
            stamp.playbackSequence,
            0.0,
            0.0,
            0.0,
            1.0,
            miacode::v2::PlaybackTransportState::Stopped,
        };
    }
    return {
        identity_.sessionGeneration(),
        identity_.documentRevision(),
        identity_.playbackSequence(),
        positionSeconds(),
        durationSeconds(),
        lowerBoundSeconds(),
        playbackRate(),
        playbackTransportState(),
    };
}

bool PlaybackCoordinator::acceptsPlaybackCallback(
    const miacode::v2::PlaybackCallbackStamp& stamp) const
{
    return identity_.accepts(stamp);
}

double PlaybackCoordinator::currentAudioClockSecond() const
{
    return playbackSnapshot().canonicalChartTime;
}

bool PlaybackCoordinator::beginPlaybackCommand()
{
    if (!identity_.active()) {
        return false;
    }
    identity_.advanceSequence();
    return true;
}

void PlaybackCoordinator::updateScrub(double second)
{
    updateScrub(second, true);
}

void PlaybackCoordinator::endScrub(double second)
{
    endScrub(second, true);
}

}  // namespace miacode::runtime
