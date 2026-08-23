#include "MainWindow.TimelineSection.h"
#include "../../MainWindowShared.h"
#include "../dialogs/MainWindow.DialogsSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "../validation/EditorSelectionUtils.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/ProcessDiagnostics.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/latency/LatencySandboxController.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include "MainWindow.PreviewTimelineFlow.Internal.h"

using namespace miacode::mainwindow::shared;
using namespace miacode::mainwindow::preview_timeline_flow_detail;

namespace {

constexpr int kTimelineAnalysisIdleDelayMs = 180;

}  // namespace

void MainWindow::invalidateDocumentValidationRevision()
{
    ++state_.timelineRevision_;
    emit documentValidationChanged();
}

void MainWindow::TimelineSection::scheduleTimelineAnalysisRefresh(
    const TimelineSlowRefreshRequest& request,
    const SimaiNativeParseResult& parseResult,
    const TimelinePreviewRefreshState& previewState)
{
    state_.pendingTimelineAnalysisRefresh_.revision = request.revision;
    state_.pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    state_.pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    state_.pendingTimelineAnalysisRefresh_.validationLocale = request.validationLocale;
    state_.pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    state_.pendingTimelineAnalysisRefresh_.parseResult = parseResult;
    state_.pendingTimelineAnalysisRefresh_.noteMarkerSignature = previewState.noteMarkerSignature;
    state_.pendingTimelineAnalysisRefresh_.noteMarkers = previewState.shiftedNoteMarkers;
    state_.pendingTimelineAnalysisRefresh_.renderOptions = state_.muriRenderOptions_;
    state_.pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0;
    state_.timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch();
}

bool MainWindow::TimelineSection::scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs)
{
    if (!hasActiveDifficulty()
        || !state_.latestTimelinePreviewSnapshotReady_
        || state_.lastTimelineParseDifficultyId_ != activeDifficultyId()
        || state_.lastTimelineParseChartText_ != activeChartText()
        || state_.lastTimelineParseTimingMetadata_ != currentTimingMetadata()) {
        return false;
    }

    TimelineSlowRefreshRequest request;
    request.revision = state_.latestTimelinePreviewRevision_;
    request.difficultyId = activeDifficultyId();
    request.chartText = state_.lastTimelineParseChartText_;
    request.timingMetadata = state_.lastTimelineParseTimingMetadata_;
    request.validationLocale = uiValidationLocale();

    state_.pendingTimelineAnalysisRefresh_.revision = request.revision;
    state_.pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    state_.pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    state_.pendingTimelineAnalysisRefresh_.validationLocale = request.validationLocale;
    state_.pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    state_.pendingTimelineAnalysisRefresh_.parseResult = state_.lastTimelineParseResult_;
    state_.pendingTimelineAnalysisRefresh_.noteMarkerSignature = state_.latestTimelineNoteMarkerSignature_;
    state_.pendingTimelineAnalysisRefresh_.noteMarkers = state_.latestTimelineNoteMarkers_;
    state_.pendingTimelineAnalysisRefresh_.renderOptions = state_.muriRenderOptions_;
    state_.pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0;
    state_.timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch(delayMs);
    return true;
}

void MainWindow::TimelineSection::requestTimelineAnalysisDispatch(int delayMs)
{
    if (state_.pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }
    if (state_.qtPreviewPlaying_) {
        if (ui_.timelineAnalysisIdleTimer_ != nullptr) {
            ui_.timelineAnalysisIdleTimer_->stop();
        }
        return;
    }
    if (ui_.timelineAnalysisIdleTimer_ != nullptr) {
        const int effectiveDelayMs = delayMs >= 0 ? delayMs : kTimelineAnalysisIdleDelayMs;
        ui_.timelineAnalysisIdleTimer_->start(effectiveDelayMs);
        return;
    }
    dispatchTimelineAnalysisRefresh();
}

void MainWindow::TimelineSection::dispatchTimelineAnalysisRefresh()
{
    if (!hasActiveDifficulty() || state_.qtPreviewPlaying_ || state_.timelineAnalysisWorkerRunning_ || state_.pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }

    const TimelineAnalysisRefreshRequest request = state_.pendingTimelineAnalysisRefresh_;
    state_.pendingTimelineAnalysisRefresh_ = TimelineAnalysisRefreshRequest();
    state_.timelineAnalysisWorkerRunning_ = true;
    state_.timelineAnalysisRunningRevision_ = request.revision;
    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = state_.timelineAnalysisPool_ != nullptr
        ? state_.timelineAnalysisPool_
        : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        miacode::diag::MemoryStageScope memScope("preview/mem_stage", "analysis_build");
        TimelineAnalysisRefreshResult result;
        {
            // beta7 probe 2.1 — tight core bracket around the parse + Muri-analyze build only.
            miacode::diag::MemoryStageScope memScopeCore("preview/mem_stage", "analysis_core");
            result = buildTimelineAnalysisRefreshResult(request);
        }
        if (guard.isNull()) {
            return;
        }
        miacode::diag::leak_gauge::noteInflightDispatch();
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                miacode::diag::leak_gauge::noteInflightApplied();
                if (guard.isNull()) {
                    return;
                }

                miacode::diag::MemoryStageScope memScope("preview/mem_stage", "analysis_apply");
                QElapsedTimer applyTimer;
                applyTimer.start();

                guard->state_.timelineAnalysisWorkerRunning_ = false;
                if (result.revision != guard->state_.timelineAnalysisRequestedRevision_
                    || result.revision != guard->state_.timelineRevision_
                    || !guard->hasActiveDifficulty()
                    || result.difficultyId != guard->activeDifficultyId()
                    || result.chartText != guard->activeChartText()
                    || result.timingMetadata != guard->currentTimingMetadata()
                    || result.noteMarkerSignature != guard->state_.latestTimelineNoteMarkerSignature_) {
                    guard->requestTimelineAnalysisDispatch();
                    return;
                }

                ValidationCacheEntry entry;
                entry.chartText = result.chartText;
                entry.validationLocale = result.validationReport.issues.isEmpty() ? uiValidationLocale() : result.validationLocale;
                entry.timingMetadata = result.timingMetadata;
                entry.validationRevision = result.revision;
                entry.ok = result.validationReport.ok;
                entry.errorCount = result.validationReport.errorCount;
                entry.warningCount = result.validationReport.warningCount;
                entry.lenientNoteCount = result.validationReport.lenientNoteCount;
                entry.lenientErrorCount = result.validationReport.lenientErrorCount;
                entry.strictNoteCount = result.validationReport.strictNoteCount;
                entry.strictErrorCount = result.validationReport.strictErrorCount;
                entry.issues.reserve(result.validationReport.issues.size());
                for (const SimaiNativeValidationIssue& issue : result.validationReport.issues) {
                    ValidationCachedIssue cachedIssue;
                    cachedIssue.line = issue.line;
                    cachedIssue.col = issue.col;
                    cachedIssue.endCol = issue.endCol;
                    cachedIssue.severity = issue.severity;
                    cachedIssue.rawMessage = issue.rawMessage;
                    cachedIssue.displayMessage = issue.displayMessage;
                    entry.issues.append(cachedIssue);
                }
                const int validationIssueCount = entry.issues.size();
                const int muriDiagnosticCount = result.analysisReport.diagnostics.size();
                const int muriStaticReferenceCount = result.staticReferences.size();
                guard->state_.validationCacheByDifficulty_[result.difficultyId] = std::move(entry);
                emit guard->documentValidationChanged();
                guard->state_.pendingDeferredValidationUiRefresh_ = true;
                guard->state_.muriAnalysisReport_ = std::move(result.analysisReport);
                guard->state_.muriAnalysisReport_.revision = ++guard->state_.muriAnalysisReportRevisionCounter_;
                guard->state_.muriAnalysisReportNoteMarkerSignature_ = result.noteMarkerSignature;
                guard->state_.muriAnalysisReportDifficultyId_ = result.difficultyId;
                guard->state_.muriAnalysisReportTimelineRevision_ = result.revision;
                guard->state_.muriAnalysisResultAvailable_ = true;
                guard->state_.muriStaticReferences_ = std::move(result.staticReferences);
                guard->state_.muriStaticReferencesNoteMarkerSignature_ = result.noteMarkerSignature;
                guard->state_.muriStaticReferencesDifficultyId_ = result.difficultyId;
                guard->state_.muriStaticReferencesTimelineRevision_ = result.revision;
                guard->state_.muriStaticReferencesAvailable_ = true;
                guard->state_.pendingDeferredMuriUiRefresh_ = true;
                if (!guard->state_.qtPreviewPlaying_) {
                    guard->applyDeferredAnalysisUiUpdates();
                }
                if (guard->state_.runtimeDebugOutputEnabled_) {
                    appendTimelinePerfLog(
                        QStringLiteral("edit/muri_perf"),
                        QStringLiteral("phase=analysis_apply validation_issues=%1 diagnostics=%2 static_refs=%3 elapsed_ms=%4")
                            .arg(validationIssueCount)
                            .arg(muriDiagnosticCount)
                            .arg(muriStaticReferenceCount)
                            .arg(applyTimer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
                    );
                }
                guard->requestTimelineAnalysisDispatch();
            },
            Qt::QueuedConnection
        );
    });
}
