#include "MainWindow.TimelineSection.h"
#include "../../MainWindowShared.h"
#include "../dialogs/MainWindow.DialogsSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "../validation/EditorSelectionUtils.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/latency/LatencySandboxController.h"

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

bool MainWindow::TimelineSection::timelineTabIsForeground() const
{
    return owner_.bottomTabsTabVisible(MainWindow::BottomTabsTabId::Timeline)
        && owner_.currentBottomTabsTabId() == MainWindow::BottomTabsTabId::Timeline;
}

bool MainWindow::TimelineSection::quickTimelineBridgeReady() const
{
    return !state_.quickShellUiFocusBridgeMode_ || state_.quickTimelineSurfaceReady_;
}

void MainWindow::TimelineSection::queueTimelineCursorBridgeUpdate(double second, bool centerView)
{
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    state_.pendingQuickTimelineCursorSync_ = true;
    state_.pendingQuickTimelineCursorSecond_ = second;
    state_.pendingQuickTimelineCursorCenterView_ =
        state_.pendingQuickTimelineCursorCenterView_ || centerView;
}

void MainWindow::TimelineSection::scheduleDeferredTimelineBridgeFlush()
{
    const quint64 generation = ++state_.deferredTimelineBridgeFlushGeneration_;
    QTimer::singleShot(0, &owner_, [this, generation]() {
        if (generation != state_.deferredTimelineBridgeFlushGeneration_) {
            return;
        }
        flushDeferredTimelineBridgeState();
    });
}

void MainWindow::TimelineSection::deferTimelineCursorBridgeUpdate(double second, bool centerView)
{
    queueTimelineCursorBridgeUpdate(second, centerView);
    scheduleDeferredTimelineBridgeFlush();
}

void MainWindow::TimelineSection::flushDeferredTimelineBridgeState()
{
    if (state_.timelineQuickStateBridge_ == nullptr
        || !quickTimelineBridgeReady()
        || !timelineTabIsForeground()) {
        return;
    }

    flushQtPreviewTimelinePosition();
    if (!state_.pendingQuickTimelineCursorSync_) {
        return;
    }

    state_.timelineQuickStateBridge_->setCursorSeconds(
        state_.pendingQuickTimelineCursorSecond_,
        state_.pendingQuickTimelineCursorCenterView_);
    state_.pendingQuickTimelineCursorSync_ = false;
    state_.pendingQuickTimelineCursorSecond_ = 0.0;
    state_.pendingQuickTimelineCursorCenterView_ = false;
}

namespace {

constexpr double kTimelineZeroSecondTolerance = 1e-6;
constexpr int kTimelineAnalysisIdleDelayMs = 180;

void appendTimelineInteractionLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/interaction"),
        text
    );
}

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

void appendTimelinePerfLog(const QString& tag, const QString& payload)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        tag,
        payload,
        true
    );
}

}  // namespace

void MainWindow::TimelineSection::invalidatePreviewFollowBindingCache()
{
    state_.previewFollowBindingCacheValid_ = false;
    state_.previewFollowBindingCache_ = TimelineQuickModel::PreviewFollowBinding();
}

bool MainWindow::TimelineSection::cachedPreviewFollowBindingContainsSecond(double second) const
{
    if (!state_.previewFollowBindingCacheValid_ || !state_.previewFollowBindingCache_.resolved) {
        return false;
    }

    const double targetSecond = qMax(0.0, second);
    const TimelineQuickModel::PreviewFollowBinding& binding = state_.previewFollowBindingCache_;
    if (targetSecond + kTimelineZeroSecondTolerance < binding.startSecond) {
        return false;
    }
    if (qIsFinite(binding.endSecondExclusive)
        && targetSecond + kTimelineZeroSecondTolerance >= binding.endSecondExclusive) {
        return false;
    }
    return true;
}

void MainWindow::TimelineSection::cachePreviewFollowBinding(
    const TimelineQuickModel::PreviewFollowBinding& binding)
{
    if (!binding.resolved) {
        invalidatePreviewFollowBindingCache();
        return;
    }
    state_.previewFollowBindingCacheValid_ = true;
    state_.previewFollowBindingCache_ = binding;
}

void MainWindow::TimelineSection::resetPreviewTrackTimelineOffsets()
{
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setBackgroundTrackOffsetSeconds(0.0);
    }
    owner_.resetPreviewStageMediaRouteTimelineOffset();
}

void MainWindow::TimelineSection::applyWaveformData(
    const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData)
{
    state_.previewTrackDurationSeconds_ = waveformData ? qMax(0.0, waveformData->durationSeconds) : 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setWaveformData(waveformData);
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
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }

    ++state_.waveformRefreshGeneration_;
    const quint64 generation = state_.waveformRefreshGeneration_;
    const QString trackPath = state_.lastTrackPath_;
    if (trackPath.isEmpty()) {
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(0.0));
        return;
    }

    const QFileInfo trackInfo(trackPath);
    if (!trackInfo.exists() || !trackInfo.isFile()) {
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(0.0));
        return;
    }

    if (knownDurationSeconds > 0.0) {
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(knownDurationSeconds));
    } else {
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(0.0));
    }

    const QString cacheDirectoryPath = miacode::waveform::waveformCacheDirectoryPath(
        miacode::waveform::projectDataDirectoryPathForFile(state_.currentFilePath_));
    owner_.ensureWaveformCacheService()->requestWaveform(
        trackPath,
        cacheDirectoryPath,
        [this, generation, trackPath](miacode::waveform::WaveformDataPtr waveformData) {
            if (generation != state_.waveformRefreshGeneration_ || state_.lastTrackPath_ != trackPath) {
                return;
            }

            const QFileInfo currentTrackInfo(trackPath);
            if (!currentTrackInfo.exists() || !currentTrackInfo.isFile()) {
                return;
            }
            if (waveformData && waveformData->fileSize >= 0) {
                const qint64 currentLastModifiedMs = fileLastModifiedMs(currentTrackInfo);
                if (currentTrackInfo.size() != waveformData->fileSize
                    || currentLastModifiedMs != waveformData->lastModifiedMs) {
                    return;
                }
            }
            applyWaveformData(waveformData);
        });
}

bool MainWindow::TimelineSection::hasActiveDifficulty() const
{
    return state_.activeDifficultyId_ > 0 && state_.document_.difficulty(state_.activeDifficultyId_) != nullptr;
}

bool MainWindow::TimelineSection::hasPreviewableChart() const
{
    // latencySandboxAuditionActive_ is set while the latency page has its
    // synthesized test chart installed as the preview source.
    // exportPreviewAuditionActive_ is set while the export page has the
    // badge-selected difficulty installed as a playable preview source — so the
    // normal transport plays it even though activeDifficultyId_ == 0 (D4).
    return hasActiveDifficulty()
        || state_.latencySandboxAuditionActive_
        || state_.exportPreviewAuditionActive_;
}

int MainWindow::TimelineSection::activeDifficultyId() const
{
    return state_.activeDifficultyId_;
}

QString MainWindow::TimelineSection::activeChartText() const
{
    if (!hasActiveDifficulty()) {
        return QString();
    }
    if (ui_.editorStack_ != nullptr && ui_.editorStack_->currentWidget() == ui_.chartPage_) {
        return editorText();
    }
    const SimaiDifficultyData* difficultyData = state_.document_.difficulty(state_.activeDifficultyId_);
    return difficultyData != nullptr ? difficultyData->chart : QString();
}

miacode::simai::SimaiTimingMetadata MainWindow::TimelineSection::currentTimingMetadata() const
{
    if (ui_.metadataExtraEdit_ != nullptr) {
        return miacode::simai::buildTimingMetadataFromRawText(ui_.metadataExtraEdit_->toPlainText(), true);
    }
    return miacode::simai::buildTimingMetadata(state_.document_);
}

double MainWindow::TimelineSection::parsedRawFirstSeconds(bool* ok) const
{
    QString rawValue = state_.document_.first;
    // The offset field now lives in the difficulty-page header. Read its live
    // text whenever a difficulty is active so an uncommitted edit reflows the
    // timeline/preview immediately (it commits to document_.first on field save).
    if (hasActiveDifficulty() && ui_.firstEdit_ != nullptr) {
        rawValue = ui_.firstEdit_->text();
    }
    const QString trimmedRawValue = rawValue.trimmed();
    bool localOk = false;
    const double value = trimmedRawValue.isEmpty() ? 0.0 : trimmedRawValue.toDouble(&localOk);
    if (ok != nullptr) {
        *ok = trimmedRawValue.isEmpty() ? true : localOk;
    }
    return (trimmedRawValue.isEmpty() || localOk) ? value : 0.0;
}

double MainWindow::TimelineSection::parsedFirstSeconds(bool* ok) const
{
    return parsedRawFirstSeconds(ok);
}

double MainWindow::TimelineSection::parsedWholeBpm(bool* ok) const
{
    const QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
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
    state_.document_.first = serialized;
    if (ui_.firstEdit_ != nullptr) {
        QSignalBlocker blocker(ui_.firstEdit_);
        ui_.firstEdit_->setText(serialized);
    }
    state_.documentDirty_ = true;
    owner_.updateDirtyState();
    resetPreviewTrackTimelineOffsets();
    refreshTimelineMetadata();
}

void MainWindow::TimelineSection::applyLatencyDetectorBpm(double bpm)
{
    if (!qIsFinite(bpm) || bpm <= 0.0) {
        return;
    }
    QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
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
    state_.document_.extraFields = fields;
    owner_.setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    state_.documentDirty_ = true;
    owner_.updateDirtyState();
}

void MainWindow::TimelineSection::setCurrentFilePath(const QString& path, bool suppressImmediateRefresh)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const bool pathChanged = normalizedPath != state_.currentFilePath_;
    const QString previousTrackPath = state_.lastTrackPath_;
    QString nextTrackPath;
    if (!normalizedPath.isEmpty()) {
        nextTrackPath = miacode::chart_assets::resolveTrackPath(normalizedPath);
    }
    const bool trackPathChanged = previousTrackPath != nextTrackPath;
    if (pathChanged) {
        invalidatePreviewFollowBindingCache();
        owner_.clearValidationCache();
        owner_.clearValidationErrors();
        owner_.clearValidationDecorations();
        stopQtPreviewPlayback(false);
        if (owner_.latencySandboxController() != nullptr) {
            owner_.latencySandboxController()->exitIfActive();
        }
    }
    if ((pathChanged || trackPathChanged) && state_.waveformCacheService_ != nullptr) {
        state_.waveformCacheService_->clear();
    }
    state_.currentFilePath_ = normalizedPath;
    state_.lastSessionFilePath_ = state_.currentFilePath_;
    // Abnormal-exit detection: record which chart this GUI session has
    // open (empty path clears the marker). Cleanly removed again in the
    // two close paths; a marker still present at next startup widens the
    // chart-open recovery prompt to the debounced autosave snapshot.
    miacode::crash_recovery::updateSessionMarker(state_.currentFilePath_);
    const QString projectDataDirectoryPath = resolveProjectDataDirectoryPath(state_.currentFilePath_);
    miacode::debug_log::setSessionProjectLogDirectory(
        projectDataDirectoryPath.isEmpty()
            ? QString()
            : QDir(projectDataDirectoryPath).filePath(QStringLiteral("logs"))
    );
    if (!state_.currentFilePath_.isEmpty()) {
        owner_.setLastOpenDirectory(state_.currentFilePath_);

        if (!nextTrackPath.isEmpty()) {
            // Keep preview audio in sync with the currently opened chart directory.
            state_.lastTrackPath_ = nextTrackPath;
        } else {
            state_.lastTrackPath_.clear();
        }
    } else {
        state_.lastTrackPath_.clear();
    }
    if (state_.previewCanvas_ != nullptr) {
#ifdef HAVE_QT_MULTIMEDIA
        state_.previewCanvas_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(state_.currentFilePath_));
#else
        state_.previewCanvas_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(state_.currentFilePath_, false));
#endif
    }
    updateWindowTitle();
    updateCurrentFileLabel();
    if (pathChanged) {
        owner_.loadProjectRenderState();
    }
    owner_.syncPreviewStageMediaRouteChartPath(state_.currentFilePath_, state_.lastTrackPath_, state_.qtPreviewPauseSecond_, state_.document_.videoPath);  // Phase 4c &video= override
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setPlayheadSeconds(state_.qtPreviewPauseSecond_, false);
    }
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setChartPath(state_.currentFilePath_);
    }
    owner_.applyPreviewAudioSettingsToRuntime();
    if (!suppressImmediateRefresh) {
        refreshWaveformCache();
        refreshTimelineMetadata();
    }
    if (pathChanged && state_.previewWarmupGeneration_ > 0) {
        owner_.schedulePreviewSubsystemWarmup();
    }
}

void MainWindow::TimelineSection::updateWindowTitle()
{
    QString titleText = state_.document_.title;
    if (ui_.editorStack_ != nullptr && ui_.editorStack_->currentWidget() == ui_.metadataPage_ && ui_.titleEdit_ != nullptr) {
        titleText = ui_.titleEdit_->text();
    }
    if (titleText.trimmed().isEmpty()) {
        titleText = state_.currentFilePath_.isEmpty()
            ? QString("Untitled.simai")
            : QFileInfo(state_.currentFilePath_).fileName();
    }
    const QFontMetrics metrics(owner_.font());
    const QString elided = metrics.elidedText(titleText, Qt::ElideRight, 420);
    const bool dirty = state_.documentDirty_ || state_.currentFieldDirty_;
    owner_.setWindowTitle(QString("MiaCode - %1%2").arg(elided, dirty ? QStringLiteral("[*]") : QString()));
}

void MainWindow::TimelineSection::updateCurrentFileLabel()
{
    if (ui_.currentFileLabel_ == nullptr) {
        return;
    }
    if (state_.currentFilePath_.isEmpty()) {
        ui_.currentFileLabel_->setText("(unsaved)");
    } else {
        ui_.currentFileLabel_->setText(QDir::toNativeSeparators(state_.currentFilePath_));
    }
}

QString MainWindow::TimelineSection::editorText() const
{
    return qobject_cast<PlainCodeEditor*>(ui_.editorWidget_)->toPlainText();
}

void MainWindow::TimelineSection::scheduleTimelineRefresh()
{
    if (!hasActiveDifficulty() || state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    ++state_.timelineRevision_;
    refreshTimelineQuickModelFromCurrentText();
    requestTimelineSlowRefresh();
}

void MainWindow::TimelineSection::refreshTimelineMetadata()
{
    scheduleTimelineRefresh();
}

void MainWindow::TimelineSection::applyTimelineQuickChange(int position, int charsRemoved, int charsAdded)
{
    if (state_.timelineQuickStateBridge_ == nullptr || !hasActiveDifficulty()) {
        return;
    }

    miacode::debug_log::MemoryStageScope memScope("preview/mem_stage", "timeline_quick_change");
    QElapsedTimer timer;
    timer.start();

    const double firstSeconds = parsedFirstSeconds();
    const miacode::simai::SimaiTimingMetadata timingMetadata = currentTimingMetadata();
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    QTextDocument* document = editor != nullptr ? editor->document() : nullptr;
    if (document != nullptr) {
        state_.timelineQuickModel_.applyContentsChange(
            document,
            position,
            charsRemoved,
            charsAdded,
            firstSeconds,
            timingMetadata);
    } else {
        state_.timelineQuickModel_.rebuildFromText(activeChartText(), firstSeconds, timingMetadata);
    }
    invalidatePreviewFollowBindingCache();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setTimelineData(state_.timelineQuickModel_.snapshot());
    }
    updatePreviewSliderRange();
    if (state_.runtimeDebugOutputEnabled_) {
        appendTimelinePerfLog(
            QStringLiteral("edit/quick_timeline_perf"),
            QStringLiteral("mode=incremental revision=%1 chars_removed=%2 chars_added=%3 lines=%4 elapsed_ms=%5")
                .arg(state_.timelineRevision_)
                .arg(charsRemoved)
                .arg(charsAdded)
                .arg(state_.timelineQuickModel_.snapshot().lines.size())
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
    }
}

void MainWindow::TimelineSection::refreshTimelineQuickModelFromCurrentText()
{
    if (state_.timelineQuickStateBridge_ == nullptr || !hasActiveDifficulty()) {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    state_.timelineQuickModel_.rebuildFromText(activeChartText(), parsedFirstSeconds(), currentTimingMetadata());
    invalidatePreviewFollowBindingCache();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setTimelineData(state_.timelineQuickModel_.snapshot());
    }
    updatePreviewSliderRange();
    if (state_.runtimeDebugOutputEnabled_) {
        appendTimelinePerfLog(
            QStringLiteral("edit/quick_timeline_perf"),
            QStringLiteral("mode=full revision=%1 lines=%2 elapsed_ms=%3")
                .arg(state_.timelineRevision_)
                .arg(state_.timelineQuickModel_.snapshot().lines.size())
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
    }
}

void MainWindow::TimelineSection::onTimelineHeaderNavigateRequested(double second)
{
    navigateTimelineToSecond(second, true);
}

void MainWindow::TimelineSection::onTimelineUserInteractionStarted()
{
    const bool pauseForViewportLock =
        state_.previewFollowEnabled_ && state_.previewViewportLockEnabled_;
    if (!state_.previewProgressFollowEnabled_ && !pauseForViewportLock) {
        return;
    }
    if (state_.qtPreviewPlaying_) {
        pauseQtPreviewPlaybackExact();
        owner_.updatePauseButtonAppearance();
        if (pauseForViewportLock) {
            const double second = qMax(0.0, state_.qtPreviewPauseSecond_);
            QTimer::singleShot(0, &owner_, [this, second]() {
                if (state_.previewFollowEnabled_ && state_.previewViewportLockEnabled_) {
                    syncEditorCursorToPreviewSecond(second, true, false);
                }
            });
        }
    }
}

void MainWindow::TimelineSection::onTimelineDragStarted()
{
    appendTimelineInteractionLog(
        QStringLiteral("drag_scrub_begin"),
        QString("playing=%1 current_second=%2")
            .arg(state_.qtPreviewPlaying_ ? 1 : 0)
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6));
    stopPreviewHeldSeek();
    QToolTip::hideText();
    state_.previewScrubRenderElapsed_.invalidate();
    if (state_.previewFullscreenActive_) {
        owner_.showPreviewFullscreenControls(false);
    }
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
}

void MainWindow::TimelineSection::onTimelineCenterNavigateRequested(double second)
{
    if (!state_.previewProgressFollowEnabled_) {
        Q_UNUSED(second);
        return;
    }
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    const int elapsedMs = state_.previewScrubRenderElapsed_.isValid()
        ? static_cast<int>(state_.previewScrubRenderElapsed_.elapsed())
        : -1;
    const bool shouldRenderNow = !state_.previewScrubRenderElapsed_.isValid()
        || elapsedMs >= kPreviewScrubRenderIntervalMs;
    if (shouldRenderNow) {
        owner_.ensurePreviewStageMediaRouteInitialized();
        owner_.ensurePreviewSfxRuntimePrepared();
        requestPausedPreviewSeek(clampedSecond, false, false, false);
        state_.previewScrubRenderElapsed_.restart();
    } else {
        schedulePreviewSeek(clampedSecond, false);
    }
}

void MainWindow::TimelineSection::onTimelineWheelNavigateRequested(double second)
{
    if (!state_.previewProgressFollowEnabled_) {
        Q_UNUSED(second);
        if (ui_.previewSeekDebounceTimer_ != nullptr) {
            ui_.previewSeekDebounceTimer_->stop();
        }
        return;
    }
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    seekPreviewToSecond(clampedSecond, false);
}

void MainWindow::TimelineSection::onTimelineDragFinished(double second)
{
    appendTimelineInteractionLog(
        QStringLiteral("drag_scrub_end"),
        QString("second=%1")
            .arg(second, 0, 'f', 6));
    stopPreviewHeldSeek();
    QToolTip::hideText();
    state_.previewScrubRenderElapsed_.invalidate();
    if (!state_.previewProgressFollowEnabled_) {
        Q_UNUSED(second);
        if (state_.previewFullscreenActive_) {
            owner_.showPreviewFullscreenControls(false);
        }
        if (ui_.previewSeekDebounceTimer_ != nullptr) {
            ui_.previewSeekDebounceTimer_->stop();
        }
        return;
    }
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (state_.previewFullscreenActive_) {
        owner_.showPreviewFullscreenControls(false);
    }
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    seekPreviewToSecond(clampedSecond, false);
}

void MainWindow::TimelineSection::onTimelineFollowPreviewToggled(bool enabled)
{
    state_.previewFollowEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setFollowPreviewEnabled(enabled);
    }
    invalidatePreviewFollowBindingCache();
    owner_.savePortableState();
    if (!enabled) {
        owner_.clearPreviewFollowDecoration();
        return;
    }
    if (!hasActiveDifficulty()) {
        return;
    }
    const double second = qMax(0.0, owner_.currentPreviewAuthoritativeAudioClockSecond());
    syncEditorCursorToPreviewSecond(second, state_.qtPreviewPlaying_ && state_.previewViewportLockEnabled_, !state_.qtPreviewPlaying_);
}

void MainWindow::TimelineSection::onTimelineViewportLockToggled(bool enabled)
{
    state_.previewViewportLockEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setViewportLockEnabled(enabled);
    }
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->setViewportLockEnabled(enabled);
    }
    owner_.savePortableState();
    if (!enabled || !hasActiveDifficulty()) {
        return;
    }
    if (state_.qtPreviewPlaying_ && state_.previewFollowEnabled_) {
        const double second = qMax(0.0, owner_.currentPreviewAuthoritativeAudioClockSecond());
        syncEditorCursorToPreviewSecond(second, true, false);
    }
}

void MainWindow::TimelineSection::onTimelineFollowProgressToggled(bool enabled)
{
    state_.previewProgressFollowEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setFollowProgressEnabled(enabled);
    }
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->setFollowProgressEnabled(enabled);
    }
    owner_.savePortableState();
}

void MainWindow::TimelineSection::onTimelineSyncToggled(bool enabled)
{
    state_.timelineSyncEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setTimelineSyncEnabled(enabled);
    }
    owner_.savePortableState();
}

void MainWindow::TimelineSection::applyLatestTimelinePreviewStateToPausedPreview()
{
    if (state_.qtPreviewPlaying_) {
        return;
    }

    miacode::debug_log::MemoryStageScope memScope("preview/mem_stage", "preview_state_push");
    const bool noteMarkersChanged = state_.latestTimelineNoteMarkerSignature_ != state_.lastPreviewNoteMarkerSignature_;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->applyPausedPreviewState(
            state_.latestTimelineNoteMarkers_,
            noteMarkersChanged,
            state_.qtPreviewPauseSecond_,
            state_.previewPlaybackRate_,
            state_.previewTimingSettings_);
    }

    refreshPreviewObjectStatsTotals(state_.latestTimelineNoteMarkers_);
    if (state_.previewCanvas_ != nullptr && noteMarkersChanged) {
        state_.previewCanvas_->setNoteMarkers(state_.latestTimelineNoteMarkers_);
    }
    owner_.applyAlignedMuriAnalysisReportToViews();
    state_.lastPreviewNoteMarkerSignature_ = state_.latestTimelineNoteMarkerSignature_;
}

void MainWindow::TimelineSection::requestTimelineSlowRefresh()
{
    if (!hasActiveDifficulty()) {
        return;
    }

    state_.pendingTimelineSlowRefresh_.revision = state_.timelineRevision_;
    state_.pendingTimelineSlowRefresh_.difficultyId = activeDifficultyId();
    state_.pendingTimelineSlowRefresh_.chartText = activeChartText();
    state_.pendingTimelineSlowRefresh_.firstSeconds = parsedFirstSeconds();
    state_.pendingTimelineSlowRefresh_.timingMetadata = currentTimingMetadata();
    state_.pendingTimelineSlowRefresh_.chineseUi = UiText::isChineseUi();
    state_.timelineSlowRequestedRevision_ = state_.pendingTimelineSlowRefresh_.revision;
    if (state_.pendingPreviewPlaybackStart_) {
        state_.pendingPreviewPlaybackRevision_ = state_.timelineRevision_;
        state_.pendingPreviewPlaybackDifficultyId_ = activeDifficultyId();
    }
    dispatchTimelineSlowRefresh();
}

void MainWindow::TimelineSection::dispatchTimelineSlowRefresh()
{
    if (state_.timelineSlowWorkerRunning_ || state_.pendingTimelineSlowRefresh_.revision == 0) {
        return;
    }

    const TimelineSlowRefreshRequest request = state_.pendingTimelineSlowRefresh_;
    state_.pendingTimelineSlowRefresh_ = TimelineSlowRefreshRequest();
    state_.timelineSlowWorkerRunning_ = true;
    state_.timelineSlowRunningRevision_ = request.revision;
    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = state_.timelineSlowRefreshPool_ != nullptr
        ? state_.timelineSlowRefreshPool_
        : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        miacode::debug_log::MemoryStageScope memScope("preview/mem_stage", "slow_refresh_build");
        SimaiNativeParseResult parseResult;
        TimelinePreviewRefreshState previewState;
        {
            // beta7 probe 2.1 — tight core bracket excludes the invokeMethod result COPY below,
            // so (slow_refresh_build − slow_refresh_core) isolates the in-flight handoff cost.
            miacode::debug_log::MemoryStageScope memScopeCore(
                "preview/mem_stage", "slow_refresh_core");
            parseResult = SimaiNativeParser::parseForTimeline(
                request.chartText,
                request.timingMetadata);
            previewState = buildTimelinePreviewRefreshState(parseResult, request.firstSeconds);
        }
        if (guard.isNull()) {
            return;
        }
        miacode::debug_log::leak_gauge::noteInflightDispatch();
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, request, parseResult, previewState]() mutable {
                miacode::debug_log::leak_gauge::noteInflightApplied();
                if (guard.isNull()) {
                    return;
                }

                guard->state_.timelineSlowWorkerRunning_ = false;
                if (request.revision != guard->state_.timelineSlowRequestedRevision_
                    || !guard->hasActiveDifficulty()
                    || request.difficultyId != guard->activeDifficultyId()
                    || request.chartText != guard->activeChartText()) {
                    guard->dispatchTimelineSlowRefresh();
                    return;
                }

                guard->state_.lastTimelineParseDifficultyId_ = request.difficultyId;
                guard->state_.lastTimelineParseChartText_ = request.chartText;
                guard->state_.lastTimelineParseTimingMetadata_ = request.timingMetadata;
                guard->state_.lastTimelineParseResult_ = parseResult;
                guard->state_.latestTimelineNoteMarkers_ = previewState.shiftedNoteMarkers;
                guard->state_.latestTimelineNoteMarkerSignature_ = previewState.noteMarkerSignature;
                guard->state_.latestTimelinePreviewRevision_ = request.revision;
                guard->state_.latestTimelinePreviewSnapshotReady_ = true;
                if (!guard->state_.qtPreviewPlaying_) {
                    guard->applyLatestTimelinePreviewStateToPausedPreview();
                }
                if (guard->state_.pendingDifficultySwitchPreviewRestore_
                    && guard->state_.pendingDifficultySwitchPreviewRestoreRevision_ == request.revision
                    && guard->state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ == request.difficultyId) {
                    const double restoreSecond = guard->state_.pendingDifficultySwitchPreviewRestoreSecond_;
                    guard->state_.pendingDifficultySwitchPreviewRestore_ = false;
                    guard->state_.pendingDifficultySwitchPreviewRestoreRevision_ = 0;
                    guard->state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ = 0;
                    guard->state_.pendingDifficultySwitchPreviewRestoreSecond_ = 0.0;
                    guard->seekPreviewDiscreteToSecond(restoreSecond, false);
                    guard->deferTimelineCursorBridgeUpdate(restoreSecond, false);
                }
                guard->scheduleTimelineAnalysisRefresh(request, parseResult, previewState);
                if (guard->state_.pendingPreviewPlaybackStart_
                    && !guard->state_.qtPreviewPlaying_
                    && guard->state_.pendingPreviewPlaybackRevision_ == request.revision
                    && guard->state_.pendingPreviewPlaybackDifficultyId_ == request.difficultyId) {
                    const double pendingSecond = guard->state_.pendingPreviewPlaybackSecond_;
                    const bool resumeFromPause = guard->state_.pendingPreviewPlaybackResumeFromPause_;
                    guard->state_.pendingPreviewPlaybackStart_ = false;
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
    state_.pendingTimelineAnalysisRefresh_.revision = request.revision;
    state_.pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    state_.pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    state_.pendingTimelineAnalysisRefresh_.chineseUi = request.chineseUi;
    state_.pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    state_.pendingTimelineAnalysisRefresh_.parseResult = parseResult;
    state_.pendingTimelineAnalysisRefresh_.noteMarkerSignature = previewState.noteMarkerSignature;
    state_.pendingTimelineAnalysisRefresh_.noteMarkers = previewState.shiftedNoteMarkers;
    state_.pendingTimelineAnalysisRefresh_.renderOptions = state_.muriRenderOptions_;
    state_.pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0;
    state_.timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch();
}

bool MainWindow::TimelineSection::scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs)
{
    if (!hasActiveDifficulty()
        || !state_.latestTimelinePreviewSnapshotReady_
        || state_.lastTimelineParseDifficultyId_ != activeDifficultyId()
        || state_.lastTimelineParseChartText_ != activeChartText()
        || state_.lastTimelineParseTimingMetadata_ != currentTimingMetadata()) {
        return false;
    }

    TimelineSlowRefreshRequest request;
    request.revision = state_.latestTimelinePreviewRevision_;
    request.difficultyId = activeDifficultyId();
    request.chartText = state_.lastTimelineParseChartText_;
    request.timingMetadata = state_.lastTimelineParseTimingMetadata_;
    request.chineseUi = UiText::isChineseUi();

    state_.pendingTimelineAnalysisRefresh_.revision = request.revision;
    state_.pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    state_.pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    state_.pendingTimelineAnalysisRefresh_.chineseUi = request.chineseUi;
    state_.pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    state_.pendingTimelineAnalysisRefresh_.parseResult = state_.lastTimelineParseResult_;
    state_.pendingTimelineAnalysisRefresh_.noteMarkerSignature = state_.latestTimelineNoteMarkerSignature_;
    state_.pendingTimelineAnalysisRefresh_.noteMarkers = state_.latestTimelineNoteMarkers_;
    state_.pendingTimelineAnalysisRefresh_.renderOptions = state_.muriRenderOptions_;
    state_.pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0;
    state_.timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch(delayMs);
    return true;
}

void MainWindow::TimelineSection::requestTimelineAnalysisDispatch(int delayMs)
{
    if (state_.pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }
    if (state_.qtPreviewPlaying_) {
        if (ui_.timelineAnalysisIdleTimer_ != nullptr) {
            ui_.timelineAnalysisIdleTimer_->stop();
        }
        return;
    }
    if (ui_.timelineAnalysisIdleTimer_ != nullptr) {
        const int effectiveDelayMs = delayMs >= 0 ? delayMs : kTimelineAnalysisIdleDelayMs;
        ui_.timelineAnalysisIdleTimer_->start(effectiveDelayMs);
        return;
    }
    dispatchTimelineAnalysisRefresh();
}

void MainWindow::TimelineSection::dispatchTimelineAnalysisRefresh()
{
    if (!hasActiveDifficulty() || state_.qtPreviewPlaying_ || state_.timelineAnalysisWorkerRunning_ || state_.pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }

    const TimelineAnalysisRefreshRequest request = state_.pendingTimelineAnalysisRefresh_;
    state_.pendingTimelineAnalysisRefresh_ = TimelineAnalysisRefreshRequest();
    state_.timelineAnalysisWorkerRunning_ = true;
    state_.timelineAnalysisRunningRevision_ = request.revision;
    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = state_.timelineAnalysisPool_ != nullptr
        ? state_.timelineAnalysisPool_
        : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        miacode::debug_log::MemoryStageScope memScope("preview/mem_stage", "analysis_build");
        TimelineAnalysisRefreshResult result;
        {
            // beta7 probe 2.1 — tight core bracket around the parse + Muri-analyze build only.
            miacode::debug_log::MemoryStageScope memScopeCore("preview/mem_stage", "analysis_core");
            result = buildTimelineAnalysisRefreshResult(request);
        }
        if (guard.isNull()) {
            return;
        }
        miacode::debug_log::leak_gauge::noteInflightDispatch();
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                miacode::debug_log::leak_gauge::noteInflightApplied();
                if (guard.isNull()) {
                    return;
                }

                miacode::debug_log::MemoryStageScope memScope("preview/mem_stage", "analysis_apply");
                QElapsedTimer applyTimer;
                applyTimer.start();

                guard->state_.timelineAnalysisWorkerRunning_ = false;
                if (result.revision != guard->state_.timelineAnalysisRequestedRevision_
                    || !guard->hasActiveDifficulty()
                    || result.difficultyId != guard->activeDifficultyId()
                    || result.chartText != guard->activeChartText()
                    || result.noteMarkerSignature != guard->state_.latestTimelineNoteMarkerSignature_) {
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
                const int validationIssueCount = entry.issues.size();
                const int muriDiagnosticCount = result.analysisReport.diagnostics.size();
                const int muriStaticReferenceCount = result.staticReferences.size();
                guard->state_.validationCacheByDifficulty_[result.difficultyId] = std::move(entry);
                guard->state_.pendingDeferredValidationUiRefresh_ = true;
                guard->state_.muriAnalysisReport_ = std::move(result.analysisReport);
                guard->state_.muriAnalysisReport_.revision = ++guard->state_.muriAnalysisReportRevisionCounter_;
                guard->state_.muriAnalysisReportNoteMarkerSignature_ = result.noteMarkerSignature;
                guard->state_.muriStaticReferences_ = std::move(result.staticReferences);
                guard->state_.pendingDeferredMuriUiRefresh_ = true;
                if (!guard->state_.qtPreviewPlaying_) {
                    guard->applyDeferredAnalysisUiUpdates();
                }
                if (guard->state_.runtimeDebugOutputEnabled_) {
                    appendTimelinePerfLog(
                        QStringLiteral("edit/muri_perf"),
                        QStringLiteral("phase=analysis_apply validation_issues=%1 diagnostics=%2 static_refs=%3 elapsed_ms=%4")
                            .arg(validationIssueCount)
                            .arg(muriDiagnosticCount)
                            .arg(muriStaticReferenceCount)
                            .arg(applyTimer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
                    );
                }
                guard->requestTimelineAnalysisDispatch();
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::TimelineSection::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    state_.muriStaticReferences_ = miacode::muri::buildStaticMuriReferences(
        noteMarkers,
        static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0);
}

double MainWindow::TimelineSection::timelineSecondForCursor(int line, int col) const
{
    QElapsedTimer timer;
    timer.start();
    const double second = state_.timelineQuickModel_.timelineSecondForCursor(line, col);
    if (state_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/cursor_map"),
            QStringLiteral("action=timeline_second_for_cursor line=%1 col=%2 second=%3 elapsed_ms=%4")
                .arg(line)
                .arg(col)
                .arg(second, 0, 'f', 6)
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
    }
    return second;
}

void MainWindow::TimelineSection::seekTimelineToCursor(int line, int col)
{
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        if (quickTimelineBridgeReady() && timelineTabIsForeground()) {
            state_.pendingQuickTimelineCursorSync_ = false;
            state_.pendingQuickTimelineCursorSecond_ = 0.0;
            state_.pendingQuickTimelineCursorCenterView_ = false;
            state_.timelineQuickStateBridge_->setCursorSeconds(second, true);
        } else {
            queueTimelineCursorBridgeUpdate(second, true);
        }
    }
}

void MainWindow::TimelineSection::syncTimelineToEditorCursor(bool centerView)
{
    if (state_.suppressTimelineCursorSync_
        || state_.editorCtrlLeftJumpPending_
        || state_.editorCtrlLeftJumpDispatchActive_
        || !hasActiveDifficulty()
        || state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    const auto [line, col] = owner_.currentCursorLineCol();
    const double second = timelineSecondForCursor(line, col);
    if (state_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/deferred_ui"),
            QStringLiteral("action=sync_timeline_to_editor_cursor second=%1 center=%2 quick_ready=%3")
                .arg(second, 0, 'f', 6)
                .arg((!state_.qtPreviewPlaying_ && centerView) ? 1 : 0)
                .arg(quickTimelineBridgeReady() ? 1 : 0)
        );
    }
    if (state_.timelineQuickStateBridge_ != nullptr) {
        const bool bridgeCenterView = !state_.qtPreviewPlaying_ && centerView;
        if (quickTimelineBridgeReady() && timelineTabIsForeground()) {
            state_.pendingQuickTimelineCursorSync_ = false;
            state_.pendingQuickTimelineCursorSecond_ = 0.0;
            state_.pendingQuickTimelineCursorCenterView_ = false;
            state_.timelineQuickStateBridge_->setCursorSeconds(second, bridgeCenterView);
        } else {
            queueTimelineCursorBridgeUpdate(second, bridgeCenterView);
        }
    }
}

void MainWindow::TimelineSection::navigateTimelineToSecond(double second, bool focusEditor)
{
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }

    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    int line = 1;
    int col = 1;
    double cursorSecond = 0.0;
    state_.timelineQuickModel_.resolveTimelineNavigateCursor(clampedSecond, &line, &col, &cursorSecond);
    const bool previousSuppressState = state_.suppressTimelineCursorSync_;
    state_.suppressTimelineCursorSync_ = true;

    state_.previewPendingSeekSecond_ = clampedSecond;
    state_.previewPendingSeekCenterView_ = true;
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    seekPreviewDiscreteToSecond(clampedSecond, true);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        deferTimelineCursorBridgeUpdate(cursorSecond, false);
    }

    moveEditorCursorToTimelineLocation(line, col, false, focusEditor, true, true);

    state_.suppressTimelineCursorSync_ = previousSuppressState;

    owner_.statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(clampedSecond, 0, 'f', 3)
            .arg(line)
            .arg(col)
    );
}

bool MainWindow::TimelineSection::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    return state_.timelineQuickModel_.resolveNearestTimelineNote(second, lane, line, col, noteSecond);
}

bool MainWindow::TimelineSection::moveEditorCursorToTimelineLocation(
    int line,
    int col,
    bool selectToken,
    bool focusEditor,
    bool centerView,
    bool suppressSignals,
    qint64* cursorMoveElapsedNs,
    qint64* followOverlayElapsedNs
)
{
    if (cursorMoveElapsedNs != nullptr) {
        *cursorMoveElapsedNs = 0;
    }
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = 0;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        return false;
    }

    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        QElapsedTimer cursorTimer;
        cursorTimer.start();
        owner_.jumpToLocation(line, col);
        if (cursorMoveElapsedNs != nullptr) {
            *cursorMoveElapsedNs = cursorTimer.nsecsElapsed();
        }
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

    QElapsedTimer cursorTimer;
    cursorTimer.start();
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
    if (cursorMoveElapsedNs != nullptr) {
        *cursorMoveElapsedNs = cursorTimer.nsecsElapsed();
    }

    QElapsedTimer followOverlayTimer;
    followOverlayTimer.start();
    if (focusEditor) {
        editor->setFocus();
        owner_.clearPreviewFollowDecoration();
    } else {
        int startCol = col;
        int endCol = col;
        state_.timelineQuickModel_.resolvePreviewFollowSelectionRange(line, col, &startCol, &endCol);
        owner_.setPreviewFollowDecoration(line, startCol, line, endCol);
    }
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = followOverlayTimer.nsecsElapsed();
    }
    return true;
}

void MainWindow::TimelineSection::updatePreviewFollowDecorationForTimelineBlueLine(
    double second,
    bool ensureVisible,
    qint64* resolveElapsedNs,
    qint64* followOverlayElapsedNs,
    TimelineQuickModel::PreviewFollowSpan* spanOut)
{
    if (resolveElapsedNs != nullptr) {
        *resolveElapsedNs = 0;
    }
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = 0;
    }
    if (spanOut != nullptr) {
        *spanOut = TimelineQuickModel::PreviewFollowSpan();
    }

    if (!hasActiveDifficulty() || !state_.previewFollowEnabled_) {
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        owner_.clearPreviewFollowDecoration();
        invalidatePreviewFollowBindingCache();
        if (followOverlayElapsedNs != nullptr) {
            *followOverlayElapsedNs = overlayTimer.nsecsElapsed();
        }
        return;
    }

    TimelineQuickModel::PreviewFollowBinding binding;
    bool resolved = false;
    if (cachedPreviewFollowBindingContainsSecond(second)) {
        binding = state_.previewFollowBindingCache_;
    } else {
        QElapsedTimer resolveTimer;
        resolveTimer.start();
        resolved = state_.timelineQuickModel_.resolvePreviewFollowBinding(qMax(0.0, second), &binding);
        if (resolveElapsedNs != nullptr) {
            *resolveElapsedNs = resolveTimer.nsecsElapsed();
        }
        if (resolved) {
            cachePreviewFollowBinding(binding);
        } else {
            invalidatePreviewFollowBindingCache();
        }
    }
    if (!resolved) {
        resolved = binding.resolved;
    }
    if (!resolved) {
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        owner_.clearPreviewFollowDecoration();
        if (followOverlayElapsedNs != nullptr) {
            *followOverlayElapsedNs = overlayTimer.nsecsElapsed();
        }
        return;
    }
    if (spanOut != nullptr) {
        *spanOut = binding.span;
    }
    QElapsedTimer overlayTimer;
    overlayTimer.start();
    owner_.setPreviewFollowDecoration(
        binding.span.startLine,
        binding.span.startCol,
        binding.span.endLine,
        binding.span.endCol,
        binding.span.cursorLine,
        binding.span.cursorCol,
        ensureVisible);
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = overlayTimer.nsecsElapsed();
    }
}

void MainWindow::TimelineSection::syncEditorCursorToPreviewSecond(
    double second,
    bool centerView,
    bool ensureVisibleWhenPaused)
{
    QElapsedTimer timer;
    timer.start();
    const auto logPerf =
        [&](const QString& action,
            bool resolved,
            bool moved,
            const TimelineQuickModel::PreviewFollowSpan* span,
            qint64 resolveElapsedNs,
            qint64 cursorMoveElapsedNs,
            qint64 followOverlayElapsedNs,
            qint64 timelineCursorElapsedNs) {
        if (!state_.runtimeDebugOutputEnabled_) {
            return;
        }
        const qint64 totalElapsedNs = timer.nsecsElapsed();
        const bool hotAction = action == QStringLiteral("selection_end_unchanged")
            || action == QStringLiteral("anchor_unchanged")
            || action == QStringLiteral("binding_unchanged")
            || action == QStringLiteral("cursor_moved")
            || action == QStringLiteral("paused_decoration");
        constexpr qint64 kFollowPerfLogThresholdNs = 4 * 1000 * 1000;
        if (hotAction
            && totalElapsedNs < kFollowPerfLogThresholdNs
            && resolveElapsedNs < kFollowPerfLogThresholdNs
            && cursorMoveElapsedNs < kFollowPerfLogThresholdNs
            && followOverlayElapsedNs < kFollowPerfLogThresholdNs
            && timelineCursorElapsedNs < kFollowPerfLogThresholdNs) {
            return;
        }
        const int line = (span != nullptr) ? span->cursorLine : 1;
        const int col = (span != nullptr) ? span->cursorCol : 1;
        const int endCol = (span != nullptr) ? span->endCol : 1;
        const int startLine = (span != nullptr) ? span->startLine : 1;
        const int startCol = (span != nullptr) ? span->startCol : 1;
        const int endLine = (span != nullptr) ? span->endLine : 1;
        const double totalElapsedMs = totalElapsedNs / 1000000.0;
        appendTimelinePerfLog(
            QStringLiteral("edit/follow_sync_perf"),
            QStringLiteral(
                "action=%1 resolved=%2 moved=%3 second=%4 line=%5 col=%6 end_col=%7 start_line=%8 start_col=%9 end_line=%10 end_col=%11 center=%12 ensure_visible=%13 playing=%14 elapsed_ms=%15"
            )
                .arg(action)
                .arg(resolved ? 1 : 0)
                .arg(moved ? 1 : 0)
                .arg(second, 0, 'f', 6)
                .arg(line)
                .arg(col)
                .arg(endCol)
                .arg(startLine)
                .arg(startCol)
                .arg(endLine)
                .arg(endCol)
                .arg(centerView ? 1 : 0)
                .arg(ensureVisibleWhenPaused ? 1 : 0)
                .arg(state_.qtPreviewPlaying_ ? 1 : 0)
                .arg(totalElapsedMs, 0, 'f', 3)
        );
        appendTimelinePerfLog(
            QStringLiteral("edit/follow_sync_breakdown"),
            QStringLiteral(
                "action=%1 resolved=%2 moved=%3 center=%4 playing=%5 resolve_ms=%6 cursor_move_ms=%7 follow_overlay_ms=%8 timeline_cursor_ms=%9 total_ms=%10"
            )
                .arg(action)
                .arg(resolved ? 1 : 0)
                .arg(moved ? 1 : 0)
                .arg(centerView ? 1 : 0)
                .arg(state_.qtPreviewPlaying_ ? 1 : 0)
                .arg(resolveElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(cursorMoveElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(followOverlayElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(timelineCursorElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(totalElapsedMs, 0, 'f', 3)
        );
    };

    qint64 resolveElapsedNs = 0;
    qint64 cursorMoveElapsedNs = 0;
    qint64 followOverlayElapsedNs = 0;
    TimelineQuickModel::PreviewFollowSpan span;

    if (state_.suppressTimelineCursorSync_ || !hasActiveDifficulty()) {
        owner_.clearPreviewFollowDecoration();
        if (!hasActiveDifficulty()) {
            invalidatePreviewFollowBindingCache();
        }
        logPerf(QStringLiteral("suppressed"), false, false, nullptr, 0, 0, 0, 0);
        return;
    }
    if (!state_.previewFollowEnabled_) {
        owner_.clearPreviewFollowDecoration();
        invalidatePreviewFollowBindingCache();
        logPerf(QStringLiteral("disabled"), false, false, nullptr, 0, 0, 0, 0);
        return;
    }
    if (!state_.qtPreviewPlaying_) {
        updatePreviewFollowDecorationForTimelineBlueLine(
            second,
            ensureVisibleWhenPaused,
            &resolveElapsedNs,
            &followOverlayElapsedNs,
            &span);
        logPerf(
            QStringLiteral("paused_decoration"),
            state_.previewFollowDecorationActive_,
            false,
            state_.previewFollowDecorationActive_ ? &span : nullptr,
            resolveElapsedNs,
            0,
            followOverlayElapsedNs,
            0);
        return;
    }

    if (cachedPreviewFollowBindingContainsSecond(second)) {
        span = state_.previewFollowBindingCache_.span;
        logPerf(
            QStringLiteral("binding_unchanged"),
            true,
            false,
            &span,
            0,
            0,
            0,
            0);
        return;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor currentEditorCursor =
        (editor != nullptr && editor->document() != nullptr) ? editor->textCursor() : QTextCursor();
    TimelineQuickModel::PreviewFollowBinding binding;
    QElapsedTimer resolveTimer;
    resolveTimer.start();
    const bool resolved = state_.timelineQuickModel_.resolvePreviewFollowBinding(second, &binding);
    resolveElapsedNs = resolveTimer.nsecsElapsed();
    if (resolved) {
        cachePreviewFollowBinding(binding);
        span = binding.span;
    } else {
        invalidatePreviewFollowBindingCache();
    }
    const int targetLine = resolved ? binding.span.cursorLine : 1;
    const int targetCol = resolved ? binding.span.cursorCol : 1;
    bool alreadyAtSelectionEnd = false;
    if (editor != nullptr && editor->document() != nullptr) {
        if (resolved) {
            if (binding.span.hasVisibleBody) {
                alreadyAtSelectionEnd = currentEditorCursor.hasSelection()
                    && currentEditorCursor.position() == binding.span.cursorPosition
                    && currentEditorCursor.selectionStart() == binding.span.startPosition
                    && currentEditorCursor.selectionEnd() == binding.span.endPositionExclusive;
            } else {
                alreadyAtSelectionEnd = !currentEditorCursor.hasSelection()
                    && currentEditorCursor.position() == binding.span.cursorPosition;
            }
        } else {
            alreadyAtSelectionEnd = !currentEditorCursor.hasSelection() && currentEditorCursor.position() == 0;
        }
    }

    const auto applyFollowOverlay = [&](bool followResolved) {
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        if (followResolved) {
            owner_.setPreviewFollowDecoration(
                binding.span.startLine,
                binding.span.startCol,
                binding.span.endLine,
                binding.span.endCol,
                binding.span.cursorLine,
                binding.span.cursorCol);
        } else {
            owner_.clearPreviewFollowDecoration();
        }
        return overlayTimer.nsecsElapsed();
    };

    if (state_.qtPreviewPlaying_ && !centerView) {
        followOverlayElapsedNs = applyFollowOverlay(resolved);
        logPerf(
            QStringLiteral("visual_follow_updated"),
            resolved,
            false,
            resolved ? &span : nullptr,
            resolveElapsedNs,
            0,
            followOverlayElapsedNs,
            0);
        return;
    }

    if (alreadyAtSelectionEnd) {
        followOverlayElapsedNs = applyFollowOverlay(resolved);
        logPerf(
            QStringLiteral("selection_end_unchanged"),
            resolved,
            false,
            resolved ? &span : nullptr,
            resolveElapsedNs,
            0,
            followOverlayElapsedNs,
            0);
        return;
    }

    if (editor != nullptr && editor->document() != nullptr) {
        QTextCursor cursor;
        const bool builtCursor = resolved && binding.span.hasVisibleBody
            ? miacode::mainwindow::editor_selection::buildSelectionCursor(
                  editor,
                  binding.span.startLine,
                  binding.span.startCol,
                  binding.span.endLine,
                  binding.span.endCol,
                  &cursor)
            : miacode::mainwindow::editor_selection::buildCaretCursor(editor, targetLine, targetCol, &cursor);
        if (builtCursor) {
            QElapsedTimer cursorTimer;
            cursorTimer.start();
            editor->applyPreviewFollowCursor(cursor, centerView, true);
            cursorMoveElapsedNs = cursorTimer.nsecsElapsed();
            followOverlayElapsedNs = applyFollowOverlay(resolved);
            logPerf(
                QStringLiteral("cursor_moved"),
                resolved,
                true,
                resolved ? &span : nullptr,
                resolveElapsedNs,
                cursorMoveElapsedNs,
                followOverlayElapsedNs,
                0);
            return;
        }
    }
    logPerf(
        QStringLiteral("cursor_move_failed"),
        resolved,
        false,
        resolved ? &span : nullptr,
        resolveElapsedNs,
        cursorMoveElapsedNs,
        followOverlayElapsedNs,
        0);
}

void MainWindow::scheduleDeferredEditorUiUpdate(
    bool updateStatus,
    bool updateEmptyState,
    bool syncTimelineCursor,
    bool centerView,
    bool syncPreviewFollow,
    double previewFollowSecond,
    bool ensurePreviewFollowVisible)
{
    const bool allowTimelineCursorSync =
        !(state_.editorCtrlLeftJumpPending_ || state_.editorCtrlLeftJumpDispatchActive_);
    const bool effectiveSyncTimelineCursor = syncTimelineCursor && allowTimelineCursorSync;
    const bool effectiveCenterView = centerView && effectiveSyncTimelineCursor;
    if (state_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/deferred_ui"),
            QStringLiteral(
                "action=schedule status=%1 empty=%2 sync_timeline=%3 center=%4 sync_follow=%5 follow_second=%6 quick_ready=%7"
            )
                .arg(updateStatus ? 1 : 0)
                .arg(updateEmptyState ? 1 : 0)
                .arg(effectiveSyncTimelineCursor ? 1 : 0)
                .arg(effectiveCenterView ? 1 : 0)
                .arg(syncPreviewFollow ? 1 : 0)
                .arg(previewFollowSecond, 0, 'f', 6)
                .arg(state_.quickTimelineSurfaceReady_ ? 1 : 0)
        );
    }
    deferredEditorUiStatusPending_ = deferredEditorUiStatusPending_ || updateStatus;
    deferredEditorUiEmptyStatePending_ = deferredEditorUiEmptyStatePending_ || updateEmptyState;
    deferredEditorUiTimelineCursorPending_ =
        deferredEditorUiTimelineCursorPending_ || effectiveSyncTimelineCursor;
    deferredEditorUiCenterView_ = deferredEditorUiCenterView_ || effectiveCenterView;
    if (syncPreviewFollow) {
        deferredEditorUiPreviewFollowPending_ = true;
        deferredEditorUiPreviewFollowSecond_ = previewFollowSecond;
        deferredEditorUiEnsureFollowVisible_ =
            deferredEditorUiEnsureFollowVisible_ || ensurePreviewFollowVisible;
    }
    if (deferredEditorUiUpdatePending_) {
        return;
    }
    deferredEditorUiUpdatePending_ = true;
    QTimer::singleShot(0, this, [this]() { flushDeferredEditorUiUpdate(); });
}

void MainWindow::flushDeferredEditorUiUpdate()
{
    if (!deferredEditorUiUpdatePending_) {
        return;
    }

    deferredEditorUiUpdatePending_ = false;
    const bool updateStatus = deferredEditorUiStatusPending_;
    const bool updateEmptyState = deferredEditorUiEmptyStatePending_;
    const bool syncTimelineCursor = deferredEditorUiTimelineCursorPending_;
    const bool centerView = deferredEditorUiCenterView_;
    const bool syncPreviewFollow = deferredEditorUiPreviewFollowPending_;
    const double previewFollowSecond = deferredEditorUiPreviewFollowSecond_;
    const bool ensurePreviewFollowVisible = deferredEditorUiEnsureFollowVisible_;

    if (state_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/deferred_ui"),
            QStringLiteral(
                "action=flush status=%1 empty=%2 sync_timeline=%3 center=%4 sync_follow=%5 follow_second=%6 ensure_follow_visible=%7 quick_ready=%8"
            )
                .arg(updateStatus ? 1 : 0)
                .arg(updateEmptyState ? 1 : 0)
                .arg(syncTimelineCursor ? 1 : 0)
                .arg(centerView ? 1 : 0)
                .arg(syncPreviewFollow ? 1 : 0)
                .arg(previewFollowSecond, 0, 'f', 6)
                .arg(ensurePreviewFollowVisible ? 1 : 0)
                .arg(state_.quickTimelineSurfaceReady_ ? 1 : 0)
        );
    }

    deferredEditorUiStatusPending_ = false;
    deferredEditorUiEmptyStatePending_ = false;
    deferredEditorUiTimelineCursorPending_ = false;
    deferredEditorUiCenterView_ = false;
    deferredEditorUiPreviewFollowPending_ = false;
    deferredEditorUiEnsureFollowVisible_ = false;

    if (updateEmptyState) {
        updateEditorEmptyState();
    }
    if (updateStatus) {
        updateEditorStatus();
    }

    bool previewFollowHandled = false;
    if (syncPreviewFollow
        && hasActiveDifficulty()
        && state_.previewFollowEnabled_) {
        previewFollowHandled = true;
        syncEditorCursorToPreviewSecond(
            previewFollowSecond,
            centerView,
            ensurePreviewFollowVisible);
    }

    if (syncTimelineCursor && !previewFollowHandled) {
        syncTimelineToEditorCursor(centerView);
    }
}

void MainWindow::resetPreviewTrackTimelineOffsets()
{
    timelineSection_->resetPreviewTrackTimelineOffsets();
}

miacode::waveform::WaveformCacheService* MainWindow::ensureWaveformCacheService()
{
    if (state_.waveformCacheService_ == nullptr) {
        state_.waveformCacheService_ = new miacode::waveform::WaveformCacheService(this);
    }
    state_.waveformCacheService_->setThreadPool(
        state_.previewWarmupPool_ != nullptr ? state_.previewWarmupPool_ : QThreadPool::globalInstance());
    return state_.waveformCacheService_;
}

void MainWindow::applyWaveformData(
    const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData)
{
    timelineSection_->applyWaveformData(waveformData);
}

void MainWindow::refreshWaveformCache()
{
    timelineSection_->refreshWaveformCache();
}

void MainWindow::refreshWaveformCache(double knownDurationSeconds)
{
    timelineSection_->refreshWaveformCache(knownDurationSeconds);
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

void MainWindow::deferTimelineCursorBridgeUpdate(double second, bool centerView)
{
    timelineSection_->deferTimelineCursorBridgeUpdate(second, centerView);
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
    bool suppressSignals,
    qint64* cursorMoveElapsedNs,
    qint64* followOverlayElapsedNs)
{
    return timelineSection_->moveEditorCursorToTimelineLocation(
        line,
        col,
        selectToken,
        focusEditor,
        centerView,
        suppressSignals,
        cursorMoveElapsedNs,
        followOverlayElapsedNs
    );
}

void MainWindow::syncEditorCursorToPreviewSecond(
    double second,
    bool centerView,
    bool ensureVisibleWhenPaused)
{
    timelineSection_->syncEditorCursorToPreviewSecond(second, centerView, ensureVisibleWhenPaused);
}
