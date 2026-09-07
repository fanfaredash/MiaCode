// Session::-owned audition-scene registration and export-intro forwarders,
// split out of PlaybackGlue.cpp.
//
// Stage 4.9d-6: PlaybackCoordinator's implementation TUs are being separated
// from the Session assembly so the coordinator can eventually link on its
// own (see the Result Packet for the link-probe evidence). PlaybackGlue.cpp
// now holds only PlaybackCoordinator::-owned playback-start/stop/toggle
// logic; this file holds the Session::-owned methods that used to share
// that TU.

#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

void Session::setAuditionSceneReady(std::function<bool()> stillCurrent,
                                       std::function<void()> reinstall)
{
    state_.auditionSceneStillCurrent_ = std::move(stillCurrent);
    state_.auditionSceneReinstall_ = std::move(reinstall);
    state_.auditionSceneReady_ = true;
}

void Session::clearAuditionSceneReady()
{
    state_.auditionSceneReady_ = false;
    state_.auditionSceneStillCurrent_ = {};
    state_.auditionSceneReinstall_ = {};
}

void Session::onStopPreview()
{
    playback_->onStopPreview();
}

void Session::onTogglePreviewPause()
{
    playback_->onTogglePreviewPause();
}

void Session::cancelExportIntroLeadIn()
{
    playback_->cancelExportIntroLeadIn();
}

bool Session::exportIntroLeadInPlaying() const
{
    return playback_->exportIntroLeadInPlaying();
}

bool Session::handleExportIntroSliderSeek(double second)
{
    return playback_->handleExportIntroSliderSeek(second);
}

double Session::exportIntroLowerBoundSeconds() const
{
    return playback_->exportIntroLowerBoundSeconds();
}

void Session::refreshExportIntroState()
{
    playback_->refreshExportIntroState();
}

void Session::setExportAuditionClockSchedule(int clockCount, double clockBpm)
{
    playback_->setExportAuditionClockSchedule(clockCount, clockBpm);
}

void Session::clearExportAuditionClockSchedule()
{
    playback_->clearExportAuditionClockSchedule();
}
