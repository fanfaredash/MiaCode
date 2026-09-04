#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

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

using namespace miacode::runtime::shared;

double Session::previewDurationSeconds() const
{
    return playback_->previewDurationSeconds();
}

double Session::previewPlaybackEndSeconds() const
{
    return playback_->previewPlaybackEndSeconds();
}

void Session::updatePreviewSliderRange()
{
    playback_->updatePreviewSliderRange();
}

void Session::updatePreviewSliderPosition(double second)
{
    playback_->updatePreviewSliderPosition(second);
}

void Session::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    playback_->refreshPreviewObjectStatsTotals(noteMarkers);
}

void Session::clearPreviewObjectStats()
{
    playback_->clearPreviewObjectStats();
}

void Session::emitChartSwitchResourceGauge()
{
    playback_->emitChartSwitchResourceGauge();
}

int Session::updatePreviewStatsLayoutMode(int hostWidth)
{
    return playback_->updatePreviewStatsLayoutMode(hostWidth);
}

int Session::previewStatsMinimumHeightForPanelWidth(int panelWidth) const
{
    return playback_->previewStatsMinimumHeightForPanelWidth(panelWidth);
}

double Session::normalizedPreviewCanvasAspectRatio(double ratio) const
{
    return playback_->normalizedPreviewCanvasAspectRatio(ratio);
}

QString Session::previewCanvasFrameRateModeStorageValue() const
{
    return playback_->previewCanvasFrameRateModeStorageValue();
}

QString Session::previewFrameRateModeStorageValue(PreviewCanvasFrameRateMode mode) const
{
    return playback_->previewFrameRateModeStorageValue(mode);
}

QString Session::previewStageMediaFrameRateModeStorageValue() const
{
    return playback_->previewStageMediaFrameRateModeStorageValue();
}

QString Session::timelineFrameRateModeStorageValue() const
{
    return playback_->timelineFrameRateModeStorageValue();
}

double Session::currentPreviewCanvasRefreshRate() const
{
    return playback_->currentPreviewCanvasRefreshRate();
}

bool Session::previewCanvasUsesFrameSwappedPacing() const
{
    return playback_->previewCanvasUsesFrameSwappedPacing();
}

qint64 Session::previewCanvasTargetFrameIntervalNs() const
{
    return playback_->previewCanvasTargetFrameIntervalNs();
}

void Session::resetQtPreviewFixedFramePacing()
{
    playback_->resetQtPreviewFixedFramePacing();
}

void Session::scheduleNextQtPreviewTick()
{
    playback_->scheduleNextQtPreviewTick();
}

void Session::requestNextDisplayRefreshPreviewFrame()
{
    playback_->requestNextDisplayRefreshPreviewFrame();
}

void Session::requestNextFixedIntervalPreviewFrame()
{
    playback_->requestNextFixedIntervalPreviewFrame();
}

void Session::advanceFixedIntervalGateAfterPresent()
{
    playback_->advanceFixedIntervalGateAfterPresent();
}

void Session::requestNextPreviewCanvasFrame()
{
    playback_->requestNextPreviewCanvasFrame();
}

void Session::refreshPreviewFrameRateTimers()
{
    playback_->refreshPreviewFrameRateTimers();
}

void Session::setPreviewCanvasAspectRatio(double ratio, bool persistState)
{
    playback_->setPreviewCanvasAspectRatio(ratio, persistState);
}

void Session::togglePreviewFullscreen()
{
    playback_->togglePreviewFullscreen();
}

void Session::enterPreviewFullscreen()
{
    playback_->enterPreviewFullscreen();
}

void Session::exitPreviewFullscreen()
{
    playback_->exitPreviewFullscreen();
}

void Session::updatePreviewFullscreenButtonAppearance()
{
    playback_->updatePreviewFullscreenButtonAppearance();
}

bool Session::shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const
{
    return playback_->shouldRevealPreviewFullscreenControls(globalCursorPos);
}

QRect Session::previewFullscreenControlCardRect(bool visible) const
{
    return playback_->previewFullscreenControlCardRect(visible);
}

void Session::showPreviewFullscreenControls(bool animate)
{
    playback_->showPreviewFullscreenControls(animate);
}

void Session::hidePreviewFullscreenControls(bool animate)
{
    playback_->hidePreviewFullscreenControls(animate);
}

void Session::schedulePreviewFullscreenControlsAutoHide()
{
    playback_->schedulePreviewFullscreenControlsAutoHide();
}

void Session::pollPreviewFullscreenCursor()
{
    playback_->pollPreviewFullscreenCursor();
}

void Session::updatePreviewFullscreenOverlayGeometry()
{
    playback_->updatePreviewFullscreenOverlayGeometry();
}

void Session::updatePreviewWorkspaceLayout()
{
    playback_->updatePreviewWorkspaceLayout();
}

void Session::cacheWorkspaceLayoutSizes()
{
    playback_->cacheWorkspaceLayoutSizes();
}

void Session::restoreWorkspaceLayoutSizes()
{
    playback_->restoreWorkspaceLayoutSizes();
}

void Session::applyWorkspacePanelArrangement()
{
    playback_->applyWorkspacePanelArrangement();
}

void Session::refreshLayoutAfterPageSwitch()
{
    playback_->refreshLayoutAfterPageSwitch();
}

void Session::updatePreviewPanelLayout(int panelWidthOverride, int panelHeightOverride)
{
    playback_->updatePreviewPanelLayout(panelWidthOverride, panelHeightOverride);
}

void Session::updatePreviewObjectStats(double second)
{
    playback_->updatePreviewObjectStats(second);
}

QString Session::formatPreviewTimestamp(double second) const
{
    return playback_->formatPreviewTimestamp(second);
}

void Session::showPreviewSliderTimeHint(int sliderValue)
{
    playback_->showPreviewSliderTimeHint(sliderValue);
}

// Moved from AnalysisFlow.cpp (stage 4.9d-6: TU boundary split — AnalysisFlow.cpp
// keeps only the Coordinator::-owned analysis dispatch, this is the one
// Session::-owned method it used to carry).
void Session::invalidateDocumentValidationRevision()
{
    ++state_.timelineRevision_;
    emit documentValidationChanged();
}
