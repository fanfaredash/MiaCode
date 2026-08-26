#include "AnalysisService.h"

#include "common/MuriConfig.h"
#include "core/chart/document/SimaiTimingMetadata.h"
#include "timeline/TimelineSlowRefresh.h"
#include "timeline/TimelineMarkerOffset.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace miacode::v2 {

AnalysisService::AnalysisService(
    ChartWorkspace& workspace,
    SimaiNativeValidationLocale locale,
    const MuriRenderOptions& renderOptions,
    double staticTapOnSlideThresholdSeconds,
    QObject* parent)
    : QObject(parent)
    , workspace_(&workspace)
    , locale_(locale)
    , renderOptions_(renderOptions)
    , staticTapOnSlideThresholdSeconds_(staticTapOnSlideThresholdSeconds)
{
    connect(workspace_, &ChartWorkspace::changed, this, [this](quint64) {
        requestAnalysis();
    });
    if (workspace_->snapshot().hasDocument) requestAnalysis();
}

AnalysisSnapshot AnalysisService::snapshot() const
{
    return snapshot_;
}

void AnalysisService::requestAnalysis()
{
    if (workspace_ == nullptr) return;
    AnalysisRequest request;
    request.workspace = workspace_->snapshot();
    request.document = workspace_->document();
    request.locale = locale_;
    request.renderOptions = renderOptions_;
    request.staticTapOnSlideThresholdSeconds = staticTapOnSlideThresholdSeconds_;

    AnalysisSnapshot pending;
    pending.revision = request.workspace.revision;
    pending.difficultyId = request.workspace.activeDifficultyId;
    pending.locale = request.locale;
    pending.pending = request.workspace.hasDocument && request.workspace.activeDifficultyId > 0;
    snapshot_ = std::move(pending);
    emit snapshotChanged(snapshot_.difficultyId, snapshot_.revision);

    if (!snapshot_.pending) {
        pendingRequest_.reset();
        return;
    }
    pendingRequest_ = std::move(request);
    dispatchPendingRequest();
}

AnalysisSnapshot AnalysisService::analyze(
    const ChartWorkspace& workspace,
    SimaiNativeValidationLocale locale,
    const MuriRenderOptions& renderOptions,
    double staticTapOnSlideThresholdSeconds)
{
    AnalysisRequest request;
    request.workspace = workspace.snapshot();
    request.document = workspace.document();
    request.locale = locale;
    request.renderOptions = renderOptions;
    request.staticTapOnSlideThresholdSeconds = staticTapOnSlideThresholdSeconds;
    return analyzeRequest(std::move(request));
}

AnalysisSnapshot AnalysisService::analyzeRequest(AnalysisRequest analysisRequest)
{
    AnalysisSnapshot snapshot;
    snapshot.revision = analysisRequest.workspace.revision;
    snapshot.difficultyId = analysisRequest.workspace.activeDifficultyId;
    snapshot.locale = analysisRequest.locale;

    const SimaiDifficultyData* difficulty =
        analysisRequest.document.difficulty(analysisRequest.workspace.activeDifficultyId);
    if (!analysisRequest.workspace.hasDocument || difficulty == nullptr) return snapshot;

    const miacode::simai::SimaiTimingMetadata timing =
        miacode::simai::buildTimingMetadata(analysisRequest.document);
    const SimaiNativeParseResult parseResult =
        SimaiNativeParser::parseForTimeline(difficulty->chart, timing);
    const TimelinePreviewRefreshState previewState = buildTimelinePreviewRefreshState(
        parseResult,
        miacode::timeline::offset::parsedFirstSeconds(analysisRequest.document.first));
    TimelineAnalysisRefreshRequest timelineRequest;
    timelineRequest.revision = snapshot.revision;
    timelineRequest.difficultyId = snapshot.difficultyId;
    timelineRequest.chartText = difficulty->chart;
    timelineRequest.validationLocale = snapshot.locale;
    timelineRequest.timingMetadata = timing;
    timelineRequest.parseResult = parseResult;
    timelineRequest.noteMarkerSignature = previewState.noteMarkerSignature;
    timelineRequest.noteMarkers = previewState.shiftedNoteMarkers;
    timelineRequest.renderOptions = analysisRequest.renderOptions;
    timelineRequest.staticTapOnSlideThresholdSeconds =
        analysisRequest.staticTapOnSlideThresholdSeconds >= 0.0
        ? analysisRequest.staticTapOnSlideThresholdSeconds
        : static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0;

    const TimelineAnalysisRefreshResult result = buildTimelineAnalysisRefreshResult(timelineRequest);
    snapshot.available = true;
    snapshot.pending = false;
    snapshot.validation = result.validationReport;
    snapshot.noteMarkers = std::move(timelineRequest.noteMarkers);
    snapshot.noteMarkerSignature = result.noteMarkerSignature;
    snapshot.muri = result.analysisReport;
    snapshot.muriStaticReferences = result.staticReferences;
    return snapshot;
}

void AnalysisService::dispatchPendingRequest()
{
    if (workerRunning_ || !pendingRequest_.has_value()) return;
    AnalysisRequest request = std::move(*pendingRequest_);
    pendingRequest_.reset();
    workerRunning_ = true;
    QPointer<AnalysisService> guard(this);
    QThreadPool::globalInstance()->start([guard, request = std::move(request)]() mutable {
        AnalysisSnapshot result = AnalysisService::analyzeRequest(std::move(request));
        if (guard.isNull()) return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) return;
                guard->workerRunning_ = false;
                if (guard->pendingRequest_.has_value()
                    && guard->pendingRequest_->workspace.activeDifficultyId == result.difficultyId
                    && guard->pendingRequest_->workspace.revision == result.revision) {
                    guard->pendingRequest_.reset();
                }
                if (guard->identityIsCurrent(result.difficultyId, result.revision)) {
                    guard->snapshot_ = std::move(result);
                    emit guard->snapshotChanged(
                        guard->snapshot_.difficultyId, guard->snapshot_.revision);
                    emit guard->analysisReady(
                        guard->snapshot_.difficultyId, guard->snapshot_.revision);
                }
                guard->dispatchPendingRequest();
            },
            Qt::QueuedConnection);
    });
}

bool AnalysisService::identityIsCurrent(int difficultyId, quint64 revision) const
{
    if (workspace_ == nullptr) return false;
    const ChartWorkspaceSnapshot current = workspace_->snapshot();
    return current.hasDocument && current.activeDifficultyId == difficultyId
        && current.revision == revision && snapshot_.pending
        && snapshot_.difficultyId == difficultyId && snapshot_.revision == revision;
}

}  // namespace miacode::v2
