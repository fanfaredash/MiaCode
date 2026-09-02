#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

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

void Session::setAuditionSceneReady(std::function<bool()> stillCurrent,
                                       std::function<void()> reinstall)
{
    state_.auditionSceneStillCurrent_ = std::move(stillCurrent);
    state_.auditionSceneReinstall_ = std::move(reinstall);
    state_.auditionSceneReady_ = true;
}

void Session::clearAuditionSceneReady()
{
    state_.auditionSceneReady_ = false;
    state_.auditionSceneStillCurrent_ = {};
    state_.auditionSceneReinstall_ = {};
}

bool Session::ensureAuditionSceneReady()
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

bool miacode::runtime::PlaybackCoordinator::preparePreviewStartState()
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
        return session_.ensureAuditionSceneReady();
    }

    const bool chartFieldVisible = state_.activeOutlineKey_ == QLatin1String("chart");
    if (state_.currentFieldDirty_ && !chartFieldVisible && !session_.applyCurrentFieldToDocument()) {
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

void miacode::runtime::PlaybackCoordinator::onStopPreview()
{
    MC_OP("miacode::runtime::PlaybackCoordinator::onStopPreview");
    // Stop the export-page intro animation if it's mid-play (it clears the
    // overlay and leaves the chart paused at 0).
    if (state_.exportIntroLeadInActive_) {
        cancelExportIntroLeadIn();
    }
    // The latency page now reuses this exact transport (its synthesized test
    // chart is the preview source), so no special-casing is needed here.
    const quint64 opId = ++state_.previewInteractionSequence_;
    const double returnSecond = qBound(0.0, state_.qtPreviewPlaybackReturnSecond_, previewDurationSeconds());
    const bool wasActive = state_.playing_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_;
    appendPreviewInteractionLog(
        QStringLiteral("stop_request"),
        QString("op=%1 source=stop_action was_active=%2 return_second=%3 current_second=%4")
            .arg(opId)
            .arg(wasActive ? 1 : 0)
            .arg(returnSecond, 0, 'f', 6)
            .arg(authoritativeAudioClockSecond(), 0, 'f', 6));
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.previewPendingPlayInteractionId_ = 0;
    state_.previewPendingPlayInteractionSource_.clear();
    seekPreviewDiscreteToSecond(returnSecond, true);
    state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Stopped;
    appendPreviewInteractionLog(
        QStringLiteral("stop_complete"),
        QString("op=%1 source=stop_action final_second=%2")
            .arg(opId)
            .arg(state_.pauseSecond_, 0, 'f', 6));
}

void miacode::runtime::PlaybackCoordinator::onTogglePreviewPause()
{
    MC_OP("miacode::runtime::PlaybackCoordinator::onTogglePreviewPause");
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
            .arg(state_.playing_ ? 1 : 0)
            .arg(state_.previewStartupSyncPending_ ? 1 : 0)
            .arg(state_.previewAudioDeviceChangeSequence_)
            .arg(state_.pauseSecond_, 0, 'f', 6)
            .arg(authoritativeAudioClockSecond(), 0, 'f', 6)
            .arg(state_.previewPendingPlayInteractionId_));
    if (state_.playing_) {
        appendPreviewInteractionLog(
            QStringLiteral("pause_request"),
            QString("op=%1 source=toggle_action current_second=%2")
                .arg(opId)
                .arg(authoritativeAudioClockSecond(), 0, 'f', 6));
        pauseQtPreviewPlaybackExact();
        appendPreviewInteractionLog(
            QStringLiteral("pause_complete"),
            QString("op=%1 source=toggle_action paused_second=%2")
                .arg(opId)
                .arg(state_.pauseSecond_, 0, 'f', 6));
        updatePauseButtonAppearance();
        return;
    }

    if (!hasPreviewableChart()) {
        return;
    }
    state_.previewPendingPlayInteractionId_ = opId;
    state_.previewPendingPlayInteractionSource_ = QStringLiteral("toggle_action");
    appendPreviewInteractionLog(
        QStringLiteral("play_request"),
        QString("op=%1 source=toggle_action requested_second=%2 device_change_seq=%3")
            .arg(opId)
            .arg(state_.pauseSecond_, 0, 'f', 6)
            .arg(state_.previewAudioDeviceChangeSequence_));
    if (!startQtPreviewPlayback(state_.pauseSecond_, true)) {
        appendPreviewInteractionLog(
            QStringLiteral("play_deferred"),
            QString("op=%1 source=toggle_action requested_second=%2")
                .arg(opId)
                .arg(state_.pauseSecond_, 0, 'f', 6));
        return;
    }
    updatePauseButtonAppearance();
}

void Session::onStopPreview()
{
    playback_->onStopPreview();
}

void Session::onTogglePreviewPause()
{
    playback_->onTogglePreviewPause();
}

void Session::cancelExportIntroLeadIn()
{
    playback_->cancelExportIntroLeadIn();
}

bool Session::exportIntroLeadInPlaying() const
{
    return playback_->exportIntroLeadInPlaying();
}

bool Session::handleExportIntroSliderSeek(double second)
{
    return playback_->handleExportIntroSliderSeek(second);
}

double Session::exportIntroLowerBoundSeconds() const
{
    return playback_->exportIntroLowerBoundSeconds();
}

void Session::refreshExportIntroState()
{
    playback_->refreshExportIntroState();
}

void Session::setExportAuditionClockSchedule(int clockCount, double clockBpm)
{
    playback_->setExportAuditionClockSchedule(clockCount, clockBpm);
}

void Session::clearExportAuditionClockSchedule()
{
    playback_->clearExportAuditionClockSchedule();
}
