#include "EditorSyncController.h"

#include <QMetaObject>

namespace miacode::v2 {

EditorSyncController::EditorSyncController(QObject* parent)
    : QObject(parent)
{
}

bool EditorSyncController::followActive() const { return follow_.active; }
int EditorSyncController::followDifficultyId() const { return follow_.difficultyId; }
qulonglong EditorSyncController::followRevision() const { return follow_.revision; }
int EditorSyncController::followStart() const { return follow_.start; }
int EditorSyncController::followEnd() const { return follow_.end; }
int EditorSyncController::followCaret() const { return follow_.caret; }
bool EditorSyncController::followReveal() const { return follow_.reveal; }
bool EditorSyncController::followPlaybackActive() const { return follow_.playbackActive; }

void EditorSyncController::publishFollow(const EditorFollowState& state)
{
    if (follow_.difficultyId == state.difficultyId
        && follow_.revision == state.revision
        && follow_.start == state.start
        && follow_.end == state.end
        && follow_.caret == state.caret
        && follow_.active == state.active
        && follow_.reveal == state.reveal
        && follow_.playbackActive == state.playbackActive) {
        return;
    }
    follow_ = state;
    scheduleFollowDelivery();
}

void EditorSyncController::setPlaybackActive(bool active)
{
    if (follow_.playbackActive == active) {
        return;
    }
    follow_.playbackActive = active;
    scheduleFollowDelivery();
}

qulonglong EditorSyncController::requestNavigation(
    int difficultyId, qulonglong revision, int start, int end, bool focus, bool reveal)
{
    if (!readinessAccepts(difficultyId, revision) || start < 0 || end < start) {
        return 0;
    }
    caretPending_ = false;
    deliveredCaretValid_ = false;
    if (navigationPending_) {
        scheduleNavigationFinished(pendingNavigation_.sequence, false);
    }
    pendingNavigation_ = {
        ++nextNavigationSequence_, difficultyId, revision, start, end, focus, reveal};
    navigationPending_ = true;
    scheduleNavigationDelivery();
    return pendingNavigation_.sequence;
}

bool EditorSyncController::requestTouchPadAuthoring(
    const QString& pad, bool useBacktickSeparator)
{
    if (!editorContextActive() || pad.trimmed().isEmpty()) {
        return false;
    }
    pendingTouchPadRequests_.enqueue({
        pad,
        useBacktickSeparator,
        contextDifficultyId_,
        contextRevision_,
        contextAnchor_,
        contextPosition_});
    scheduleTouchPadDelivery();
    return true;
}

bool EditorSyncController::editorContextActive() const
{
    return editorVisible_ && !metadataMode_ && editorFocused_ && !imeComposing_
        && contextDifficultyId_ > 0
        && contextDifficultyId_ == readyDifficultyId_
        && contextRevision_ == readyRevision_;
}

void EditorSyncController::setEditorReadiness(
    int difficultyId, qulonglong revision, bool visible, bool metadataMode)
{
    const bool changed = readyDifficultyId_ != difficultyId
        || readyRevision_ != revision
        || editorVisible_ != visible
        || metadataMode_ != metadataMode;
    readyDifficultyId_ = difficultyId;
    readyRevision_ = revision;
    editorVisible_ = visible;
    metadataMode_ = metadataMode;
    if (!editorVisible_ || metadataMode_) {
        if (navigationPending_) {
            scheduleNavigationFinished(pendingNavigation_.sequence, false);
        }
        navigationPending_ = false;
        pendingTouchPadRequests_.clear();
        caretPending_ = false;
        pointerInteractionPending_ = false;
        touchPadPreviewAnchorPending_ = false;
        previewSeekPending_ = false;
        pendingTouchPadControlHold_ = false;
        scheduleTouchPadControlHoldDelivery();
    }
    if (changed) {
        deliveredCaretValid_ = false;
    }
    if (changed && navigationPending_) {
        const quint64 sequence = pendingNavigation_.sequence;
        navigationPending_ = false;
        scheduleNavigationFinished(sequence, false);
    }
    if (changed && deliveredNavigationSequence_ != 0) {
        const quint64 sequence = deliveredNavigationSequence_;
        deliveredNavigationSequence_ = 0;
        scheduleNavigationFinished(sequence, false);
    }
    if (changed) {
        scheduleEditorContextDelivery();
        if (editorVisible_ && !metadataMode_) {
            scheduleFollowDelivery();
        }
    }
}

void EditorSyncController::setEditorContext(
    int difficultyId, qulonglong revision, int anchor, int position,
    bool focused, bool imeComposing, int line, int column, bool publishCaret)
{
    const bool activityChanged = contextDifficultyId_ != difficultyId
        || contextRevision_ != revision
        || editorFocused_ != focused
        || imeComposing_ != imeComposing;
    const bool selectionChanged = contextAnchor_ != anchor
        || contextPosition_ != position;
    contextDifficultyId_ = difficultyId;
    contextRevision_ = revision;
    contextAnchor_ = qMax(0, anchor);
    contextPosition_ = qMax(0, position);
    editorFocused_ = focused;
    imeComposing_ = imeComposing;
    if (activityChanged) {
        scheduleEditorContextDelivery();
    }
    if ((activityChanged || selectionChanged) && !publishCaret) {
        caretPending_ = false;
        deliveredCaretValid_ = false;
    }
    if (!editorFocused_ || imeComposing_) {
        pendingTouchPadControlHold_ = false;
        scheduleTouchPadControlHoldDelivery();
    }
    if (publishCaret && !imeComposing_ && readinessAccepts(difficultyId, revision)) {
        pendingCaret_ = {difficultyId, revision, qMax(1, line), qMax(1, column)};
        caretPending_ = true;
        scheduleCaretDelivery();
    }
}

void EditorSyncController::acknowledgeNavigation(qulonglong sequence, bool applied)
{
    if (sequence == 0 || sequence != deliveredNavigationSequence_) {
        return;
    }
    deliveredNavigationSequence_ = 0;
    scheduleNavigationFinished(sequence, applied);
    if (navigationPending_) {
        scheduleNavigationDelivery();
    }
}

void EditorSyncController::setTouchPadControlHold(bool active)
{
    pendingTouchPadControlHold_ = active;
    scheduleTouchPadControlHoldDelivery();
}

bool EditorSyncController::beginPointerInteraction(int difficultyId, qulonglong revision)
{
    if (!readinessAccepts(difficultyId, revision)) {
        return false;
    }
    pendingPointerDifficultyId_ = difficultyId;
    pendingPointerRevision_ = revision;
    pointerInteractionPending_ = true;
    schedulePointerInteractionDelivery();
    return true;
}

bool EditorSyncController::setTouchPadPreviewAnchor(
    int difficultyId, qulonglong revision, const QString& text, int tokenStart)
{
    if (!readinessAccepts(difficultyId, revision)) {
        return false;
    }
    const int position = qBound(0, tokenStart, text.size());
    const int newline = text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1));
    const int line = text.left(position).count(QLatin1Char('\n')) + 1;
    const int column = position - newline;
    pendingTouchPadPreviewAnchor_ = {difficultyId, revision, line, column};
    touchPadPreviewAnchorPending_ = true;
    scheduleTouchPadPreviewAnchorDelivery();
    return true;
}

bool EditorSyncController::seekPreviewToEditorLocation(
    int difficultyId, qulonglong revision, int line, int column)
{
    if (!readinessAccepts(difficultyId, revision)) {
        return false;
    }
    pendingPreviewSeek_ = {difficultyId, revision, qMax(1, line), qMax(1, column)};
    previewSeekPending_ = true;
    schedulePreviewSeekDelivery();
    return true;
}

bool EditorSyncController::readinessAccepts(int difficultyId, quint64 revision) const
{
    return editorVisible_ && !metadataMode_ && difficultyId > 0
        && difficultyId == readyDifficultyId_ && revision == readyRevision_;
}

void EditorSyncController::scheduleFollowDelivery()
{
    if (followDeliveryQueued_) {
        return;
    }
    followDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        followDeliveryQueued_ = false;
        emit followChanged();
    }, Qt::QueuedConnection);
}

void EditorSyncController::scheduleEditorContextDelivery()
{
    if (editorContextDeliveryQueued_) {
        return;
    }
    editorContextDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        editorContextDeliveryQueued_ = false;
        emit editorContextChanged();
    }, Qt::QueuedConnection);
}

void EditorSyncController::scheduleCaretDelivery()
{
    if (caretDeliveryQueued_) {
        return;
    }
    caretDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        caretDeliveryQueued_ = false;
        if (!caretPending_) {
            return;
        }
        const EditorLocationState request = pendingCaret_;
        caretPending_ = false;
        if (!readinessAccepts(request.difficultyId, request.revision)) {
            return;
        }
        if (deliveredCaretValid_
            && deliveredCaret_.difficultyId == request.difficultyId
            && deliveredCaret_.revision == request.revision
            && deliveredCaret_.line == request.line
            && deliveredCaret_.column == request.column) {
            return;
        }
        deliveredCaret_ = request;
        deliveredCaretValid_ = true;
        emit caretLocationPublished(
            request.difficultyId, request.revision, request.line, request.column);
        if (caretPending_) {
            scheduleCaretDelivery();
        }
    }, Qt::QueuedConnection);
}

void EditorSyncController::schedulePointerInteractionDelivery()
{
    if (pointerInteractionDeliveryQueued_) {
        return;
    }
    pointerInteractionDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        pointerInteractionDeliveryQueued_ = false;
        if (!pointerInteractionPending_) {
            return;
        }
        const int difficultyId = pendingPointerDifficultyId_;
        const quint64 revision = pendingPointerRevision_;
        pointerInteractionPending_ = false;
        if (!readinessAccepts(difficultyId, revision)) {
            return;
        }
        emit pointerInteractionStarted(difficultyId);
    }, Qt::QueuedConnection);
}

void EditorSyncController::scheduleTouchPadControlHoldDelivery()
{
    if (touchPadControlHoldDeliveryQueued_) {
        return;
    }
    touchPadControlHoldDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        touchPadControlHoldDeliveryQueued_ = false;
        const bool active = pendingTouchPadControlHold_ && editorContextActive();
        if (active == deliveredTouchPadControlHold_) {
            return;
        }
        deliveredTouchPadControlHold_ = active;
        emit touchPadControlHoldChanged(active);
    }, Qt::QueuedConnection);
}

void EditorSyncController::scheduleTouchPadPreviewAnchorDelivery()
{
    if (touchPadPreviewAnchorDeliveryQueued_) {
        return;
    }
    touchPadPreviewAnchorDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        touchPadPreviewAnchorDeliveryQueued_ = false;
        if (!touchPadPreviewAnchorPending_) {
            return;
        }
        const EditorLocationState request = pendingTouchPadPreviewAnchor_;
        touchPadPreviewAnchorPending_ = false;
        if (!readinessAccepts(request.difficultyId, request.revision)) {
            return;
        }
        emit touchPadPreviewAnchorPublished(
            request.difficultyId, request.line, request.column);
    }, Qt::QueuedConnection);
}

void EditorSyncController::schedulePreviewSeekDelivery()
{
    if (previewSeekDeliveryQueued_) {
        return;
    }
    previewSeekDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        previewSeekDeliveryQueued_ = false;
        if (!previewSeekPending_) {
            return;
        }
        const EditorLocationState request = pendingPreviewSeek_;
        previewSeekPending_ = false;
        if (!readinessAccepts(request.difficultyId, request.revision)) {
            return;
        }
        emit previewSeekPublished(request.difficultyId, request.line, request.column);
    }, Qt::QueuedConnection);
}

void EditorSyncController::scheduleNavigationDelivery()
{
    if (navigationDeliveryQueued_ || deliveredNavigationSequence_ != 0) {
        return;
    }
    navigationDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        navigationDeliveryQueued_ = false;
        if (!navigationPending_ || deliveredNavigationSequence_ != 0) {
            return;
        }
        const NavigationState request = pendingNavigation_;
        navigationPending_ = false;
        if (!readinessAccepts(request.difficultyId, request.revision)) {
            scheduleNavigationFinished(request.sequence, false);
            return;
        }
        deliveredNavigationSequence_ = request.sequence;
        emit navigationRequested(
            request.sequence, request.difficultyId, request.revision,
            request.start, request.end, request.focus, request.reveal);
    }, Qt::QueuedConnection);
}

void EditorSyncController::scheduleNavigationFinished(quint64 sequence, bool applied)
{
    QMetaObject::invokeMethod(this, [this, sequence, applied] {
        emit navigationFinished(sequence, applied);
    }, Qt::QueuedConnection);
}

void EditorSyncController::scheduleTouchPadDelivery()
{
    if (touchPadDeliveryQueued_ || pendingTouchPadRequests_.isEmpty()) {
        return;
    }
    touchPadDeliveryQueued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        touchPadDeliveryQueued_ = false;
        const TouchPadRequest request = pendingTouchPadRequests_.dequeue();
        if (readinessAccepts(request.difficultyId, request.revision)
            && editorFocused_ && !imeComposing_) {
            emit touchPadAuthoringRequested(
                request.pad, request.useBacktickSeparator,
                request.difficultyId, request.revision,
                request.anchor, request.position);
        }
        scheduleTouchPadDelivery();
    }, Qt::QueuedConnection);
}

} // namespace miacode::v2
