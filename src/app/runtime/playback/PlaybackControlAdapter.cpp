#include "PlaybackControlAdapter.h"

namespace miacode::runtime {

PlaybackControlAdapter::PlaybackControlAdapter(
    miacode::v2::PreviewSurface& legacySurface, quint64 sessionGeneration)
    : legacySurface_(&legacySurface)
    , sessionGeneration_(sessionGeneration != 0
                             ? sessionGeneration
                             : miacode::v2::nextSessionGeneration())
{
}

void PlaybackControlAdapter::setDocumentRevision(quint64 revision)
{
    if (documentRevision_ == revision) {
        return;
    }
    documentRevision_ = revision;
}

void PlaybackControlAdapter::invalidateSession()
{
    ++sessionGeneration_;
    ++playbackSequence_;
    legacySurface_ = nullptr;
}

miacode::v2::PlaybackSnapshot PlaybackControlAdapter::playbackSnapshot() const
{
    return {
        sessionGeneration_,
        documentRevision_,
        playbackSequence_,
        legacySurface_ != nullptr ? legacySurface_->positionSeconds() : 0.0,
        legacySurface_ != nullptr ? legacySurface_->durationSeconds() : 0.0,
        legacySurface_ != nullptr ? legacySurface_->lowerBoundSeconds() : 0.0,
        legacySurface_ != nullptr ? legacySurface_->playbackRate() : 1.0,
        legacySurface_ != nullptr ? legacySurface_->playbackTransportState()
                                  : miacode::v2::PlaybackTransportState::Stopped,
    };
}

bool PlaybackControlAdapter::acceptsPlaybackCallback(
    const miacode::v2::PlaybackCallbackStamp& stamp) const
{
    return stamp == playbackSnapshot().stamp();
}

quint64 PlaybackControlAdapter::advanceSequence()
{
    return ++playbackSequence_;
}

void PlaybackControlAdapter::togglePlayback()
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->togglePlayback();
}

void PlaybackControlAdapter::stop()
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->stop();
}

void PlaybackControlAdapter::seek(double second)
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->seek(second);
}

void PlaybackControlAdapter::beginScrub()
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->beginScrub();
}

void PlaybackControlAdapter::updateScrub(double second)
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->updateScrub(second, true);
}

void PlaybackControlAdapter::endScrub(double second)
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->endScrub(second, true);
}

void PlaybackControlAdapter::setPlaybackRate(double rate)
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->setPlaybackRate(rate);
}

void PlaybackControlAdapter::nudgePlaybackRate(int direction)
{
    if (legacySurface_ == nullptr) {
        return;
    }
    advanceSequence();
    legacySurface_->nudgePlaybackRate(direction);
}

}  // namespace miacode::runtime
