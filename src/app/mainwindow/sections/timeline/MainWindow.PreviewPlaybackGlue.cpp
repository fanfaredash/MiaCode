#include "MainWindow.TimelineSection.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

bool MainWindow::TimelineSection::preparePreviewStartState()
{
    const bool chartFieldVisible = ui_.editorStack_ != nullptr && ui_.editorStack_->currentWidget() == ui_.chartPage_;
    if (state_.currentFieldDirty_ && !chartFieldVisible && !owner_.applyCurrentFieldToDocument()) {
        return false;
    }

    if (!hasActiveDifficulty()) {
        return false;
    }

    if (state_.latestTimelinePreviewSnapshotReady_ && state_.latestTimelinePreviewRevision_ == state_.timelineRevision_) {
        return true;
    }

    requestTimelineSlowRefresh();
    return false;
}

void MainWindow::TimelineSection::onStopPreview()
{
    const double returnSecond = qBound(0.0, state_.qtPreviewPlaybackReturnSecond_, previewDurationSeconds());
    const bool wasActive = state_.qtPreviewPlaying_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_;
    if (state_.qtPreviewPlaying_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        anchorQtPreviewPlaybackToSecond(returnSecond, true);
    }
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    if (!wasActive) {
        anchorQtPreviewPlaybackToSecond(returnSecond, true);
    }
    owner_.statusBar()->showMessage("Qt preview stopped.");
}

void MainWindow::TimelineSection::onTogglePreviewPause()
{
    if (state_.qtPreviewPlaying_) {
        pauseQtPreviewPlaybackExact();
        owner_.updatePauseButtonAppearance();
        owner_.statusBar()->showMessage(
            QString("Qt preview paused at %1s.").arg(state_.qtPreviewPauseSecond_, 0, 'f', 2)
        );
        return;
    }

    if (!hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!startQtPreviewPlayback(state_.qtPreviewPauseSecond_, true)) {
        return;
    }
    owner_.updatePauseButtonAppearance();
    if (state_.previewStartupSyncPending_) {
        owner_.statusBar()->showMessage(
            QString("Qt preview starting at %1s.").arg(state_.qtPreviewPauseSecond_, 0, 'f', 2)
        );
    } else {
        owner_.statusBar()->showMessage(
            QString("Qt preview resumed at %1s.").arg(state_.qtPreviewPauseSecond_, 0, 'f', 2)
        );
    }
}

bool MainWindow::preparePreviewStartState()
{
    return timelineSection_->preparePreviewStartState();
}

void MainWindow::onStopPreview()
{
    timelineSection_->onStopPreview();
}

void MainWindow::onTogglePreviewPause()
{
    timelineSection_->onTogglePreviewPause();
}
