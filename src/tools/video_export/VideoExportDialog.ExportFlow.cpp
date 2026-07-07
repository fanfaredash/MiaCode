#include "VideoExportDialog.h"

#include "BusySpinner.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewInteractionConfig.h"
#include "common/UiHangWatchdog.h"
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
#include <QElapsedTimer>
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

namespace {

enum class VideoExportShortcutAction {
    None,
    TogglePlayPause,
    StopOrPlay,
};

VideoExportShortcutAction matchVideoExportShortcut(const QKeyEvent* event)
{
    if (event == nullptr) {
        return VideoExportShortcutAction::None;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers == Qt::NoModifier && event->key() == Qt::Key_Space) {
        return VideoExportShortcutAction::TogglePlayPause;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        return VideoExportShortcutAction::StopOrPlay;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_X) {
        return VideoExportShortcutAction::TogglePlayPause;
    }
    return VideoExportShortcutAction::None;
}

QString resolveOutputPathForExport(const QString& outputPath, const QString& baseDirectory)
{
    const QString trimmed = QDir::fromNativeSeparators(outputPath.trimmed());
    if (trimmed.isEmpty()) {
        return QString();
    }
    const QFileInfo outputInfo(trimmed);
    const QString absolutePath = outputInfo.isRelative()
        ? QDir(baseDirectory).absoluteFilePath(trimmed)
        : outputInfo.absoluteFilePath();
    return QDir::cleanPath(absolutePath);
}

QString videoExportWidgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("(null)");
    }
    return QStringLiteral("class=%1 name=%2 size=%3x%4 min=%5x%6 max=%7x%8 visible=%9")
        .arg(QString::fromUtf8(widget->metaObject()->className()))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(empty)") : widget->objectName())
        .arg(widget->width())
        .arg(widget->height())
        .arg(widget->minimumWidth())
        .arg(widget->minimumHeight())
        .arg(widget->maximumWidth())
        .arg(widget->maximumHeight())
        .arg(widget->isVisible() ? 1 : 0);
}

void appendEmbeddedDialogLayoutDiag(
    const QString& action,
    qint64 elapsedMs,
    const QString& detail = QString(),
    miacode::debug_log::Level level = miacode::debug_log::Level::Info)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    QString payload = QStringLiteral("action=%1 elapsed_ms=%2").arg(action).arg(elapsedMs);
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" %1").arg(detail.trimmed());
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("video_export/embedded_layout"),
        payload,
        /*force=*/false,
        level);
}

}  // namespace

void VideoExportDialog::setEmbeddedPanelMode(bool embedded)
{
    MC_OP("VideoExportDialog::setEmbeddedPanelMode");
    QElapsedTimer totalTimer;
    totalTimer.start();
    MIACODE_HANG_PHASE(
        "VideoExportDialog::setEmbeddedPanelMode",
        QStringLiteral("embedded=%1 dialog=%2")
            .arg(embedded ? 1 : 0)
            .arg(videoExportWidgetSummary(this)));
    if (embeddedPanelMode_ == embedded) {
        return;
    }
    embeddedPanelMode_ = embedded;
    if (!embedded) {
        return;
    }
    setModal(false);
    // CRITICAL: QDialog carries Qt::Dialog (a Qt::Window flag) from its ctor,
    // and QLayout::addWidget only strips window flags when it has to
    // REPARENT — when the panel was constructed with the host as parent
    // already, the flag survives and the "embedded" panel pops up as a
    // top-level window while its layout slot stays behind as a phantom
    // (the 2026-06-11 导出页错位+弹窗 bug). Clear it explicitly.
    setWindowFlags(Qt::Widget);
    // Surface unification: the ctor applied exportDialogStyleSheet(), whose
    // `QDialog { background: windowAltBg }` rule keeps matching this widget by
    // class even after it is reparented into the page — so the whole video
    // sub-page painted a windowAltBg rectangle against the page's windowBg (the
    // "整块面板偏色" color seam). Re-paint THIS instance with the page background
    // via a higher-specificity ID selector so the embedded panel reads as one
    // continuous surface with the export page header (the modal dialog, which
    // keeps its own objectName-less stylesheet, is unaffected).
    setObjectName(QStringLiteral("EmbeddedVideoExportPanel"));
    setStyleSheet(styleSheet()
        + QStringLiteral("QDialog#EmbeddedVideoExportPanel { background: %1; }")
              .arg(UiTheme::colors().windowBg.name(QColor::HexRgb)));
    // The ctor's refreshDialogGeometry() already locked the dialog height —
    // clear the locks so the host layout owns sizing from here on. The
    // 560px dialog minimum width is also dropped: the quick-shell workspace
    // surface can be ~700 logical px total, so the embedded panel must be
    // allowed to compress to its real layout minimum instead of forcing a
    // horizontal scrollbar onto the page.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumWidth(0);
    // Vertical Expanding: the host page hands the panel ALL remaining height
    // (fixed header above, nothing below) and the tab area absorbs it.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    if (cancelButton_ != nullptr) {
        cancelButton_->hide();
    }
    if (exportButton_ != nullptr) {
        exportButton_->setText(UiText::text(QStringLiteral("video_export.start_export")));
    }

    // ---- Fixed-frame page layout (2026-06-12 redesign) ----
    // The page never scrolls as a whole: the tab bar and the Start-Export
    // footer stay put; only a tab's own content scrolls (vertically) when the
    // viewport is genuinely too short. Horizontal scrolling is forbidden —
    // content must compress into the available width.

    // The in-panel transport strip is gone: the preview-area transport on the
    // right is the single seek/play surface; the range tab mirrors its clock.
    if (previewStrip_ != nullptr) {
        previewStrip_->hide();
    }

    // The 片头 live preview is sacrificed in the embedded page (product
    // decision 2026-06-12): it was the tallest tab content by far, and the
    // page must fit the viewport without scrolling at default window sizes.
    if (introPreview_ != nullptr) {
        QWidget* previewColumn = introPreview_->parentWidget();
        introPreview_ = nullptr;
        delete previewColumn;
    }

    // Re-host every tab page inside a vertical-only scroll viewport. The
    // ctor's modal-path refreshDialogGeometry() floored each page to the
    // tallest page — undo that first; pages now keep natural height and the
    // scroll area is only a too-short-window fallback.
    if (settingsTabs_ != nullptr) {
        QElapsedTimer rehostTimer;
        rehostTimer.start();
        MIACODE_HANG_PHASE(
            "VideoExportDialog::setEmbeddedPanelMode.rehostTabs",
            QStringLiteral("tab_count=%1 tabs=%2")
                .arg(settingsTabs_->count())
                .arg(videoExportWidgetSummary(settingsTabs_)));
        const int currentIndex = settingsTabs_->currentIndex();
        QStringList tabLabels;
        QList<QWidget*> tabPages;
        while (settingsTabs_->count() > 0) {
            tabLabels.append(settingsTabs_->tabText(0));
            tabPages.append(settingsTabs_->widget(0));
            settingsTabs_->removeTab(0);
        }
        for (int i = 0; i < tabPages.size(); ++i) {
            QWidget* page = tabPages.at(i);
            page->setMinimumHeight(0);
            page->setAutoFillBackground(false);
            auto* pageScroll = new QScrollArea(settingsTabs_);
            pageScroll->setObjectName(QStringLiteral("EmbeddedExportTabScroll"));
            pageScroll->setWidgetResizable(true);
            pageScroll->setFrameShape(QFrame::NoFrame);
            pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            pageScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            pageScroll->viewport()->setAutoFillBackground(false);
            pageScroll->setWidget(page);
            settingsTabs_->addTab(pageScroll, tabLabels.at(i));
            appendEmbeddedDialogLayoutDiag(
                QStringLiteral("embedded_tab_wrapped"),
                rehostTimer.elapsed(),
                QStringLiteral("index=%1 label=\"%2\" page=\"%3\" scroll=\"%4\"")
                    .arg(i)
                    .arg(tabLabels.at(i))
                    .arg(videoExportWidgetSummary(page))
                    .arg(videoExportWidgetSummary(pageScroll)));
        }
        settingsTabs_->setCurrentIndex(qMax(0, currentIndex));
        settingsTabs_->setStyleSheet(UiTheme::embeddedExportTabStyleSheet());
        appendEmbeddedDialogLayoutDiag(
            rehostTimer.elapsed() >= 80
                ? QStringLiteral("embedded_tabs_rehost_slow")
                : QStringLiteral("embedded_tabs_rehost_complete"),
            rehostTimer.elapsed(),
            QStringLiteral("tab_count=%1 current_index=%2 tabs=\"%3\"")
                .arg(settingsTabs_->count())
                .arg(settingsTabs_->currentIndex())
                .arg(videoExportWidgetSummary(settingsTabs_)),
            rehostTimer.elapsed() >= 80
                ? miacode::debug_log::Level::Warn
                : miacode::debug_log::Level::Info);
    }

    if (auto* root = qobject_cast<QVBoxLayout*>(layout()); root != nullptr) {
        if (settingsTabs_ != nullptr) {
            root->setStretchFactor(settingsTabs_, 1);
        }
        // Hairline above the pinned footer. Plain styled QWidget — QFrame::HLine
        // is a known-rejected divider (its content rect collapses under QSS).
        if (buttonBox_ != nullptr) {
            auto* footerRule = new QWidget(this);
            footerRule->setObjectName(QStringLiteral("EmbeddedExportFooterRule"));
            footerRule->setAttribute(Qt::WA_StyledBackground, true);
            footerRule->setFixedHeight(1);
            footerRule->setStyleSheet(QStringLiteral("background: %1;")
                .arg(UiTheme::colors().border.name(QColor::HexRgb)));
            root->insertWidget(root->indexOf(buttonBox_), footerRule);
        }
    }

    // With no in-panel transport the tick timer doubles as the clock mirror
    // for the range tab's current-time readout — keep it running for the
    // panel's whole life (see onRangePreviewTick's embedded branch).
    if (previewTimer_ != nullptr) {
        previewTimer_->start();
    }

    {
        QElapsedTimer refreshTimer;
        refreshTimer.start();
        MIACODE_HANG_PHASE(
            "VideoExportDialog::setEmbeddedPanelMode.refreshDialogGeometry",
            videoExportWidgetSummary(this));
        refreshDialogGeometry();
        if (refreshTimer.elapsed() >= 50) {
            appendEmbeddedDialogLayoutDiag(
                QStringLiteral("embedded_refresh_geometry_slow"),
                refreshTimer.elapsed(),
                QStringLiteral("dialog=\"%1\"").arg(videoExportWidgetSummary(this)),
                miacode::debug_log::Level::Warn);
        }
    }
    appendEmbeddedDialogLayoutDiag(
        totalTimer.elapsed() >= 120
            ? QStringLiteral("embedded_panel_mode_slow")
            : QStringLiteral("embedded_panel_mode_complete"),
        totalTimer.elapsed(),
        QStringLiteral("dialog=\"%1\" tabs=\"%2\"")
            .arg(videoExportWidgetSummary(this))
            .arg(videoExportWidgetSummary(settingsTabs_)),
        totalTimer.elapsed() >= 120
            ? miacode::debug_log::Level::Warn
            : miacode::debug_log::Level::Info);
}

void VideoExportDialog::setEmbeddedExportRunning(bool running)
{
    embeddedExportRunning_ = running;
    if (!embeddedPanelMode_ || exportButton_ == nullptr) {
        return;
    }
    exportButton_->setText(running
        ? UiText::text(QStringLiteral("video_export.cancel_export"))
        : UiText::text(QStringLiteral("video_export.start_export")));
}

void VideoExportDialog::finalizeEmbeddedSession()
{
    stopRangePreview(false);
    restoreLivePreviewState();
}

void VideoExportDialog::applyThemeStyles()
{
    // Mirror every baked stylesheet/icon site in buildUi(). The dialog's own
    // sheet re-themes most children by QSS type-selector cascade; the rest set
    // their own literal stylesheet (or an accent-/icon-tinted QIcon) and must be
    // re-applied by hand.
    // In embedded mode setEmbeddedPanelMode() appended a higher-specificity
    // `QDialog#EmbeddedVideoExportPanel { background: windowBg }` override so the
    // panel reads as one continuous surface with the export-page header. A bare
    // setStyleSheet(exportDialogStyleSheet()) would DROP that override and leave
    // the panel painting the baked startup-theme color — re-append it here.
    QString sheet = UiTheme::exportDialogStyleSheet();
    if (embeddedPanelMode_) {
        sheet += QStringLiteral("QDialog#EmbeddedVideoExportPanel { background: %1; }")
                     .arg(UiTheme::colors().windowBg.name(QColor::HexRgb));
    }
    setStyleSheet(sheet);

    // Dropdown menu buttons (createDialogMenuButton).
    for (QToolButton* button : {resolutionButton_, fpsButton_, audioBitrateButton_,
                                presetButton_, backgroundScaleModeButton_}) {
        if (button != nullptr) {
            button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        }
    }

    // Plain push buttons.
    for (QPushButton* button : {outputBrowseButton_, setStartButton_, setEndButton_, cancelButton_}) {
        if (button != nullptr) {
            button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
    }
    if (introBackgroundBrowse_ != nullptr) {
        introBackgroundBrowse_->setStyleSheet(UiTheme::dialogAuxiliaryButtonStyleSheet());
    }
    if (introBackgroundPathEdit_ != nullptr) {
        introBackgroundPathEdit_->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet(UiTheme::colors().windowAltBg));
    }
    if (introBackgroundCombo_ != nullptr) {
        UiTheme::styleDialogComboBox(introBackgroundCombo_, 12);
    }
    if (introCardModeCombo_ != nullptr) {
        UiTheme::styleDialogComboBox(introCardModeCombo_, 12);
    }
    if (exportButton_ != nullptr) {
        exportButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    }

    if (rangeTrack_ != nullptr) {
        rangeTrack_->update();
    }
    if (rangeSummaryLabel_ != nullptr) {
        rangeSummaryLabel_->setStyleSheet(
            QStringLiteral("color: %1;").arg(UiTheme::colors().textMuted.name(QColor::HexRgb)));
    }

    // Sliders: the scrubber uses the form style; the visuals sliders the dialog
    // style.
    if (previewSlider_ != nullptr) {
        previewSlider_->setStyleSheet(UiTheme::formSliderStyleSheet());
    }
    for (QSlider* slider : {brightnessOuterSlider_, brightnessInnerSlider_, layoutSquareScaleSlider_}) {
        if (slider != nullptr) {
            slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        }
    }

    // Transport controls: stylesheet + accent/icon-tinted QIcon.
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
        stopPreviewButton_->setIcon(makePreviewStopIcon(UiTheme::colors().iconPrimary));
    }
    // Re-renders previewRangeButton_'s play/pause icon + stylesheet for the
    // current state and theme.
    updatePreviewPlayPauseUi();

    // Embedded panel chrome: flat-underline tab strip + the footer hairline,
    // both of which bake colors at construction.
    if (embeddedPanelMode_) {
        if (settingsTabs_ != nullptr) {
            settingsTabs_->setStyleSheet(UiTheme::embeddedExportTabStyleSheet());
        }
        if (auto* footerRule = findChild<QWidget*>(QStringLiteral("EmbeddedExportFooterRule"));
            footerRule != nullptr) {
            footerRule->setStyleSheet(
                QStringLiteral("background: %1;").arg(UiTheme::colors().border.name(QColor::HexRgb)));
        }
    }
}

void VideoExportDialog::injectOwnerWiredSettings(
    QWidget* videoExtras,
    QWidget* gameplayWidget,
    QWidget* skinWidget,
    OwnerWiredSettingsRefreshCallback refreshCallback)
{
    // The injected widgets are built by MainWindow (they need owner-side data
    // + wiring the decoupled dialog can't reach). Drop them in just before each
    // page's trailing stretch so they hug the existing controls.
    if (videoExtras != nullptr && visualsPageLayout_ != nullptr) {
        const int insertIndex = qMax(0, visualsPageLayout_->count() - 1);
        visualsPageLayout_->insertWidget(insertIndex, videoExtras, 0, Qt::AlignTop);
    }
    if (gameplayWidget != nullptr && gameplayPageLayout_ != nullptr) {
        const int insertIndex = qMax(0, gameplayPageLayout_->count() - 1);
        gameplayPageLayout_->insertWidget(insertIndex, gameplayWidget, 0, Qt::AlignTop);
    }
    if (skinWidget != nullptr && skinPageLayout_ != nullptr) {
        const int insertIndex = qMax(0, skinPageLayout_->count() - 1);
        skinPageLayout_->insertWidget(insertIndex, skinWidget, 0, Qt::AlignTop);
    }
    ownerWiredSettingsRefreshCallback_ = std::move(refreshCallback);
    refreshSharedSettingsFromCallback();
    refreshDialogGeometry();
}

void VideoExportDialog::refreshDialogGeometry()
{
    // Embedded panel mode: the host page owns the outer geometry — the tab
    // area stretches to fill it and each page lives in its own vertical-only
    // scroll viewport (see setEmbeddedPanelMode). No equal-height flooring,
    // no window-only height lock / resize below.
    if (embeddedPanelMode_) {
        updateGeometry();
        return;
    }

    // Clear any prior height lock first so the content is measured freely.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);

    // Size the tab content area to the TALLEST tab, not the current one, so no
    // tab's bottom rows get clipped. Measure each page's natural content height
    // (reset its floor + activate its layout first), take the max, then floor
    // every page to it (each page ends with a stretch, so shorter tabs just gain
    // trailing slack — nothing overlaps).
    int maxPageHeight = 0;
    if (settingsTabs_ != nullptr) {
        settingsTabs_->setMinimumHeight(0);
        for (int i = 0; i < settingsTabs_->count(); ++i) {
            QWidget* page = settingsTabs_->widget(i);
            if (page == nullptr) {
                continue;
            }
            page->setMinimumHeight(0);
            if (QLayout* pageLayout = page->layout()) {
                pageLayout->invalidate();
                pageLayout->activate();
            }
            maxPageHeight = qMax(maxPageHeight, page->sizeHint().height());
        }
        for (int i = 0; i < settingsTabs_->count(); ++i) {
            if (QWidget* page = settingsTabs_->widget(i)) {
                page->setMinimumHeight(maxPageHeight);
            }
        }
        settingsTabs_->updateGeometry();
    }

    if (QLayout* l = layout()) {
        l->invalidate();
        l->activate();
    }
    adjustSize();
    // Flush any deferred LayoutRequest on the tab widget so its internal stacked
    // pane geometry is current before we read it for the chrome calculation
    // below (otherwise the first call would measure a stale/zero pane height).
    QCoreApplication::sendPostedEvents(settingsTabs_, QEvent::LayoutRequest);
    const int targetWidth = qMax(minimumWidth(), width());
    int targetHeight = sizeHint().height();

    // sizeHint() under-reports here: QStyleSheetStyle does NOT fold the styled
    // QTabWidget::pane padding (the 14px export override = 28px vertical) into
    // the tab widget's sizeHint, so a sizeHint-locked dialog leaves the tallest
    // tab clipped by that padding. Once the dialog is realized, the LIVE
    // geometry of the internal stacked pane tells us the true chrome, so size
    // from that instead: make the pane exactly tall enough for the tallest page.
    //   targetHeight = maxPage + tabChrome + nonTabHeight
    // where tabChrome = settingsTabs - stack, nonTabHeight = dialog - settingsTabs.
    // Both terms are invariant under our own resize, so this is idempotent (a
    // second pass computes the same height rather than collapsing back).
    if (settingsTabs_ != nullptr && maxPageHeight > 0) {
        if (QStackedWidget* stack = settingsTabs_->findChild<QStackedWidget*>()) {
            if (stack->height() > 0 && settingsTabs_->height() > 0) {
                const int tabChrome = qMax(0, settingsTabs_->height() - stack->height());
                const int nonTabHeight = qMax(0, height() - settingsTabs_->height());
                targetHeight = qMax(targetHeight, maxPageHeight + tabChrome + nonTabHeight);
            }
        }
    }

    setMinimumHeight(targetHeight);
    setMaximumHeight(targetHeight);
    resize(targetWidth, targetHeight);
}

bool VideoExportDialog::stepPreviewSliderBySeconds(double deltaSeconds)
{
    if (previewSlider_ == nullptr || !qIsFinite(deltaSeconds)) {
        return false;
    }
    const int deltaMs = qRound(deltaSeconds * 1000.0);
    if (deltaMs == 0) {
        return false;
    }
    const int value = qBound(
        previewSlider_->minimum(),
        previewSlider_->value() + deltaMs,
        previewSlider_->maximum()
    );
    if (value == previewSlider_->value()) {
        return true;
    }
    previewSlider_->setValue(value);
    return true;
}

bool VideoExportDialog::handlePreviewSliderWheel(QWheelEvent* event)
{
    if (previewSlider_ == nullptr || event == nullptr) {
        return false;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        delta = event->pixelDelta().x();
    }
    if (delta == 0) {
        return false;
    }
    const int steps = delta > 0 ? qMax(1, qRound(static_cast<double>(delta) / 120.0))
                                : qMin(-1, qRound(static_cast<double>(delta) / 120.0));
    previewSlider_->setFocus(Qt::MouseFocusReason);
    const bool handled = stepPreviewSliderBySeconds(
        static_cast<double>(steps) * miacode::preview_interaction::kSeekSingleStepSeconds
    );
    if (handled) {
        event->accept();
    }
    return handled;
}

void VideoExportDialog::beginPreviewHeldSeek(int direction, int key)
{
    if (direction == 0 || previewSlider_ == nullptr) {
        return;
    }
    previewHeldSeekDirection_ = direction > 0 ? 1 : -1;
    previewSeekHeldArrowKey_ = key;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.restart();
    if (previewHeldSeekTimer_ != nullptr && !previewHeldSeekTimer_->isActive()) {
        previewHeldSeekTimer_->start();
    }
}

void VideoExportDialog::stopPreviewHeldSeek(int key)
{
    if (key != 0 && previewSeekHeldArrowKey_ != key) {
        return;
    }
    previewHeldSeekDirection_ = 0;
    previewSeekHeldArrowKey_ = 0;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.invalidate();
    if (previewHeldSeekTimer_ != nullptr) {
        previewHeldSeekTimer_->stop();
    }
}

void VideoExportDialog::applyPreviewHeldSeekTick()
{
    if (previewHeldSeekDirection_ == 0
        || previewSeekHeldArrowKey_ == 0
        || !previewSeekHeldArrowElapsed_.isValid()) {
        return;
    }
    const int elapsedMs = static_cast<int>(previewSeekHeldArrowElapsed_.elapsed());
    const int deltaMs = previewSeekHeldArrowLastElapsedMs_ > 0
        ? (elapsedMs - previewSeekHeldArrowLastElapsedMs_)
        : miacode::preview_interaction::kSeekHoldTickIntervalMs;
    previewSeekHeldArrowLastElapsedMs_ = elapsedMs;
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    stepPreviewSliderBySeconds(
        static_cast<double>(previewHeldSeekDirection_)
            * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds)
    );
}

void VideoExportDialog::applySelectedAspectRatioToPreview(bool markChanged)
{
    const double aspectRatio = selectedResolutionAspectRatio();
    if (markChanged && qAbs(aspectRatio - initialResolutionAspectRatio_) > 0.0001) {
        previewAspectChangedByDialog_ = true;
    }
    if (previewAspectRatioCallback_) {
        previewAspectRatioCallback_(aspectRatio);
    }
    // The intro tab's read-only preview tracks the export aspect too.
    resizeIntroPreviewToAspect();
}

void VideoExportDialog::browseOutputPath()
{
    const QString baseDirectory = exportBaseDirectory(baseTask_);
    const QString initial = outputPathEdit_ != nullptr
        ? resolveOutputPathForExport(outputPathEdit_->text(), baseDirectory)
        : QString();
    const QString selected = QFileDialog::getSaveFileName(
        this,
        UiText::text(QStringLiteral("video_export.export_video")),
        initial,
        QStringLiteral("MP4 Video (*.mp4)")
    );
    if (selected.isEmpty() || outputPathEdit_ == nullptr) {
        return;
    }
    outputPathEdit_->setText(displayOutputPathForDialog(selected, baseDirectory));
}

bool VideoExportDialog::applyUiToTask(VideoExportTask* task, QString* errorMessage) const
{
    if (task == nullptr) {
        return false;
    }
    VideoExportTask updated = baseTask_;
    const QString baseDirectory = exportBaseDirectory(updated);
    const QString outputPath = outputPathEdit_ != nullptr ? outputPathEdit_->text().trimmed() : QString();
    if (outputPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("video_export.please_choose_an_output_path"));
        }
        return false;
    }
    updated.outputPath = resolveOutputPathForExport(outputPath, baseDirectory);
    const QSize selectedSize = selectedResolution();
    updated.outputWidth = selectedSize.width() > 0 ? selectedSize.width() : updated.outputWidth;
    updated.outputHeight = selectedSize.height() > 0 ? selectedSize.height() : updated.outputHeight;
    updated.fps = qMax(1, selectedFps_);
    updated.audioBitrateKbps = normaliseAudioBitrateKbps(selectedAudioBitrateKbps_);
    updated.preset = selectedPreset_;
    updated.showTimestamp = showTimestampCheck_ != nullptr ? showTimestampCheck_->isChecked() : true;
    updated.showObjectStatsHud = showObjectStatsCheck_ != nullptr ? showObjectStatsCheck_->isChecked() : false;
    updated.showChartInfoHud = showChartInfoCheck_ != nullptr ? showChartInfoCheck_->isChecked() : false;
    // clock_count count-in is opt-in. The VALUE stays = the chart's (already copied
    // via `updated = baseTask_`); only the on/off flag changes — so the label and
    // the document's &clock_count= are never affected.
    updated.clockCountEnabled = clockCountCheck_ != nullptr && clockCountCheck_->isChecked();
    updated.backgroundBrightnessOuter = brightnessOuterSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessOuterSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessOuter;
    updated.backgroundBrightnessInner = brightnessInnerSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessInnerSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessInner;
    updated.layoutSquareScale = layoutSquareScaleSlider_ != nullptr
        ? miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(layoutSquareScaleSlider_->value()) / 100.0)
        : updated.layoutSquareScale;
    updated.smoothBrightness = smoothBrightnessCheck_ != nullptr
        ? smoothBrightnessCheck_->isChecked()
        : updated.smoothBrightness;
    updated.backgroundScaleMode = selectedBackgroundScaleMode_;
    double exportTapFlowSpeed = selectedTapFlowSpeed_;
    if (tapFlowSpeedEdit_ != nullptr) {
        bool ok = false;
        const double typedSpeed = tapFlowSpeedEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            exportTapFlowSpeed = typedSpeed;
        }
    }
    double exportTouchFlowSpeed = selectedTouchFlowSpeed_;
    if (touchFlowSpeedEdit_ != nullptr) {
        bool ok = false;
        const double typedSpeed = touchFlowSpeedEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            exportTouchFlowSpeed = typedSpeed;
        }
    }
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    updated.tapFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((exportTapFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    updated.touchFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((exportTouchFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    const double selectedRangeStart = rangeStartSeconds();
    const double selectedRangeEnd = rangeEndSeconds();
    updated.exportStartSeconds = selectedRangeStart;
    updated.contentDurationSeconds = qMax(0.0, selectedRangeEnd - updated.exportStartSeconds);
    // A range that STARTS at chart 0 behaves exactly like a full export
    // regardless of where it ends: the deliverable begins with the chart's
    // real opening (2 s count-down lead-in, BGM from source 0, clock count),
    // so the partial-range frozen preload + pause glyph would be wrong
    // there. Only a non-zero start needs the freeze-then-go pre-roll.
    // The epsilon tolerates spinbox / floating-point slop (the dialog
    // displays at 1 ms precision).
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    updated.fullRangeExport = selectedRangeStart <= kFullRangeEpsilonSeconds;
    // The maimai intro is a full-range-only pre-roll; clips starting
    // mid-chart never get it regardless of the checkbox. (A clip starting
    // at chart 0 counts as full-range, so it may carry the intro.)
    // "片头" tab styling (background + difficulty card) — shared with the
    // read-only preview via currentIntroSpec, so preview == export. `enabled`
    // is recomputed below from the checkbox + range.
    updated.intro = currentIntroSpec();
    updated.intro.enabled =
        (addIntroCheck_ != nullptr && addIntroCheck_->isChecked())
        && updated.fullRangeExport;
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Export,
        QStringLiteral("dialog_range_decision"),
        QStringLiteral("rangeStart=%1 rangeEnd=%2 totalDuration=%3 fullRangeExport=%4 epsilon=%5")
            .arg(selectedRangeStart, 0, 'f', 9)
            .arg(selectedRangeEnd, 0, 'f', 9)
            .arg(totalDurationSeconds_, 0, 'f', 9)
            .arg(updated.fullRangeExport ? 1 : 0)
            .arg(kFullRangeEpsilonSeconds, 0, 'f', 6));

    const QFileInfo outputInfo(updated.outputPath);
    const QDir outputDir = outputInfo.absoluteDir();
    if (!outputDir.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("video_export.output_directory_does_not_exist"));
        }
        return false;
    }
    if (updated.outputWidth <= 0 || updated.outputHeight <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("video_export.resolution_is_invalid"));
        }
        return false;
    }
    if (updated.contentDurationSeconds <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("video_export.export_range_is_empty"));
        }
        return false;
    }
    if (updated.exportStartSeconds < 0.0 || updated.exportStartSeconds > totalDurationSeconds_ + 1e-6) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("video_export.export_start_is_out_of"));
        }
        return false;
    }
    *task = updated;
    return true;
}

QSize VideoExportDialog::selectedResolution() const
{
    if (selectedResolution_.width() <= 0 || selectedResolution_.height() <= 0) {
        return QSize(qMax(1, baseTask_.outputWidth), qMax(1, baseTask_.outputHeight));
    }
    return selectedResolution_;
}

double VideoExportDialog::selectedResolutionAspectRatio() const
{
    const QSize size = selectedResolution();
    if (size.width() <= 0 || size.height() <= 0) {
        return 1.0;
    }
    return static_cast<double>(size.width()) / static_cast<double>(size.height());
}

void VideoExportDialog::onExportButtonClicked()
{
    // Embedded panel: while a worker run launched from here is active, the
    // export button doubles as the cancel affordance.
    if (embeddedPanelMode_ && embeddedExportRunning_) {
        emit exportCancelRequested();
        return;
    }
    startExport();
}

void VideoExportDialog::startExport()
{
    stopRangePreview(false);
    refreshSharedSettingsFromCallback();

    VideoExportTask task;
    QString errorMessage;
    if (!applyUiToTask(&task, &errorMessage)) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            UiText::text(QStringLiteral("dialog.video_export.title")),
            errorMessage
        );
        return;
    }
    savePersistedSettings(task);
    requestedExportTask_ = task;
    exportRequested_ = true;
    if (embeddedPanelMode_) {
        // The panel stays open while the host launches the worker (progress
        // shows on the preview-area transport, A3 as amended 2026-06-11).
        emit exportConfirmed();
        return;
    }
    accept();
}

void VideoExportDialog::closeEvent(QCloseEvent* event)
{
    stopRangePreview(false);
    restoreLivePreviewState();
    QDialog::closeEvent(event);
}

void VideoExportDialog::done(int result)
{
    // Embedded panel: there is no window to close — Esc (QDialog's default
    // reject) and programmatic done() must NOT hide the panel or restore the
    // live-preview state mid-session. Teardown happens explicitly via
    // finalizeEmbeddedSession() when the host leaves the video sub-page.
    if (embeddedPanelMode_) {
        return;
    }
    stopRangePreview(false);
    restoreLivePreviewState();
    QDialog::done(result);
}

bool VideoExportDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && UiDialogs::dialogOwnsPreviewShortcutScope(this)) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (matchVideoExportShortcut(keyEvent) != VideoExportShortcutAction::None) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const VideoExportShortcutAction action = matchVideoExportShortcut(keyEvent);
            if (action != VideoExportShortcutAction::None) {
                if (keyEvent->isAutoRepeat()) {
                    event->accept();
                    return true;
                }
                switch (action) {
                case VideoExportShortcutAction::TogglePlayPause:
                    handlePreviewPlayPauseShortcut();
                    break;
                case VideoExportShortcutAction::StopOrPlay:
                    handlePreviewStopOrPlayShortcut();
                    break;
                case VideoExportShortcutAction::None:
                    break;
                }
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (matchVideoExportShortcut(keyEvent) != VideoExportShortcutAction::None) {
                event->accept();
                return true;
            }
        }
    }
    if (previewSlider_ != nullptr && watched == previewSlider_) {
        if (event->type() == QEvent::Wheel) {
            stopPreviewHeldSeek();
            if (handlePreviewSliderWheel(static_cast<QWheelEvent*>(event))) {
                return true;
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (keyEvent->modifiers() != Qt::NoModifier) {
                    return QDialog::eventFilter(watched, event);
                }
                if (keyEvent->isAutoRepeat()) {
                    return true;
                }
                beginPreviewHeldSeek(direction, keyEvent->key());
                stepPreviewSliderBySeconds(
                    static_cast<double>(direction) * miacode::preview_interaction::kSeekSingleStepSeconds
                );
                return true;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && previewSeekHeldArrowKey_ == keyEvent->key()) {
                stopPreviewHeldSeek(keyEvent->key());
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}
