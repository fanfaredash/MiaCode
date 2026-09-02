#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

#include "app/v2/EditorSyncController.h"
#include "common/PreviewInteractionConfig.h"

#include <QtCore>

double miacode::runtime::PlaybackCoordinator::touchPadAuthoringAnchoredSecond(double previewSecond) const
{
    if (state_.touchPadAuthoringAnchorSeekSecond_ < 0.0) {
        return previewSecond;
    }
    return qAbs(previewSecond - state_.touchPadAuthoringAnchorSeekSecond_)
            <= miacode::preview_interaction::kTouchPadAuthoringAnchorToleranceSeconds
        ? state_.touchPadAuthoringAnchorTokenSecond_
        : previewSecond;
}

void miacode::runtime::PlaybackCoordinator::setTouchPadAuthoringAnchor(double seekSecond, double tokenSecond)
{
    state_.touchPadAuthoringAnchorSeekSecond_ = seekSecond;
    state_.touchPadAuthoringAnchorTokenSecond_ = tokenSecond;
}

void miacode::runtime::PlaybackCoordinator::updatePreviewFollowDecorationForTimelineBlueLine(
    double second,
    bool ensureVisible,
    qint64* resolveElapsedNs,
    qint64* followOverlayElapsedNs,
    TimelineQuickModel::PreviewFollowSpan* spanOut)
{
    if (resolveElapsedNs != nullptr) {
        *resolveElapsedNs = 0;
    }
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = 0;
    }
    if (spanOut != nullptr) {
        *spanOut = TimelineQuickModel::PreviewFollowSpan();
    }
    if (!hasActiveDifficulty()) {
        session_.clearPreviewFollowDecoration();
        invalidatePreviewFollowBindingCache();
        return;
    }

    const double anchoredSecond = touchPadAuthoringAnchoredSecond(second);
    TimelineQuickModel::PreviewFollowBinding binding;
    bool resolved = cachedPreviewFollowBindingContainsSecond(anchoredSecond);
    if (resolved) {
        binding = state_.previewFollowBindingCache_;
    } else {
        QElapsedTimer timer;
        timer.start();
        resolved = state_.timelineQuickModel_.resolvePreviewFollowBinding(
            qMax(0.0, anchoredSecond), &binding);
        if (resolveElapsedNs != nullptr) {
            *resolveElapsedNs = timer.nsecsElapsed();
        }
        if (resolved) {
            cachePreviewFollowBinding(binding);
        } else {
            invalidatePreviewFollowBindingCache();
        }
    }
    if (!resolved || !binding.resolved) {
        session_.clearPreviewFollowDecoration();
        return;
    }
    if (spanOut != nullptr) {
        *spanOut = binding.span;
    }

    QElapsedTimer timer;
    timer.start();
    miacode::v2::EditorFollowState follow;
    follow.difficultyId = activeDifficultyId();
    // The workspace revision QML last committed — the same identity
    // requestEditorNavigation publishes, and the one the editor compares
    // against. This used to carry the validation snapshot's revision instead,
    // which is timelineRevision_: a different counter that only advances on
    // some of the commits the workspace counts, so a single difficulty switch
    // was enough to put the two permanently out of step and silently kill
    // 代码跟随 for the rest of the session.
    follow.revision = session_.appliedQmlWorkspaceRevision_;
    follow.start = binding.span.startPosition;
    follow.end = binding.span.endPositionExclusive;
    follow.caret = binding.span.cursorPosition;
    follow.active = true;
    follow.reveal = ensureVisible;
    follow.playbackActive = state_.playing_;
    session_.editorSyncController().publishFollow(follow);
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = timer.nsecsElapsed();
    }
}

void miacode::runtime::PlaybackCoordinator::syncEditorCursorToPreviewSecond(
    double second,
    bool centerView,
    bool ensureVisibleWhenPaused)
{
    if (state_.suppressTimelineCursorSync_ || !hasActiveDifficulty()) {
        session_.clearPreviewFollowDecoration();
        if (!hasActiveDifficulty()) {
            invalidatePreviewFollowBindingCache();
        }
        return;
    }

    const bool reveal = state_.previewFollowEnabled_
        && (state_.playing_ ? centerView : ensureVisibleWhenPaused);
    updatePreviewFollowDecorationForTimelineBlueLine(second, reveal);
}
