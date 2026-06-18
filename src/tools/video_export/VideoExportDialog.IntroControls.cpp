#include "VideoExportDialog.h"

#include "BusySpinner.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/PreviewInteractionConfig.h"
#include "core/scene/PreviewHudState.h"
#include "tools/video_export/HudFontSettings.h"
#include "tools/video_export/IntroPreviewWidget.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleValidator>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QWidgetAction>

#include <limits>
#include <utility>

#include "VideoExportDialogInternal.h"

using namespace miacode::video_export::dialog_detail;

void VideoExportDialog::syncLivePreviewTimestampVisibility()
{
    if (showTimestampCheck_ == nullptr || !previewTimestampCallback_) {
        return;
    }
    previewTimestampCallback_(showTimestampCheck_->isChecked());
}

void VideoExportDialog::syncLivePreviewObjectStatsVisibility()
{
    if (showObjectStatsCheck_ == nullptr || !previewObjectStatsCallback_) {
        return;
    }
    previewObjectStatsCallback_(showObjectStatsCheck_->isChecked());
}

void VideoExportDialog::syncLivePreviewChartInfoVisibility()
{
    if (showChartInfoCheck_ == nullptr || !previewChartInfoCallback_) {
        return;
    }
    previewChartInfoCallback_(showChartInfoCheck_->isChecked());
}

void VideoExportDialog::restoreLivePreviewState()
{
    if (previewStateRestored_) {
        return;
    }
    previewStateRestored_ = true;
    if (previewTimestampCallback_) {
        previewTimestampCallback_(initialShowTimestamp_);
    }
    if (previewObjectStatsCallback_) {
        previewObjectStatsCallback_(initialShowObjectStats_);
    }
    if (previewChartInfoCallback_) {
        previewChartInfoCallback_(initialShowChartInfo_);
    }
    if (previewAspectRatioCallback_) {
        previewAspectRatioCallback_(1.0);
    }
}

void VideoExportDialog::openHudFontSettingsDialog()
{
    // Shared dialog (tools/video_export/HudFontSettings.cpp). The callback
    // re-renders this dialog's paused preview frame so the new HUD font shows
    // immediately.
    miacode::video_export::openHudFontSettingsDialog(this, [this]() {
        refreshLivePreviewHudFont();
    });
}

void VideoExportDialog::refreshLivePreviewHudFont()
{
    const double second = currentPreviewSecond();
    if (qIsFinite(second)) {
        seekPreview(second);
    } else {
        seekPreview(previewCursorSecond_);
    }
}

void VideoExportDialog::refreshAddIntroEnabledState()
{
    if (addIntroCheck_ == nullptr) {
        return;
    }
    // MUST mirror applyUiToTask's bake gate: any range STARTING at chart 0
    // counts as full-range and may carry the intro (see the comment there).
    // The old extra "end reaches the chart tail" condition froze a checked-
    // but-disabled box for [0, partial] clips whose state was STILL baked
    // into the export — misleading and uncontrollable.
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    addIntroCheck_->setEnabled(rangeStartSeconds() <= kFullRangeEpsilonSeconds);
    syncIntroControlsEnabled();
    // The host's negative-time intro audition is gated on isAddIntroActiveForPreview()
    // (== 添加片头 checked AND a full-range export). The 片头-tab controls already
    // notify the host via introUiChanged; the export RANGE is the OTHER input to
    // that gate, so when moving start on/off 0 flips it we must notify too —
    // otherwise the host never calls refreshExportIntroState() to tear the intro
    // region down, and the preview playhead gets stranded in negative time (stuck
    // thumb + dead play toggle). Emit only on an actual flip so a range drag does
    // not re-render the intro overlay on every spin tick.
    const bool introActiveForPreview = isAddIntroActiveForPreview();
    if (introActiveForPreview != introActiveForPreviewLast_) {
        introActiveForPreviewLast_ = introActiveForPreview;
        emit introPreviewSettingsChanged();
    }
}

void VideoExportDialog::syncIntroControlsEnabled()
{
    // Everything on the "片头" tab follows 添加片头 (which itself is greyed on a
    // partial range); the path row additionally follows the 背景 combo.
    const bool introOn = addIntroCheck_ != nullptr
        && addIntroCheck_->isEnabled() && addIntroCheck_->isChecked();
    const bool customBg = introBackgroundCombo_ != nullptr
        && introBackgroundCombo_->currentData().toString() == QStringLiteral("custom");
    if (introBackgroundCombo_ != nullptr) introBackgroundCombo_->setEnabled(introOn);
    if (introBackgroundPathEdit_ != nullptr) introBackgroundPathEdit_->setEnabled(introOn && customBg);
    if (introBackgroundBrowse_ != nullptr) introBackgroundBrowse_->setEnabled(introOn && customBg);
    if (introBlurCheck_ != nullptr) introBlurCheck_->setEnabled(introOn);
    if (introCardModeCombo_ != nullptr) introCardModeCombo_->setEnabled(introOn);
    if (introCardShadowCheck_ != nullptr) introCardShadowCheck_->setEnabled(introOn);
    if (introLevelTextCheck_ != nullptr) introLevelTextCheck_->setEnabled(introOn);
}

void VideoExportDialog::browseIntroBackground()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        l10n(QStringLiteral("Choose background image"), QStringLiteral("选择背景图片")),
        QString(),
        l10n(QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"),
             QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp)")));
    if (file.isEmpty()) {
        return;
    }
    if (introBackgroundCombo_ != nullptr) {
        const int customIndex = introBackgroundCombo_->findData(QStringLiteral("custom"));
        if (customIndex >= 0) {
            introBackgroundCombo_->setCurrentIndex(customIndex);   // fires introUiChanged
        }
    }
    if (introBackgroundPathEdit_ != nullptr) {
        introBackgroundPathEdit_->setText(file);
    }
    persistExportOnlySettings();
}

bool VideoExportDialog::isAddIntroActiveForPreview() const
{
    const bool checked = addIntroCheck_ != nullptr && addIntroCheck_->isChecked();
    // Full-range gate — mirror refreshAddIntroEnabledState's RANGE logic directly
    // rather than addIntroCheck_->isEnabled(). isEnabled() reads the widget's
    // *effective* enabled state, which the embedded tab-rehost can flip to false
    // even while 添加片头 stays checked; that was collapsing the intro region /
    // negative slider while the overlay lingered.
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    const bool fullRange = rangeStartSeconds() <= kFullRangeEpsilonSeconds;
    return checked && fullRange;
}

IntroBannerSpec VideoExportDialog::currentIntroSpec() const
{
    IntroBannerSpec spec = baseTask_.intro;   // chart payload: title/level/曲绘…
    if (introCardModeCombo_ != nullptr) {
        spec.mode = introCardModeCombo_->currentData().toString();
    }
    if (introLevelTextCheck_ != nullptr) {
        spec.lvRenderMode = introLevelTextCheck_->isChecked()
            ? QStringLiteral("text")
            : QStringLiteral("atlas");
    }
    if (introBackgroundCombo_ != nullptr) {
        spec.backgroundMode = introBackgroundCombo_->currentData().toString();
    }
    if (introBackgroundPathEdit_ != nullptr) {
        spec.customBackgroundPath = introBackgroundPathEdit_->text().trimmed();
    }
    if (introBlurCheck_ != nullptr) {
        spec.blurBackground = introBlurCheck_->isChecked();
    }
    if (introCardShadowCheck_ != nullptr) {
        spec.cardShadow = introCardShadowCheck_->isChecked();
    }
    return spec;
}

void VideoExportDialog::refreshIntroPreview()
{
    if (introPreview_ != nullptr) {
        introPreview_->applySpec(currentIntroSpec());
    }
}

void VideoExportDialog::resizeIntroPreviewToAspect()
{
    // The preview widget is a FIXED box (the dialog never resizes when the
    // resolution changes); only the letterboxed frame inside it follows the
    // output aspect. Reuses the same ratio source as the main live preview.
    if (introPreview_ != nullptr) {
        introPreview_->setOutputAspectRatio(selectedResolutionAspectRatio());
    }
}
