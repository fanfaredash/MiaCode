#include "AnalysisService.h"

#include "common/MuriConfig.h"
#include "core/chart/document/SimaiTimingMetadata.h"
#include "timeline/TimelineSlowRefresh.h"
#include "timeline/TimelineMarkerOffset.h"

namespace miacode::v2 {

AnalysisSnapshot AnalysisService::analyze(
    const ChartWorkspace& workspace,
    SimaiNativeValidationLocale locale,
    const MuriRenderOptions& renderOptions,
    double staticTapOnSlideThresholdSeconds)
{
    AnalysisSnapshot snapshot;
    const ChartWorkspaceSnapshot workspaceSnapshot = workspace.snapshot();
    snapshot.revision = workspaceSnapshot.revision;
    snapshot.difficultyId = workspaceSnapshot.activeDifficultyId;

    const SimaiDifficultyData* difficulty =
        workspace.document().difficulty(workspaceSnapshot.activeDifficultyId);
    if (!workspaceSnapshot.hasDocument || difficulty == nullptr) return snapshot;

    const miacode::simai::SimaiTimingMetadata timing =
        miacode::simai::buildTimingMetadata(workspace.document());
    const SimaiNativeParseResult parseResult =
        SimaiNativeParser::parseForTimeline(difficulty->chart, timing);
    const TimelinePreviewRefreshState previewState = buildTimelinePreviewRefreshState(
        parseResult,
        miacode::timeline::offset::parsedFirstSeconds(workspace.document().first));
    TimelineAnalysisRefreshRequest request;
    request.revision = workspaceSnapshot.revision;
    request.difficultyId = workspaceSnapshot.activeDifficultyId;
    request.chartText = difficulty->chart;
    request.validationLocale = locale;
    request.timingMetadata = timing;
    request.parseResult = parseResult;
    request.noteMarkerSignature = previewState.noteMarkerSignature;
    request.noteMarkers = previewState.shiftedNoteMarkers;
    request.renderOptions = renderOptions;
    request.staticTapOnSlideThresholdSeconds = staticTapOnSlideThresholdSeconds >= 0.0
        ? staticTapOnSlideThresholdSeconds
        : static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0;

    const TimelineAnalysisRefreshResult result = buildTimelineAnalysisRefreshResult(request);
    snapshot.available = true;
    snapshot.validation = result.validationReport;
    snapshot.noteMarkers = std::move(request.noteMarkers);
    snapshot.noteMarkerSignature = result.noteMarkerSignature;
    snapshot.muri = result.analysisReport;
    snapshot.muriStaticReferences = result.staticReferences;
    return snapshot;
}

}  // namespace miacode::v2
