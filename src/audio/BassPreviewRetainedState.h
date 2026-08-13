#pragma once

#include <QtGlobal>

#include "PreviewAudioBackend.h"

namespace miacode::preview_audio::bass {

constexpr double kRetainedSecondToleranceSeconds = 0.002;

inline bool backgroundTrackTargetIsPastEnd(double rawSecond, double durationSeconds)
{
    return qIsFinite(rawSecond)
        && qIsFinite(durationSeconds)
        && durationSeconds > 0.0
        && rawSecond >= durationSeconds;
}

enum class RetainedSeekAction {
    KeepPaused,
    ResumeExact,
    ResumeAnchored,
    RepositionPaused,
    RepositionAndResume,
    AnchorPaused,
    AnchorAndResume,
};

inline bool retainedSecondsMatch(
    double retainedSecond,
    double targetSecond,
    double toleranceSeconds = kRetainedSecondToleranceSeconds)
{
    return qAbs(retainedSecond - targetSecond) <= toleranceSeconds;
}

inline bool canReusePausedTransport(
    RetainedPlaybackMode mode,
    bool transportInvalidated,
    double retainedSecond,
    double targetSecond)
{
    if (transportInvalidated || !retainedSecondsMatch(retainedSecond, targetSecond)) {
        return false;
    }
    return mode == RetainedPlaybackMode::PausedExact
        || mode == RetainedPlaybackMode::PausedAnchored;
}

inline RetainedSeekAction retainedSeekAction(
    RetainedPlaybackMode mode,
    bool sameSecond,
    bool continuePlaying)
{
    if (sameSecond) {
        if (mode == RetainedPlaybackMode::PausedExact) {
            return continuePlaying ? RetainedSeekAction::ResumeExact : RetainedSeekAction::KeepPaused;
        }
        if (mode == RetainedPlaybackMode::PausedAnchored) {
            return continuePlaying ? RetainedSeekAction::ResumeAnchored : RetainedSeekAction::KeepPaused;
        }
    }
    if (mode == RetainedPlaybackMode::PausedExact || mode == RetainedPlaybackMode::PausedAnchored) {
        return continuePlaying ? RetainedSeekAction::RepositionAndResume : RetainedSeekAction::RepositionPaused;
    }
    return continuePlaying ? RetainedSeekAction::AnchorAndResume : RetainedSeekAction::AnchorPaused;
}

}  // namespace miacode::preview_audio::bass
