#include "runtime/playback/PlaybackHost.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/ContentDurationConfig.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewInteractionConfig.h"
#include "common/UiHangWatchdog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace miacode::runtime::shared;

namespace {

constexpr qint64 kPageLayoutStepSlowMs = 50;
constexpr qint64 kPageLayoutTotalSlowMs = 100;

QString layoutWidgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("(null)");
    }
    return QStringLiteral("class=%1 name=%2 size=%3x%4 visible=%5")
        .arg(QString::fromUtf8(widget->metaObject()->className()))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(empty)") : widget->objectName())
        .arg(widget->width())
        .arg(widget->height())
        .arg(widget->isVisible() ? 1 : 0);
}

void appendPageLayoutDiag(
    const QString& action,
    const QString& step,
    QWidget* widget,
    qint64 elapsedMs,
    miacode::debug_log::Level level = miacode::debug_log::Level::Info,
    const QString& detail = QString())
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    QString payload = QStringLiteral("action=%1 step=%2 elapsed_ms=%3 widget=\"%4\"")
        .arg(action, step)
        .arg(elapsedMs)
        .arg(layoutWidgetSummary(widget));
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" %1").arg(detail.trimmed());
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("layout/export_page"),
        payload,
        /*force=*/false,
        level);
}

}  // namespace

double miacode::runtime::PlaybackHost::previewDurationSeconds() const
{
    // Unified content-duration policy = max(chartEnd + tail, music) — see
    // common/ContentDurationConfig.h. The chart end is the timeline bridge's
    // durationSeconds (last note/beat/measure); the runtime cursors below are
    // maxed in WITHOUT the tail so the range merely covers an active playhead.
    double chartEndSeconds = 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        chartEndSeconds = qMax(chartEndSeconds, state_.timelineQuickStateBridge_->durationSeconds());
    }
    double duration = miacode::content_duration::totalContentDurationSeconds(
        chartEndSeconds, state_.previewTrackDurationSeconds_);
    if (state_.playing_ && state_.qtPreviewPlaybackEndSecond_ > 0.0) {
        duration = qMax(duration, state_.qtPreviewPlaybackEndSecond_);
    }
    if (state_.timelineQuickStateBridge_ != nullptr) {
        duration = qMax(duration, state_.timelineQuickStateBridge_->playheadSeconds());
        duration = qMax(duration, state_.timelineQuickStateBridge_->playbackEntrySeconds());
    }
    duration = qMax(duration, qMax(0.0, state_.pauseSecond_));
    return qMax(0.0, duration);
}

double miacode::runtime::PlaybackHost::previewPlaybackEndSeconds() const
{
    if (state_.playing_ && state_.qtPreviewPlaybackEndSecond_ > 0.0) {
        return qMax(0.0, state_.qtPreviewPlaybackEndSecond_);
    }
    // Same unified content-duration policy as previewDurationSeconds(), so
    // playback auto-stops exactly where the slider/total duration ends.
    double chartEndSeconds = 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        chartEndSeconds = qMax(chartEndSeconds, state_.timelineQuickStateBridge_->durationSeconds());
    }
    return miacode::content_duration::totalContentDurationSeconds(
        chartEndSeconds, state_.previewTrackDurationSeconds_);
}

void miacode::runtime::PlaybackHost::updatePreviewSliderRange()
{
    if (ui_.previewSlider_ == nullptr) {
        return;
    }
    const int maximum = qMax(1, qRound(previewDurationSeconds() * 1000.0));
    // Negative-time intro region (export page, 添加片头 on): the slider extends
    // left to -introDuration so the intro can be scrubbed/played; otherwise 0.
    const int minimum = qMin(0, qRound(exportIntroLowerBoundSeconds() * 1000.0));
    QSignalBlocker blocker(ui_.previewSlider_);
    ui_.previewSlider_->setMinimum(minimum);
    ui_.previewSlider_->setMaximum(maximum);
}

void miacode::runtime::PlaybackHost::updatePreviewSliderPosition(double second)
{
    // The v2 transport's twin of the slider below, and announced before the
    // guards rather than after: the guards are about a v1 widget that may not
    // exist and about a v1 drag, neither of which is a reason to leave the QML
    // transport behind. This is the one place the playhead is published from,
    // which is why the export intro — whose lead-in moves the playhead without
    // ever reaching applyQtPreviewPosition — is now visible to the shell at
    // all. Before this, the intro played while the QML thumb sat at 0.
    emit session_.previewPlayheadChanged();
    if (ui_.previewSlider_ == nullptr || state_.previewScrubDragging_) {
        return;
    }
    // The slider minimum is negative while the intro region is shown, so clamp to
    // the slider's own minimum (not 0) to let the playhead sit in the intro.
    const int value = qBound(
        ui_.previewSlider_->minimum(), qRound(second * 1000.0), ui_.previewSlider_->maximum());
    QSignalBlocker blocker(ui_.previewSlider_);
    ui_.previewSlider_->setValue(value);
}

void miacode::runtime::PlaybackHost::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(noteMarkers);
    state_.previewProgressStatsCache_ = cache;
    if (state_.scene_ != nullptr) {
        state_.scene_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(state_.pauseSecond_);
}

void miacode::runtime::PlaybackHost::clearPreviewObjectStats()
{
    state_.previewProgressStatsCache_.reset();
    if (state_.scene_ != nullptr) {
        state_.scene_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(0.0);
}

int miacode::runtime::PlaybackHost::updatePreviewStatsLayoutMode(int hostWidth)
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
    const QFontMetrics chipMetrics(widthTemplateLabel != nullptr ? widthTemplateLabel->font() : QGuiApplication::font());
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

int miacode::runtime::PlaybackHost::previewStatsMinimumHeightForPanelWidth(int panelWidth) const
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
    const QFontMetrics chipMetrics(widthTemplateLabel != nullptr ? widthTemplateLabel->font() : QGuiApplication::font());
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

double miacode::runtime::PlaybackHost::normalizedPreviewCanvasAspectRatio(double ratio) const
{
    if (!qIsFinite(ratio)) {
        return 1.0;
    }
    return qBound(1.0, ratio, 3.0);
}

void miacode::runtime::PlaybackHost::setPreviewCanvasAspectRatio(double ratio, bool persistState)
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
    session_.refreshQuickShellPreviewCompositeSurfaceState();
    if (ui_.workspaceSplitter_ != nullptr && ui_.previewPanel_ != nullptr && ui_.previewLeftColumn_ != nullptr) {
        const bool restoringToSquare = qAbs(normalized - 1.0) <= 1e-6 && previousRatio > 1.0 + 1e-6;
        const int availableWidth = qMax(0, ui_.workspaceSplitter_->contentsRect().width());
        const int availableHeight = qMax(0, ui_.workspaceSplitter_->contentsRect().height());
        const int leftMinWidth = qMax(
            miacode::window_parity::kWorkspaceContentMinWidth,
            ui_.previewLeftColumn_->minimumWidth());
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
            // Reserve the left column's minimum: a wider aspect ratio must
            // letterbox the preview, never squeeze the content column below
            // its design-width budget (spec: kWorkspaceContentMinWidth).
            qMax(kEmbeddedPreviewPanelMinWidth, availableWidth - leftMinWidth)
        );
        ui_.previewPanel_->setMinimumWidth(clampedRightWidth);
        if (availableWidth > 0) {
            const int leftWidth = qMax(leftMinWidth, availableWidth - clampedRightWidth);
            ui_.workspaceSplitter_->setSizes({leftWidth, clampedRightWidth});
        }
    }
    refreshLayoutAfterPageSwitch();
    if (persistState) {
        session_.savePortableState();
    }
}

void miacode::runtime::PlaybackHost::updatePreviewWorkspaceLayout()
{
    updatePreviewPanelLayout();
    session_.refreshQuickShellRehostedWidgetParent(ui_.outlineDock_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.workspaceContentWidget_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.bottomTabs_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
    session_.shell_->updateEditorFindBarGeometry();
    session_.shell_->applyFindOverlayInset();
}

void miacode::runtime::PlaybackHost::cacheWorkspaceLayoutSizes()
{
}

void miacode::runtime::PlaybackHost::restoreWorkspaceLayoutSizes()
{
}

void miacode::runtime::PlaybackHost::setWorkspacePanelsSwapped(bool swapped, bool persistState)
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
        session_.savePortableState();
    }
}

void miacode::runtime::PlaybackHost::applyWorkspacePanelArrangement()
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

void miacode::runtime::PlaybackHost::refreshLayoutAfterPageSwitch()
{
    MC_OP("miacode::runtime::PlaybackHost::refreshLayoutAfterPageSwitch");
    QElapsedTimer totalTimer;
    totalTimer.start();
    QWidget* currentPageForDiag = ui_.editorStack_ != nullptr ? ui_.editorStack_->currentWidget() : nullptr;
    MIACODE_HANG_PHASE(
        "TimelineSection::refreshLayoutAfterPageSwitch",
        QStringLiteral("current_page=%1").arg(layoutWidgetSummary(currentPageForDiag)));
    if (ui_.previewLeftColumn_ != nullptr) {
        QElapsedTimer stepTimer;
        stepTimer.start();
        MIACODE_HANG_PHASE(
            "TimelineSection::refreshLayoutAfterPageSwitch.previewLeftColumn",
            layoutWidgetSummary(ui_.previewLeftColumn_));
        ui_.previewLeftColumn_->updateGeometry();
        if (QLayout* layout = ui_.previewLeftColumn_->layout(); layout != nullptr) {
            layout->activate();
        }
        const qint64 elapsedMs = stepTimer.elapsed();
        if (elapsedMs >= kPageLayoutStepSlowMs) {
            appendPageLayoutDiag(
                QStringLiteral("refresh_layout_step_slow"),
                QStringLiteral("preview_left_column"),
                ui_.previewLeftColumn_,
                elapsedMs,
                miacode::debug_log::Level::Warn);
        }
    }
    if (ui_.editorStack_ != nullptr) {
        QElapsedTimer stackTimer;
        stackTimer.start();
        MIACODE_HANG_PHASE(
            "TimelineSection::refreshLayoutAfterPageSwitch.editorStack",
            layoutWidgetSummary(ui_.editorStack_));
        ui_.editorStack_->updateGeometry();
        // updateGeometry() alone only marks the stack's size hint dirty — it does
        // NOT re-lay-out the current page. The export page inserts a heavy embedded
        // export page on entry, so its (and any freshly-shown page's)
        // internal layout must be invalidated + activated here, or its children
        // keep the geometry they were first built with. invalidate() clears cached
        // sizeHints so the just-inserted panel is measured fresh; activate() does a
        // full geometry pass that cascades into the panel's own nested layout.
        if (QWidget* currentPage = ui_.editorStack_->currentWidget(); currentPage != nullptr) {
            if (QLayout* pageLayout = currentPage->layout(); pageLayout != nullptr) {
                QElapsedTimer pageTimer;
                pageTimer.start();
                MIACODE_HANG_PHASE(
                    "TimelineSection::refreshLayoutAfterPageSwitch.currentPage.activate",
                    layoutWidgetSummary(currentPage));
                pageLayout->invalidate();
                pageLayout->activate();
                const qint64 elapsedMs = pageTimer.elapsed();
                if (elapsedMs >= kPageLayoutStepSlowMs) {
                    appendPageLayoutDiag(
                        QStringLiteral("refresh_layout_step_slow"),
                        QStringLiteral("current_page_layout_activate"),
                        currentPage,
                        elapsedMs,
                        miacode::debug_log::Level::Warn);
                }
            }
            currentPage->updateGeometry();
            currentPage->update();
        }
        const qint64 elapsedMs = stackTimer.elapsed();
        if (elapsedMs >= kPageLayoutStepSlowMs) {
            appendPageLayoutDiag(
                QStringLiteral("refresh_layout_step_slow"),
                QStringLiteral("editor_stack"),
                ui_.editorStack_,
                elapsedMs,
                miacode::debug_log::Level::Warn,
                QStringLiteral("current_page=\"%1\"").arg(layoutWidgetSummary(ui_.editorStack_->currentWidget())));
        }
    }
    if (ui_.bottomTabs_ != nullptr) {
        ui_.bottomTabs_->updateGeometry();
    }
    if (ui_.workspaceSplitter_ != nullptr) {
        QElapsedTimer stepTimer;
        stepTimer.start();
        MIACODE_HANG_PHASE(
            "TimelineSection::refreshLayoutAfterPageSwitch.workspaceSplitter",
            layoutWidgetSummary(ui_.workspaceSplitter_));
        ui_.workspaceSplitter_->updateGeometry();
        if (QLayout* layout = ui_.workspaceSplitter_->layout(); layout != nullptr) {
            layout->activate();
        }
        const qint64 elapsedMs = stepTimer.elapsed();
        if (elapsedMs >= kPageLayoutStepSlowMs) {
            appendPageLayoutDiag(
                QStringLiteral("refresh_layout_step_slow"),
                QStringLiteral("workspace_splitter"),
                ui_.workspaceSplitter_,
                elapsedMs,
                miacode::debug_log::Level::Warn);
        }
    }
    session_.refreshQuickShellRehostedWidgetParent(ui_.outlineDock_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.workspaceContentWidget_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.bottomTabs_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
    session_.updateEditorHeaderLayoutMode();
    const qint64 totalMs = totalTimer.elapsed();
    if (totalMs >= kPageLayoutTotalSlowMs) {
        appendPageLayoutDiag(
            QStringLiteral("refresh_layout_total_slow"),
            QStringLiteral("total"),
            currentPageForDiag,
            totalMs,
            miacode::debug_log::Level::Warn);
    }
}

void miacode::runtime::PlaybackHost::updatePreviewPanelLayout(int panelWidthOverride, int panelHeightOverride)
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
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
    session_.updatePreviewPlaybackRateToastGeometry();
}

void miacode::runtime::PlaybackHost::updatePreviewObjectStats(double second)
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
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewControlCard_);
    session_.refreshQuickShellRehostedWidgetParent(ui_.previewStatsCard_);
}

QString miacode::runtime::PlaybackHost::formatPreviewTimestamp(double second) const
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

void miacode::runtime::PlaybackHost::showPreviewSliderTimeHint(int sliderValue)
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
