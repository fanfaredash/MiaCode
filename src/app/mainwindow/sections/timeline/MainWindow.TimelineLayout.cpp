#include "MainWindow.TimelineSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/latency/LatencyDetectorDialog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

QString workspaceSwapPreviewPanelStyleSheet(bool swapped)
{
    QString style = UiTheme::previewPanelStyleSheet();
    if (swapped) {
        style.replace(QStringLiteral("border-left: 1px solid"), QStringLiteral("border-right: 1px solid"));
    }
    return style;
}

void updatePreviewControlsLayout(
    QHBoxLayout* previewControlsLayout,
    QToolButton* stopPreviewButton,
    QToolButton* pausePreviewButton,
    QSlider* previewSlider,
    QToolButton* previewSpeedButton,
    QToolButton* previewFullscreenButton,
    bool swapped)
{
    if (previewControlsLayout == nullptr
        || stopPreviewButton == nullptr
        || pausePreviewButton == nullptr
        || previewSlider == nullptr
        || previewSpeedButton == nullptr
        || previewFullscreenButton == nullptr) {
        return;
    }

    previewControlsLayout->removeWidget(stopPreviewButton);
    previewControlsLayout->removeWidget(pausePreviewButton);
    previewControlsLayout->removeWidget(previewSlider);
    previewControlsLayout->removeWidget(previewSpeedButton);
    previewControlsLayout->removeWidget(previewFullscreenButton);

    if (swapped) {
        previewControlsLayout->addWidget(previewSpeedButton, 0);
        previewControlsLayout->addWidget(previewFullscreenButton, 0);
        previewControlsLayout->addWidget(previewSlider, 1);
        previewControlsLayout->addWidget(stopPreviewButton, 0);
        previewControlsLayout->addWidget(pausePreviewButton, 0);
    } else {
        previewControlsLayout->addWidget(stopPreviewButton, 0);
        previewControlsLayout->addWidget(pausePreviewButton, 0);
        previewControlsLayout->addWidget(previewSlider, 1);
        previewControlsLayout->addWidget(previewSpeedButton, 0);
        previewControlsLayout->addWidget(previewFullscreenButton, 0);
    }
}

}  // namespace

double MainWindow::TimelineSection::previewDurationSeconds() const
{
    double duration = 0.0;
    if (state_.qtPreviewPlaying_ && state_.qtPreviewPlaybackEndSecond_ > 0.0) {
        duration = qMax(duration, state_.qtPreviewPlaybackEndSecond_);
    }
    if (state_.timelineQuickStateBridge_ != nullptr) {
        duration = qMax(duration, state_.timelineQuickStateBridge_->durationSeconds());
        duration = qMax(duration, state_.timelineQuickStateBridge_->playheadSeconds());
        duration = qMax(duration, state_.timelineQuickStateBridge_->playbackEntrySeconds());
    }
    duration = qMax(duration, qMax(0.0, state_.qtPreviewPauseSecond_));
    if (state_.previewTrackDurationSeconds_ > 0.0) {
        duration = qMax(duration, state_.previewTrackDurationSeconds_ + 3.0);
    }
    return qMax(0.0, duration);
}

double MainWindow::TimelineSection::previewPlaybackEndSeconds() const
{
    if (state_.qtPreviewPlaying_ && state_.qtPreviewPlaybackEndSecond_ > 0.0) {
        return qMax(0.0, state_.qtPreviewPlaybackEndSecond_);
    }
    double duration = 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        duration = qMax(duration, state_.timelineQuickStateBridge_->durationSeconds());
    }
    if (state_.previewTrackDurationSeconds_ > 0.0) {
        duration = qMax(duration, state_.previewTrackDurationSeconds_);
    }
    return qMax(0.0, duration);
}

void MainWindow::TimelineSection::updatePreviewSliderRange()
{
    if (ui_.previewSlider_ == nullptr) {
        return;
    }
    const int maximum = qMax(1, qRound(previewDurationSeconds() * 1000.0));
    QSignalBlocker blocker(ui_.previewSlider_);
    ui_.previewSlider_->setMaximum(maximum);
}

void MainWindow::TimelineSection::updatePreviewSliderPosition(double second)
{
    if (ui_.previewSlider_ == nullptr || state_.previewScrubDragging_) {
        return;
    }
    const int value = qBound(0, qRound(second * 1000.0), ui_.previewSlider_->maximum());
    QSignalBlocker blocker(ui_.previewSlider_);
    ui_.previewSlider_->setValue(value);
}

void MainWindow::TimelineSection::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(noteMarkers);
    state_.previewProgressStatsCache_ = cache;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(state_.qtPreviewPauseSecond_);
}

void MainWindow::TimelineSection::clearPreviewObjectStats()
{
    state_.previewProgressStatsCache_.reset();
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(0.0);
}

int MainWindow::TimelineSection::updatePreviewStatsLayoutMode(int hostWidth)
{
    if (ui_.previewStatsCard_ == nullptr || ui_.previewStatsGridLayout_ == nullptr || ui_.previewStatsChips_.isEmpty()) {
        return 0;
    }

    const int itemCount = ui_.previewStatsChips_.size();
    const QWidget* gridHost = ui_.previewStatsGridLayout_->parentWidget();
    const int horizontalSpacing = qMax(0, ui_.previewStatsGridLayout_->horizontalSpacing());
    const int verticalSpacing = qMax(0, ui_.previewStatsGridLayout_->verticalSpacing());
    const QMargins gridMargins = ui_.previewStatsGridLayout_->contentsMargins();
    const int resolvedHostWidth =
        (hostWidth >= 0)
        ? hostWidth
        : ((gridHost != nullptr) ? gridHost->contentsRect().width() : ui_.previewStatsCard_->contentsRect().width());
    if (resolvedHostWidth <= 0) {
        return ui_.previewStatsCard_->minimumHeight();
    }
    const int chipHeight = qMax(
        miacode::window_parity::kPreviewStatsChipHeight,
        !ui_.previewStatsChips_.isEmpty() && ui_.previewStatsChips_.constFirst() != nullptr
            ? ui_.previewStatsChips_.constFirst()->sizeHint().height()
            : miacode::window_parity::kPreviewStatsChipHeight
    );
    const miacode::window_parity::PreviewStatsLayout baseLayout = miacode::window_parity::computePreviewStatsLayout(
        resolvedHostWidth,
        itemCount,
        horizontalSpacing,
        verticalSpacing,
        chipHeight,
        gridMargins.top(),
        gridMargins.bottom()
    );
    int cols = baseLayout.columns;
    int rows = baseLayout.rows;

    const QLabel* widthTemplateLabel =
        ui_.previewTotalStatsLabel_ != nullptr ? ui_.previewTotalStatsLabel_ : ui_.previewStatsChips_.constFirst();
    const QFontMetrics chipMetrics(widthTemplateLabel != nullptr ? widthTemplateLabel->font() : owner_.font());
    constexpr int kPreviewStatsChipHorizontalPadding = 18;
    const int maxChipHintWidth =
        chipMetrics.horizontalAdvance(QStringLiteral("Total  xxxxx/xxxxx"))
        + kPreviewStatsChipHorizontalPadding;

    auto availableWidthForColumns = [&](int columnCount) {
        const int totalSpacing = horizontalSpacing * qMax(0, columnCount - 1);
        return qMax(0, resolvedHostWidth - gridMargins.left() - gridMargins.right() - totalSpacing);
    };

    constexpr int kMinimumAllowedStatsColumns = 2;
    while (cols > kMinimumAllowedStatsColumns) {
        const int availableWidth = availableWidthForColumns(cols);
        const int columnWidth = cols > 0 ? (availableWidth / cols) : 0;
        if (columnWidth >= maxChipHintWidth) {
            break;
        }
        --cols;
    }
    rows = qMax(1, (itemCount + cols - 1) / cols);
    const bool structureChanged = (rows != state_.previewStatsLayoutRows_) || (cols != state_.previewStatsLayoutCols_);
    state_.previewStatsLayoutRows_ = rows;
    state_.previewStatsLayoutCols_ = cols;

    const int cardHeight = 16
        + qMax(0, gridMargins.top())
        + qMax(0, gridMargins.bottom())
        + rows * chipHeight
        + qMax(0, rows - 1) * verticalSpacing;
    ui_.previewStatsCard_->setMinimumHeight(cardHeight);

    if (structureChanged) {
        while (QLayoutItem* item = ui_.previewStatsGridLayout_->takeAt(0)) {
            delete item;
        }
        for (int col = 0; col < 6; ++col) {
            ui_.previewStatsGridLayout_->setColumnStretch(col, 0);
            ui_.previewStatsGridLayout_->setColumnMinimumWidth(col, 0);
        }
        for (int row = 0; row < 6; ++row) {
            ui_.previewStatsGridLayout_->setRowStretch(row, 0);
        }

        for (int i = 0; i < itemCount; ++i) {
            const int row = i / cols;
            const int col = i % cols;
            ui_.previewStatsGridLayout_->addWidget(ui_.previewStatsChips_.at(i), row, col);
        }
        for (int col = 0; col < cols; ++col) {
            ui_.previewStatsGridLayout_->setColumnStretch(col, 1);
        }
        for (int row = 0; row < rows; ++row) {
            ui_.previewStatsGridLayout_->setRowStretch(row, 1);
        }
    }

    // Keep chip widths column-driven and independent from text metrics.
    const int totalSpacing = horizontalSpacing * qMax(0, cols - 1);
    const int availableWidth = qMax(0, resolvedHostWidth - gridMargins.left() - gridMargins.right() - totalSpacing);
    const int columnWidth = (cols > 0) ? (availableWidth / cols) : 0;
    for (QLabel* chip : ui_.previewStatsChips_) {
        if (chip == nullptr) {
            continue;
        }
        chip->setFixedWidth(qMax(0, columnWidth));
    }

    return cardHeight;
}

int MainWindow::TimelineSection::previewStatsMinimumHeightForPanelWidth(int panelWidth) const
{
    const int statsHostWidth = qMax(0, panelWidth - kPreviewPanelMarginX * 2 - 16);
    if (ui_.previewStatsGridLayout_ == nullptr || ui_.previewStatsChips_.isEmpty()) {
        return miacode::window_parity::computePreviewStatsLayout(statsHostWidth).minCardHeight;
    }

    const int itemCount = ui_.previewStatsChips_.size();
    const int horizontalSpacing = qMax(0, ui_.previewStatsGridLayout_->horizontalSpacing());
    const int verticalSpacing = qMax(0, ui_.previewStatsGridLayout_->verticalSpacing());
    const QMargins gridMargins = ui_.previewStatsGridLayout_->contentsMargins();
    const int chipHeight = qMax(
        miacode::window_parity::kPreviewStatsChipHeight,
        ui_.previewStatsChips_.constFirst() != nullptr
            ? ui_.previewStatsChips_.constFirst()->sizeHint().height()
            : miacode::window_parity::kPreviewStatsChipHeight
    );
    const QLabel* widthTemplateLabel =
        ui_.previewTotalStatsLabel_ != nullptr ? ui_.previewTotalStatsLabel_ : ui_.previewStatsChips_.constFirst();
    const QFontMetrics chipMetrics(widthTemplateLabel != nullptr ? widthTemplateLabel->font() : owner_.font());
    constexpr int kPreviewStatsChipHorizontalPadding = 18;
    const int minChipWidth =
        chipMetrics.horizontalAdvance(QStringLiteral("Total  xxxxx/xxxxx"))
        + kPreviewStatsChipHorizontalPadding;
    int cols = qMin(
        itemCount,
        statsHostWidth >= minChipWidth * miacode::window_parity::kPreviewStatsWideLayoutCols
                + horizontalSpacing * qMax(0, miacode::window_parity::kPreviewStatsWideLayoutCols - 1)
            ? miacode::window_parity::kPreviewStatsWideLayoutCols
            : miacode::window_parity::kPreviewStatsNarrowLayoutCols
    );
    constexpr int kMinimumAllowedStatsColumns = 2;
    const auto availableWidthForColumns = [&](int columnCount) {
        const int totalSpacing = horizontalSpacing * qMax(0, columnCount - 1);
        return qMax(0, statsHostWidth - gridMargins.left() - gridMargins.right() - totalSpacing);
    };
    while (cols > kMinimumAllowedStatsColumns) {
        const int availableColumnWidth = availableWidthForColumns(cols);
        const int columnWidth = cols > 0 ? (availableColumnWidth / cols) : 0;
        if (columnWidth >= minChipWidth) {
            break;
        }
        --cols;
    }
    cols = qMax(1, cols);
    const int rows = qMax(1, (itemCount + cols - 1) / cols);
    return 16
        + qMax(0, gridMargins.top())
        + qMax(0, gridMargins.bottom())
        + rows * chipHeight
        + qMax(0, rows - 1) * verticalSpacing;
}

double MainWindow::TimelineSection::normalizedPreviewCanvasAspectRatio(double ratio) const
{
    if (!qIsFinite(ratio)) {
        return 1.0;
    }
    return qBound(1.0, ratio, 3.0);
}

MainWindow::PreviewCanvasFrameRateMode MainWindow::previewCanvasFrameRateModeFromStorageValue(const QString& value) const
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("120") || normalized == QLatin1String("120fps")) {
        return PreviewCanvasFrameRateMode::Fps120;
    }
    if (normalized == QLatin1String("display")
        || normalized == QLatin1String("display_max")
        || normalized == QLatin1String("screen")
        || normalized == QLatin1String("unlimited")) {
        return PreviewCanvasFrameRateMode::DisplayRefresh;
    }
    return PreviewCanvasFrameRateMode::Fps60;
}

QString MainWindow::TimelineSection::previewCanvasFrameRateModeStorageValue() const
{
    switch (state_.previewCanvasFrameRateMode_) {
    case PreviewCanvasFrameRateMode::Fps120:
        return QStringLiteral("120");
    case PreviewCanvasFrameRateMode::DisplayRefresh:
        return QStringLiteral("display_max");
    case PreviewCanvasFrameRateMode::Fps60:
    default:
        return QStringLiteral("60");
    }
}

double MainWindow::TimelineSection::currentPreviewCanvasRefreshRate() const
{
    QScreen* targetScreen = owner_.screen();
    if (owner_.windowHandle() != nullptr && owner_.windowHandle()->screen() != nullptr) {
        targetScreen = owner_.windowHandle()->screen();
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    const double refreshRate = targetScreen != nullptr ? targetScreen->refreshRate() : 0.0;
    if (!qIsFinite(refreshRate) || refreshRate < 1.0) {
        return 60.0;
    }
    return refreshRate;
}

bool MainWindow::TimelineSection::previewCanvasUsesFrameSwappedPacing() const
{
    return state_.previewCanvasFrameRateMode_ == PreviewCanvasFrameRateMode::DisplayRefresh;
}

qint64 MainWindow::TimelineSection::previewCanvasTargetFrameIntervalNs() const
{
    switch (state_.previewCanvasFrameRateMode_) {
    case PreviewCanvasFrameRateMode::Fps120:
        return 1000000000LL / 120LL;
    case PreviewCanvasFrameRateMode::DisplayRefresh:
        return qMax<qint64>(1LL, qRound64(1000000000.0 / currentPreviewCanvasRefreshRate()));
    case PreviewCanvasFrameRateMode::Fps60:
    default:
        return 1000000000LL / 60LL;
    }
}

qint64 MainWindow::TimelineSection::timelineTargetFrameIntervalNs() const
{
    const qint64 previewIntervalNs = qMax<qint64>(1LL, previewCanvasTargetFrameIntervalNs());
    const qint64 timelineCapIntervalNs =
        qMax<qint64>(1LL, qRound64(1000000000.0 / miacode::mainwindow::shared::kTimelineMaxUiUpdateFps));
    return qMax(previewIntervalNs, timelineCapIntervalNs);
}

void MainWindow::TimelineSection::resetQtPreviewFixedFramePacing()
{
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    if (previewCanvasUsesFrameSwappedPacing()) {
        return;
    }
    state_.qtPreviewFixedTickOriginNs_ = state_.qtPreviewWatchdogElapsed_.nsecsElapsed();
    state_.qtPreviewNextFixedTickDueNs_ =
        state_.qtPreviewFixedTickOriginNs_ + previewCanvasTargetFrameIntervalNs();
}

void MainWindow::TimelineSection::scheduleNextQtPreviewTick()
{
    if (ui_.qtPreviewTimer_ == nullptr || !state_.qtPreviewPlaying_) {
        return;
    }
    if (previewCanvasUsesFrameSwappedPacing()) {
        ui_.qtPreviewTimer_->start();
        return;
    }
    if (state_.qtPreviewNextFixedTickDueNs_ < 0 || state_.qtPreviewFixedTickOriginNs_ < 0) {
        resetQtPreviewFixedFramePacing();
    }
    const qint64 nowNs = state_.qtPreviewWatchdogElapsed_.nsecsElapsed();
    const qint64 delayNs = qMax<qint64>(0, state_.qtPreviewNextFixedTickDueNs_ - nowNs);
    ui_.qtPreviewTimer_->setInterval(std::chrono::nanoseconds(delayNs));
    ui_.qtPreviewTimer_->start();
}

void MainWindow::TimelineSection::requestNextDisplayRefreshPreviewFrame()
{
    if (!state_.qtPreviewPlaying_
        || state_.previewCanvas_ == nullptr
        || !previewCanvasUsesFrameSwappedPacing()
        || state_.qtPreviewAwaitingFrameSwap_) {
        return;
    }
    state_.qtPreviewAwaitingFrameSwap_ = true;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = state_.qtPreviewWatchdogElapsed_.elapsed();
    state_.previewCanvas_->update();
    scheduleNextQtPreviewTick();
}

void MainWindow::TimelineSection::refreshPreviewFrameRateTimers()
{
    const qint64 targetIntervalNs = previewCanvasTargetFrameIntervalNs();
    const qint64 timelineIntervalNs = timelineTargetFrameIntervalNs();
    if (ui_.qtPreviewTimer_ != nullptr) {
        ui_.qtPreviewTimer_->setInterval(std::chrono::nanoseconds(qMax<qint64>(1, targetIntervalNs)));
    }
    if (ui_.qtPreviewTimelineTimer_ != nullptr) {
        ui_.qtPreviewTimelineTimer_->setInterval(std::chrono::nanoseconds(qMax<qint64>(1, timelineIntervalNs)));
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setFramePacingDebugState(
            previewCanvasUsesFrameSwappedPacing(),
            targetIntervalNs > 0 ? (1000000000.0 / static_cast<double>(targetIntervalNs)) : 0.0,
            currentPreviewCanvasRefreshRate()
        );
    }
}

double MainWindow::TimelineSection::fixedIntervalPreviewSecondForDeadlineNs(qint64 deadlineNs) const
{
    if (state_.qtPreviewFixedTickOriginNs_ < 0) {
        return qMax(0.0, state_.qtPreviewPauseSecond_);
    }
    const qint64 elapsedNs = qMax<qint64>(0, deadlineNs - state_.qtPreviewFixedTickOriginNs_);
    return qMax(
        0.0,
        state_.qtPreviewStartSecond_
            + (static_cast<double>(elapsedNs) / 1000000000.0) * state_.previewPlaybackRate_
    );
}

void MainWindow::TimelineSection::setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    if (state_.previewCanvasFrameRateMode_ == mode) {
        refreshPreviewFrameRateTimers();
        return;
    }
    state_.previewCanvasFrameRateMode_ = mode;
    refreshPreviewFrameRateTimers();
    if (ui_.qtPreviewTimer_ != nullptr) {
        ui_.qtPreviewTimer_->stop();
    }
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    resetQtPreviewFixedFramePacing();
    if (state_.qtPreviewPlaying_) {
        if (state_.previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
            requestNextDisplayRefreshPreviewFrame();
        } else {
            scheduleNextQtPreviewTick();
        }
    }
    if (persistState) {
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    }
}

void MainWindow::TimelineSection::setPreviewCanvasAspectRatio(double ratio, bool persistState)
{
    const double normalized = normalizedPreviewCanvasAspectRatio(ratio);
    if (qAbs(state_.previewCanvasAspectRatio_ - normalized) <= 1e-6) {
        return;
    }
    const double previousRatio = state_.previewCanvasAspectRatio_;
    state_.previewCanvasAspectRatio_ = normalized;
    if (normalized + 1e-6 < previousRatio) {
        updatePreviewWorkspaceLayout();
    } else {
        updatePreviewPanelLayout();
    }
    owner_.refreshQuickShellPreviewCompositeSurfaceState();
    if (ui_.workspaceSplitter_ != nullptr && ui_.previewPanel_ != nullptr && ui_.previewLeftColumn_ != nullptr) {
        const bool restoringToSquare = qAbs(normalized - 1.0) <= 1e-6 && previousRatio > 1.0 + 1e-6;
        const int availableWidth = qMax(0, ui_.workspaceSplitter_->contentsRect().width());
        const int availableHeight = qMax(0, ui_.workspaceSplitter_->contentsRect().height());
        const int leftMinWidth = qMax(320, ui_.previewLeftColumn_->minimumWidth());
        const int controlHeight =
            ui_.previewControlCard_ != nullptr
                ? qMax(ui_.previewControlCard_->minimumSizeHint().height(), ui_.previewControlCard_->sizeHint().height())
                : 0;
        const int targetRightWidth =
            restoringToSquare
                ? qMax(kEmbeddedPreviewPanelMinWidth, ui_.previewPanel_->width())
                : miacode::window_parity::computePreviewPanelTargetWidthForAdaptiveStats(
                    availableWidth,
                    availableHeight,
                    leftMinWidth,
                    controlHeight,
                    normalized
                );
        const int clampedRightWidth = qBound(
            kEmbeddedPreviewPanelMinWidth,
            targetRightWidth,
            qMax(kEmbeddedPreviewPanelMinWidth, availableWidth)
        );
        ui_.previewPanel_->setMinimumWidth(clampedRightWidth);
        if (availableWidth > 0) {
            const int leftWidth = qMax(leftMinWidth, availableWidth - clampedRightWidth);
            ui_.workspaceSplitter_->setSizes({leftWidth, clampedRightWidth});
        }
    }
    refreshLayoutAfterPageSwitch();
    if (persistState) {
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    }
}
void MainWindow::TimelineSection::togglePreviewFullscreen()
{
    if (state_.previewFullscreenActive_) {
        exitPreviewFullscreen();
        return;
    }
    enterPreviewFullscreen();
}

void MainWindow::TimelineSection::enterPreviewFullscreen()
{
    if (state_.previewFullscreenActive_) {
        return;
    }
    state_.previewFullscreenActive_ = true;
    state_.previewFullscreenControlsVisible_ = false;
    state_.previewFullscreenCursorTrackingInitialized_ = false;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setSuppressObjectStatsHud(true);
    }
    owner_.refreshQuickShellPreviewCompositeSurfaceState();
    owner_.updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
}

void MainWindow::TimelineSection::exitPreviewFullscreen()
{
    if (!state_.previewFullscreenActive_) {
        return;
    }
    state_.previewFullscreenActive_ = false;
    state_.previewFullscreenControlsVisible_ = false;
    state_.previewFullscreenCursorTrackingInitialized_ = false;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setSuppressObjectStatsHud(false);
    }
    owner_.refreshQuickShellPreviewCompositeSurfaceState();
    owner_.updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
}

void MainWindow::TimelineSection::updatePreviewFullscreenButtonAppearance()
{
    if (ui_.previewFullscreenButton_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(ui_.previewFullscreenButton_);
    const QColor iconColor =
        state_.previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : UiTheme::colors().iconPrimary;
    ui_.previewFullscreenButton_->setChecked(state_.previewFullscreenActive_);
    ui_.previewFullscreenButton_->setText(QString());
    ui_.previewFullscreenButton_->setIcon(
        state_.previewFullscreenActive_ ? makePreviewExitFullscreenIcon(iconColor) : makePreviewEnterFullscreenIcon(iconColor)
    );
    ui_.previewFullscreenButton_->setToolTip(QString());
}

bool MainWindow::TimelineSection::shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenWindow_ == nullptr) {
        return false;
    }

    const QRect windowGlobalRect(
        ui_.previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        ui_.previewFullscreenWindow_->size()
    );
    if (!windowGlobalRect.contains(globalCursorPos)) {
        return false;
    }

    if (ui_.previewFullscreenControlsWindow_ != nullptr
        && ui_.previewFullscreenControlsWindow_->isVisible()
        && ui_.previewFullscreenControlsWindow_->geometry().contains(globalCursorPos)) {
        return true;
    }

    const int controlsHeight =
        ui_.previewControlCard_ != nullptr
            ? qMax(ui_.previewControlCard_->minimumSizeHint().height(), ui_.previewControlCard_->sizeHint().height())
            : 0;
    const int revealHotzoneHeight = qMin(
        windowGlobalRect.height(),
        qMax(kPreviewFullscreenControlsRevealHotzoneHeight, controlsHeight + kPreviewFullscreenOverlayBottomMargin)
    );
    return globalCursorPos.y() >= windowGlobalRect.bottom() - revealHotzoneHeight;
}

QRect MainWindow::TimelineSection::previewFullscreenControlCardRect(bool visible) const
{
    if (ui_.previewFullscreenWindow_ == nullptr || ui_.previewControlCard_ == nullptr) {
        return QRect();
    }
    const QRect windowRect = ui_.previewFullscreenWindow_->contentsRect();
    if (windowRect.width() <= 0 || windowRect.height() <= 0) {
        return QRect();
    }
    const QPoint globalTopLeft = ui_.previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    const int horizontalMargin = qMin(kPreviewFullscreenOverlaySideMargin, qMax(12, windowRect.width() / 20));
    const int availableWidth = qMax(0, windowRect.width() - horizontalMargin * 2);
    if (availableWidth <= 0) {
        return QRect();
    }

    const QSize preferredSize = ui_.previewControlCard_->sizeHint().expandedTo(ui_.previewControlCard_->minimumSizeHint());
    const int cardWidth = qMax(
        ui_.previewControlCard_->minimumSizeHint().width(),
        qMin(availableWidth, kPreviewFullscreenOverlayMaxWidth)
    );
    const int cardHeight = qMax(preferredSize.height(), ui_.previewControlCard_->minimumSizeHint().height());
    const int cardX = globalTopLeft.x() + qMax(0, (windowRect.width() - cardWidth) / 2);
    const int visibleY = globalTopLeft.y() + windowRect.height() - cardHeight - kPreviewFullscreenOverlayBottomMargin;
    const int hiddenY = globalTopLeft.y() + windowRect.height() + kPreviewFullscreenOverlayHideOffset;
    return QRect(cardX, visible ? visibleY : hiddenY, cardWidth, cardHeight);
}

void MainWindow::TimelineSection::showPreviewFullscreenControls(bool animate)
{
    if (!state_.previewFullscreenActive_
        || ui_.previewFullscreenWindow_ == nullptr
        || ui_.previewFullscreenControlsWindow_ == nullptr
        || ui_.previewControlCard_ == nullptr
        || ui_.previewControlCard_->parentWidget() != ui_.previewFullscreenControlsWindow_) {
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(true);
    if (!targetRect.isValid()) {
        return;
    }

    if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
        ui_.previewFullscreenControlsAnimation_->stop();
    }
    if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
        ui_.previewFullscreenControlsOpacityAnimation_->stop();
    }

    QRect currentRect = ui_.previewFullscreenControlsWindow_->geometry();
    if (!currentRect.isValid()) {
        currentRect = previewFullscreenControlCardRect(false);
    }
    if (!ui_.previewFullscreenControlsWindow_->isVisible()) {
        ui_.previewFullscreenControlsWindow_->setGeometry(currentRect);
        ui_.previewFullscreenControlsWindow_->setWindowOpacity(0.0);
    }

    ui_.previewFullscreenControlsWindow_->show();
    ui_.previewFullscreenControlsWindow_->raise();
    ui_.previewControlCard_->show();

    const qreal currentOpacity = ui_.previewFullscreenControlsWindow_->windowOpacity();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        ui_.previewFullscreenControlsWindow_->setWindowOpacity(1.0);
    } else {
        if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
            ui_.previewFullscreenControlsAnimation_->setStartValue(currentRect);
            ui_.previewFullscreenControlsAnimation_->setEndValue(targetRect);
            ui_.previewFullscreenControlsAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
            ui_.previewFullscreenControlsOpacityAnimation_->setStartValue(currentOpacity);
            ui_.previewFullscreenControlsOpacityAnimation_->setEndValue(1.0);
            ui_.previewFullscreenControlsOpacityAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setWindowOpacity(1.0);
        }
    }

    state_.previewFullscreenControlsVisible_ = true;
    schedulePreviewFullscreenControlsAutoHide();
}

void MainWindow::TimelineSection::hidePreviewFullscreenControls(bool animate)
{
    if (!state_.previewFullscreenActive_
        || ui_.previewFullscreenWindow_ == nullptr
        || ui_.previewFullscreenControlsWindow_ == nullptr
        || ui_.previewControlCard_ == nullptr
        || ui_.previewControlCard_->parentWidget() != ui_.previewFullscreenControlsWindow_) {
        return;
    }

    const bool pointerOverControls =
        ui_.previewControlCard_->underMouse()
        || (ui_.previewSlider_ != nullptr && ui_.previewSlider_->underMouse())
        || (ui_.stopPreviewButton_ != nullptr && ui_.stopPreviewButton_->underMouse())
        || (ui_.pausePreviewButton_ != nullptr && ui_.pausePreviewButton_->underMouse())
        || (ui_.previewSpeedButton_ != nullptr && ui_.previewSpeedButton_->underMouse())
        || (ui_.previewFullscreenButton_ != nullptr && ui_.previewFullscreenButton_->underMouse());
    const bool speedMenuVisible =
        ui_.previewSpeedButton_ != nullptr
        && ui_.previewSpeedButton_->menu() != nullptr
        && ui_.previewSpeedButton_->menu()->isVisible();
    if (state_.previewScrubDragging_ || pointerOverControls || speedMenuVisible) {
        schedulePreviewFullscreenControlsAutoHide();
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(false);
    if (!targetRect.isValid()) {
        return;
    }

    if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
        ui_.previewFullscreenControlsAnimation_->stop();
    }
    if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
        ui_.previewFullscreenControlsOpacityAnimation_->stop();
    }

    const QRect currentRect = ui_.previewFullscreenControlsWindow_->geometry();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        ui_.previewFullscreenControlsWindow_->setWindowOpacity(0.0);
        ui_.previewFullscreenControlsWindow_->hide();
    } else {
        if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
            ui_.previewFullscreenControlsAnimation_->setStartValue(currentRect);
            ui_.previewFullscreenControlsAnimation_->setEndValue(targetRect);
            ui_.previewFullscreenControlsAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
            ui_.previewFullscreenControlsOpacityAnimation_->setStartValue(ui_.previewFullscreenControlsWindow_->windowOpacity());
            ui_.previewFullscreenControlsOpacityAnimation_->setEndValue(0.0);
            ui_.previewFullscreenControlsOpacityAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setWindowOpacity(0.0);
            ui_.previewFullscreenControlsWindow_->hide();
        }
    }

    state_.previewFullscreenControlsVisible_ = false;
}

void MainWindow::TimelineSection::schedulePreviewFullscreenControlsAutoHide()
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenControlsTimer_ == nullptr) {
        return;
    }
    ui_.previewFullscreenControlsTimer_->start(kPreviewFullscreenControlsAutoHideDelayMs);
}

void MainWindow::TimelineSection::pollPreviewFullscreenCursor()
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QPoint globalCursorPos = QCursor::pos();
    const QRect windowGlobalRect(
        ui_.previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        ui_.previewFullscreenWindow_->size()
    );
    const bool insideWindow = windowGlobalRect.contains(globalCursorPos);
    if (!insideWindow) {
        if (state_.previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
        state_.previewFullscreenCursorTrackingInitialized_ = false;
        return;
    }

    if (!state_.previewFullscreenCursorTrackingInitialized_) {
        state_.previewFullscreenLastCursorPos_ = globalCursorPos;
        state_.previewFullscreenCursorTrackingInitialized_ = true;
        return;
    }

    if (state_.previewFullscreenLastCursorPos_ != globalCursorPos) {
        state_.previewFullscreenLastCursorPos_ = globalCursorPos;
        if (shouldRevealPreviewFullscreenControls(globalCursorPos)) {
            showPreviewFullscreenControls(true);
        } else if (state_.previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
    }
}

void MainWindow::TimelineSection::updatePreviewFullscreenOverlayGeometry()
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QRect windowRect = ui_.previewFullscreenWindow_->contentsRect();
    const QPoint globalTopLeft = ui_.previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    if (ui_.previewFullscreenHintWindow_ != nullptr
        && ui_.previewFullscreenHintLabel_ != nullptr
        && ui_.previewFullscreenHintLabel_->isVisible()) {
        const QSize hintSize = ui_.previewFullscreenHintLabel_->sizeHint().expandedTo(QSize(220, 42));
        ui_.previewFullscreenHintWindow_->resize(hintSize);
        ui_.previewFullscreenHintWindow_->move(
            globalTopLeft.x() + qMax(16, (windowRect.width() - hintSize.width()) / 2),
            globalTopLeft.y() + kPreviewFullscreenHintTopMargin
        );
        ui_.previewFullscreenHintLabel_->resize(hintSize);
        ui_.previewFullscreenHintLabel_->raise();
        ui_.previewFullscreenHintWindow_->raise();
    }

    if (ui_.previewFullscreenControlsWindow_ != nullptr
        && ui_.previewControlCard_ != nullptr
        && ui_.previewControlCard_->parentWidget() == ui_.previewFullscreenControlsWindow_) {
        const QRect targetRect = previewFullscreenControlCardRect(state_.previewFullscreenControlsVisible_);
        if (targetRect.isValid()) {
            if (ui_.previewFullscreenControlsAnimation_ != nullptr
                && ui_.previewFullscreenControlsAnimation_->state() == QAbstractAnimation::Running) {
                ui_.previewFullscreenControlsAnimation_->stop();
            }
            ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
            ui_.previewFullscreenControlsWindow_->setWindowOpacity(state_.previewFullscreenControlsVisible_ ? 1.0 : 0.0);
            if (state_.previewFullscreenControlsVisible_) {
                ui_.previewFullscreenControlsWindow_->show();
            }
            ui_.previewFullscreenControlsWindow_->raise();
        }
    }
}

void MainWindow::TimelineSection::updatePreviewWorkspaceLayout()
{
    updatePreviewPanelLayout();
    owner_.refreshQuickShellRehostedWidgetParent(ui_.outlineDock_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.workspaceContentWidget_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.bottomTabs_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
    owner_.windowSection_->updateEditorFindBarGeometry();
    owner_.windowSection_->applyFindOverlayInset();
}

void MainWindow::TimelineSection::cacheWorkspaceLayoutSizes()
{
}

void MainWindow::TimelineSection::restoreWorkspaceLayoutSizes()
{
}

void MainWindow::TimelineSection::setWorkspacePanelsSwapped(bool swapped, bool persistState)
{
    if (state_.workspacePanelsSwapped_ == swapped) {
        if (ui_.swapWorkspaceSidesAction_ != nullptr) {
            ui_.swapWorkspaceSidesAction_->blockSignals(true);
            ui_.swapWorkspaceSidesAction_->setChecked(state_.workspacePanelsSwapped_);
            ui_.swapWorkspaceSidesAction_->blockSignals(false);
        }
        return;
    }

    cacheWorkspaceLayoutSizes();
    state_.workspacePanelsSwapped_ = swapped;
    applyWorkspacePanelArrangement();
    if (persistState) {
        owner_.savePortableState();
    }
}

void MainWindow::TimelineSection::applyWorkspacePanelArrangement()
{
    if (ui_.swapWorkspaceSidesAction_ != nullptr) {
        ui_.swapWorkspaceSidesAction_->blockSignals(true);
        ui_.swapWorkspaceSidesAction_->setChecked(state_.workspacePanelsSwapped_);
        ui_.swapWorkspaceSidesAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, state_.workspacePanelsSwapped_)
        );
        ui_.swapWorkspaceSidesAction_->blockSignals(false);
    }
    refreshLayoutAfterPageSwitch();
}

void MainWindow::TimelineSection::refreshLayoutAfterPageSwitch()
{
    if (ui_.previewLeftColumn_ != nullptr) {
        ui_.previewLeftColumn_->updateGeometry();
        if (QLayout* layout = ui_.previewLeftColumn_->layout(); layout != nullptr) {
            layout->activate();
        }
    }
    if (ui_.editorStack_ != nullptr) {
        ui_.editorStack_->updateGeometry();
    }
    if (ui_.bottomTabs_ != nullptr) {
        ui_.bottomTabs_->updateGeometry();
    }
    if (ui_.workspaceSplitter_ != nullptr) {
        ui_.workspaceSplitter_->updateGeometry();
        if (QLayout* layout = ui_.workspaceSplitter_->layout(); layout != nullptr) {
            layout->activate();
        }
    }
    owner_.refreshQuickShellRehostedWidgetParent(ui_.outlineDock_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.workspaceContentWidget_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.bottomTabs_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
    owner_.updateEditorHeaderLayoutMode();
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->updateGeometry();
        ui_.timelineView_->viewport()->update();
    }
}

void MainWindow::TimelineSection::updatePreviewPanelLayout(int panelWidthOverride, int panelHeightOverride)
{
    if (ui_.previewPanel_ != nullptr) {
        const QRect panelRect = ui_.previewPanel_->contentsRect();
        const int resolvedWidth = panelWidthOverride >= 0 ? panelWidthOverride : panelRect.width();
        const int resolvedHeight = panelHeightOverride >= 0 ? panelHeightOverride : panelRect.height();
        const int controlHeight =
            ui_.previewControlCard_ != nullptr
                ? qMax(ui_.previewControlCard_->minimumSizeHint().height(), ui_.previewControlCard_->sizeHint().height())
                : 0;
        const miacode::window_parity::PreviewPanelLayout layout =
            miacode::window_parity::computePreviewPanelLayout(
                resolvedWidth,
                resolvedHeight,
                controlHeight,
                state_.previewCanvasAspectRatio_
            );

        if (ui_.previewCanvasFrame_ != nullptr) {
            ui_.previewCanvasFrame_->setGeometry(
                panelRect.x() + layout.previewX,
                panelRect.y() + layout.previewY,
                layout.previewWidth,
                layout.previewHeight
            );
            ui_.previewCanvasFrame_->show();
        }
        if (ui_.previewCanvasContainer_ != nullptr && ui_.previewCanvasFrame_ != nullptr) {
            ui_.previewCanvasContainer_->setGeometry(ui_.previewCanvasFrame_->contentsRect());
            ui_.previewCanvasContainer_->show();
        }
        if (ui_.previewControlCard_ != nullptr) {
            ui_.previewControlCard_->setGeometry(
                panelRect.x() + layout.controlX,
                panelRect.y() + layout.controlY,
                layout.controlWidth,
                controlHeight
            );
            ui_.previewControlCard_->show();
        }
        if (ui_.previewStatsCard_ != nullptr) {
            const int statsHeight = qMax(layout.statsHeight, previewStatsMinimumHeightForPanelWidth(layout.statsWidth));
            ui_.previewStatsCard_->setGeometry(
                panelRect.x() + layout.statsX,
                panelRect.y() + layout.statsY,
                layout.statsWidth,
                statsHeight
            );
            updatePreviewStatsLayoutMode(layout.statsHostWidth);
            ui_.previewStatsCard_->show();
        }
    }
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
}

void MainWindow::TimelineSection::updatePreviewObjectStats(double second)
{
    if (ui_.previewTapStatsLabel_ == nullptr
        || ui_.previewHoldStatsLabel_ == nullptr
        || ui_.previewSlideStatsLabel_ == nullptr
        || ui_.previewTouchStatsLabel_ == nullptr
        || ui_.previewBreakStatsLabel_ == nullptr
        || ui_.previewTotalStatsLabel_ == nullptr) {
        return;
    }

    const miacode::preview::scene::PreviewObjectStatsSnapshot stats =
        state_.previewProgressStatsCache_ != nullptr
        ? state_.previewProgressStatsCache_->snapshotAt(second)
        : miacode::preview::scene::PreviewObjectStatsSnapshot();

    const auto fmt = [](const QString& name, int played, int total) {
        return QString("%1  %2/%3")
            .arg(name.leftJustified(5, QChar(' '), true))
            .arg(played)
            .arg(total);
    };
    ui_.previewTapStatsLabel_->setText(fmt("Tap", stats.tapPlayed, stats.tapTotal));
    ui_.previewHoldStatsLabel_->setText(fmt("Hold", stats.holdPlayed, stats.holdTotal));
    ui_.previewSlideStatsLabel_->setText(fmt("Slide", stats.slidePlayed, stats.slideTotal));
    ui_.previewTouchStatsLabel_->setText(fmt("Touch", stats.touchPlayed, stats.touchTotal));
    ui_.previewBreakStatsLabel_->setText(fmt("Break", stats.breakPlayed, stats.breakTotal));
    ui_.previewTotalStatsLabel_->setText(fmt("Total", stats.totalPlayed, stats.totalCount));
    updatePreviewStatsLayoutMode(-1);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
}

QString MainWindow::TimelineSection::formatPreviewTimestamp(double second) const
{
    const int totalCentiseconds = qMax(0, qRound(second * 100.0));
    const int minutes = totalCentiseconds / 6000;
    const int secondsPart = (totalCentiseconds / 100) % 60;
    const int centiseconds = totalCentiseconds % 100;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secondsPart, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

void MainWindow::TimelineSection::showPreviewSliderTimeHint(int sliderValue)
{
    if (ui_.previewSlider_ == nullptr) {
        return;
    }
    const double second = static_cast<double>(sliderValue) / 1000.0;
    QStyleOptionSlider option;
    option.initFrom(ui_.previewSlider_);
    option.subControls = QStyle::SC_SliderHandle;
    option.orientation = ui_.previewSlider_->orientation();
    option.minimum = ui_.previewSlider_->minimum();
    option.maximum = ui_.previewSlider_->maximum();
    option.sliderPosition = sliderValue;
    option.sliderValue = sliderValue;
    option.upsideDown = false;
    const QRect handleRect = ui_.previewSlider_->style()->subControlRect(
        QStyle::CC_Slider,
        &option,
        QStyle::SC_SliderHandle,
        ui_.previewSlider_
    );
    const QPoint global = ui_.previewSlider_->mapToGlobal(handleRect.center() + QPoint(0, -18));
    QToolTip::showText(global, formatPreviewTimestamp(second), ui_.previewSlider_, ui_.previewSlider_->rect(), 600);
}

double MainWindow::previewDurationSeconds() const
{
    return timelineSection_->previewDurationSeconds();
}

double MainWindow::previewPlaybackEndSeconds() const
{
    return timelineSection_->previewPlaybackEndSeconds();
}

void MainWindow::updatePreviewSliderRange()
{
    timelineSection_->updatePreviewSliderRange();
}

void MainWindow::updatePreviewSliderPosition(double second)
{
    timelineSection_->updatePreviewSliderPosition(second);
}

void MainWindow::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    timelineSection_->refreshPreviewObjectStatsTotals(noteMarkers);
}

void MainWindow::clearPreviewObjectStats()
{
    timelineSection_->clearPreviewObjectStats();
}

int MainWindow::updatePreviewStatsLayoutMode(int hostWidth)
{
    return timelineSection_->updatePreviewStatsLayoutMode(hostWidth);
}

int MainWindow::previewStatsMinimumHeightForPanelWidth(int panelWidth) const
{
    return timelineSection_->previewStatsMinimumHeightForPanelWidth(panelWidth);
}

double MainWindow::normalizedPreviewCanvasAspectRatio(double ratio) const
{
    return timelineSection_->normalizedPreviewCanvasAspectRatio(ratio);
}

QString MainWindow::previewCanvasFrameRateModeStorageValue() const
{
    return timelineSection_->previewCanvasFrameRateModeStorageValue();
}

double MainWindow::currentPreviewCanvasRefreshRate() const
{
    return timelineSection_->currentPreviewCanvasRefreshRate();
}

bool MainWindow::previewCanvasUsesFrameSwappedPacing() const
{
    return timelineSection_->previewCanvasUsesFrameSwappedPacing();
}

qint64 MainWindow::previewCanvasTargetFrameIntervalNs() const
{
    return timelineSection_->previewCanvasTargetFrameIntervalNs();
}

void MainWindow::resetQtPreviewFixedFramePacing()
{
    timelineSection_->resetQtPreviewFixedFramePacing();
}

void MainWindow::scheduleNextQtPreviewTick()
{
    timelineSection_->scheduleNextQtPreviewTick();
}

void MainWindow::requestNextDisplayRefreshPreviewFrame()
{
    timelineSection_->requestNextDisplayRefreshPreviewFrame();
}

void MainWindow::refreshPreviewFrameRateTimers()
{
    timelineSection_->refreshPreviewFrameRateTimers();
}

void MainWindow::setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    timelineSection_->setPreviewCanvasFrameRateMode(mode, persistState);
}

void MainWindow::setPreviewCanvasAspectRatio(double ratio, bool persistState)
{
    timelineSection_->setPreviewCanvasAspectRatio(ratio, persistState);
}

void MainWindow::togglePreviewFullscreen()
{
    timelineSection_->togglePreviewFullscreen();
}

void MainWindow::enterPreviewFullscreen()
{
    timelineSection_->enterPreviewFullscreen();
}

void MainWindow::exitPreviewFullscreen()
{
    timelineSection_->exitPreviewFullscreen();
}

void MainWindow::updatePreviewFullscreenButtonAppearance()
{
    timelineSection_->updatePreviewFullscreenButtonAppearance();
}

bool MainWindow::shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const
{
    return timelineSection_->shouldRevealPreviewFullscreenControls(globalCursorPos);
}

QRect MainWindow::previewFullscreenControlCardRect(bool visible) const
{
    return timelineSection_->previewFullscreenControlCardRect(visible);
}

void MainWindow::showPreviewFullscreenControls(bool animate)
{
    timelineSection_->showPreviewFullscreenControls(animate);
}

void MainWindow::hidePreviewFullscreenControls(bool animate)
{
    timelineSection_->hidePreviewFullscreenControls(animate);
}

void MainWindow::schedulePreviewFullscreenControlsAutoHide()
{
    timelineSection_->schedulePreviewFullscreenControlsAutoHide();
}

void MainWindow::pollPreviewFullscreenCursor()
{
    timelineSection_->pollPreviewFullscreenCursor();
}

void MainWindow::updatePreviewFullscreenOverlayGeometry()
{
    timelineSection_->updatePreviewFullscreenOverlayGeometry();
}

void MainWindow::updatePreviewWorkspaceLayout()
{
    timelineSection_->updatePreviewWorkspaceLayout();
}

void MainWindow::cacheWorkspaceLayoutSizes()
{
    timelineSection_->cacheWorkspaceLayoutSizes();
}

void MainWindow::restoreWorkspaceLayoutSizes()
{
    timelineSection_->restoreWorkspaceLayoutSizes();
}

void MainWindow::setWorkspacePanelsSwapped(bool swapped, bool persistState)
{
    timelineSection_->setWorkspacePanelsSwapped(swapped, persistState);
}

void MainWindow::applyWorkspacePanelArrangement()
{
    timelineSection_->applyWorkspacePanelArrangement();
}

void MainWindow::refreshLayoutAfterPageSwitch()
{
    timelineSection_->refreshLayoutAfterPageSwitch();
}

void MainWindow::updatePreviewPanelLayout(int panelWidthOverride, int panelHeightOverride)
{
    timelineSection_->updatePreviewPanelLayout(panelWidthOverride, panelHeightOverride);
}

void MainWindow::updatePreviewObjectStats(double second)
{
    timelineSection_->updatePreviewObjectStats(second);
}

QString MainWindow::formatPreviewTimestamp(double second) const
{
    return timelineSection_->formatPreviewTimestamp(second);
}

void MainWindow::showPreviewSliderTimeHint(int sliderValue)
{
    timelineSection_->showPreviewSliderTimeHint(sliderValue);
}
