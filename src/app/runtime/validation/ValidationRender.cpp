#include "runtime/validation/ValidationHost.h"
#include "runtime/Shared.h"

#include "UiText.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriPanelEntries.h"

#include <QtCore>

using namespace miacode::runtime::shared;

namespace {

bool previewConsumesMuriAnalysisReport(const MuriRenderOptions& options)
{
    return options.renderMode == RenderMode::MaimuriDxStyle
        || options.showJudgeMarkers
        || options.showTouchTrail;
}

MuriAnalysisReport muriPromptReportForTimelineDots(
    const MuriAnalysisReport& alignedReport,
    const QVector<MuriStaticReference>& alignedStaticReferences)
{
    MuriAnalysisReport promptReport;
    const QVector<miacode::muri::MuriPanelEntry> entries =
        miacode::muri::buildVisibleMuriPanelEntries(alignedReport, alignedStaticReferences);
    promptReport.diagnostics.reserve(entries.size());
    for (const miacode::muri::MuriPanelEntry& entry : entries) {
        MuriDiagnostic diagnostic;
        diagnostic.kind = entry.kind;
        diagnostic.alertLevel = entry.alertLevel;
        diagnostic.second = entry.occurrenceSecond;
        diagnostic.anchorSecond = entry.second;
        diagnostic.line = entry.line;
        diagnostic.col = entry.col;
        diagnostic.detail = entry.rawDetail;
        diagnostic.detailKind = entry.detailKind;
        diagnostic.detailArgs = entry.detailArgs;
        promptReport.diagnostics.append(diagnostic);
    }
    promptReport.sourceSignature = alignedReport.sourceSignature;
    return promptReport;
}

const QVector<MuriStaticReference>& alignedMuriStaticReferencesForTimeline(
    const QByteArray& latestSignature,
    const QByteArray& analysisSignature,
    const QVector<MuriStaticReference>& references)
{
    static const QVector<MuriStaticReference> kEmptyReferences;
    return latestSignature == analysisSignature ? references : kEmptyReferences;
}

}  // namespace

const MuriAnalysisReport& miacode::runtime::ValidationHost::alignedMuriAnalysisReportForPreview() const
{
    static const MuriAnalysisReport kEmptyReport;
    if (state_.latestTimelineNoteMarkerSignature_ != state_.muriAnalysisReportNoteMarkerSignature_) {
        return kEmptyReport;
    }
    return state_.muriAnalysisReport_;
}

void miacode::runtime::ValidationHost::applyAlignedMuriAnalysisReportToViews()
{
    static const MuriAnalysisReport kEmptyReport;
    const MuriAnalysisReport& alignedReport = alignedMuriAnalysisReportForPreview();
    const QVector<MuriStaticReference>& alignedStaticReferences = alignedMuriStaticReferencesForTimeline(
        state_.latestTimelineNoteMarkerSignature_,
        state_.muriAnalysisReportNoteMarkerSignature_,
        state_.muriStaticReferences_);
    const MuriAnalysisReport timelinePromptReport = state_.ignoreMuriIssuePrompts_
        ? MuriAnalysisReport()
        : muriPromptReportForTimelineDots(alignedReport, alignedStaticReferences);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setMuriAnalysisReport(timelinePromptReport);
    }
    if (state_.scene_ != nullptr) {
        state_.scene_->setMuriAnalysisReport(
            previewConsumesMuriAnalysisReport(state_.muriRenderOptions_) ? alignedReport : kEmptyReport
        );
    }
}

void miacode::runtime::ValidationHost::onToggleJudgeMarkers(bool checked)
{
    state_.showJudgeMarkers_ = checked;
    applyMuriRenderOptions();
    session_.savePortableState();
    session_.noteStatus(
        state_.showJudgeMarkers_
            ? UiText::text(QStringLiteral("status.judge_marker_enabled"))
            : UiText::text(QStringLiteral("status.judge_marker_disabled"))
    );
}

void miacode::runtime::ValidationHost::onToggleTouchTrail(bool checked)
{
    state_.showTouchTrail_ = checked;
    applyMuriRenderOptions();
    session_.savePortableState();
    session_.noteStatus(
        state_.showTouchTrail_
            ? UiText::text(QStringLiteral("status.touch_trail_enabled"))
            : UiText::text(QStringLiteral("status.touch_trail_disabled"))
    );
}

void miacode::runtime::ValidationHost::applyMuriRenderOptions()
{
    state_.muriRenderOptions_.showSlideTracks = state_.showSlideTracks_;
    state_.muriRenderOptions_.showJudgeMarkers = state_.showJudgeMarkers_;
    state_.muriRenderOptions_.showTouchTrail = state_.showTouchTrail_;

    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setShowSlideTracks(state_.muriRenderOptions_.showSlideTracks);
    }
    if (state_.scene_ != nullptr) {
        state_.scene_->setMuriRenderOptions(state_.muriRenderOptions_);
    }
    applyAlignedMuriAnalysisReportToViews();
}

void miacode::runtime::ValidationHost::setMuriRenderMode(RenderMode mode, bool persistState)
{
    if (state_.muriRenderOptions_.renderMode == mode && !persistState) {
        applyMuriRenderOptions();
        return;
    }
    state_.muriRenderOptions_.renderMode = mode;
    applyMuriRenderOptions();
    if (persistState) {
        session_.savePortableState();
    }
    if (session_.hasActiveDifficulty() && !session_.scheduleTimelineAnalysisRefreshFromLatestPreviewState()) {
        session_.refreshTimelineMetadata();
    }
    QString modeMessageKey = QStringLiteral("status.muri_render_mode_native");
    if (mode == RenderMode::MaimuriDxStyle) {
        modeMessageKey = QStringLiteral("status.muri_render_mode_dx");
    } else if (mode == RenderMode::EraseByArea) {
        modeMessageKey = QStringLiteral("status.muri_render_mode_erase_by_area");
    }
    session_.noteStatus(UiText::text(modeMessageKey));
}

const MuriAnalysisReport& Session::alignedMuriAnalysisReportForPreview() const
{
    return validation_->alignedMuriAnalysisReportForPreview();
}

void Session::applyAlignedMuriAnalysisReportToViews()
{
    validation_->applyAlignedMuriAnalysisReportToViews();
}

void Session::onToggleJudgeMarkers(bool checked)
{
    validation_->onToggleJudgeMarkers(checked);
}

void Session::onToggleTouchTrail(bool checked)
{
    validation_->onToggleTouchTrail(checked);
}

void Session::applyMuriRenderOptions()
{
    validation_->applyMuriRenderOptions();
}

void Session::setMuriRenderMode(RenderMode mode, bool persistState)
{
    validation_->setMuriRenderMode(mode, persistState);
}
