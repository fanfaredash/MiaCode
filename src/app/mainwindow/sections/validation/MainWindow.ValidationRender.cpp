#include "MainWindow.ValidationSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "DialogLocalization.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriPanelEntries.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

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

const MuriAnalysisReport& MainWindow::ValidationSection::alignedMuriAnalysisReportForPreview() const
{
    static const MuriAnalysisReport kEmptyReport;
    if (state_.latestTimelineNoteMarkerSignature_ != state_.muriAnalysisReportNoteMarkerSignature_) {
        return kEmptyReport;
    }
    return state_.muriAnalysisReport_;
}

void MainWindow::ValidationSection::applyAlignedMuriAnalysisReportToViews()
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
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setMuriAnalysisReport(
            previewConsumesMuriAnalysisReport(state_.muriRenderOptions_) ? alignedReport : kEmptyReport
        );
    }
}

void MainWindow::ValidationSection::onToggleJudgeMarkers(bool checked)
{
    state_.showJudgeMarkers_ = checked;
    applyMuriRenderOptions();
    owner_.savePortableState();
    owner_.statusBar()->showMessage(
        state_.showJudgeMarkers_
            ? UiText::text(QStringLiteral("status.judge_marker_enabled"))
            : UiText::text(QStringLiteral("status.judge_marker_disabled"))
    );
}

void MainWindow::ValidationSection::onToggleTouchTrail(bool checked)
{
    state_.showTouchTrail_ = checked;
    applyMuriRenderOptions();
    owner_.savePortableState();
    owner_.statusBar()->showMessage(
        state_.showTouchTrail_
            ? UiText::text(QStringLiteral("status.touch_trail_enabled"))
            : UiText::text(QStringLiteral("status.touch_trail_disabled"))
    );
}

void MainWindow::ValidationSection::onEditStaticTapOnSlideThreshold()
{
    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(UiText::text(QStringLiteral("validation.tap_on_slide_threshold")));
    dialog.setModal(true);
    dialog.setMinimumWidth(360);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(10);

    auto* hintLabel = new QLabel(
        UiText::text(QStringLiteral("validation.adjust_the_static_tap_on")),
        &dialog);
    hintLabel->setWordWrap(true);
    rootLayout->addWidget(hintLabel);

    auto* valueRow = new QWidget(&dialog);
    auto* valueLayout = new QHBoxLayout(valueRow);
    valueLayout->setContentsMargins(0, 0, 0, 0);
    valueLayout->setSpacing(10);

    auto* slider = new QSlider(Qt::Horizontal, valueRow);
    slider->setRange(
        miacode::muri::kStaticTapOnSlideThresholdMinMs,
        miacode::muri::kStaticTapOnSlideThresholdMaxMs);
    slider->setValue(state_.staticTapOnSlideThresholdMs_);

    auto* spinBox = new QSpinBox(valueRow);
    spinBox->setRange(
        miacode::muri::kStaticTapOnSlideThresholdMinMs,
        miacode::muri::kStaticTapOnSlideThresholdMaxMs);
    spinBox->setSuffix(QStringLiteral(" ms"));
    spinBox->setValue(state_.staticTapOnSlideThresholdMs_);

    connect(slider, &QSlider::valueChanged, spinBox, &QSpinBox::setValue);
    connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);

    valueLayout->addWidget(slider, 1);
    valueLayout->addWidget(spinBox, 0);
    rootLayout->addWidget(valueRow);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int newThresholdMs = spinBox->value();
    if (newThresholdMs == state_.staticTapOnSlideThresholdMs_) {
        return;
    }

    state_.staticTapOnSlideThresholdMs_ = newThresholdMs;
    owner_.savePortableState();
    if (owner_.hasActiveDifficulty()) {
        if (!owner_.scheduleTimelineAnalysisRefreshFromLatestPreviewState()) {
            owner_.refreshTimelineMetadata();
        }
    } else {
        state_.muriStaticReferences_.clear();
        refreshMuriDiagnosticsPanel();
    }
    owner_.statusBar()->showMessage(
        UiText::text(QStringLiteral("validation.tap_on_slide_threshold_set")).arg(state_.staticTapOnSlideThresholdMs_));
}

void MainWindow::ValidationSection::applyMuriRenderOptions()
{
    state_.muriRenderOptions_.showSlideTracks = state_.showSlideTracks_;
    state_.muriRenderOptions_.showJudgeMarkers = state_.showJudgeMarkers_;
    state_.muriRenderOptions_.showTouchTrail = state_.showTouchTrail_;

    if (ui_.renderModeNativeAction_ != nullptr) {
        QSignalBlocker blocker(ui_.renderModeNativeAction_);
        ui_.renderModeNativeAction_->setChecked(state_.muriRenderOptions_.renderMode == RenderMode::Native);
        ui_.renderModeNativeAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, ui_.renderModeNativeAction_->isChecked())
        );
    }
    if (ui_.renderModeMaimuriDxAction_ != nullptr) {
        QSignalBlocker blocker(ui_.renderModeMaimuriDxAction_);
        ui_.renderModeMaimuriDxAction_->setChecked(state_.muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle);
        ui_.renderModeMaimuriDxAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, ui_.renderModeMaimuriDxAction_->isChecked())
        );
    }
    if (ui_.renderModeEraseByAreaAction_ != nullptr) {
        QSignalBlocker blocker(ui_.renderModeEraseByAreaAction_);
        ui_.renderModeEraseByAreaAction_->setChecked(
            state_.muriRenderOptions_.renderMode == RenderMode::EraseByArea);
        ui_.renderModeEraseByAreaAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, ui_.renderModeEraseByAreaAction_->isChecked())
        );
    }
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setShowSlideTracks(state_.muriRenderOptions_.showSlideTracks);
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setMuriRenderOptions(state_.muriRenderOptions_);
    }
    applyAlignedMuriAnalysisReportToViews();
}

void MainWindow::ValidationSection::setMuriRenderMode(RenderMode mode, bool persistState)
{
    if (state_.muriRenderOptions_.renderMode == mode && !persistState) {
        applyMuriRenderOptions();
        return;
    }
    state_.muriRenderOptions_.renderMode = mode;
    applyMuriRenderOptions();
    if (persistState) {
        owner_.savePortableState();
    }
    if (owner_.hasActiveDifficulty() && !owner_.scheduleTimelineAnalysisRefreshFromLatestPreviewState()) {
        owner_.refreshTimelineMetadata();
    }
    QString modeMessageKey = QStringLiteral("status.muri_render_mode_native");
    if (mode == RenderMode::MaimuriDxStyle) {
        modeMessageKey = QStringLiteral("status.muri_render_mode_dx");
    } else if (mode == RenderMode::EraseByArea) {
        modeMessageKey = QStringLiteral("status.muri_render_mode_erase_by_area");
    }
    owner_.statusBar()->showMessage(UiText::text(modeMessageKey));
}

const MuriAnalysisReport& MainWindow::alignedMuriAnalysisReportForPreview() const
{
    return validationSection_->alignedMuriAnalysisReportForPreview();
}

void MainWindow::applyAlignedMuriAnalysisReportToViews()
{
    validationSection_->applyAlignedMuriAnalysisReportToViews();
}

void MainWindow::onToggleJudgeMarkers(bool checked)
{
    validationSection_->onToggleJudgeMarkers(checked);
}

void MainWindow::onToggleTouchTrail(bool checked)
{
    validationSection_->onToggleTouchTrail(checked);
}

void MainWindow::onEditStaticTapOnSlideThreshold()
{
    validationSection_->onEditStaticTapOnSlideThreshold();
}

void MainWindow::applyMuriRenderOptions()
{
    validationSection_->applyMuriRenderOptions();
}

void MainWindow::setMuriRenderMode(RenderMode mode, bool persistState)
{
    validationSection_->setMuriRenderMode(mode, persistState);
}
