#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "app/v2/ApplicationServices.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
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

// Stage 4.9d-3 (D-class): local copy of the rehost-refresh helpers, moved
// verbatim from ShellHost's real refreshQuickShellRehostedWidgetParent
// (shell/Runtime.cpp) since the function body touches only the passed-in
// widget and Qt Widgets APIs, not any Session/ShellHost state. ShellHost
// keeps its own copy (DocumentPages.cpp still calls through Session), so
// this is a deliberate, temporary duplication rather than a shared helper.
QString rehostPointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

constexpr qint64 kRehostLayoutStepSlowMs = 50;
constexpr qint64 kRehostLayoutTotalSlowMs = 80;


QString rehostWidgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("(null)");
    }
    return QStringLiteral("class=%1 name=%2 ptr=%3 size=%4x%5 visible=%6 parent=%7")
        .arg(QString::fromUtf8(widget->metaObject()->className()))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(empty)") : widget->objectName())
        .arg(rehostPointerHex(widget))
        .arg(widget->width())
        .arg(widget->height())
        .arg(widget->isVisible() ? 1 : 0)
        .arg(rehostPointerHex(widget->parentWidget()));
}

void appendRehostLayoutDiag(
    const QString& action,
    const QString& step,
    QWidget* widget,
    qint64 elapsedMs,
    miacode::debug_log::Level level = miacode::debug_log::Level::Info)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("layout/rehosted_widget"),
        QStringLiteral("action=%1 step=%2 elapsed_ms=%3 widget=\"%4\"")
            .arg(action, step)
            .arg(elapsedMs)
            .arg(rehostWidgetSummary(widget)),
        /*force=*/false,
        level);
}

void refreshQuickShellRehostedWidgetParent(QWidget* widget)
{
    MC_OP("miacode::runtime::PlaybackCoordinator::refreshQuickShellRehostedWidgetParent");
    if (widget == nullptr) {
        return;
    }
    QElapsedTimer totalTimer;
    totalTimer.start();
    MIACODE_HANG_PHASE(
        "WindowSection::refreshQuickShellRehostedWidgetParent",
        rehostWidgetSummary(widget));
    if (auto* dock = qobject_cast<QDockWidget*>(widget); dock != nullptr) {
        if (QWidget* dockWidget = dock->widget(); dockWidget != nullptr) {
            dockWidget->updateGeometry();
        }
    }
    if (QLayout* layout = widget->layout(); layout != nullptr) {
        QElapsedTimer stepTimer;
        stepTimer.start();
        MIACODE_HANG_PHASE(
            "WindowSection::refreshQuickShellRehostedWidgetParent.widgetLayout",
            rehostWidgetSummary(widget));
        layout->activate();
        const qint64 elapsedMs = stepTimer.elapsed();
        if (elapsedMs >= kRehostLayoutStepSlowMs) {
            appendRehostLayoutDiag(
                QStringLiteral("rehost_refresh_step_slow"),
                QStringLiteral("widget_layout_activate"),
                widget,
                elapsedMs,
                miacode::debug_log::Level::Warn);
        }
    }
    widget->updateGeometry();
    widget->update();
    if (QWidget* parentWidget = widget->parentWidget(); parentWidget != nullptr) {
        if (QLayout* parentLayout = parentWidget->layout(); parentLayout != nullptr) {
            QElapsedTimer stepTimer;
            stepTimer.start();
            MIACODE_HANG_PHASE(
                "WindowSection::refreshQuickShellRehostedWidgetParent.parentLayout",
                rehostWidgetSummary(parentWidget));
            parentLayout->activate();
            const qint64 elapsedMs = stepTimer.elapsed();
            if (elapsedMs >= kRehostLayoutStepSlowMs) {
                appendRehostLayoutDiag(
                    QStringLiteral("rehost_refresh_step_slow"),
                    QStringLiteral("parent_layout_activate"),
                    parentWidget,
                    elapsedMs,
                    miacode::debug_log::Level::Warn);
            }
        }
        parentWidget->updateGeometry();
        parentWidget->update();
    }
    const qint64 totalMs = totalTimer.elapsed();
    if (totalMs >= kRehostLayoutTotalSlowMs) {
        appendRehostLayoutDiag(
            QStringLiteral("rehost_refresh_total_slow"),
            QStringLiteral("total"),
            widget,
            totalMs,
            miacode::debug_log::Level::Warn);
    }
}

}  // namespace

double miacode::runtime::PlaybackCoordinator::previewDurationSeconds() const
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

double miacode::runtime::PlaybackCoordinator::previewPlaybackEndSeconds() const
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

void miacode::runtime::PlaybackCoordinator::publishPreviewPlayhead()
{
    emit services_.shellNotifications().previewPlayheadChanged();
}

void miacode::runtime::PlaybackCoordinator::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(noteMarkers);
    state_.previewProgressStatsCache_ = cache;
    if (state_.scene_ != nullptr) {
        state_.scene_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(state_.pauseSecond_);
}

void miacode::runtime::PlaybackCoordinator::clearPreviewObjectStats()
{
    state_.previewProgressStatsCache_.reset();
    if (state_.scene_ != nullptr) {
        state_.scene_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(0.0);
}

int miacode::runtime::PlaybackCoordinator::updatePreviewStatsLayoutMode(int hostWidth)
{
    // previewStatsGridLayout_ / previewStatsChips_ never exist on this side
    // of the QML migration.
    Q_UNUSED(hostWidth);
    return 0;
}

int miacode::runtime::PlaybackCoordinator::previewStatsMinimumHeightForPanelWidth(int panelWidth) const
{
    const int statsHostWidth = qMax(0, panelWidth - kPreviewPanelMarginX * 2 - 16);
    return miacode::window_parity::computePreviewStatsLayout(statsHostWidth).minCardHeight;
}

double miacode::runtime::PlaybackCoordinator::normalizedPreviewCanvasAspectRatio(double ratio) const
{
    if (!qIsFinite(ratio) || ratio <= 0.0) {
        return 1.0;
    }
    return qBound(1.0, ratio, 3.0);
}

void miacode::runtime::PlaybackCoordinator::setPreviewCanvasAspectRatio(double ratio, bool persistState)
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
    refreshQuickShellPreviewCompositeSurfaceState(state_, owner_);
    refreshLayoutAfterPageSwitch();
    if (persistState) {
        preferences_.savePortableState();
    }
}

void miacode::runtime::PlaybackCoordinator::updatePreviewWorkspaceLayout()
{
    updatePreviewPanelLayout();
    refreshQuickShellRehostedWidgetParent(ui_.workspaceContentWidget_);
    refreshQuickShellRehostedWidgetParent(ui_.bottomTabs_);
    updateEditorFindBarGeometry();
    applyFindOverlayInset();
}

// Stage 4.9d-3 (D-class): moved verbatim from ShellHost::updateEditorFindBarGeometry
// (shell/Interaction.cpp) — pure ui_ widget geometry, no ShellHost-own state.
void miacode::runtime::PlaybackCoordinator::updateEditorFindBarGeometry()
{
    // editorFindBar_ / editorFindGeometryHost_ never exist on this side of
    // the QML migration.
}

// Stage 4.9d-3 (D-class): moved verbatim from ShellHost::applyFindOverlayInset
// (shell/Interaction.cpp) — pure ui_ widget read, no ShellHost-own state.
void miacode::runtime::PlaybackCoordinator::applyFindOverlayInset()
{
}

void miacode::runtime::PlaybackCoordinator::cacheWorkspaceLayoutSizes()
{
}

void miacode::runtime::PlaybackCoordinator::restoreWorkspaceLayoutSizes()
{
}

void miacode::runtime::PlaybackCoordinator::setWorkspacePanelsSwapped(bool swapped, bool persistState)
{
    if (state_.workspacePanelsSwapped_ == swapped) {
        return;
    }

    cacheWorkspaceLayoutSizes();
    state_.workspacePanelsSwapped_ = swapped;
    applyWorkspacePanelArrangement();
    if (persistState) {
        preferences_.savePortableState();
    }
}

void miacode::runtime::PlaybackCoordinator::applyWorkspacePanelArrangement()
{
    refreshLayoutAfterPageSwitch();
}

void miacode::runtime::PlaybackCoordinator::refreshLayoutAfterPageSwitch()
{
    MC_OP("miacode::runtime::PlaybackCoordinator::refreshLayoutAfterPageSwitch");
    QElapsedTimer totalTimer;
    totalTimer.start();
    MIACODE_HANG_PHASE(
        "TimelineSection::refreshLayoutAfterPageSwitch",
        QStringLiteral("current_page=%1").arg(layoutWidgetSummary(nullptr)));
    refreshQuickShellRehostedWidgetParent(ui_.workspaceContentWidget_);
    const qint64 totalMs = totalTimer.elapsed();
    if (totalMs >= kPageLayoutTotalSlowMs) {
        appendPageLayoutDiag(
            QStringLiteral("refresh_layout_total_slow"),
            QStringLiteral("total"),
            nullptr,
            totalMs,
            miacode::debug_log::Level::Warn);
    }
}

void miacode::runtime::PlaybackCoordinator::updatePreviewPanelLayout(int panelWidthOverride, int panelHeightOverride)
{
    Q_UNUSED(panelWidthOverride);
    Q_UNUSED(panelHeightOverride);
    updatePreviewPlaybackRateToastGeometry();
}

void miacode::runtime::PlaybackCoordinator::updatePreviewObjectStats(double second)
{
    Q_UNUSED(second);
}
