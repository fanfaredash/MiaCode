#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Shared.h"

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

// ---- miacode::v2::PlaybackStateAuthority ----
//
// See PlaybackStateAuthority.h for what separates these from the
// PlaybackControl command overrides above: none of the three below are a
// response to a user transport command, so none of them may carry that
// path's side effects (stage-media initialization, a portable-state save, a
// wall-clock re-anchor while playing, or a transport-state change).

// Moved from EditorHost::resetPortablePreviewSettingsToDefaults /
// applyPortablePreviewSettings (EditorDisplay.cpp), which used to assign
// state_.previewPlaybackRate_ directly. The floor matches
// applyPreviewPlaybackRate's clamp (Playback.cpp) so both entry points honor
// the same lower bound; everything else that command does —
// ensurePreviewStageMediaRouteInitialized(), the toast, the live BASS tempo
// push, savePortableState() — stays out, because there is no preview backend
// yet when a portable-state load calls this.
void PlaybackCoordinator::restorePlaybackRate(double rate)
{
    state_.previewPlaybackRate_ = qMax(0.25, rate);
}

// Moved from the previewStageMediaHost_ playbackPositionChanged handler
// (StageMediaRoute.cpp): the media backend reporting where it actually is
// while paused. Keeps that handler's guard verbatim — the handshake with a
// paused seek already in flight, and the "playback owns the clock while
// playing" rule — so any future caller of this port gets the same protection
// without repeating it. The debug-log diagnostics that guard produces stay at
// the call site; they are StageMediaHost's own instrumentation, not part of
// the write.
void PlaybackCoordinator::reanchorObservedSecond(double second)
{
    if (state_.playing_ || state_.pausedSeekMediaPending_) {
        return;
    }
    state_.qtPreviewStartSecond_ = second;
    state_.qtPreviewElapsed_.restart();
}

// Moved from the five non-command writePreviewPauseSecond call sites
// (DocumentSessionHost's page switches, VideoExportHost's export-audition
// install, LatencySandboxController's playhead re-anchor) — a silent
// relocation of the paused second with none of the surrounding UI push those
// call sites still do themselves (slider range/value, timeline bridge,
// scene). Still routes through the single writer so the backward-move log
// keeps working; the per-call-site reason string collapses to one, since the
// port carries no reason parameter.
void PlaybackCoordinator::repositionSilently(double second, const char* reason)
{
    miacode::runtime::shared::writePreviewPauseSecond(
        state_.pauseSecond_, second, state_.playing_, reason);
}

}  // namespace miacode::runtime
