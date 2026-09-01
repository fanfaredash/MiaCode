#pragma once

#include <QtGlobal>

namespace miacode::v2 {

// The one transport state shared by Preview and Timeline projections. A
// surface may render this state, but it must not create a second playhead or
// timer for it.
enum class PlaybackTransportState {
    Stopped,
    Paused,
    Playing,
    Scrubbing,
};

struct PlaybackCallbackStamp {
    quint64 sessionGeneration = 0;
    quint64 documentRevision = 0;
    quint64 playbackSequence = 0;

    friend bool operator==(const PlaybackCallbackStamp&, const PlaybackCallbackStamp&) = default;
};

// A read-only value crossing from the playback authority to Preview,
// Timeline, export, or QML projection code. `canonicalChartTime` is the only
// playhead value consumers may use for cross-domain decisions.
struct PlaybackSnapshot {
    quint64 sessionGeneration = 0;
    quint64 documentRevision = 0;
    quint64 playbackSequence = 0;
    double canonicalChartTime = 0.0;
    double durationSeconds = 0.0;
    double lowerBoundSeconds = 0.0;
    double playbackRate = 1.0;
    PlaybackTransportState transportState = PlaybackTransportState::Stopped;

    PlaybackCallbackStamp stamp() const
    {
        return {sessionGeneration, documentRevision, playbackSequence};
    }
};

// State is intentionally separate from commands. A future coordinator can
// publish this feed without exposing its mutable transport implementation.
class PlaybackStateFeed
{
public:
    virtual ~PlaybackStateFeed() = default;

    virtual PlaybackSnapshot playbackSnapshot() const = 0;
    virtual bool acceptsPlaybackCallback(const PlaybackCallbackStamp& stamp) const = 0;
};

class PlaybackControl : public PlaybackStateFeed
{
public:
    ~PlaybackControl() override = default;

    virtual void togglePlayback() = 0;
    virtual void stop() = 0;
    virtual void seek(double second) = 0;
    virtual void beginScrub() = 0;
    virtual void updateScrub(double second) = 0;
    virtual void endScrub(double second) = 0;
    virtual void setPlaybackRate(double rate) = 0;
    virtual void nudgePlaybackRate(int direction) = 0;
};

}  // namespace miacode::v2
