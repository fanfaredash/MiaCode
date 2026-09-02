// Session::-owned timeline/editor-sync forwarders and legacy latency-detector
// accessors, split out of TimelineFlow.cpp.
//
// Stage 4.9d-6: PlaybackCoordinator's implementation TUs are being separated
// from the Session assembly so the coordinator can eventually link on its
// own (see the Result Packet for the link-probe evidence). TimelineFlow.cpp
// now holds only PlaybackCoordinator::-owned timeline-bridge/analysis logic
// (plus the one interleaved ensureWaveformCacheService() Coordinator method
// left in place); this file holds the Session::-owned deferred-UI-update
// pair, the latency-sandbox document accessors, and the editor/timeline
// cursor-sync group that used to share that TU.

#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

#include "common/ChartClockCount.h"
#include "common/DebugLog.h"
#include "common/PreviewInteractionConfig.h"
#include "common/WaveformCache.h"

#include <QtCore>

void Session::scheduleDeferredEditorUiUpdate(
    bool updateStatus,
    bool updateEmptyState,
    bool syncTimelineCursor,
    bool centerView,
    bool syncPreviewFollow,
    double previewFollowSecond,
    bool ensurePreviewFollowVisible)
{
    const bool allowTimelineCursorSync =
        !(state_.editorCtrlLeftJumpPending_ || state_.editorCtrlLeftJumpDispatchActive_);
    const bool effectiveSyncTimelineCursor = syncTimelineCursor && allowTimelineCursorSync;
    const bool effectiveCenterView = centerView && effectiveSyncTimelineCursor;
    if (state_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/deferred_ui"),
            QStringLiteral(
                "action=schedule status=%1 empty=%2 sync_timeline=%3 center=%4 sync_follow=%5 follow_second=%6 quick_ready=%7"
            )
                .arg(updateStatus ? 1 : 0)
                .arg(updateEmptyState ? 1 : 0)
                .arg(effectiveSyncTimelineCursor ? 1 : 0)
                .arg(effectiveCenterView ? 1 : 0)
                .arg(syncPreviewFollow ? 1 : 0)
                .arg(previewFollowSecond, 0, 'f', 6)
                .arg(state_.timelineReady_ ? 1 : 0)
        );
    }
    deferredEditorUiStatusPending_ = deferredEditorUiStatusPending_ || updateStatus;
    deferredEditorUiEmptyStatePending_ = deferredEditorUiEmptyStatePending_ || updateEmptyState;
    deferredEditorUiTimelineCursorPending_ =
        deferredEditorUiTimelineCursorPending_ || effectiveSyncTimelineCursor;
    deferredEditorUiCenterView_ = deferredEditorUiCenterView_ || effectiveCenterView;
    if (syncPreviewFollow) {
        deferredEditorUiPreviewFollowPending_ = true;
        deferredEditorUiPreviewFollowSecond_ = previewFollowSecond;
        deferredEditorUiEnsureFollowVisible_ =
            deferredEditorUiEnsureFollowVisible_ || ensurePreviewFollowVisible;
    }
    if (deferredEditorUiUpdatePending_) {
        return;
    }
    deferredEditorUiUpdatePending_ = true;
    QTimer::singleShot(0, this, [this]() { flushDeferredEditorUiUpdate(); });
}

void Session::flushDeferredEditorUiUpdate()
{
    if (!deferredEditorUiUpdatePending_) {
        return;
    }

    deferredEditorUiUpdatePending_ = false;
    const bool updateStatus = deferredEditorUiStatusPending_;
    const bool updateEmptyState = deferredEditorUiEmptyStatePending_;
    const bool syncTimelineCursor = deferredEditorUiTimelineCursorPending_;
    const bool centerView = deferredEditorUiCenterView_;
    const bool syncPreviewFollow = deferredEditorUiPreviewFollowPending_;
    const double previewFollowSecond = deferredEditorUiPreviewFollowSecond_;
    const bool ensurePreviewFollowVisible = deferredEditorUiEnsureFollowVisible_;

    if (state_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/deferred_ui"),
            QStringLiteral(
                "action=flush status=%1 empty=%2 sync_timeline=%3 center=%4 sync_follow=%5 follow_second=%6 ensure_follow_visible=%7 quick_ready=%8"
            )
                .arg(updateStatus ? 1 : 0)
                .arg(updateEmptyState ? 1 : 0)
                .arg(syncTimelineCursor ? 1 : 0)
                .arg(centerView ? 1 : 0)
                .arg(syncPreviewFollow ? 1 : 0)
                .arg(previewFollowSecond, 0, 'f', 6)
                .arg(ensurePreviewFollowVisible ? 1 : 0)
                .arg(state_.timelineReady_ ? 1 : 0)
        );
    }

    deferredEditorUiStatusPending_ = false;
    deferredEditorUiEmptyStatePending_ = false;
    deferredEditorUiTimelineCursorPending_ = false;
    deferredEditorUiCenterView_ = false;
    deferredEditorUiPreviewFollowPending_ = false;
    deferredEditorUiEnsureFollowVisible_ = false;

    if (updateEmptyState) {
        updateEditorEmptyState();
    }
    if (updateStatus) {
        updateEditorStatus();
    }

    bool previewFollowHandled = false;
    if (syncPreviewFollow
        && hasActiveDifficulty()
        && state_.previewFollowEnabled_) {
        previewFollowHandled = true;
        syncEditorCursorToPreviewSecond(
            previewFollowSecond,
            centerView,
            ensurePreviewFollowVisible);
    }

    const bool previewFollowOwnsPlaybackCursor =
        previewFollowHandled
        && (state_.playing_
            || state_.previewStartupSyncPending_
            || state_.previewLateVideoStartPending_);
    // The editor→timeline cursor sync is published by EditorSyncController now
    // (caretLocationPublished → publishEditorCaret); this branch read the hidden
    // widget's cursor, which no QML selection ever reached.
    Q_UNUSED(syncTimelineCursor);
    Q_UNUSED(previewFollowOwnsPlaybackCursor);
    Q_UNUSED(centerView);
}

void Session::resetPreviewTrackTimelineOffsets()
{
    playback_->resetPreviewTrackTimelineOffsets();
}

void Session::applyWaveformData(
    const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData)
{
    playback_->applyWaveformData(waveformData);
}

void Session::refreshWaveformCache()
{
    playback_->refreshWaveformCache();
}

void Session::refreshWaveformCache(double knownDurationSeconds)
{
    playback_->refreshWaveformCache(knownDurationSeconds);
}

void Session::applyLatencyDetectorOffset(double seconds)
{
    playback_->applyLatencyDetectorOffset(seconds);
}

double Session::latencyDocumentWholeBpm() const
{
    // Same resolution the Widgets page used: prefer &wholebpm, else the first
    // inline (BPM) of any non-empty difficulty. BPM is song-wide and the
    // latency page has no active difficulty, so scanning is correct here.
    bool ok = false;
    const double whole = parsedWholeBpm(&ok);
    if (ok && whole > 0.0) {
        return whole;
    }
    for (const int id : applicationServices_.workspace().document().difficultyIds()) {
        const SimaiDifficultyData* difficultyData = applicationServices_.workspace().document().difficulty(id);
        if (difficultyData == nullptr || difficultyData->chart.isEmpty()) {
            continue;
        }
        const double firstBpm = miacode::chart_clock::firstBpmFromChart(difficultyData->chart);
        if (firstBpm > 0.0) {
            return firstBpm;
        }
    }
    return 0.0;
}

double Session::latencyDocumentOffsetSeconds() const
{
    bool ok = false;
    const double value = applicationServices_.workspace().document().first.trimmed().toDouble(&ok);
    return ok ? value : 0.0;
}

int Session::latencyDocumentClockCount() const
{
    return parsedClockCount();
}

QString Session::latencyTrackPath() const
{
    return lastTrackPath_;
}

void Session::applyLatencyDetectorBpm(double bpm)
{
    playback_->applyLatencyDetectorBpm(bpm);
}

void Session::applyLatencyDetectorClockCount(int clockCount)
{
    playback_->applyLatencyDetectorClockCount(clockCount);
}

void Session::setCurrentFilePath(const QString& path, bool suppressImmediateRefresh)
{
    playback_->setCurrentFilePath(path, suppressImmediateRefresh);
}

void Session::updateWindowTitle()
{
    playback_->updateWindowTitle();
}

void Session::updateCurrentFileLabel()
{
    playback_->updateCurrentFileLabel();
}

QString Session::editorText() const
{
    return playback_->editorText();
}

void Session::scheduleTimelineRefresh()
{
    playback_->scheduleTimelineRefresh();
}

void Session::refreshTimelineMetadata()
{
    playback_->refreshTimelineMetadata();
}

void Session::refreshTimelineQuickModelFromCurrentText()
{
    playback_->refreshTimelineQuickModelFromCurrentText();
}

void Session::applyLatestTimelinePreviewStateToPausedPreview()
{
    playback_->applyLatestTimelinePreviewStateToPausedPreview();
}

void Session::requestTimelineSlowRefresh()
{
    playback_->requestTimelineSlowRefresh();
}

void Session::dispatchTimelineSlowRefresh()
{
    playback_->dispatchTimelineSlowRefresh();
}

void Session::scheduleTimelineAnalysisRefresh(
    const TimelineSlowRefreshRequest& request,
    const SimaiNativeParseResult& parseResult,
    const TimelinePreviewRefreshState& previewState)
{
    playback_->scheduleTimelineAnalysisRefresh(request, parseResult, previewState);
}

bool Session::scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs)
{
    return playback_->scheduleTimelineAnalysisRefreshFromLatestPreviewState(delayMs);
}

void Session::requestTimelineAnalysisDispatch(int delayMs)
{
    playback_->requestTimelineAnalysisDispatch(delayMs);
}

void Session::dispatchTimelineAnalysisRefresh()
{
    playback_->dispatchTimelineAnalysisRefresh();
}

void Session::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    playback_->rebuildStaticMuriReferences(noteMarkers);
}

double Session::timelineSecondForCursor(int line, int col) const
{
    return playback_->timelineSecondForCursor(line, col);
}

void Session::setTouchPadAuthoringAnchor(double seekSecond, double tokenSecond)
{
    playback_->setTouchPadAuthoringAnchor(seekSecond, tokenSecond);
}

bool Session::resolveTimelineSecondForCursor(int line, int col, double* second) const
{
    return playback_->resolveTimelineSecondForCursor(line, col, second);
}

void Session::publishEditorCaret(int difficultyId, int line, int column)
{
    if (!hasActiveDifficulty() || difficultyId != activeDifficultyId_) {
        return;
    }
    playback_->updateTimelineCursorFromEditorLocation(
        qMax(1, line), qMax(1, column), false);
}

void Session::handleEditorPointerInteraction(int difficultyId)
{
    if (!hasActiveDifficulty() || difficultyId != activeDifficultyId_
        || !playing_) {
        return;
    }
    pauseQtPreviewPlaybackExact();
    updatePauseButtonAppearance();
    syncEditorCursorToPreviewSecond(qMax(0.0, pauseSecond_), true, false);
}

miacode::v2::EditorSyncController& Session::editorSyncController()
{
    return *editorSyncController_;
}

const miacode::v2::EditorSyncController& Session::editorSyncController() const
{
    return *editorSyncController_;
}

bool Session::applyTouchPadAuthoringPreviewAnchor(int difficultyId, int line, int column)
{
    if (!hasActiveDifficulty() || difficultyId != activeDifficultyId_) {
        return false;
    }
    double tokenSecond = 0.0;
    if (!resolveTimelineSecondForCursor(qMax(1, line), qMax(1, column), &tokenSecond)) {
        return false;
    }
    const double seekSecond = qMax(0.0,
        tokenSecond - miacode::preview_interaction::kTouchPadAuthoringPreviewLeadSeconds);
    setTouchPadAuthoringAnchor(seekSecond, tokenSecond);
    seekPreviewDiscreteToSecond(seekSecond, true);
    return true;
}

bool Session::seekPreviewToEditorLocation(int difficultyId, int line, int column)
{
    if (!hasActiveDifficulty() || difficultyId != activeDifficultyId_) {
        return false;
    }
    double second = 0.0;
    if (!resolveTimelineSecondForCursor(qMax(1, line), qMax(1, column), &second)) {
        return false;
    }
    // Park playback first so the seek lands on a stable clock, suppress the
    // timeline cursor feedback while seeking, then publish the new cursor.
    if (playing_) {
        pauseQtPreviewPlaybackExact();
    }
    const bool previousSuppressTimelineCursorSync = suppressTimelineCursorSync_;
    suppressTimelineCursorSync_ = true;
    seekPreviewDiscreteToSecond(second, true);
    if (timelineQuickStateBridge_ != nullptr) {
        deferTimelineCursorBridgeUpdate(second, false);
    }
    suppressTimelineCursorSync_ = previousSuppressTimelineCursorSync;
    return true;
}

bool Session::editorAuthoringContextActive() const
{
    return editorSyncController_ != nullptr && editorSyncController_->editorContextActive();
}

void Session::navigateTimelineToSecond(double second, bool focusEditor)
{
    playback_->navigateTimelineToSecond(second, focusEditor);
}

void Session::deferTimelineCursorBridgeUpdate(double second, bool centerView)
{
    playback_->deferTimelineCursorBridgeUpdate(second, centerView);
}

bool Session::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    return playback_->resolveNearestTimelineNote(second, lane, line, col, noteSecond);
}

bool Session::moveEditorCursorToTimelineLocation(
    int line,
    int col,
    bool selectToken,
    bool focusEditor,
    bool centerView,
    bool suppressSignals,
    qint64* cursorMoveElapsedNs,
    qint64* followOverlayElapsedNs)
{
    return playback_->moveEditorCursorToTimelineLocation(
        line,
        col,
        selectToken,
        focusEditor,
        centerView,
        suppressSignals,
        cursorMoveElapsedNs,
        followOverlayElapsedNs
    );
}

void Session::syncEditorCursorToPreviewSecond(
    double second,
    bool centerView,
    bool ensureVisibleWhenPaused)
{
    playback_->syncEditorCursorToPreviewSecond(second, centerView, ensureVisibleWhenPaused);
}
