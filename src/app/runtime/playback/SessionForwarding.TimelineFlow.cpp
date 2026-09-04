// Session::-owned timeline/editor-sync forwarders and legacy latency-detector
// accessors, split out of TimelineFlow.cpp.
//
// Stage 4.9d-6: PlaybackCoordinator's implementation TUs are being separated
// from the Session assembly so the coordinator can eventually link on its
// own (see the Result Packet for the link-probe evidence). TimelineFlow.cpp
// now holds only PlaybackCoordinator::-owned timeline-bridge/analysis logic
// (plus the one interleaved ensureWaveformCacheService() Coordinator method
// left in place); this file holds the latency-sandbox document accessors and
// the editor/timeline cursor-sync group that used to share that TU.

#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

#include "common/ChartClockCount.h"
#include "common/PreviewInteractionConfig.h"
#include "common/WaveformCache.h"

#include <QtCore>

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
