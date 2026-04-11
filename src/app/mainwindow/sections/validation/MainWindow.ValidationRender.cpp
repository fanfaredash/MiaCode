#include "MainWindow.ValidationSection.h"
#include "../../MainWindowShared.h"

#include "DialogLocalization.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "preview/runtime/PreviewRuntime.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

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
    const MuriAnalysisReport& alignedReport = alignedMuriAnalysisReportForPreview();
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->setMuriAnalysisReport(alignedReport);
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setMuriAnalysisReport(alignedReport);
    }
}

void MainWindow::ValidationSection::onToggleJudgeMarkers(bool checked)
{
    state_.showJudgeMarkers_ = checked;
    applyMuriRenderOptions();
    owner_.savePortableState();
    owner_.saveProjectRenderState();
    owner_.statusBar()->showMessage(
        state_.showJudgeMarkers_
            ? uiText("status.judge_marker_enabled", "Judge markers enabled.")
            : uiText("status.judge_marker_disabled", "Judge markers hidden.")
    );
}

void MainWindow::ValidationSection::onToggleTouchTrail(bool checked)
{
    state_.showTouchTrail_ = checked;
    applyMuriRenderOptions();
    owner_.savePortableState();
    owner_.saveProjectRenderState();
    owner_.statusBar()->showMessage(
        state_.showTouchTrail_
            ? uiText("status.touch_trail_enabled", "Touch trail enabled.")
            : uiText("status.touch_trail_disabled", "Touch trail hidden.")
    );
}

void MainWindow::ValidationSection::onEditStaticTapOnSlideThreshold()
{
    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(UiText::isChineseUi() ? QStringLiteral("撞尾阈值") : QStringLiteral("Tap-On-Slide Threshold"));
    dialog.setModal(true);
    dialog.setMinimumWidth(360);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());
    owner_.applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(10);

    auto* hintLabel = new QLabel(
        UiText::isChineseUi()
            ? QStringLiteral("调整静态“撞尾无理”参考检查阈值。")
            : QStringLiteral("Adjust the static Tap-On-Slide reference threshold."),
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
        UiText::isChineseUi()
            ? QStringLiteral("撞尾阈值已更新为 %1 ms。").arg(state_.staticTapOnSlideThresholdMs_)
            : QStringLiteral("Tap-On-Slide threshold set to %1 ms.").arg(state_.staticTapOnSlideThresholdMs_));
}

void MainWindow::ValidationSection::applyMuriRenderOptions()
{
    state_.showJudgeMarkers_ = false;
    state_.showTouchTrail_ = false;
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
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->setShowSlideTracks(state_.muriRenderOptions_.showSlideTracks);
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
        owner_.saveProjectRenderState();
    }
    if (owner_.hasActiveDifficulty() && !owner_.scheduleTimelineAnalysisRefreshFromLatestPreviewState()) {
        owner_.refreshTimelineMetadata();
    }
    owner_.statusBar()->showMessage(
        mode == RenderMode::MaimuriDxStyle
            ? uiText("status.muri_render_mode_dx", "Preview mode: muri check.")
            : uiText("status.muri_render_mode_native", "Preview mode: chart review.")
    );
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
