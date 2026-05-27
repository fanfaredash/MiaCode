#include "MainWindow.TimelineSection.h"

#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "tools/latency/LatencySandboxController.h"

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
    MC_OP("MainWindow::TimelineSection::onStopPreview");
    // Latency-page sandbox takes over the stop/space transport when
    // the user is on its outline entry, so the existing player keys
    // double as the audition controls (per design — no new shortcut).
    if (state_.activeOutlineKey_ == QLatin1String("latency")) {
        if (auto* sandbox = owner_.latencySandboxController(); sandbox != nullptr) {
            sandbox->stopAudition();
        }
        return;
    }
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
    if (state_.activeOutlineKey_ == QLatin1String("latency")) {
        if (auto* sandbox = owner_.latencySandboxController(); sandbox != nullptr) {
            sandbox->toggleAudition();
        }
        return;
    }
    if (state_.qtPreviewPlaying_) {
        const quint64 opId = ++state_.previewInteractionSequence_;
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

    if (!hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    const quint64 opId = ++state_.previewInteractionSequence_;
    state_.previewPendingPlayInteractionId_ = opId;
    state_.previewPendingPlayInteractionSource_ = QStringLiteral("toggle_action");
    appendPreviewInteractionLog(
        QStringLiteral("play_request"),
        QString("op=%1 source=toggle_action requested_second=%2")
            .arg(opId)
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6));
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
