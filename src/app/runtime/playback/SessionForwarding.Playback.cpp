// Session::-owned playback forwarders and legacy widgets, split out of
// Playback.cpp.
//
// Stage 4.9d-6: PlaybackCoordinator's implementation TUs are being separated
// from the Session assembly so the coordinator can eventually link on its
// own (see the Result Packet for the link-probe evidence). Playback.cpp now
// holds only PlaybackCoordinator::-owned seek/tick/transport logic; this
// file holds the Session::-owned thin forwarders (and the two legacy
// playback-rate-toast stubs still reachable from Widgets call sites) that
// used to share that TU.

#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

#include <QtCore>
#include <QtGui>

bool Session::hasActiveDifficulty() const
{
    return playback_->hasActiveDifficulty();
}

int Session::activeDifficultyId() const
{
    return playback_->activeDifficultyId();
}

QString Session::activeChartText() const
{
    return playback_->activeChartText();
}

miacode::simai::SimaiTimingMetadata Session::currentTimingMetadata() const
{
    return playback_->currentTimingMetadata();
}

double Session::parsedRawFirstSeconds(bool* ok) const
{
    return playback_->parsedRawFirstSeconds(ok);
}

double Session::parsedFirstSeconds(bool* ok) const
{
    return playback_->parsedFirstSeconds(ok);
}

double Session::parsedWholeBpm(bool* ok) const
{
    return playback_->parsedWholeBpm(ok);
}

int Session::parsedClockCount() const
{
    return playback_->parsedClockCount();
}

QString Session::parsedLatencyMeterId() const
{
    return playback_->parsedLatencyMeterId();
}

double Session::currentPreviewAuthoritativeAudioClockSecond() const
{
    return playback_->authoritativeAudioClockSecond();
}

void Session::schedulePreviewSeek(double second, bool centerView)
{
    playback_->schedulePreviewSeek(second, centerView);
}

void Session::seekPreviewDiscreteToSecond(double second, bool centerView)
{
    playback_->seekPreviewDiscreteToSecond(second, centerView);
}

void Session::requestPausedPreviewSeek(
    double second,
    bool centerView,
    bool submitMediaImmediately,
    bool logHotPath)
{
    playback_->requestPausedPreviewSeek(second, centerView, submitMediaImmediately, logHotPath);
}

void Session::applyPausedPreviewVisualSecond(double second, bool centerView)
{
    playback_->applyPausedPreviewVisualSecond(second, centerView);
}

void Session::submitPausedMediaSeek(double second, quint64 generation)
{
    playback_->submitPausedMediaSeek(second, generation);
}

void Session::maybeSubmitLatestPausedMediaSeek()
{
    playback_->maybeSubmitLatestPausedMediaSeek();
}

void Session::handlePausedPreviewMediaSeekCompleted(double second, quint64 generation)
{
    playback_->handlePausedPreviewMediaSeekCompleted(second, generation);
}

bool Session::stepPreviewBySeconds(double deltaSeconds, bool centerView)
{
    return playback_->stepPreviewBySeconds(deltaSeconds, centerView);
}

bool Session::handlePreviewSeekWheel(QWheelEvent* event)
{
    return playback_->handlePreviewSeekWheel(event);
}

void Session::beginPreviewHeldSeek(int direction, int key)
{
    playback_->beginPreviewHeldSeek(direction, key);
}

void Session::stopPreviewHeldSeek(int key)
{
    playback_->stopPreviewHeldSeek(key);
}

void Session::applyPreviewHeldSeekTick()
{
    playback_->applyPreviewHeldSeekTick();
}

void Session::seekPreviewToSecond(double second, bool centerView)
{
    playback_->seekPreviewToSecond(second, centerView);
}

void Session::applyPreviewPlaybackRate(double rate)
{
    playback_->applyPreviewPlaybackRate(rate);
}

void Session::updatePreviewPlaybackRateToastGeometry()
{
}

void Session::hidePreviewPlaybackRateToast()
{
}

bool Session::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    return playback_->startQtPreviewPlayback(second, resumeFromPause);
}

void Session::pauseQtPreviewPlaybackExact()
{
    playback_->pauseQtPreviewPlaybackExact();
}

void Session::stopQtPreviewPlayback(bool keepPosition)
{
    playback_->stopQtPreviewPlayback(keepPosition);
}

void Session::applyQtPreviewPosition(double second, bool centerView)
{
    playback_->applyQtPreviewPosition(second, centerView);
}

void Session::syncPausedPreviewMediaTimestamps(double second)
{
    playback_->syncPausedPreviewMediaTimestamps(second);
}

void Session::flushQtPreviewTimelinePosition()
{
    playback_->flushQtPreviewTimelinePosition();
}

void Session::onTimelineRenderCadenceTick()
{
    playback_->onTimelineRenderCadenceTick();
}

void Session::onTimelineCadenceWatchdogTick()
{
    playback_->onTimelineCadenceWatchdogTick();
}

void Session::onQtPreviewTick()
{
    playback_->onQtPreviewTick();
}

void Session::jumpToNearestTimelineNote(double second, int lane)
{
    playback_->jumpToNearestTimelineNote(second, lane);
}
