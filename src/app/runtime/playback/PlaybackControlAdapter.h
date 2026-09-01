#pragma once

#include "app/v2/PlaybackControl.h"
#include "app/v2/PreviewSurface.h"

namespace miacode::runtime {

// Transitional adapter: exposes the new single playback contract while the
// implementation still lives in the composite PlaybackHost. It owns no
// preview resources and deliberately forwards every command to the legacy
// surface, so 4.5 cannot accidentally create a second transport engine.
class PlaybackControlAdapter final : public miacode::v2::PlaybackControl
{
public:
    explicit PlaybackControlAdapter(miacode::v2::PreviewSurface& legacySurface,
                                    quint64 sessionGeneration = 0);

    void setDocumentRevision(quint64 revision);
    void invalidateSession();

    miacode::v2::PlaybackSnapshot playbackSnapshot() const override;
    bool acceptsPlaybackCallback(
        const miacode::v2::PlaybackCallbackStamp& stamp) const override;

    void togglePlayback() override;
    void stop() override;
    void seek(double second) override;
    void beginScrub() override;
    void updateScrub(double second) override;
    void endScrub(double second) override;
    void setPlaybackRate(double rate) override;
    void nudgePlaybackRate(int direction) override;

private:
    quint64 advanceSequence();

    miacode::v2::PreviewSurface* legacySurface_ = nullptr;
    quint64 sessionGeneration_ = 1;
    quint64 documentRevision_ = 0;
    quint64 playbackSequence_ = 0;
};

}  // namespace miacode::runtime
