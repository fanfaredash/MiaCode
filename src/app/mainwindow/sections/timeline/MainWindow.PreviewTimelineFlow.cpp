#include "MainWindow.TimelineSection.h"
#include "../../MainWindowShared.h"

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
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/latency/LatencyDetectorDialog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

MainWindow::TimelineSection::TimelineSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

#define timelineView_ ui_.timelineView_
#define previewFullscreenControlsWindow_ ui_.previewFullscreenControlsWindow_
#define previewSlider_ ui_.previewSlider_
#define previewCanvas_ state_.previewCanvas_
#define qtPreviewPlaying_ state_.qtPreviewPlaying_
#define pendingTimelineAnalysisRefresh_ state_.pendingTimelineAnalysisRefresh_
#define previewSfxRuntime_ state_.previewSfxRuntime_
#define previewControlCard_ ui_.previewControlCard_
#define previewStatsGridLayout_ ui_.previewStatsGridLayout_
#define qtPreviewPauseSecond_ state_.qtPreviewPauseSecond_
#define previewFullscreenControlsAnimation_ ui_.previewFullscreenControlsAnimation_
#define waveformCacheEntry_ state_.waveformCacheEntry_
#define currentFilePath_ state_.currentFilePath_
#define previewFullscreenActive_ state_.previewFullscreenActive_
#define previewFullscreenWindow_ ui_.previewFullscreenWindow_
#define previewStatsChips_ ui_.previewStatsChips_
#define previewPlaybackRate_ state_.previewPlaybackRate_
#define previewFullscreenControlsOpacityAnimation_ ui_.previewFullscreenControlsOpacityAnimation_
#define qtPreviewTimer_ ui_.qtPreviewTimer_
#define pendingTimelineSlowRefresh_ state_.pendingTimelineSlowRefresh_
#define timelineQuickModel_ state_.timelineQuickModel_
#define swapWorkspaceSidesAction_ ui_.swapWorkspaceSidesAction_
#define previewFullscreenControlsVisible_ state_.previewFullscreenControlsVisible_
#define previewFullscreenButton_ ui_.previewFullscreenButton_
#define previewSpeedButton_ ui_.previewSpeedButton_
#define qtPreviewPendingTimelineCenterView_ state_.qtPreviewPendingTimelineCenterView_
#define previewStatsCard_ ui_.previewStatsCard_
#define editorStack_ ui_.editorStack_
#define previewSeekDebounceTimer_ ui_.previewSeekDebounceTimer_
#define qtPreviewPendingTimelineSecond_ state_.qtPreviewPendingTimelineSecond_
#define metadataExtraEdit_ ui_.metadataExtraEdit_
#define qtPreviewPlaybackEndSecond_ state_.qtPreviewPlaybackEndSecond_
#define previewProgressStatsCache_ state_.previewProgressStatsCache_
#define previewTotalStatsLabel_ ui_.previewTotalStatsLabel_
#define qtPreviewTimelineDirty_ state_.qtPreviewTimelineDirty_
#define previewTrackDurationSeconds_ state_.previewTrackDurationSeconds_
#define lastTrackPath_ state_.lastTrackPath_
#define suppressTimelineCursorSync_ state_.suppressTimelineCursorSync_
#define workspacePanelsSwapped_ state_.workspacePanelsSwapped_
#define previewCanvasFrameRateMode_ state_.previewCanvasFrameRateMode_
#define previewFullscreenHintLabel_ ui_.previewFullscreenHintLabel_
#define latestTimelineNoteMarkerSignature_ state_.latestTimelineNoteMarkerSignature_
#define previewLeftColumn_ ui_.previewLeftColumn_
#define latestTimelineNoteMarkers_ state_.latestTimelineNoteMarkers_
#define firstEdit_ ui_.firstEdit_
#define previewStatsUiTimer_ ui_.previewStatsUiTimer_
#define qtPreviewTimelineTimer_ ui_.qtPreviewTimelineTimer_
#define previewFullscreenCursorTrackingInitialized_ state_.previewFullscreenCursorTrackingInitialized_
#define pendingPreviewPlaybackStart_ state_.pendingPreviewPlaybackStart_
#define qtPreviewLastTimelineSecond_ state_.qtPreviewLastTimelineSecond_
#define qtPreviewNextFixedTickDueNs_ state_.qtPreviewNextFixedTickDueNs_
#define previewSeekHeldArrowLastElapsedMs_ state_.previewSeekHeldArrowLastElapsedMs_
#define qtPreviewAwaitingFrameSwap_ state_.qtPreviewAwaitingFrameSwap_
#define previewHeldSeekTimer_ ui_.previewHeldSeekTimer_
#define previewSeekHeldArrowKey_ state_.previewSeekHeldArrowKey_
#define qtPreviewAwaitingFrameSwapSinceMs_ state_.qtPreviewAwaitingFrameSwapSinceMs_
#define activeDifficultyId_ state_.activeDifficultyId_
#define previewFullscreenHintWindow_ ui_.previewFullscreenHintWindow_
#define previewHeldSeekDirection_ state_.previewHeldSeekDirection_
#define timelineRevision_ state_.timelineRevision_
#define timelineAnalysisIdleTimer_ ui_.timelineAnalysisIdleTimer_
#define qtPreviewStartSecond_ state_.qtPreviewStartSecond_
#define previewSeekHeldArrowElapsed_ state_.previewSeekHeldArrowElapsed_
#define qtPreviewWatchdogElapsed_ state_.qtPreviewWatchdogElapsed_
#define pendingPreviewPlaybackRevision_ state_.pendingPreviewPlaybackRevision_
#define lastTimelineParseChartText_ state_.lastTimelineParseChartText_
#define currentFileLabel_ ui_.currentFileLabel_
#define pendingPreviewPlaybackDifficultyId_ state_.pendingPreviewPlaybackDifficultyId_
#define qtPreviewTimelineElapsed_ state_.qtPreviewTimelineElapsed_
#define qtPreviewTimelineStartSecond_ state_.qtPreviewTimelineStartSecond_
#define previewCanvasAspectRatio_ state_.previewCanvasAspectRatio_
#define lastTimelineParseTimingMetadata_ state_.lastTimelineParseTimingMetadata_
#define timelineSlowWorkerRunning_ state_.timelineSlowWorkerRunning_
#define latencyDetectorDialog_ ui_.latencyDetectorDialog_
#define editorWidget_ ui_.editorWidget_
#define waveformRefreshGeneration_ state_.waveformRefreshGeneration_
#define workspaceSplitter_ ui_.workspaceSplitter_
#define previewFullscreenLastCursorPos_ state_.previewFullscreenLastCursorPos_
#define timelineAnalysisRequestedRevision_ state_.timelineAnalysisRequestedRevision_
#define staticTapOnSlideThresholdMs_ state_.staticTapOnSlideThresholdMs_
#define qtPreviewElapsed_ state_.qtPreviewElapsed_
#define timelineAnalysisWorkerRunning_ state_.timelineAnalysisWorkerRunning_
#define documentDirty_ state_.documentDirty_
#define previewFullscreenControlsTimer_ ui_.previewFullscreenControlsTimer_
#define pausedPreviewMediaSeekPending_ state_.pausedPreviewMediaSeekPending_
#define previewSlideStatsLabel_ ui_.previewSlideStatsLabel_
#define previewTouchStatsLabel_ ui_.previewTouchStatsLabel_
#define previewTapStatsLabel_ ui_.previewTapStatsLabel_
#define previewHoldStatsLabel_ ui_.previewHoldStatsLabel_
#define qtPreviewPlaybackReturnSecond_ state_.qtPreviewPlaybackReturnSecond_
#define bottomTabs_ ui_.bottomTabs_
#define previewBreakStatsLabel_ ui_.previewBreakStatsLabel_
#define outlineDock_ ui_.outlineDock_
#define latestTimelinePreviewSnapshotReady_ state_.latestTimelinePreviewSnapshotReady_
#define latestTimelinePreviewRevision_ state_.latestTimelinePreviewRevision_
#define lastTimelineParseResult_ state_.lastTimelineParseResult_
#define muriRenderOptions_ state_.muriRenderOptions_
#define pendingPreviewPlaybackResumeFromPause_ state_.pendingPreviewPlaybackResumeFromPause_
#define pendingPreviewPlaybackSecond_ state_.pendingPreviewPlaybackSecond_
#define lastTimelineParseDifficultyId_ state_.lastTimelineParseDifficultyId_
#define titleEdit_ ui_.titleEdit_
#define metadataPage_ ui_.metadataPage_
#define previewWarmupPool_ state_.previewWarmupPool_
#define timelineSlowRefreshPool_ state_.timelineSlowRefreshPool_
#define timelineSlowRequestedRevision_ state_.timelineSlowRequestedRevision_
#define lastPreviewNoteMarkerSignature_ state_.lastPreviewNoteMarkerSignature_
#define timelineAnalysisPool_ state_.timelineAnalysisPool_
#define previewPendingSeekSecond_ state_.previewPendingSeekSecond_
#define previewStatsLayoutRows_ state_.previewStatsLayoutRows_
#define previewPendingSeekCenterView_ state_.previewPendingSeekCenterView_
#define previewSliderDragging_ state_.previewSliderDragging_
#define previewStatsLayoutCols_ state_.previewStatsLayoutCols_
#define stopPreviewButton_ ui_.stopPreviewButton_
#define pausePreviewButton_ ui_.pausePreviewButton_
#define muriStaticReferences_ state_.muriStaticReferences_
#define runtimeDebugOutputEnabled_ state_.runtimeDebugOutputEnabled_
#define lastSessionFilePath_ state_.lastSessionFilePath_
#define previewWarmupGeneration_ state_.previewWarmupGeneration_
#define chartPage_ ui_.chartPage_
#define currentFieldDirty_ state_.currentFieldDirty_
#define pendingDeferredValidationUiRefresh_ state_.pendingDeferredValidationUiRefresh_
#define validationCacheByDifficulty_ state_.validationCacheByDifficulty_
#define timelineAnalysisRunningRevision_ state_.timelineAnalysisRunningRevision_
#define muriAnalysisReport_ state_.muriAnalysisReport_
#define timelineSlowRunningRevision_ state_.timelineSlowRunningRevision_
#define pendingDeferredMuriUiRefresh_ state_.pendingDeferredMuriUiRefresh_
#define muriAnalysisReportNoteMarkerSignature_ state_.muriAnalysisReportNoteMarkerSignature_
#define document_ state_.document_

#define statusBar() owner_.statusBar()
#define appendOutput(...) owner_.appendOutput(__VA_ARGS__)
#define clearValidationCache() owner_.clearValidationCache()
#define clearValidationErrors() owner_.clearValidationErrors()
#define clearValidationDecorations() owner_.clearValidationDecorations()
#define updateDirtyState() owner_.updateDirtyState()
#define setMetadataExtraText(...) owner_.setMetadataExtraText(__VA_ARGS__)
#define setLastOpenDirectory(...) owner_.setLastOpenDirectory(__VA_ARGS__)
#define updateLatencyDetectorAvailability() owner_.updateLatencyDetectorAvailability()
#define loadProjectRenderState() owner_.loadProjectRenderState()
#define syncPreviewStageMediaRouteChartPath(...) owner_.syncPreviewStageMediaRouteChartPath(__VA_ARGS__)
#define applyPreviewAudioSettingsToRuntime() owner_.applyPreviewAudioSettingsToRuntime()
#define schedulePreviewSubsystemWarmup() owner_.schedulePreviewSubsystemWarmup()
#define saveProjectRenderState() owner_.saveProjectRenderState()
#define savePortableState() owner_.savePortableState()
#define currentCursorLineCol() owner_.currentCursorLineCol()
#define clearPreviewFollowDecoration() owner_.clearPreviewFollowDecoration()
#define setPreviewFollowDecoration(...) owner_.setPreviewFollowDecoration(__VA_ARGS__)
#define updatePauseButtonAppearance() owner_.updatePauseButtonAppearance()
#define refreshQuickShellRehostedWidgetParent(...) owner_.refreshQuickShellRehostedWidgetParent(__VA_ARGS__)
#define updateEditorFindBarGeometry() owner_.updateEditorFindBarGeometry()
#define applyFindOverlayInset() owner_.applyFindOverlayInset()
#define refreshValidationPanelForActiveField() owner_.refreshValidationPanelForActiveField()
#define refreshMuriDiagnosticsPanel() owner_.refreshMuriDiagnosticsPanel()
#define applyAlignedMuriAnalysisReportToViews() owner_.applyAlignedMuriAnalysisReportToViews()
#define updateEditorValidationSummary() owner_.updateEditorValidationSummary()
#define refreshEditorExtraSelections() owner_.refreshEditorExtraSelections()
#define setValidationTabVisible(...) owner_.setValidationTabVisible(__VA_ARGS__)
#define jumpToLocation(...) owner_.jumpToLocation(__VA_ARGS__)
#define quickShellPreviewUsesSeparateSurface() owner_.quickShellPreviewUsesSeparateSurface()
#define refreshQuickShellPreviewCompositeSurfaceState() owner_.refreshQuickShellPreviewCompositeSurfaceState()
#define quickShellPreviewCompositeWindow() owner_.quickShellPreviewCompositeWindow()
#define previewStageMediaRouteHasVideo() owner_.previewStageMediaRouteHasVideo()
#define startPreviewStageMediaRoutePlayback(...) owner_.startPreviewStageMediaRoutePlayback(__VA_ARGS__)
#define syncPreviewStageMediaRoutePlayback(...) owner_.syncPreviewStageMediaRoutePlayback(__VA_ARGS__)
#define pausePreviewStageMediaRoutePlayback() owner_.pausePreviewStageMediaRoutePlayback()
#define seekPreviewStageMediaRouteWhilePaused(...) owner_.seekPreviewStageMediaRouteWhilePaused(__VA_ARGS__)
#define setPreviewStageMediaRouteObservedPlayheadSecond(...) owner_.setPreviewStageMediaRouteObservedPlayheadSecond(__VA_ARGS__)
#define previewStageMediaRouteCurrentPlaybackSecond() owner_.previewStageMediaRouteCurrentPlaybackSecond()
#define previewUsesStageMediaHostRoute() owner_.previewUsesStageMediaHostRoute()
#define refreshPreviewStageMediaRouteDebugState(...) owner_.refreshPreviewStageMediaRouteDebugState(__VA_ARGS__)
#define updatePreviewStageMediaPresentationMode(...) owner_.updatePreviewStageMediaPresentationMode(__VA_ARGS__)
#define resetPreviewStageMediaRouteTimelineOffset() owner_.resetPreviewStageMediaRouteTimelineOffset()
#define ensurePreviewStageMediaRouteInitialized() owner_.ensurePreviewStageMediaRouteInitialized()
#define ensurePreviewSfxRuntimePrepared() owner_.ensurePreviewSfxRuntimePrepared()
#define applyPreviewStageMediaRoutePlaybackRate(...) owner_.applyPreviewStageMediaRoutePlaybackRate(__VA_ARGS__)
#define preparePreviewStartState() owner_.preparePreviewStartState()
#define updateEditorHeaderLayoutMode() owner_.updateEditorHeaderLayoutMode()

namespace {

constexpr double kTimelineZeroSecondTolerance = 1e-6;
constexpr int kTimelineAnalysisIdleDelayMs = 180;

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
    bool swapped
)
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

double shiftedTimelineSecond(double second, double offsetSeconds)
{
    if (!qIsFinite(second) || !qIsFinite(offsetSeconds)) {
        return second;
    }
    return second + offsetSeconds;
}

QVector<TimelineBeatMarker> shiftedBeatMarkers(
    const QVector<TimelineBeatMarker>& beatMarkers,
    double offsetSeconds
)
{
    QVector<TimelineBeatMarker> shifted = beatMarkers;
    for (TimelineBeatMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
    }
    return shifted;
}

QVector<TimelineNoteMarker> shiftedNoteMarkers(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double offsetSeconds
)
{
    QVector<TimelineNoteMarker> shifted = noteMarkers;
    for (TimelineNoteMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
        if (marker.endSecond >= 0.0) {
            marker.endSecond = shiftedTimelineSecond(marker.endSecond, offsetSeconds);
        }
        if (marker.slideTraceSecond >= 0.0) {
            marker.slideTraceSecond = shiftedTimelineSecond(marker.slideTraceSecond, offsetSeconds);
        }
        if (marker.availableSecond >= 0.0) {
            marker.availableSecond = shiftedTimelineSecond(marker.availableSecond, offsetSeconds);
        }
        for (double& shootSecond : marker.slideSegmentShootSeconds) {
            shootSecond = shiftedTimelineSecond(shootSecond, offsetSeconds);
        }
    }
    return shifted;
}

std::pair<int, int> lineColForTextOffset(const QString& text, int offset)
{
    const int boundedOffset = qBound(0, offset, text.size());
    int line = 1;
    int col = 1;
    for (int index = 0; index < boundedOffset; ++index) {
        if (text.at(index) == QChar('\n')) {
            ++line;
            col = 1;
            continue;
        }
        ++col;
    }
    return {line, col};
}

}  // namespace

void MainWindow::TimelineSection::resetPreviewTrackTimelineOffsets()
{
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackOffsetSeconds(0.0);
    }
    resetPreviewStageMediaRouteTimelineOffset();
}

void MainWindow::TimelineSection::applyWaveformData(const QVector<float>& peaks, double durationSeconds)
{
    previewTrackDurationSeconds_ = qMax(0.0, durationSeconds);
    if (timelineView_ != nullptr) {
        timelineView_->setWaveformData(peaks, 0.0, previewTrackDurationSeconds_);
    }
    updatePreviewSliderRange();
}

void MainWindow::TimelineSection::refreshWaveformCache()
{
    refreshWaveformCache(-1.0);
}

void MainWindow::TimelineSection::refreshWaveformCache(double knownDurationSeconds)
{
    resetPreviewTrackTimelineOffsets();
    if (timelineView_ == nullptr) {
        return;
    }

    ++waveformRefreshGeneration_;
    const quint64 generation = waveformRefreshGeneration_;
    const QString trackPath = lastTrackPath_;
    if (trackPath.isEmpty()) {
        applyWaveformData(QVector<float>(), 0.0);
        return;
    }

    const QFileInfo trackInfo(trackPath);
    if (!trackInfo.exists() || !trackInfo.isFile()) {
        applyWaveformData(QVector<float>(), 0.0);
        return;
    }

    const qint64 fileSize = trackInfo.size();
    const qint64 lastModifiedMs = fileLastModifiedMs(trackInfo);
    const bool cacheMatches =
        waveformCacheEntry_.trackPath == trackPath
        && waveformCacheEntry_.fileSize == fileSize
        && waveformCacheEntry_.lastModifiedMs == lastModifiedMs;
    const bool cacheHasPeaks = cacheMatches && !waveformCacheEntry_.peaks.isEmpty();
    if (cacheHasPeaks) {
        applyWaveformData(
            waveformCacheEntry_.peaks,
            knownDurationSeconds > 0.0 ? knownDurationSeconds : waveformCacheEntry_.durationSeconds
        );
        return;
    }

    if (knownDurationSeconds > 0.0) {
        applyWaveformData(QVector<float>(), knownDurationSeconds);
    } else if (cacheMatches && waveformCacheEntry_.durationSeconds > 0.0) {
        applyWaveformData(QVector<float>(), waveformCacheEntry_.durationSeconds);
    } else {
        applyWaveformData(QVector<float>(), 0.0);
    }

    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = previewWarmupPool_ != nullptr
        ? previewWarmupPool_
        : QThreadPool::globalInstance();
    pool->start([guard, generation, trackPath, fileSize, lastModifiedMs]() {
        double audioDurationSeconds = 0.0;
        QElapsedTimer timer;
        timer.start();
        const QVector<float> peaks = buildWaveformPeaks(trackPath, &audioDurationSeconds, kWaveformPeakCount);
        const qint64 buildElapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, trackPath, fileSize, lastModifiedMs, audioDurationSeconds, peaks, buildElapsedMs]() {
                if (guard.isNull()) {
                    return;
                }
                guard->applyWaveformCacheEntry(
                    generation,
                    trackPath,
                    fileSize,
                    lastModifiedMs,
                    audioDurationSeconds,
                    peaks,
                    buildElapsedMs
                );
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::TimelineSection::applyWaveformCacheEntry(
    quint64 generation,
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs,
    double durationSeconds,
    const QVector<float>& peaks,
    qint64 buildElapsedMs)
{
    Q_UNUSED(buildElapsedMs);

    if (generation != waveformRefreshGeneration_ || lastTrackPath_ != trackPath) {
        return;
    }

    const QFileInfo currentTrackInfo(trackPath);
    if (!currentTrackInfo.exists()
        || currentTrackInfo.size() != fileSize
        || fileLastModifiedMs(currentTrackInfo) != lastModifiedMs) {
        return;
    }

    waveformCacheEntry_.trackPath = trackPath;
    waveformCacheEntry_.fileSize = fileSize;
    waveformCacheEntry_.lastModifiedMs = lastModifiedMs;
    waveformCacheEntry_.durationSeconds = qMax(0.0, durationSeconds);
    waveformCacheEntry_.peaks = peaks;
    applyWaveformData(peaks, waveformCacheEntry_.durationSeconds);
}

bool MainWindow::TimelineSection::hasActiveDifficulty() const
{
    return activeDifficultyId_ > 0 && document_.difficulty(activeDifficultyId_) != nullptr;
}

int MainWindow::TimelineSection::activeDifficultyId() const
{
    return activeDifficultyId_;
}

QString MainWindow::TimelineSection::activeChartText() const
{
    if (!hasActiveDifficulty()) {
        return QString();
    }
    if (editorStack_ != nullptr && editorStack_->currentWidget() == chartPage_) {
        return editorText();
    }
    const SimaiDifficultyData* difficultyData = document_.difficulty(activeDifficultyId_);
    return difficultyData != nullptr ? difficultyData->chart : QString();
}

miacode::simai::SimaiTimingMetadata MainWindow::TimelineSection::currentTimingMetadata() const
{
    if (metadataExtraEdit_ != nullptr) {
        return miacode::simai::buildTimingMetadataFromRawText(metadataExtraEdit_->toPlainText(), true);
    }
    return miacode::simai::buildTimingMetadata(document_);
}

double MainWindow::TimelineSection::parsedFirstSeconds(bool* ok) const
{
    QString rawValue = document_.first;
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && firstEdit_ != nullptr) {
        rawValue = firstEdit_->text();
    }
    bool localOk = false;
    const double value = rawValue.trimmed().isEmpty() ? 0.0 : rawValue.trimmed().toDouble(&localOk);
    if (ok != nullptr) {
        *ok = rawValue.trimmed().isEmpty() ? true : localOk;
    }
    if (rawValue.trimmed().isEmpty()) {
        return 0.0;
    }
    return localOk ? value : 0.0;
}

double MainWindow::TimelineSection::parsedWholeBpm(bool* ok) const
{
    const QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool localOk = false;
        const double value = field.value.trimmed().toDouble(&localOk);
        if (ok != nullptr) {
            *ok = localOk && value > 0.0;
        }
        return (localOk && value > 0.0) ? value : 0.0;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return 0.0;
}

QString MainWindow::TimelineSection::parsedLatencyMeterId() const
{
    return miacode::simai::latencyMeterIdForTimingMetadata(currentTimingMetadata());
}

void MainWindow::TimelineSection::applyLatencyDetectorOffset(double seconds)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    const QString serialized = QString::number(normalized, 'f', 3);
    document_.first = serialized;
    if (firstEdit_ != nullptr) {
        QSignalBlocker blocker(firstEdit_);
        firstEdit_->setText(serialized);
    }
    documentDirty_ = true;
    updateDirtyState();
    resetPreviewTrackTimelineOffsets();
    refreshTimelineMetadata();
}

void MainWindow::TimelineSection::applyLatencyDetectorBpm(double bpm)
{
    if (!qIsFinite(bpm) || bpm <= 0.0) {
        return;
    }
    QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    const QString serializedBpm = QString::number(bpm, 'f', 3);
    bool foundWholeBpm = false;
    for (SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        field.value = serializedBpm;
        foundWholeBpm = true;
        break;
    }
    if (!foundWholeBpm) {
        fields.append(SimaiRawField{QStringLiteral("wholebpm"), serializedBpm});
    }
    document_.extraFields = fields;
    setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    documentDirty_ = true;
    updateDirtyState();
}

void MainWindow::TimelineSection::setCurrentFilePath(const QString& path, bool suppressImmediateRefresh)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const bool pathChanged = normalizedPath != currentFilePath_;
    if (pathChanged) {
        clearValidationCache();
        clearValidationErrors();
        clearValidationDecorations();
        stopQtPreviewPlayback(false);
        if (latencyDetectorDialog_ != nullptr) {
            latencyDetectorDialog_->close();
            latencyDetectorDialog_.clear();
        }
    }
    currentFilePath_ = normalizedPath;
    lastSessionFilePath_ = currentFilePath_;
    if (!currentFilePath_.isEmpty()) {
        setLastOpenDirectory(currentFilePath_);

        const QString siblingTrack = miacode::chart_assets::resolveTrackPath(currentFilePath_);
        if (!siblingTrack.isEmpty()) {
            // Keep preview audio in sync with the currently opened chart directory.
            lastTrackPath_ = siblingTrack;
        } else {
            lastTrackPath_.clear();
        }
    } else {
        lastTrackPath_.clear();
    }
    if (previewCanvas_ != nullptr) {
#ifdef HAVE_QT_MULTIMEDIA
        previewCanvas_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(currentFilePath_));
#else
        previewCanvas_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(currentFilePath_, false));
#endif
    }
    updateWindowTitle();
    updateCurrentFileLabel();
    updateLatencyDetectorAvailability();
    if (pathChanged) {
        loadProjectRenderState();
    }
    syncPreviewStageMediaRouteChartPath(currentFilePath_, lastTrackPath_, qtPreviewPauseSecond_);
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(qMax(0.0, qtPreviewPauseSecond_), false);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
    }
    applyPreviewAudioSettingsToRuntime();
    if (!suppressImmediateRefresh) {
        refreshWaveformCache();
        refreshTimelineMetadata();
    }
    if (pathChanged && previewWarmupGeneration_ > 0) {
        schedulePreviewSubsystemWarmup();
    }
}

void MainWindow::TimelineSection::updateWindowTitle()
{
    QString titleText = document_.title;
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && titleEdit_ != nullptr) {
        titleText = titleEdit_->text();
    }
    if (titleText.trimmed().isEmpty()) {
        titleText = currentFilePath_.isEmpty()
            ? QString("Untitled.simai")
            : QFileInfo(currentFilePath_).fileName();
    }
    const QFontMetrics metrics(owner_.font());
    const QString elided = metrics.elidedText(titleText, Qt::ElideRight, 420);
    const bool dirty = documentDirty_ || currentFieldDirty_;
    owner_.setWindowTitle(QString("MiaCode - %1%2").arg(elided, dirty ? QStringLiteral("[*]") : QString()));
}

void MainWindow::TimelineSection::updateCurrentFileLabel()
{
    if (currentFileLabel_ == nullptr) {
        return;
    }
    if (currentFilePath_.isEmpty()) {
        currentFileLabel_->setText("(unsaved)");
    } else {
        currentFileLabel_->setText(QDir::toNativeSeparators(currentFilePath_));
    }
}

QString MainWindow::TimelineSection::editorText() const
{
    return qobject_cast<PlainCodeEditor*>(editorWidget_)->toPlainText();
}

void MainWindow::TimelineSection::scheduleTimelineRefresh()
{
    if (!hasActiveDifficulty() || timelineView_ == nullptr) {
        return;
    }
    ++timelineRevision_;
    refreshTimelineQuickModelFromCurrentText();
    requestTimelineSlowRefresh();
}

void MainWindow::TimelineSection::refreshTimelineMetadata()
{
    scheduleTimelineRefresh();
}

void MainWindow::TimelineSection::applyTimelineQuickChange(int position, int charsRemoved, int charsAdded)
{
    if (timelineView_ == nullptr || !hasActiveDifficulty()) {
        return;
    }

    const double firstSeconds = parsedFirstSeconds();
    const miacode::simai::SimaiTimingMetadata timingMetadata = currentTimingMetadata();
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextDocument* document = editor != nullptr ? editor->document() : nullptr;
    if (document != nullptr) {
        timelineQuickModel_.applyContentsChange(
            document,
            position,
            charsRemoved,
            charsAdded,
            firstSeconds,
            timingMetadata);
    } else {
        timelineQuickModel_.rebuildFromText(activeChartText(), firstSeconds, timingMetadata);
    }
    timelineView_->setTimelineData(timelineQuickModel_.snapshot());
    updatePreviewSliderRange();
}

void MainWindow::TimelineSection::refreshTimelineQuickModelFromCurrentText()
{
    if (timelineView_ == nullptr || !hasActiveDifficulty()) {
        return;
    }
    timelineQuickModel_.rebuildFromText(activeChartText(), parsedFirstSeconds(), currentTimingMetadata());
    timelineView_->setTimelineData(timelineQuickModel_.snapshot());
    updatePreviewSliderRange();
}

void MainWindow::TimelineSection::applyLatestTimelinePreviewStateToPausedPreview()
{
    if (qtPreviewPlaying_) {
        return;
    }

    const bool noteMarkersChanged = latestTimelineNoteMarkerSignature_ != lastPreviewNoteMarkerSignature_;
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->applyPausedPreviewState(
            latestTimelineNoteMarkers_,
            noteMarkersChanged,
            qtPreviewPauseSecond_);
    }

    refreshPreviewObjectStatsTotals(latestTimelineNoteMarkers_);
    if (previewCanvas_ != nullptr && noteMarkersChanged) {
        previewCanvas_->setNoteMarkers(latestTimelineNoteMarkers_);
    }
    applyAlignedMuriAnalysisReportToViews();
    lastPreviewNoteMarkerSignature_ = latestTimelineNoteMarkerSignature_;
}

void MainWindow::TimelineSection::requestTimelineSlowRefresh()
{
    if (!hasActiveDifficulty()) {
        return;
    }

    pendingTimelineSlowRefresh_.revision = timelineRevision_;
    pendingTimelineSlowRefresh_.difficultyId = activeDifficultyId();
    pendingTimelineSlowRefresh_.chartText = activeChartText();
    pendingTimelineSlowRefresh_.firstSeconds = parsedFirstSeconds();
    pendingTimelineSlowRefresh_.timingMetadata = currentTimingMetadata();
    pendingTimelineSlowRefresh_.chineseUi = UiText::isChineseUi();
    timelineSlowRequestedRevision_ = pendingTimelineSlowRefresh_.revision;
    if (pendingPreviewPlaybackStart_) {
        pendingPreviewPlaybackRevision_ = timelineRevision_;
        pendingPreviewPlaybackDifficultyId_ = activeDifficultyId();
    }
    dispatchTimelineSlowRefresh();
}

void MainWindow::TimelineSection::dispatchTimelineSlowRefresh()
{
    if (timelineSlowWorkerRunning_ || pendingTimelineSlowRefresh_.revision == 0) {
        return;
    }

    const TimelineSlowRefreshRequest request = pendingTimelineSlowRefresh_;
    pendingTimelineSlowRefresh_ = TimelineSlowRefreshRequest();
    timelineSlowWorkerRunning_ = true;
    timelineSlowRunningRevision_ = request.revision;
    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = timelineSlowRefreshPool_ != nullptr
        ? timelineSlowRefreshPool_
        : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        const SimaiNativeParseResult parseResult = SimaiNativeParser::parseForTimeline(
            request.chartText,
            request.timingMetadata);
        const TimelinePreviewRefreshState previewState =
            buildTimelinePreviewRefreshState(parseResult, request.firstSeconds);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, request, parseResult, previewState]() mutable {
                if (guard.isNull()) {
                    return;
                }

                guard->timelineSlowWorkerRunning_ = false;
                if (request.revision != guard->timelineSlowRequestedRevision_
                    || !guard->hasActiveDifficulty()
                    || request.difficultyId != guard->activeDifficultyId()
                    || request.chartText != guard->activeChartText()) {
                    guard->dispatchTimelineSlowRefresh();
                    return;
                }

                guard->lastTimelineParseDifficultyId_ = request.difficultyId;
                guard->lastTimelineParseChartText_ = request.chartText;
                guard->lastTimelineParseTimingMetadata_ = request.timingMetadata;
                guard->lastTimelineParseResult_ = parseResult;
                guard->latestTimelineNoteMarkers_ = previewState.shiftedNoteMarkers;
                guard->latestTimelineNoteMarkerSignature_ = previewState.noteMarkerSignature;
                guard->latestTimelinePreviewRevision_ = request.revision;
                guard->latestTimelinePreviewSnapshotReady_ = true;
                if (!guard->qtPreviewPlaying_) {
                    guard->applyLatestTimelinePreviewStateToPausedPreview();
                }
                guard->scheduleTimelineAnalysisRefresh(request, parseResult, previewState);
                if (guard->pendingPreviewPlaybackStart_
                    && !guard->qtPreviewPlaying_
                    && guard->pendingPreviewPlaybackRevision_ == request.revision
                    && guard->pendingPreviewPlaybackDifficultyId_ == request.difficultyId) {
                    const double pendingSecond = guard->pendingPreviewPlaybackSecond_;
                    const bool resumeFromPause = guard->pendingPreviewPlaybackResumeFromPause_;
                    guard->pendingPreviewPlaybackStart_ = false;
                    guard->startQtPreviewPlayback(pendingSecond, resumeFromPause);
                }
                guard->dispatchTimelineSlowRefresh();
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::TimelineSection::scheduleTimelineAnalysisRefresh(
    const TimelineSlowRefreshRequest& request,
    const SimaiNativeParseResult& parseResult,
    const TimelinePreviewRefreshState& previewState)
{
    pendingTimelineAnalysisRefresh_.revision = request.revision;
    pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    pendingTimelineAnalysisRefresh_.chineseUi = request.chineseUi;
    pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    pendingTimelineAnalysisRefresh_.parseResult = parseResult;
    pendingTimelineAnalysisRefresh_.noteMarkerSignature = previewState.noteMarkerSignature;
    pendingTimelineAnalysisRefresh_.noteMarkers = previewState.shiftedNoteMarkers;
    pendingTimelineAnalysisRefresh_.renderOptions = muriRenderOptions_;
    pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0;
    timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch();
}

bool MainWindow::TimelineSection::scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs)
{
    if (!hasActiveDifficulty()
        || !latestTimelinePreviewSnapshotReady_
        || lastTimelineParseDifficultyId_ != activeDifficultyId()
        || lastTimelineParseChartText_ != activeChartText()
        || lastTimelineParseTimingMetadata_ != currentTimingMetadata()) {
        return false;
    }

    TimelineSlowRefreshRequest request;
    request.revision = latestTimelinePreviewRevision_;
    request.difficultyId = activeDifficultyId();
    request.chartText = lastTimelineParseChartText_;
    request.timingMetadata = lastTimelineParseTimingMetadata_;
    request.chineseUi = UiText::isChineseUi();

    pendingTimelineAnalysisRefresh_.revision = request.revision;
    pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    pendingTimelineAnalysisRefresh_.chineseUi = request.chineseUi;
    pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    pendingTimelineAnalysisRefresh_.parseResult = lastTimelineParseResult_;
    pendingTimelineAnalysisRefresh_.noteMarkerSignature = latestTimelineNoteMarkerSignature_;
    pendingTimelineAnalysisRefresh_.noteMarkers = latestTimelineNoteMarkers_;
    pendingTimelineAnalysisRefresh_.renderOptions = muriRenderOptions_;
    pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0;
    timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch(delayMs);
    return true;
}

void MainWindow::TimelineSection::requestTimelineAnalysisDispatch(int delayMs)
{
    if (pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }
    if (qtPreviewPlaying_) {
        if (timelineAnalysisIdleTimer_ != nullptr) {
            timelineAnalysisIdleTimer_->stop();
        }
        return;
    }
    if (timelineAnalysisIdleTimer_ != nullptr) {
        const int effectiveDelayMs = delayMs >= 0 ? delayMs : kTimelineAnalysisIdleDelayMs;
        timelineAnalysisIdleTimer_->start(effectiveDelayMs);
        return;
    }
    dispatchTimelineAnalysisRefresh();
}

void MainWindow::TimelineSection::dispatchTimelineAnalysisRefresh()
{
    if (!hasActiveDifficulty() || qtPreviewPlaying_ || timelineAnalysisWorkerRunning_ || pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }

    const TimelineAnalysisRefreshRequest request = pendingTimelineAnalysisRefresh_;
    pendingTimelineAnalysisRefresh_ = TimelineAnalysisRefreshRequest();
    timelineAnalysisWorkerRunning_ = true;
    timelineAnalysisRunningRevision_ = request.revision;
    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = timelineAnalysisPool_ != nullptr
        ? timelineAnalysisPool_
        : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        TimelineAnalysisRefreshResult result = buildTimelineAnalysisRefreshResult(request);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) {
                    return;
                }

                guard->timelineAnalysisWorkerRunning_ = false;
                if (result.revision != guard->timelineAnalysisRequestedRevision_
                    || !guard->hasActiveDifficulty()
                    || result.difficultyId != guard->activeDifficultyId()
                    || result.chartText != guard->activeChartText()
                    || result.noteMarkerSignature != guard->latestTimelineNoteMarkerSignature_) {
                    guard->requestTimelineAnalysisDispatch();
                    return;
                }

                ValidationCacheEntry entry;
                entry.chartText = result.chartText;
                entry.chineseUi = result.validationReport.issues.isEmpty() ? UiText::isChineseUi() : result.chineseUi;
                entry.timingMetadata = result.timingMetadata;
                entry.ok = result.validationReport.ok;
                entry.errorCount = result.validationReport.errorCount;
                entry.warningCount = result.validationReport.warningCount;
                entry.lenientNoteCount = result.validationReport.lenientNoteCount;
                entry.lenientErrorCount = result.validationReport.lenientErrorCount;
                entry.strictNoteCount = result.validationReport.strictNoteCount;
                entry.strictErrorCount = result.validationReport.strictErrorCount;
                entry.issues.reserve(result.validationReport.issues.size());
                for (const SimaiNativeValidationIssue& issue : result.validationReport.issues) {
                    ValidationCachedIssue cachedIssue;
                    cachedIssue.line = issue.line;
                    cachedIssue.col = issue.col;
                    cachedIssue.endCol = issue.endCol;
                    cachedIssue.rawMessage = issue.rawMessage;
                    cachedIssue.displayMessage = issue.displayMessage;
                    entry.issues.append(cachedIssue);
                }
                guard->validationCacheByDifficulty_.insert(result.difficultyId, entry);
                guard->pendingDeferredValidationUiRefresh_ = true;
                guard->muriAnalysisReport_ = result.analysisReport;
                guard->muriAnalysisReportNoteMarkerSignature_ = result.noteMarkerSignature;
                guard->muriStaticReferences_ = result.staticReferences;
                guard->pendingDeferredMuriUiRefresh_ = true;
                if (!guard->qtPreviewPlaying_) {
                    guard->applyDeferredAnalysisUiUpdates();
                }
                guard->requestTimelineAnalysisDispatch();
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::TimelineSection::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    muriStaticReferences_ = miacode::muri::buildStaticMuriReferences(
        noteMarkers,
        static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0);
}

double MainWindow::TimelineSection::timelineSecondForCursor(int line, int col) const
{
    return timelineQuickModel_.timelineSecondForCursor(line, col);
}

void MainWindow::TimelineSection::seekTimelineToCursor(int line, int col)
{
    if (timelineView_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    timelineView_->setCursorSeconds(second, false);
    timelineView_->focusCursor(true);
}

void MainWindow::TimelineSection::syncTimelineToEditorCursor(bool centerView)
{
    if (suppressTimelineCursorSync_ || !hasActiveDifficulty() || timelineView_ == nullptr) {
        return;
    }
    const auto [line, col] = currentCursorLineCol();
    const double second = timelineSecondForCursor(line, col);
    timelineView_->setCursorSeconds(second, false);
    if (!qtPreviewPlaying_) {
        timelineView_->focusCursor(centerView);
    }
}

void MainWindow::TimelineSection::navigateTimelineToSecond(double second, bool focusEditor)
{
    if (timelineView_ == nullptr) {
        return;
    }

    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    int line = 1;
    int col = 1;
    double cursorSecond = 0.0;
    timelineQuickModel_.resolveTimelineNavigateCursor(clampedSecond, &line, &col, &cursorSecond);
    const bool previousSuppressState = suppressTimelineCursorSync_;
    suppressTimelineCursorSync_ = true;

    previewPendingSeekSecond_ = clampedSecond;
    previewPendingSeekCenterView_ = true;
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    seekPreviewToSecond(clampedSecond, true);
    timelineView_->setCursorSeconds(cursorSecond, false);
    timelineView_->focusPlayhead(true);

    moveEditorCursorToTimelineLocation(line, col, false, focusEditor, true, true);

    suppressTimelineCursorSync_ = previousSuppressState;

    statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(clampedSecond, 0, 'f', 3)
            .arg(line)
            .arg(col)
    );
}

bool MainWindow::TimelineSection::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    return timelineQuickModel_.resolveNearestTimelineNote(second, lane, line, col, noteSecond);
}

bool MainWindow::TimelineSection::moveEditorCursorToTimelineLocation(
    int line,
    int col,
    bool selectToken,
    bool focusEditor,
    bool centerView,
    bool suppressSignals
)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        return false;
    }

    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        jumpToLocation(line, col);
        return true;
    }

    const QString blockText = block.text();
    const int lineLength = blockText.size();
    int localIndex = qBound(0, col - 1, qMax(0, lineLength));

    QTextCursor cursor(editor->document());
    if (selectToken) {
        const int commentIndex = blockText.indexOf(QStringLiteral("||"));
        const int scanEnd = (commentIndex >= 0) ? commentIndex : lineLength;
        if (localIndex > scanEnd) {
            localIndex = scanEnd;
        }
        auto isDelimiter = [](QChar ch) {
            return ch.isSpace() || ch == QChar('/') || ch == QChar(',') || ch == QChar('`');
        };

        int tokenStart = localIndex;
        while (tokenStart > 0 && !isDelimiter(blockText.at(tokenStart - 1))) {
            --tokenStart;
        }
        int tokenEnd = localIndex;
        while (tokenEnd < scanEnd && !isDelimiter(blockText.at(tokenEnd))) {
            ++tokenEnd;
        }
        if (tokenEnd <= tokenStart) {
            tokenStart = qBound(0, localIndex, lineLength);
            tokenEnd = qMin(lineLength, tokenStart + 1);
        }

        cursor.setPosition(block.position() + tokenStart);
        cursor.setPosition(block.position() + tokenEnd, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(block.position() + localIndex);
    }

    if (suppressSignals) {
        QSignalBlocker blocker(editor);
        editor->setTextCursor(cursor);
    } else {
        editor->setTextCursor(cursor);
    }

    if (centerView) {
        if (QScrollBar* vbar = editor->verticalScrollBar()) {
            const QRect caretRect = editor->cursorRect();
            const int centeredValue = vbar->value() + caretRect.center().y() - (editor->viewport()->height() / 2);
            vbar->setValue(qBound(vbar->minimum(), centeredValue, vbar->maximum()));
        }
    }
    if (focusEditor) {
        editor->setFocus();
        clearPreviewFollowDecoration();
    } else {
        setPreviewFollowDecoration(line, col);
    }
    return true;
}

void MainWindow::TimelineSection::syncEditorCursorToPreviewSecond(double second, bool centerView)
{
    if (suppressTimelineCursorSync_ || timelineView_ == nullptr || !hasActiveDifficulty()) {
        clearPreviewFollowDecoration();
        return;
    }
    if (!timelineView_->followPreviewEnabled()) {
        clearPreviewFollowDecoration();
        return;
    }
    if (!qtPreviewPlaying_) {
        return;
    }

    const auto [currentLine, currentCol] = currentCursorLineCol();
    int line = 1;
    int col = 1;
    double cursorSecond = 0.0;
    const bool resolved = timelineQuickModel_.resolvePreviewFollowCursor(second, &line, &col, &cursorSecond);
    const bool alreadyAtAnchor = (currentLine == line && currentCol == col);

    if (alreadyAtAnchor) {
        if (resolved) {
            setPreviewFollowDecoration(line, col);
        } else {
            clearPreviewFollowDecoration();
        }
        timelineView_->setCursorSeconds(cursorSecond, false);
        return;
    }

    if (moveEditorCursorToTimelineLocation(line, col, false, false, centerView, false)) {
        if (!resolved) {
            clearPreviewFollowDecoration();
        }
        timelineView_->setCursorSeconds(cursorSecond, false);
    }
}

double MainWindow::TimelineSection::previewDurationSeconds() const
{
    double duration = 0.0;
    if (qtPreviewPlaying_ && qtPreviewPlaybackEndSecond_ > 0.0) {
        duration = qMax(duration, qtPreviewPlaybackEndSecond_);
    }
    if (timelineView_ != nullptr) {
        duration = qMax(duration, timelineView_->durationSeconds());
        duration = qMax(duration, timelineView_->playheadSeconds());
        duration = qMax(duration, timelineView_->playbackEntrySeconds());
    }
    duration = qMax(duration, qMax(0.0, qtPreviewPauseSecond_));
    if (previewTrackDurationSeconds_ > 0.0) {
        duration = qMax(duration, previewTrackDurationSeconds_ + 3.0);
    }
    return qMax(0.0, duration);
}

double MainWindow::TimelineSection::previewPlaybackEndSeconds() const
{
    if (qtPreviewPlaying_ && qtPreviewPlaybackEndSecond_ > 0.0) {
        return qMax(0.0, qtPreviewPlaybackEndSecond_);
    }
    double duration = 0.0;
    if (timelineView_ != nullptr) {
        duration = qMax(duration, timelineView_->durationSeconds());
    }
    if (previewTrackDurationSeconds_ > 0.0) {
        duration = qMax(duration, previewTrackDurationSeconds_);
    }
    return qMax(0.0, duration);
}

void MainWindow::TimelineSection::updatePreviewSliderRange()
{
    if (previewSlider_ == nullptr) {
        return;
    }
    const int maximum = qMax(1, qRound(previewDurationSeconds() * 1000.0));
    QSignalBlocker blocker(previewSlider_);
    previewSlider_->setMaximum(maximum);
}

void MainWindow::TimelineSection::updatePreviewSliderPosition(double second)
{
    if (previewSlider_ == nullptr || previewSliderDragging_) {
        return;
    }
    const int value = qBound(0, qRound(second * 1000.0), previewSlider_->maximum());
    QSignalBlocker blocker(previewSlider_);
    previewSlider_->setValue(value);
}

void MainWindow::TimelineSection::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(noteMarkers);
    previewProgressStatsCache_ = cache;
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setProgressStatsCache(previewProgressStatsCache_);
    }
    updatePreviewObjectStats(qtPreviewPauseSecond_);
}

void MainWindow::TimelineSection::clearPreviewObjectStats()
{
    previewProgressStatsCache_.reset();
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setProgressStatsCache(previewProgressStatsCache_);
    }
    updatePreviewObjectStats(0.0);
}

int MainWindow::TimelineSection::updatePreviewStatsLayoutMode(int hostWidth)
{
    if (previewStatsCard_ == nullptr || previewStatsGridLayout_ == nullptr || previewStatsChips_.isEmpty()) {
        return 0;
    }

    const int itemCount = previewStatsChips_.size();
    const QWidget* gridHost = previewStatsGridLayout_->parentWidget();
    const int horizontalSpacing = qMax(0, previewStatsGridLayout_->horizontalSpacing());
    const int verticalSpacing = qMax(0, previewStatsGridLayout_->verticalSpacing());
    const QMargins gridMargins = previewStatsGridLayout_->contentsMargins();
    const int resolvedHostWidth =
        (hostWidth >= 0)
        ? hostWidth
        : ((gridHost != nullptr) ? gridHost->contentsRect().width() : previewStatsCard_->contentsRect().width());
    if (resolvedHostWidth <= 0) {
        return previewStatsCard_->minimumHeight();
    }
    const int chipHeight = qMax(
        miacode::window_parity::kPreviewStatsChipHeight,
        !previewStatsChips_.isEmpty() && previewStatsChips_.constFirst() != nullptr
            ? previewStatsChips_.constFirst()->sizeHint().height()
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
        previewTotalStatsLabel_ != nullptr ? previewTotalStatsLabel_ : previewStatsChips_.constFirst();
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
    const bool structureChanged = (rows != previewStatsLayoutRows_) || (cols != previewStatsLayoutCols_);
    previewStatsLayoutRows_ = rows;
    previewStatsLayoutCols_ = cols;

    const int cardHeight = 16
        + qMax(0, gridMargins.top())
        + qMax(0, gridMargins.bottom())
        + rows * chipHeight
        + qMax(0, rows - 1) * verticalSpacing;
    previewStatsCard_->setMinimumHeight(cardHeight);

    if (structureChanged) {
        while (QLayoutItem* item = previewStatsGridLayout_->takeAt(0)) {
            delete item;
        }
        for (int col = 0; col < 6; ++col) {
            previewStatsGridLayout_->setColumnStretch(col, 0);
            previewStatsGridLayout_->setColumnMinimumWidth(col, 0);
        }
        for (int row = 0; row < 6; ++row) {
            previewStatsGridLayout_->setRowStretch(row, 0);
        }

        for (int i = 0; i < itemCount; ++i) {
            const int row = i / cols;
            const int col = i % cols;
            previewStatsGridLayout_->addWidget(previewStatsChips_.at(i), row, col);
        }
        for (int col = 0; col < cols; ++col) {
            previewStatsGridLayout_->setColumnStretch(col, 1);
        }
        for (int row = 0; row < rows; ++row) {
            previewStatsGridLayout_->setRowStretch(row, 1);
        }
    }

    // Keep chip widths column-driven and independent from text metrics.
    const int totalSpacing = horizontalSpacing * qMax(0, cols - 1);
    const int availableWidth = qMax(0, resolvedHostWidth - gridMargins.left() - gridMargins.right() - totalSpacing);
    const int columnWidth = (cols > 0) ? (availableWidth / cols) : 0;
    for (QLabel* chip : previewStatsChips_) {
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
    if (previewStatsGridLayout_ == nullptr || previewStatsChips_.isEmpty()) {
        return miacode::window_parity::computePreviewStatsLayout(statsHostWidth).minCardHeight;
    }

    const int itemCount = previewStatsChips_.size();
    const int horizontalSpacing = qMax(0, previewStatsGridLayout_->horizontalSpacing());
    const int verticalSpacing = qMax(0, previewStatsGridLayout_->verticalSpacing());
    const QMargins gridMargins = previewStatsGridLayout_->contentsMargins();
    const int chipHeight = qMax(
        miacode::window_parity::kPreviewStatsChipHeight,
        previewStatsChips_.constFirst() != nullptr
            ? previewStatsChips_.constFirst()->sizeHint().height()
            : miacode::window_parity::kPreviewStatsChipHeight
    );
    const QLabel* widthTemplateLabel =
        previewTotalStatsLabel_ != nullptr ? previewTotalStatsLabel_ : previewStatsChips_.constFirst();
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
    switch (previewCanvasFrameRateMode_) {
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
    return previewCanvasFrameRateMode_ == PreviewCanvasFrameRateMode::DisplayRefresh;
}

qint64 MainWindow::TimelineSection::previewCanvasTargetFrameIntervalNs() const
{
    switch (previewCanvasFrameRateMode_) {
    case PreviewCanvasFrameRateMode::Fps120:
        return 1000000000LL / 120LL;
    case PreviewCanvasFrameRateMode::DisplayRefresh:
        return qMax<qint64>(1LL, qRound64(1000000000.0 / currentPreviewCanvasRefreshRate()));
    case PreviewCanvasFrameRateMode::Fps60:
    default:
        return 1000000000LL / 60LL;
    }
}

void MainWindow::TimelineSection::resetQtPreviewFixedFramePacing()
{
    qtPreviewNextFixedTickDueNs_ = -1;
    if (previewCanvasUsesFrameSwappedPacing()) {
        return;
    }
    qtPreviewNextFixedTickDueNs_ = qtPreviewWatchdogElapsed_.nsecsElapsed() + previewCanvasTargetFrameIntervalNs();
}

void MainWindow::TimelineSection::scheduleNextQtPreviewTick()
{
    if (qtPreviewTimer_ == nullptr || !qtPreviewPlaying_) {
        return;
    }
    if (previewCanvasUsesFrameSwappedPacing()) {
        qtPreviewTimer_->start(qMax(1, qtPreviewTimer_->interval()));
        return;
    }
    if (qtPreviewNextFixedTickDueNs_ < 0) {
        resetQtPreviewFixedFramePacing();
    }
    const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
    const qint64 delayNs = qMax<qint64>(0, qtPreviewNextFixedTickDueNs_ - nowNs);
    const int delayMs = delayNs <= 0 ? 0 : qMax(1, static_cast<int>((delayNs + 999999LL) / 1000000LL));
    qtPreviewTimer_->start(delayMs);
}

void MainWindow::TimelineSection::requestNextDisplayRefreshPreviewFrame()
{
    if (!qtPreviewPlaying_
        || previewCanvas_ == nullptr
        || !previewCanvasUsesFrameSwappedPacing()
        || qtPreviewAwaitingFrameSwap_) {
        return;
    }
    qtPreviewAwaitingFrameSwap_ = true;
    qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
    previewCanvas_->update();
    scheduleNextQtPreviewTick();
}

void MainWindow::TimelineSection::refreshPreviewFrameRateTimers()
{
    const int intervalMs = qMax(1, qRound(static_cast<double>(previewCanvasTargetFrameIntervalNs()) / 1000000.0));

    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->setInterval(intervalMs);
    }
}

void MainWindow::TimelineSection::setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    if (previewCanvasFrameRateMode_ == mode) {
        refreshPreviewFrameRateTimers();
        return;
    }
    previewCanvasFrameRateMode_ = mode;
    refreshPreviewFrameRateTimers();
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    resetQtPreviewFixedFramePacing();
    if (qtPreviewPlaying_) {
        if (previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
            requestNextDisplayRefreshPreviewFrame();
        } else {
            scheduleNextQtPreviewTick();
        }
    }
    if (persistState) {
        saveProjectRenderState();
        savePortableState();
    }
}

void MainWindow::TimelineSection::setPreviewCanvasAspectRatio(double ratio, bool persistState)
{
    const double normalized = normalizedPreviewCanvasAspectRatio(ratio);
    if (qAbs(previewCanvasAspectRatio_ - normalized) <= 1e-6) {
        return;
    }
    const double previousRatio = previewCanvasAspectRatio_;
    previewCanvasAspectRatio_ = normalized;
    if (normalized + 1e-6 < previousRatio) {
        updatePreviewWorkspaceLayout();
    } else {
        updatePreviewPanelLayout();
    }
    if (persistState) {
        saveProjectRenderState();
        savePortableState();
    }
}
void MainWindow::TimelineSection::togglePreviewFullscreen()
{
    if (previewFullscreenActive_) {
        exitPreviewFullscreen();
        return;
    }
    enterPreviewFullscreen();
}

void MainWindow::TimelineSection::enterPreviewFullscreen()
{
    if (previewFullscreenActive_) {
        return;
    }
    previewFullscreenActive_ = true;
    previewFullscreenControlsVisible_ = false;
    previewFullscreenCursorTrackingInitialized_ = false;
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
}

void MainWindow::TimelineSection::exitPreviewFullscreen()
{
    if (!previewFullscreenActive_) {
        return;
    }
    previewFullscreenActive_ = false;
    previewFullscreenControlsVisible_ = false;
    previewFullscreenCursorTrackingInitialized_ = false;
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
}

void MainWindow::TimelineSection::updatePreviewFullscreenButtonAppearance()
{
    if (previewFullscreenButton_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(previewFullscreenButton_);
    const QColor iconColor =
        previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : UiTheme::colors().iconPrimary;
    previewFullscreenButton_->setChecked(previewFullscreenActive_);
    previewFullscreenButton_->setText(QString());
    previewFullscreenButton_->setIcon(
        previewFullscreenActive_ ? makePreviewExitFullscreenIcon(iconColor) : makePreviewEnterFullscreenIcon(iconColor)
    );
    previewFullscreenButton_->setToolTip(QString());
}

bool MainWindow::TimelineSection::shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const
{
    if (!previewFullscreenActive_ || previewFullscreenWindow_ == nullptr) {
        return false;
    }

    const QRect windowGlobalRect(
        previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        previewFullscreenWindow_->size()
    );
    if (!windowGlobalRect.contains(globalCursorPos)) {
        return false;
    }

    if (previewFullscreenControlsWindow_ != nullptr
        && previewFullscreenControlsWindow_->isVisible()
        && previewFullscreenControlsWindow_->geometry().contains(globalCursorPos)) {
        return true;
    }

    const int controlsHeight =
        previewControlCard_ != nullptr
            ? qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height())
            : 0;
    const int revealHotzoneHeight = qMin(
        windowGlobalRect.height(),
        qMax(kPreviewFullscreenControlsRevealHotzoneHeight, controlsHeight + kPreviewFullscreenOverlayBottomMargin)
    );
    return globalCursorPos.y() >= windowGlobalRect.bottom() - revealHotzoneHeight;
}

QRect MainWindow::TimelineSection::previewFullscreenControlCardRect(bool visible) const
{
    if (previewFullscreenWindow_ == nullptr || previewControlCard_ == nullptr) {
        return QRect();
    }
    const QRect windowRect = previewFullscreenWindow_->contentsRect();
    if (windowRect.width() <= 0 || windowRect.height() <= 0) {
        return QRect();
    }
    const QPoint globalTopLeft = previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    const int horizontalMargin = qMin(kPreviewFullscreenOverlaySideMargin, qMax(12, windowRect.width() / 20));
    const int availableWidth = qMax(0, windowRect.width() - horizontalMargin * 2);
    if (availableWidth <= 0) {
        return QRect();
    }

    const QSize preferredSize = previewControlCard_->sizeHint().expandedTo(previewControlCard_->minimumSizeHint());
    const int cardWidth = qMax(
        previewControlCard_->minimumSizeHint().width(),
        qMin(availableWidth, kPreviewFullscreenOverlayMaxWidth)
    );
    const int cardHeight = qMax(preferredSize.height(), previewControlCard_->minimumSizeHint().height());
    const int cardX = globalTopLeft.x() + qMax(0, (windowRect.width() - cardWidth) / 2);
    const int visibleY = globalTopLeft.y() + windowRect.height() - cardHeight - kPreviewFullscreenOverlayBottomMargin;
    const int hiddenY = globalTopLeft.y() + windowRect.height() + kPreviewFullscreenOverlayHideOffset;
    return QRect(cardX, visible ? visibleY : hiddenY, cardWidth, cardHeight);
}

void MainWindow::TimelineSection::showPreviewFullscreenControls(bool animate)
{
    if (!previewFullscreenActive_
        || previewFullscreenWindow_ == nullptr
        || previewFullscreenControlsWindow_ == nullptr
        || previewControlCard_ == nullptr
        || previewControlCard_->parentWidget() != previewFullscreenControlsWindow_) {
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(true);
    if (!targetRect.isValid()) {
        return;
    }

    if (previewFullscreenControlsAnimation_ != nullptr) {
        previewFullscreenControlsAnimation_->stop();
    }
    if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
        previewFullscreenControlsOpacityAnimation_->stop();
    }

    QRect currentRect = previewFullscreenControlsWindow_->geometry();
    if (!currentRect.isValid()) {
        currentRect = previewFullscreenControlCardRect(false);
    }
    if (!previewFullscreenControlsWindow_->isVisible()) {
        previewFullscreenControlsWindow_->setGeometry(currentRect);
        previewFullscreenControlsWindow_->setWindowOpacity(0.0);
    }

    previewFullscreenControlsWindow_->show();
    previewFullscreenControlsWindow_->raise();
    previewControlCard_->show();

    const qreal currentOpacity = previewFullscreenControlsWindow_->windowOpacity();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        previewFullscreenControlsWindow_->setGeometry(targetRect);
        previewFullscreenControlsWindow_->setWindowOpacity(1.0);
    } else {
        if (previewFullscreenControlsAnimation_ != nullptr) {
            previewFullscreenControlsAnimation_->setStartValue(currentRect);
            previewFullscreenControlsAnimation_->setEndValue(targetRect);
            previewFullscreenControlsAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
            previewFullscreenControlsOpacityAnimation_->setStartValue(currentOpacity);
            previewFullscreenControlsOpacityAnimation_->setEndValue(1.0);
            previewFullscreenControlsOpacityAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setWindowOpacity(1.0);
        }
    }

    previewFullscreenControlsVisible_ = true;
    schedulePreviewFullscreenControlsAutoHide();
}

void MainWindow::TimelineSection::hidePreviewFullscreenControls(bool animate)
{
    if (!previewFullscreenActive_
        || previewFullscreenWindow_ == nullptr
        || previewFullscreenControlsWindow_ == nullptr
        || previewControlCard_ == nullptr
        || previewControlCard_->parentWidget() != previewFullscreenControlsWindow_) {
        return;
    }

    const bool pointerOverControls =
        previewControlCard_->underMouse()
        || (previewSlider_ != nullptr && previewSlider_->underMouse())
        || (stopPreviewButton_ != nullptr && stopPreviewButton_->underMouse())
        || (pausePreviewButton_ != nullptr && pausePreviewButton_->underMouse())
        || (previewSpeedButton_ != nullptr && previewSpeedButton_->underMouse())
        || (previewFullscreenButton_ != nullptr && previewFullscreenButton_->underMouse());
    const bool speedMenuVisible =
        previewSpeedButton_ != nullptr
        && previewSpeedButton_->menu() != nullptr
        && previewSpeedButton_->menu()->isVisible();
    if (previewSliderDragging_ || pointerOverControls || speedMenuVisible) {
        schedulePreviewFullscreenControlsAutoHide();
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(false);
    if (!targetRect.isValid()) {
        return;
    }

    if (previewFullscreenControlsAnimation_ != nullptr) {
        previewFullscreenControlsAnimation_->stop();
    }
    if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
        previewFullscreenControlsOpacityAnimation_->stop();
    }

    const QRect currentRect = previewFullscreenControlsWindow_->geometry();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        previewFullscreenControlsWindow_->setGeometry(targetRect);
        previewFullscreenControlsWindow_->setWindowOpacity(0.0);
        previewFullscreenControlsWindow_->hide();
    } else {
        if (previewFullscreenControlsAnimation_ != nullptr) {
            previewFullscreenControlsAnimation_->setStartValue(currentRect);
            previewFullscreenControlsAnimation_->setEndValue(targetRect);
            previewFullscreenControlsAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
            previewFullscreenControlsOpacityAnimation_->setStartValue(previewFullscreenControlsWindow_->windowOpacity());
            previewFullscreenControlsOpacityAnimation_->setEndValue(0.0);
            previewFullscreenControlsOpacityAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setWindowOpacity(0.0);
            previewFullscreenControlsWindow_->hide();
        }
    }

    previewFullscreenControlsVisible_ = false;
}

void MainWindow::TimelineSection::schedulePreviewFullscreenControlsAutoHide()
{
    if (!previewFullscreenActive_ || previewFullscreenControlsTimer_ == nullptr) {
        return;
    }
    previewFullscreenControlsTimer_->start(kPreviewFullscreenControlsAutoHideDelayMs);
}

void MainWindow::TimelineSection::pollPreviewFullscreenCursor()
{
    if (!previewFullscreenActive_ || previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QPoint globalCursorPos = QCursor::pos();
    const QRect windowGlobalRect(
        previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        previewFullscreenWindow_->size()
    );
    const bool insideWindow = windowGlobalRect.contains(globalCursorPos);
    if (!insideWindow) {
        if (previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
        previewFullscreenCursorTrackingInitialized_ = false;
        return;
    }

    if (!previewFullscreenCursorTrackingInitialized_) {
        previewFullscreenLastCursorPos_ = globalCursorPos;
        previewFullscreenCursorTrackingInitialized_ = true;
        return;
    }

    if (previewFullscreenLastCursorPos_ != globalCursorPos) {
        previewFullscreenLastCursorPos_ = globalCursorPos;
        if (shouldRevealPreviewFullscreenControls(globalCursorPos)) {
            showPreviewFullscreenControls(true);
        } else if (previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
    }
}

void MainWindow::TimelineSection::updatePreviewFullscreenOverlayGeometry()
{
    if (!previewFullscreenActive_ || previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QRect windowRect = previewFullscreenWindow_->contentsRect();
    const QPoint globalTopLeft = previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    if (previewFullscreenHintWindow_ != nullptr
        && previewFullscreenHintLabel_ != nullptr
        && previewFullscreenHintLabel_->isVisible()) {
        const QSize hintSize = previewFullscreenHintLabel_->sizeHint().expandedTo(QSize(220, 42));
        previewFullscreenHintWindow_->resize(hintSize);
        previewFullscreenHintWindow_->move(
            globalTopLeft.x() + qMax(16, (windowRect.width() - hintSize.width()) / 2),
            globalTopLeft.y() + kPreviewFullscreenHintTopMargin
        );
        previewFullscreenHintLabel_->resize(hintSize);
        previewFullscreenHintLabel_->raise();
        previewFullscreenHintWindow_->raise();
    }

    if (previewFullscreenControlsWindow_ != nullptr
        && previewControlCard_ != nullptr
        && previewControlCard_->parentWidget() == previewFullscreenControlsWindow_) {
        const QRect targetRect = previewFullscreenControlCardRect(previewFullscreenControlsVisible_);
        if (targetRect.isValid()) {
            if (previewFullscreenControlsAnimation_ != nullptr
                && previewFullscreenControlsAnimation_->state() == QAbstractAnimation::Running) {
                previewFullscreenControlsAnimation_->stop();
            }
            previewFullscreenControlsWindow_->setGeometry(targetRect);
            previewFullscreenControlsWindow_->setWindowOpacity(previewFullscreenControlsVisible_ ? 1.0 : 0.0);
            if (previewFullscreenControlsVisible_) {
                previewFullscreenControlsWindow_->show();
            }
            previewFullscreenControlsWindow_->raise();
        }
    }
}

void MainWindow::TimelineSection::updatePreviewWorkspaceLayout()
{
    refreshQuickShellRehostedWidgetParent(outlineDock_);
    refreshQuickShellRehostedWidgetParent(previewLeftColumn_);
    refreshQuickShellRehostedWidgetParent(previewControlCard_);
    refreshQuickShellRehostedWidgetParent(previewStatsCard_);
    updateEditorFindBarGeometry();
    applyFindOverlayInset();
}

void MainWindow::TimelineSection::cacheWorkspaceLayoutSizes()
{
}

void MainWindow::TimelineSection::restoreWorkspaceLayoutSizes()
{
}

void MainWindow::TimelineSection::setWorkspacePanelsSwapped(bool swapped, bool persistState)
{
    if (workspacePanelsSwapped_ == swapped) {
        if (swapWorkspaceSidesAction_ != nullptr) {
            swapWorkspaceSidesAction_->blockSignals(true);
            swapWorkspaceSidesAction_->setChecked(workspacePanelsSwapped_);
            swapWorkspaceSidesAction_->blockSignals(false);
        }
        return;
    }

    cacheWorkspaceLayoutSizes();
    workspacePanelsSwapped_ = swapped;
    applyWorkspacePanelArrangement();
    if (persistState) {
        savePortableState();
    }
}

void MainWindow::TimelineSection::applyWorkspacePanelArrangement()
{
    if (swapWorkspaceSidesAction_ != nullptr) {
        swapWorkspaceSidesAction_->blockSignals(true);
        swapWorkspaceSidesAction_->setChecked(workspacePanelsSwapped_);
        swapWorkspaceSidesAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, workspacePanelsSwapped_)
        );
        swapWorkspaceSidesAction_->blockSignals(false);
    }
    refreshLayoutAfterPageSwitch();
}

void MainWindow::TimelineSection::refreshLayoutAfterPageSwitch()
{
    if (previewLeftColumn_ != nullptr) {
        previewLeftColumn_->updateGeometry();
        if (QLayout* layout = previewLeftColumn_->layout(); layout != nullptr) {
            layout->activate();
        }
    }
    if (editorStack_ != nullptr) {
        editorStack_->updateGeometry();
    }
    if (bottomTabs_ != nullptr) {
        bottomTabs_->updateGeometry();
    }
    if (workspaceSplitter_ != nullptr) {
        workspaceSplitter_->updateGeometry();
        if (QLayout* layout = workspaceSplitter_->layout(); layout != nullptr) {
            layout->activate();
        }
    }
    refreshQuickShellRehostedWidgetParent(outlineDock_);
    refreshQuickShellRehostedWidgetParent(previewLeftColumn_);
    refreshQuickShellRehostedWidgetParent(previewControlCard_);
    refreshQuickShellRehostedWidgetParent(previewStatsCard_);
    updateEditorHeaderLayoutMode();
    if (timelineView_ != nullptr) {
        timelineView_->updateGeometry();
        timelineView_->viewport()->update();
    }
}

void MainWindow::TimelineSection::updatePreviewPanelLayout(int panelWidthOverride, int panelHeightOverride)
{
    Q_UNUSED(panelWidthOverride);
    Q_UNUSED(panelHeightOverride);
    refreshQuickShellRehostedWidgetParent(previewControlCard_);
    refreshQuickShellRehostedWidgetParent(previewStatsCard_);
}

void MainWindow::TimelineSection::updatePreviewObjectStats(double second)
{
    if (previewTapStatsLabel_ == nullptr
        || previewHoldStatsLabel_ == nullptr
        || previewSlideStatsLabel_ == nullptr
        || previewTouchStatsLabel_ == nullptr
        || previewBreakStatsLabel_ == nullptr
        || previewTotalStatsLabel_ == nullptr) {
        return;
    }

    const miacode::preview::scene::PreviewObjectStatsSnapshot stats =
        previewProgressStatsCache_ != nullptr
        ? previewProgressStatsCache_->snapshotAt(second)
        : miacode::preview::scene::PreviewObjectStatsSnapshot();

    const auto fmt = [](const QString& name, int played, int total) {
        return QString("%1  %2/%3")
            .arg(name.leftJustified(5, QChar(' '), true))
            .arg(played)
            .arg(total);
    };
    previewTapStatsLabel_->setText(fmt("Tap", stats.tapPlayed, stats.tapTotal));
    previewHoldStatsLabel_->setText(fmt("Hold", stats.holdPlayed, stats.holdTotal));
    previewSlideStatsLabel_->setText(fmt("Slide", stats.slidePlayed, stats.slideTotal));
    previewTouchStatsLabel_->setText(fmt("Touch", stats.touchPlayed, stats.touchTotal));
    previewBreakStatsLabel_->setText(fmt("Break", stats.breakPlayed, stats.breakTotal));
    previewTotalStatsLabel_->setText(fmt("Total", stats.totalPlayed, stats.totalCount));
    updatePreviewStatsLayoutMode(-1);
    refreshQuickShellRehostedWidgetParent(previewControlCard_);
    refreshQuickShellRehostedWidgetParent(previewStatsCard_);
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
    if (previewSlider_ == nullptr) {
        return;
    }
    const double second = static_cast<double>(sliderValue) / 1000.0;
    QStyleOptionSlider option;
    option.initFrom(previewSlider_);
    option.subControls = QStyle::SC_SliderHandle;
    option.orientation = previewSlider_->orientation();
    option.minimum = previewSlider_->minimum();
    option.maximum = previewSlider_->maximum();
    option.sliderPosition = sliderValue;
    option.sliderValue = sliderValue;
    option.upsideDown = false;
    const QRect handleRect = previewSlider_->style()->subControlRect(
        QStyle::CC_Slider,
        &option,
        QStyle::SC_SliderHandle,
        previewSlider_
    );
    const QPoint global = previewSlider_->mapToGlobal(handleRect.center() + QPoint(0, -18));
    QToolTip::showText(global, formatPreviewTimestamp(second), previewSlider_, previewSlider_->rect(), 600);
}

void MainWindow::TimelineSection::schedulePreviewSeek(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    previewPendingSeekSecond_ = clampedSecond;
    previewPendingSeekCenterView_ = centerView;
    updatePreviewSliderPosition(clampedSecond);
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->start();
    } else {
        seekPreviewToSecond(clampedSecond, centerView);
    }
}

bool MainWindow::TimelineSection::stepPreviewSliderBySeconds(double deltaSeconds, bool centerView)
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
        showPreviewSliderTimeHint(value);
        return true;
    }
    previewSlider_->setValue(value);
    showPreviewSliderTimeHint(value);
    seekPreviewToSecond(static_cast<double>(value) / 1000.0, centerView);
    return true;
}

bool MainWindow::TimelineSection::handlePreviewSliderWheel(QWheelEvent* event)
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
        static_cast<double>(steps) * miacode::preview_interaction::kSeekSingleStepSeconds,
        true
    );
    if (handled) {
        event->accept();
    }
    return handled;
}

void MainWindow::TimelineSection::beginPreviewHeldSeek(int direction, int key)
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

void MainWindow::TimelineSection::stopPreviewHeldSeek(int key)
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

void MainWindow::TimelineSection::applyPreviewHeldSeekTick()
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
            * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds),
        true
    );
}

void MainWindow::TimelineSection::seekPreviewToSecond(double second, bool centerView)
{
    ensurePreviewStageMediaRouteInitialized();
    ensurePreviewSfxRuntimePrepared();
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
    }
    qtPreviewStartSecond_ = clampedSecond;
    qtPreviewPauseSecond_ = clampedSecond;
    qtPreviewTimelineStartSecond_ = clampedSecond;
    qtPreviewTimelineElapsed_.restart();
    qtPreviewPendingTimelineSecond_ = clampedSecond;
    qtPreviewPendingTimelineCenterView_ = centerView;
    qtPreviewTimelineDirty_ = true;
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    syncPausedPreviewMediaTimestamps(clampedSecond);
    applyQtPreviewPosition(clampedSecond, centerView);
    if (timelineView_ != nullptr) {
        timelineView_->focusPlayhead(centerView);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->update();
    }
    updatePreviewSliderPosition(clampedSecond);
}

void MainWindow::TimelineSection::applyPreviewPlaybackRate(double rate)
{
    ensurePreviewStageMediaRouteInitialized();
    const double clampedRate = qMax(0.25, rate);
    if (qFuzzyCompare(previewPlaybackRate_ + 1.0, clampedRate + 1.0)) {
        return;
    }
    previewPlaybackRate_ = clampedRate;
    if (previewSpeedButton_ != nullptr) {
        QString rateText = QString::number(previewPlaybackRate_, 'f', 2);
        while (rateText.endsWith('0')) {
            rateText.chop(1);
        }
        if (rateText.endsWith('.')) {
            rateText.chop(1);
        }
        previewSpeedButton_->setText(QString("%1x").arg(rateText));
        if (QMenu* speedMenu = previewSpeedButton_->menu(); speedMenu != nullptr) {
            const int targetIndex = nearestPreviewPlaybackRateIndex(previewPlaybackRate_);
            const QList<QAction*> actions = speedMenu->actions();
            for (int index = 0; index < actions.size(); ++index) {
                QAction* action = actions[index];
                const QVariant data = action != nullptr ? action->data() : QVariant();
                const bool checked = data.isValid()
                    ? qFuzzyCompare(data.toDouble() + 1.0, previewPlaybackRate_ + 1.0)
                    : (index == targetIndex);
                if (action != nullptr) {
                    action->setChecked(checked);
                }
            }
        }
    }
    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    }
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
        startQtPreviewPlayback(qtPreviewPauseSecond_, true);
    }
}

bool MainWindow::TimelineSection::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    if (!preparePreviewStartState()) {
        pendingPreviewPlaybackStart_ = hasActiveDifficulty();
        pendingPreviewPlaybackResumeFromPause_ = resumeFromPause;
        pendingPreviewPlaybackRevision_ = timelineRevision_;
        pendingPreviewPlaybackDifficultyId_ = activeDifficultyId();
        pendingPreviewPlaybackSecond_ = qBound(0.0, second, previewDurationSeconds());
        return false;
    }

    pendingPreviewPlaybackStart_ = false;

    ensurePreviewStageMediaRouteInitialized();
    ensurePreviewSfxRuntimePrepared();
    applyLatestTimelinePreviewStateToPausedPreview();
    const double startSecond = qBound(0.0, second, previewDurationSeconds());
    const bool hasVideoMedia = previewStageMediaRouteHasVideo();
    const auto applyPlaybackClockState = [this](double initialSecond) {
        qtPreviewStartSecond_ = initialSecond;
        qtPreviewPauseSecond_ = initialSecond;
        qtPreviewLastTimelineSecond_ = initialSecond;
        qtPreviewPendingTimelineSecond_ = initialSecond;
        qtPreviewPendingTimelineCenterView_ = true;
        qtPreviewTimelineDirty_ = false;
        qtPreviewTimelineStartSecond_ = initialSecond;
    };

    qtPreviewPlaybackReturnSecond_ = startSecond;
    qtPreviewPlaybackEndSecond_ = qMax(0.0, previewPlaybackEndSeconds());
    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);

    double effectiveStartSecond = startSecond;
    if (previewSfxRuntime_ != nullptr) {
        effectiveStartSecond = previewSfxRuntime_->startPreviewPlaybackTransaction(
            startSecond,
            resumeFromPause,
            previewPlaybackRate_);
    }

    applyPlaybackClockState(effectiveStartSecond);
    pausedPreviewMediaSeekPending_ = false;
    qtPreviewElapsed_.restart();
    qtPreviewTimelineElapsed_.restart();
    if (timelineView_ != nullptr) {
        timelineView_->setPlaybackEntrySeconds(qtPreviewPlaybackReturnSecond_);
        timelineView_->setPlayheadUpperLimitSeconds(qtPreviewPlaybackEndSecond_);
        if (qFuzzyCompare(timelineView_->playheadSeconds() + 1.0, effectiveStartSecond + 1.0)) {
            timelineView_->focusPlayhead(true);
        } else {
            timelineView_->setPlayheadSeconds(effectiveStartSecond, true);
            timelineView_->focusPlayhead(false);
        }
    }
    if (previewCanvas_ != nullptr) {
        if (!resumeFromPause) {
            previewCanvas_->resetProfilingSession();
        }
        previewCanvas_->setPlayheadSeconds(effectiveStartSecond, false);
    }
    if (hasVideoMedia) {
        startPreviewStageMediaRoutePlayback(effectiveStartSecond);
    }

    qtPreviewPlaying_ = true;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    resetQtPreviewFixedFramePacing();
    if (previewCanvas_ != nullptr && !previewCanvasUsesFrameSwappedPacing()) {
        previewCanvas_->update();
    }
    if (previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        scheduleNextQtPreviewTick();
    }
    if (qtPreviewTimelineTimer_ != nullptr && !qtPreviewTimelineTimer_->isActive()) {
        qtPreviewTimelineTimer_->start();
    }
    if (previewStatsUiTimer_ != nullptr && !previewStatsUiTimer_->isActive()) {
        previewStatsUiTimer_->start();
    }
    syncEditorCursorToPreviewSecond(effectiveStartSecond, false);
    updatePreviewSliderPosition(effectiveStartSecond);
    updatePauseButtonAppearance();
    return true;
}

void MainWindow::TimelineSection::finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage)
{
    stopQtPreviewPlayback(true);
    if (statusBar() != nullptr && !statusMessage.isEmpty()) {
        statusBar()->showMessage(statusMessage);
    }
}

void MainWindow::TimelineSection::stopQtPreviewPlayback(bool keepPosition)
{
    const bool wasPlaying = qtPreviewPlaying_;
    bool pauseSecondCaptured = false;
    if (previewSfxRuntime_ != nullptr) {
        const QtPreviewSfxRuntime::PausePreviewResult pauseResult =
            previewSfxRuntime_->capturePausedPreviewTransaction();
        if (pauseResult.usedBackgroundTrack) {
            qtPreviewPauseSecond_ = pauseResult.pauseSecond;
            pauseSecondCaptured = true;
        }
    }
    if (!pauseSecondCaptured) {
        if (previewStageMediaRouteHasVideo()) {
            qtPreviewPauseSecond_ = previewStageMediaRouteCurrentPlaybackSecond();
        }
    }
    pausePreviewStageMediaRoutePlayback();
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    if (qtPreviewTimelineTimer_ != nullptr) {
        qtPreviewTimelineTimer_->stop();
    }
    if (previewStatsUiTimer_ != nullptr) {
        previewStatsUiTimer_->stop();
    }
    if (!keepPosition) {
        qtPreviewPauseSecond_ = 0.0;
    }
    pausedPreviewMediaSeekPending_ = false;
    if (wasPlaying) {
        qtPreviewPendingTimelineSecond_ = qtPreviewPauseSecond_;
        qtPreviewPendingTimelineCenterView_ = false;
        qtPreviewTimelineDirty_ = true;
    }
    qtPreviewPlaying_ = false;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    qtPreviewNextFixedTickDueNs_ = -1;
    flushQtPreviewTimelinePosition();
    if (timelineView_ != nullptr) {
        timelineView_->focusPlayhead(false);
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->stopAll();
    }
    applyLatestTimelinePreviewStateToPausedPreview();
    owner_.applyDeferredAnalysisUiUpdates();
    if (pendingTimelineAnalysisRefresh_.revision != 0) {
        requestTimelineAnalysisDispatch(0);
    }
    if (runtimeDebugOutputEnabled_ && wasPlaying && previewCanvas_ != nullptr) {
        previewCanvas_->writeProfilingSummaryToFile();
    }
    updatePreviewSliderPosition(qtPreviewPauseSecond_);
    updatePreviewObjectStats(qtPreviewPauseSecond_);
    updatePauseButtonAppearance();
}

void MainWindow::TimelineSection::applyQtPreviewPosition(double second, bool centerView)
{
    qtPreviewPauseSecond_ = second;
    if (!qtPreviewPlaying_
        && timelineView_ != nullptr
        && (qtPreviewLastTimelineSecond_ < 0.0
            || qAbs(second - qtPreviewLastTimelineSecond_) >= kTimelineUiCadenceSeconds)) {
        qtPreviewPendingTimelineSecond_ = second;
        qtPreviewPendingTimelineCenterView_ = qtPreviewPendingTimelineCenterView_ || centerView;
        qtPreviewTimelineDirty_ = true;
        flushQtPreviewTimelinePosition();
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(second, !qtPreviewPlaying_);
    }
    setPreviewStageMediaRouteObservedPlayheadSecond(second);
    refreshPreviewStageMediaRouteDebugState(!qtPreviewPlaying_);
    updatePreviewSliderPosition(second);
    if (!qtPreviewPlaying_) {
        updatePreviewObjectStats(second);
    }
    if (qtPreviewPlaying_) {
        syncEditorCursorToPreviewSecond(second, centerView);
    }
}

void MainWindow::TimelineSection::syncPausedPreviewMediaTimestamps(double second)
{
    seekPreviewStageMediaRouteWhilePaused(second);
}

void MainWindow::TimelineSection::flushQtPreviewTimelinePosition()
{
    if (timelineView_ == nullptr) {
        return;
    }
    if (qtPreviewPlaying_) {
        double second = qMax(
            0.0,
            qtPreviewTimelineStartSecond_ + ((qtPreviewTimelineElapsed_.elapsed() / 1000.0) * previewPlaybackRate_)
        );
        if (previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->hasBackgroundTrack()
            && previewSfxRuntime_->isBackgroundTrackRunning()) {
            second = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
        }
        timelineView_->setPlayheadSeconds(second, true);
        timelineView_->focusPlayhead(false);
        qtPreviewLastTimelineSecond_ = second;
        return;
    }
    if (!qtPreviewTimelineDirty_) {
        return;
    }
    timelineView_->setPlayheadSeconds(qtPreviewPendingTimelineSecond_, qtPreviewPendingTimelineCenterView_);
    timelineView_->focusPlayhead(qtPreviewPendingTimelineCenterView_);
    qtPreviewLastTimelineSecond_ = qtPreviewPendingTimelineSecond_;
    qtPreviewPendingTimelineCenterView_ = false;
    qtPreviewTimelineDirty_ = false;
}

void MainWindow::TimelineSection::onQtPreviewTick()
{
    if (!qtPreviewPlaying_) {
        return;
    }
    double second = 0.0;
    if (previewSfxRuntime_ == nullptr) {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        second = qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    } else {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        const double fallbackSecond = qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
        second = previewSfxRuntime_->syncPreviewPlaybackClockTransaction(fallbackSecond);
    }
    syncPreviewStageMediaRoutePlayback(second);
    const double playbackEndSecond = previewPlaybackEndSeconds();
    if (playbackEndSecond > 0.0
        && second + kTimelineZeroSecondTolerance >= playbackEndSecond) {
        second = playbackEndSecond;
        applyQtPreviewPosition(second, true);
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->drainEvents(second);
        }
        finishQtPreviewPlaybackAndReturnToEntry("Qt preview reached the end of current timeline.");
        return;
    }

    if (previewCanvas_ != nullptr) {
        previewCanvas_->noteTickForProfiling();
    }
    applyQtPreviewPosition(second, true);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->drainEvents(second);
    }
    requestNextDisplayRefreshPreviewFrame();
}

void MainWindow::TimelineSection::jumpToNearestTimelineNote(double second, int lane)
{
    int line = 1;
    int col = 1;
    if (!resolveNearestTimelineNote(second, lane, &line, &col, nullptr)) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    if (!moveEditorCursorToTimelineLocation(line, col, true, true, true, false)) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(qMax(0.0, second), 0, 'f', 3)
            .arg(line)
            .arg(col)
    );
}

bool MainWindow::hasActiveDifficulty() const
{
    return timelineSection_->hasActiveDifficulty();
}

int MainWindow::activeDifficultyId() const
{
    return timelineSection_->activeDifficultyId();
}

QString MainWindow::activeChartText() const
{
    return timelineSection_->activeChartText();
}

miacode::simai::SimaiTimingMetadata MainWindow::currentTimingMetadata() const
{
    return timelineSection_->currentTimingMetadata();
}

double MainWindow::parsedFirstSeconds(bool* ok) const
{
    return timelineSection_->parsedFirstSeconds(ok);
}

double MainWindow::parsedWholeBpm(bool* ok) const
{
    return timelineSection_->parsedWholeBpm(ok);
}

QString MainWindow::parsedLatencyMeterId() const
{
    return timelineSection_->parsedLatencyMeterId();
}

void MainWindow::resetPreviewTrackTimelineOffsets()
{
    timelineSection_->resetPreviewTrackTimelineOffsets();
}

void MainWindow::applyWaveformData(const QVector<float>& peaks, double durationSeconds)
{
    timelineSection_->applyWaveformData(peaks, durationSeconds);
}

void MainWindow::refreshWaveformCache()
{
    timelineSection_->refreshWaveformCache();
}

void MainWindow::refreshWaveformCache(double knownDurationSeconds)
{
    timelineSection_->refreshWaveformCache(knownDurationSeconds);
}

void MainWindow::applyWaveformCacheEntry(
    quint64 generation,
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs,
    double durationSeconds,
    const QVector<float>& peaks,
    qint64 buildElapsedMs)
{
    timelineSection_->applyWaveformCacheEntry(
        generation,
        trackPath,
        fileSize,
        lastModifiedMs,
        durationSeconds,
        peaks,
        buildElapsedMs
    );
}

void MainWindow::applyLatencyDetectorOffset(double seconds)
{
    timelineSection_->applyLatencyDetectorOffset(seconds);
}

void MainWindow::applyLatencyDetectorBpm(double bpm)
{
    timelineSection_->applyLatencyDetectorBpm(bpm);
}

void MainWindow::setCurrentFilePath(const QString& path, bool suppressImmediateRefresh)
{
    timelineSection_->setCurrentFilePath(path, suppressImmediateRefresh);
}

void MainWindow::updateWindowTitle()
{
    timelineSection_->updateWindowTitle();
}

void MainWindow::updateCurrentFileLabel()
{
    timelineSection_->updateCurrentFileLabel();
}

QString MainWindow::editorText() const
{
    return timelineSection_->editorText();
}

void MainWindow::scheduleTimelineRefresh()
{
    timelineSection_->scheduleTimelineRefresh();
}

void MainWindow::refreshTimelineMetadata()
{
    timelineSection_->refreshTimelineMetadata();
}

void MainWindow::applyTimelineQuickChange(int position, int charsRemoved, int charsAdded)
{
    timelineSection_->applyTimelineQuickChange(position, charsRemoved, charsAdded);
}

void MainWindow::refreshTimelineQuickModelFromCurrentText()
{
    timelineSection_->refreshTimelineQuickModelFromCurrentText();
}

void MainWindow::applyLatestTimelinePreviewStateToPausedPreview()
{
    timelineSection_->applyLatestTimelinePreviewStateToPausedPreview();
}

void MainWindow::requestTimelineSlowRefresh()
{
    timelineSection_->requestTimelineSlowRefresh();
}

void MainWindow::dispatchTimelineSlowRefresh()
{
    timelineSection_->dispatchTimelineSlowRefresh();
}

void MainWindow::scheduleTimelineAnalysisRefresh(
    const TimelineSlowRefreshRequest& request,
    const SimaiNativeParseResult& parseResult,
    const TimelinePreviewRefreshState& previewState)
{
    timelineSection_->scheduleTimelineAnalysisRefresh(request, parseResult, previewState);
}

bool MainWindow::scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs)
{
    return timelineSection_->scheduleTimelineAnalysisRefreshFromLatestPreviewState(delayMs);
}

void MainWindow::requestTimelineAnalysisDispatch(int delayMs)
{
    timelineSection_->requestTimelineAnalysisDispatch(delayMs);
}

void MainWindow::dispatchTimelineAnalysisRefresh()
{
    timelineSection_->dispatchTimelineAnalysisRefresh();
}

void MainWindow::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    timelineSection_->rebuildStaticMuriReferences(noteMarkers);
}

double MainWindow::timelineSecondForCursor(int line, int col) const
{
    return timelineSection_->timelineSecondForCursor(line, col);
}

void MainWindow::seekTimelineToCursor(int line, int col)
{
    timelineSection_->seekTimelineToCursor(line, col);
}

void MainWindow::syncTimelineToEditorCursor(bool centerView)
{
    timelineSection_->syncTimelineToEditorCursor(centerView);
}

void MainWindow::navigateTimelineToSecond(double second, bool focusEditor)
{
    timelineSection_->navigateTimelineToSecond(second, focusEditor);
}

bool MainWindow::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    return timelineSection_->resolveNearestTimelineNote(second, lane, line, col, noteSecond);
}

bool MainWindow::moveEditorCursorToTimelineLocation(
    int line,
    int col,
    bool selectToken,
    bool focusEditor,
    bool centerView,
    bool suppressSignals)
{
    return timelineSection_->moveEditorCursorToTimelineLocation(
        line,
        col,
        selectToken,
        focusEditor,
        centerView,
        suppressSignals
    );
}

void MainWindow::syncEditorCursorToPreviewSecond(double second, bool centerView)
{
    timelineSection_->syncEditorCursorToPreviewSecond(second, centerView);
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

void MainWindow::schedulePreviewSeek(double second, bool centerView)
{
    timelineSection_->schedulePreviewSeek(second, centerView);
}

bool MainWindow::stepPreviewSliderBySeconds(double deltaSeconds, bool centerView)
{
    return timelineSection_->stepPreviewSliderBySeconds(deltaSeconds, centerView);
}

bool MainWindow::handlePreviewSliderWheel(QWheelEvent* event)
{
    return timelineSection_->handlePreviewSliderWheel(event);
}

void MainWindow::beginPreviewHeldSeek(int direction, int key)
{
    timelineSection_->beginPreviewHeldSeek(direction, key);
}

void MainWindow::stopPreviewHeldSeek(int key)
{
    timelineSection_->stopPreviewHeldSeek(key);
}

void MainWindow::applyPreviewHeldSeekTick()
{
    timelineSection_->applyPreviewHeldSeekTick();
}

void MainWindow::seekPreviewToSecond(double second, bool centerView)
{
    timelineSection_->seekPreviewToSecond(second, centerView);
}

void MainWindow::applyPreviewPlaybackRate(double rate)
{
    timelineSection_->applyPreviewPlaybackRate(rate);
}

bool MainWindow::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    return timelineSection_->startQtPreviewPlayback(second, resumeFromPause);
}

void MainWindow::finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage)
{
    timelineSection_->finishQtPreviewPlaybackAndReturnToEntry(statusMessage);
}

void MainWindow::stopQtPreviewPlayback(bool keepPosition)
{
    timelineSection_->stopQtPreviewPlayback(keepPosition);
}

void MainWindow::applyQtPreviewPosition(double second, bool centerView)
{
    timelineSection_->applyQtPreviewPosition(second, centerView);
}

void MainWindow::syncPausedPreviewMediaTimestamps(double second)
{
    timelineSection_->syncPausedPreviewMediaTimestamps(second);
}

void MainWindow::flushQtPreviewTimelinePosition()
{
    timelineSection_->flushQtPreviewTimelinePosition();
}

void MainWindow::onQtPreviewTick()
{
    timelineSection_->onQtPreviewTick();
}

void MainWindow::jumpToNearestTimelineNote(double second, int lane)
{
    timelineSection_->jumpToNearestTimelineNote(second, lane);
}
