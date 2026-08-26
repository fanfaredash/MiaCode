#include "MainWindow.TimelineSection.h"

#include "app/v2/EditorSyncController.h"
#include "common/PreviewInteractionConfig.h"

#include <QtCore>

double MainWindow::TimelineSection::touchPadAuthoringAnchoredSecond(double previewSecond) const
{
    if (state_.touchPadAuthoringAnchorSeekSecond_ < 0.0) {
        return previewSecond;
    }
    return qAbs(previewSecond - state_.touchPadAuthoringAnchorSeekSecond_)
            <= miacode::preview_interaction::kTouchPadAuthoringAnchorToleranceSeconds
        ? state_.touchPadAuthoringAnchorTokenSecond_
        : previewSecond;
}

void MainWindow::TimelineSection::setTouchPadAuthoringAnchor(double seekSecond, double tokenSecond)
{
    state_.touchPadAuthoringAnchorSeekSecond_ = seekSecond;
    state_.touchPadAuthoringAnchorTokenSecond_ = tokenSecond;
}

void MainWindow::TimelineSection::updatePreviewFollowDecorationForTimelineBlueLine(
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
        owner_.clearPreviewFollowDecoration();
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
        owner_.clearPreviewFollowDecoration();
        return;
    }
    if (spanOut != nullptr) {
        *spanOut = binding.span;
    }

    QElapsedTimer timer;
    timer.start();
    const DocumentValidationSnapshot snapshot = owner_.documentValidationSnapshot();
    miacode::v2::EditorFollowState follow;
    follow.difficultyId = owner_.activeDifficultyId();
    follow.revision = snapshot.revision;
    follow.start = binding.span.startPosition;
    follow.end = binding.span.endPositionExclusive;
    follow.caret = binding.span.cursorPosition;
    follow.active = true;
    follow.reveal = ensureVisible;
    follow.playbackActive = state_.qtPreviewPlaying_;
    owner_.editorSyncController().publishFollow(follow);
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = timer.nsecsElapsed();
    }
}

void MainWindow::TimelineSection::syncEditorCursorToPreviewSecond(
    double second,
    bool centerView,
    bool ensureVisibleWhenPaused)
{
    if (state_.suppressTimelineCursorSync_ || !hasActiveDifficulty()) {
        owner_.clearPreviewFollowDecoration();
        if (!hasActiveDifficulty()) {
            invalidatePreviewFollowBindingCache();
        }
        return;
    }

    const bool reveal = state_.previewFollowEnabled_
        && (state_.qtPreviewPlaying_ ? centerView : ensureVisibleWhenPaused);
    updatePreviewFollowDecorationForTimelineBlueLine(second, reveal);
}
