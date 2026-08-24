#include <QCoreApplication>
#include <QTextStream>

#include "audio/BassPreviewRetainedState.h"

namespace {

using miacode::preview_audio::RetainedPlaybackMode;
using miacode::preview_audio::bass::RetainedSeekAction;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyPausedTransportReuse(QTextStream& err)
{
    using miacode::preview_audio::bass::canReusePausedTransport;

    if (!require(
            canReusePausedTransport(RetainedPlaybackMode::PausedExact, false, 12.0, 12.0015),
            QStringLiteral("paused exact should reuse paused transport when second matches"),
            err)) {
        return false;
    }
    if (!require(
            canReusePausedTransport(RetainedPlaybackMode::PausedAnchored, false, 12.0, 11.9985),
            QStringLiteral("paused anchored should reuse paused transport when second matches"),
            err)) {
        return false;
    }
    if (!require(
            !canReusePausedTransport(RetainedPlaybackMode::PausedExact, true, 12.0, 12.0),
            QStringLiteral("invalidated paused exact should not reuse paused transport"),
            err)) {
        return false;
    }
    if (!require(
            !canReusePausedTransport(RetainedPlaybackMode::PausedAnchored, false, 12.0, 12.0031),
            QStringLiteral("changed second should not reuse paused transport"),
            err)) {
        return false;
    }
    if (!require(
            !canReusePausedTransport(RetainedPlaybackMode::Invalidated, false, 12.0, 12.0),
            QStringLiteral("invalidated mode should not reuse paused transport"),
            err)) {
        return false;
    }
    return true;
}

bool verifyBackgroundTrackEndBoundary(QTextStream& err)
{
    using miacode::preview_audio::bass::backgroundTrackTargetIsPastEnd;
    return require(!backgroundTrackTargetIsPastEnd(30.0, 60.0),
               QStringLiteral("in-range BGM seek should remain playable"), err)
        && require(backgroundTrackTargetIsPastEnd(60.0, 60.0),
               QStringLiteral("BGM seek at EOF should stay silent"), err)
        && require(backgroundTrackTargetIsPastEnd(90.0, 60.0),
               QStringLiteral("BGM seek beyond EOF should stay silent"), err)
        && require(!backgroundTrackTargetIsPastEnd(-1.0, 60.0),
               QStringLiteral("negative-offset seek is pending-start, not past-end"), err)
        && require(!backgroundTrackTargetIsPastEnd(90.0, 0.0),
               QStringLiteral("unknown BGM duration should not be classified as past-end"), err);
}

bool verifyRetainedSeekActionMatrix(QTextStream& err)
{
    using miacode::preview_audio::bass::retainedSeekAction;

    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedExact, true, true) == RetainedSeekAction::ResumeExact,
            QStringLiteral("same-second paused exact play should resume exactly"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedAnchored, true, true) == RetainedSeekAction::ResumeAnchored,
            QStringLiteral("same-second paused anchored play should resume from anchor"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedExact, true, false) == RetainedSeekAction::KeepPaused,
            QStringLiteral("same-second paused exact seek should stay paused"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedAnchored, true, false) == RetainedSeekAction::KeepPaused,
            QStringLiteral("same-second paused anchored seek should stay paused"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedExact, false, false) == RetainedSeekAction::RepositionPaused,
            QStringLiteral("changed-second paused exact seek should reposition without heavy anchor"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedExact, false, true) == RetainedSeekAction::RepositionAndResume,
            QStringLiteral("changed-second paused exact play should reposition then resume"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedAnchored, false, false) == RetainedSeekAction::RepositionPaused,
            QStringLiteral("changed-second paused anchored seek should reposition without heavy anchor"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::PausedAnchored, false, true) == RetainedSeekAction::RepositionAndResume,
            QStringLiteral("changed-second paused anchored play should reposition then resume"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::Invalidated, true, false) == RetainedSeekAction::AnchorPaused,
            QStringLiteral("invalidated paused seek should re-anchor even if second matches"),
            err)) {
        return false;
    }
    if (!require(
            retainedSeekAction(RetainedPlaybackMode::None, true, true) == RetainedSeekAction::AnchorAndResume,
            QStringLiteral("no retained state should rebuild and resume"),
            err)) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyPausedTransportReuse(err)) {
        return 1;
    }
    if (!verifyRetainedSeekActionMatrix(err)) {
        return 1;
    }
    if (!verifyBackgroundTrackEndBoundary(err)) {
        return 1;
    }

    out << "bass_preview_retained_state_spec ok" << Qt::endl;
    return 0;
}
