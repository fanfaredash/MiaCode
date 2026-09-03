#pragma once

namespace miacode::v2 {

// The playback coordinator's second contract, alongside PlaybackControl.
//
// PlaybackControl models USER TRANSPORT COMMANDS — play, pause, seek, scrub,
// rate change: something the person at the keyboard asked for. This contract
// is the opposite half: writes to the same shared playback state that are
// NOT commands, because nothing asked for them — something else about the
// world changed and the state has to catch up. Three kinds of writer need
// this seam:
//
//   1. restorePlaybackRate — the portable-state JSON is being read back (or
//      reset to defaults) and the persisted rate has to land in state before
//      the preview backend even exists yet. This deliberately does NOT reuse
//      PlaybackControl::setPlaybackRate(): PlaybackCoordinator::
//      applyPreviewPlaybackRate (the command's implementation) opens by
//      calling preview_.ensurePreviewStageMediaRouteInitialized(), which
//      would drag that initialization ahead of where SessionBootstrapFinalize
//      schedules it (after loadPortableState() returns, behind its own gate);
//      and it unconditionally calls preferences_.savePortableState() near its
//      end, which would write the JSON back to disk in the middle of reading
//      the same file (save-during-load reentrancy). A restore is a plain
//      field write with none of that.
//   2. reanchorObservedSecond — the media backend reports "I am actually
//      paused at second X", correcting the wall-clock anchor the coordinator
//      extrapolates from. This is a clock-source correction, not a seek: it
//      never touches the transport state.
//   3. repositionSilently — the paused playhead second is relocated (a page
//      switch, an export/latency audition install) with no seek side effect:
//      no wall-clock re-anchor, no transport-state change, just the number.
//
// Implemented by PlaybackCoordinator, same as PlaybackControl. A future
// caller reaches it through ApplicationServices::playbackStateAuthoritySlot()
// exactly the way PlaybackControl is reached through playbackControlSlot().
class PlaybackStateAuthority
{
public:
    virtual ~PlaybackStateAuthority() = default;

    virtual void restorePlaybackRate(double rate) = 0;
    virtual void reanchorObservedSecond(double second) = 0;
    // `reason` names the caller in the backward-move log. That log exists because a
    // real "timeline steps backward after a pause" report was undiagnosable when
    // nothing recorded who moved the playhead — two guesses from reading code were
    // wrong. Collapsing every caller into one label would give back half of that,
    // so the reason travels with the call rather than being fixed here.
    virtual void repositionSilently(double second, const char* reason) = 0;
};

}  // namespace miacode::v2
