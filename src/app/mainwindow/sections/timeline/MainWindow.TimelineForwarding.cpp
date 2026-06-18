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
#include "common/ContentDurationConfig.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
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

using namespace miacode::mainwindow::shared;

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

QString MainWindow::previewFrameRateModeStorageValue(PreviewCanvasFrameRateMode mode) const
{
    return timelineSection_->previewFrameRateModeStorageValue(mode);
}

QString MainWindow::previewStageMediaFrameRateModeStorageValue() const
{
    return timelineSection_->previewStageMediaFrameRateModeStorageValue();
}

QString MainWindow::timelineFrameRateModeStorageValue() const
{
    return timelineSection_->timelineFrameRateModeStorageValue();
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

void MainWindow::requestNextFixedIntervalPreviewFrame()
{
    timelineSection_->requestNextFixedIntervalPreviewFrame();
}

void MainWindow::advanceFixedIntervalGateAfterPresent()
{
    timelineSection_->advanceFixedIntervalGateAfterPresent();
}

void MainWindow::requestNextPreviewCanvasFrame()
{
    timelineSection_->requestNextPreviewCanvasFrame();
}

void MainWindow::refreshPreviewFrameRateTimers()
{
    timelineSection_->refreshPreviewFrameRateTimers();
}

void MainWindow::setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    timelineSection_->setPreviewCanvasFrameRateMode(mode, persistState);
}

void MainWindow::setPreviewStageMediaFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    timelineSection_->setPreviewStageMediaFrameRateMode(mode, persistState);
}

void MainWindow::setVideoDecodePrefersSoftware(bool preferSoftware, bool persistState)
{
    timelineSection_->setVideoDecodePrefersSoftware(preferSoftware, persistState);
}

void MainWindow::setTimelineFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    timelineSection_->setTimelineFrameRateMode(mode, persistState);
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
