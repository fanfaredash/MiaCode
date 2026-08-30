#include "MainWindow.TimelineSection.h"

#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "tools/latency/LatencySandboxController.h"
#include "tools/video_export/VideoExportController.h"  // IntroBannerSpec (export-page intro lead-in)

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace {

void appendPreviewInteractionLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/interaction"),
        text
    );
}

}  // namespace

void MainWindow::setAuditionSceneReady(std::function<bool()> stillCurrent,
                                       std::function<void()> reinstall)
{
    state_.auditionSceneStillCurrent_ = std::move(stillCurrent);
    state_.auditionSceneReinstall_ = std::move(reinstall);
    state_.auditionSceneReady_ = true;
}

void MainWindow::clearAuditionSceneReady()
{
    state_.auditionSceneReady_ = false;
    state_.auditionSceneStillCurrent_ = {};
    state_.auditionSceneReinstall_ = {};
}

bool MainWindow::ensureAuditionSceneReady()
{
    const bool current = state_.auditionSceneReady_
        && (!state_.auditionSceneStillCurrent_ || state_.auditionSceneStillCurrent_());
    if (current) {
        return true;
    }
    if (!state_.auditionSceneReinstall_) {
        return false;
    }
    // Rebuilding is the recovery, and it is the only one available here: the
    // ordinary path's two fallbacks — the slow refresh it requests, and the
    // deferred replay its caller records — are both gated on
    // hasActiveDifficulty(), which is false on the export page by design.
    // Take the callback out first so a reinstall that fails cannot be retried
    // forever from inside itself.
    const std::function<void()> reinstall = state_.auditionSceneReinstall_;
    clearAuditionSceneReady();
    reinstall();
    return state_.auditionSceneReady_;
}

bool MainWindow::TimelineSection::preparePreviewStartState()
{
    // Latency sandbox + export-page audition: the controller/section populated
    // the preview state synchronously (installSandboxScene /
    // installExportPreviewAuditionScene), so skip the editor-dirty / difficulty
    // checks and the slow-refresh round trip. This lets the audition reuse the
    // exact same playback transport as a difficulty even though
    // activeDifficultyId_ == 0 on the export page (D4).
    //
    // Readiness is the scene's own, not the edited difficulty's snapshot: see
    // auditionSceneReady_ for why borrowing that pair wedged this page.
    if (state_.latencySandboxAuditionActive_ || state_.exportPreviewAuditionActive_) {
        return owner_.ensureAuditionSceneReady();
    }

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
    MC_OP("MainWindow::TimelineSection::onStopPreview");
    // Stop the export-page intro animation if it's mid-play (it clears the
    // overlay and leaves the chart paused at 0).
    if (state_.exportIntroLeadInActive_) {
        cancelExportIntroLeadIn();
    }
    // The latency page now reuses this exact transport (its synthesized test
    // chart is the preview source), so no special-casing is needed here.
    const quint64 opId = ++state_.previewInteractionSequence_;
    const double returnSecond = qBound(0.0, state_.qtPreviewPlaybackReturnSecond_, previewDurationSeconds());
    const bool wasActive = state_.qtPreviewPlaying_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_;
    appendPreviewInteractionLog(
        QStringLiteral("stop_request"),
        QString("op=%1 source=stop_action was_active=%2 return_second=%3 current_second=%4")
            .arg(opId)
            .arg(wasActive ? 1 : 0)
            .arg(returnSecond, 0, 'f', 6)
            .arg(owner_.currentPreviewAuthoritativeAudioClockSecond(), 0, 'f', 6));
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.previewPendingPlayInteractionId_ = 0;
    state_.previewPendingPlayInteractionSource_.clear();
    seekPreviewDiscreteToSecond(returnSecond, true);
    appendPreviewInteractionLog(
        QStringLiteral("stop_complete"),
        QString("op=%1 source=stop_action final_second=%2")
            .arg(opId)
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6));
    owner_.statusBar()->showMessage("Qt preview stopped.");
}

void MainWindow::TimelineSection::onTogglePreviewPause()
{
    MC_OP("MainWindow::TimelineSection::onTogglePreviewPause");
    if (state_.exportIntroLeadInActive_) {
        if (exportIntroEnabled()) {
            // The intro is advancing -> pause it, keeping the static frame on screen.
            pauseExportIntroAdvance();
            return;
        }
        // Stale flag (添加片头 turned off / range left full-range while the intro
        // was advancing): heal by tearing the region down, then fall through to
        // normal chart play so the toggle is never a silent no-op.
        exitExportIntroRegion();
    }
    if (state_.exportIntroRegionActive_) {
        if (exportIntroEnabled()) {
            // Paused/scrubbed inside the intro region -> play (advance from here).
            startExportIntroAdvance(state_.exportIntroPlayheadSeconds_);
            return;
        }
        // Stale region (intro no longer enabled): heal and fall through to play.
        exitExportIntroRegion();
    }
    const quint64 opId = ++state_.previewInteractionSequence_;
    appendPreviewInteractionLog(
        QStringLiteral("toggle_entry"),
        QString("op=%1 playing=%2 startup_pending=%3 device_change_seq=%4 pause_second=%5 "
                "authoritative_second=%6 pending_play_op=%7")
            .arg(opId)
            .arg(state_.qtPreviewPlaying_ ? 1 : 0)
            .arg(state_.previewStartupSyncPending_ ? 1 : 0)
            .arg(state_.previewAudioDeviceChangeSequence_)
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6)
            .arg(owner_.currentPreviewAuthoritativeAudioClockSecond(), 0, 'f', 6)
            .arg(state_.previewPendingPlayInteractionId_));
    if (state_.qtPreviewPlaying_) {
        appendPreviewInteractionLog(
            QStringLiteral("pause_request"),
            QString("op=%1 source=toggle_action current_second=%2")
                .arg(opId)
                .arg(owner_.currentPreviewAuthoritativeAudioClockSecond(), 0, 'f', 6));
        pauseQtPreviewPlaybackExact();
        appendPreviewInteractionLog(
            QStringLiteral("pause_complete"),
            QString("op=%1 source=toggle_action paused_second=%2")
                .arg(opId)
                .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6));
        owner_.updatePauseButtonAppearance();
        owner_.statusBar()->showMessage(
            QString("Qt preview paused at %1s.").arg(state_.qtPreviewPauseSecond_, 0, 'f', 2)
        );
        return;
    }

    if (!hasPreviewableChart()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    state_.previewPendingPlayInteractionId_ = opId;
    state_.previewPendingPlayInteractionSource_ = QStringLiteral("toggle_action");
    appendPreviewInteractionLog(
        QStringLiteral("play_request"),
        QString("op=%1 source=toggle_action requested_second=%2 device_change_seq=%3")
            .arg(opId)
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6)
            .arg(state_.previewAudioDeviceChangeSequence_));
    if (!startQtPreviewPlayback(state_.qtPreviewPauseSecond_, true)) {
        appendPreviewInteractionLog(
            QStringLiteral("play_deferred"),
            QString("op=%1 source=toggle_action requested_second=%2")
                .arg(opId)
                .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6));
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

void MainWindow::cancelExportIntroLeadIn()
{
    timelineSection_->cancelExportIntroLeadIn();
}

bool MainWindow::exportIntroLeadInPlaying() const
{
    return timelineSection_->exportIntroLeadInPlaying();
}

bool MainWindow::handleExportIntroSliderSeek(double second)
{
    return timelineSection_->handleExportIntroSliderSeek(second);
}

double MainWindow::exportIntroLowerBoundSeconds() const
{
    return timelineSection_->exportIntroLowerBoundSeconds();
}

void MainWindow::refreshExportIntroState()
{
    timelineSection_->refreshExportIntroState();
}

void MainWindow::setExportAuditionClockSchedule(int clockCount, double clockBpm)
{
    timelineSection_->setExportAuditionClockSchedule(clockCount, clockBpm);
}

void MainWindow::clearExportAuditionClockSchedule()
{
    timelineSection_->clearExportAuditionClockSchedule();
}
