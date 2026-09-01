#pragma once

#include "app/v2/PlaybackControl.h"

namespace miacode::v2 {

// Narrow transport port consumed by PreviewHost. The port deliberately has
// no PreviewRuntime, StageMedia, mixer, QSG, or document methods.
class PreviewPlaybackPort
{
public:
    virtual ~PreviewPlaybackPort() = default;

    virtual PlaybackSnapshot playbackSnapshot() const = 0;
    virtual bool acceptsPlaybackCallback(const PlaybackCallbackStamp& stamp) const = 0;

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
