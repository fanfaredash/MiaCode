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
#include "MainEntrypoints.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/CrashRecovery.h"
#include "common/OperationLog.h"
#include "common/DebugLog.h"
#include "common/ProcessDiagnostics.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/TimelineMarkerOffset.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/latency/LatencySandboxController.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include "MainWindow.PreviewTimelineFlow.Internal.h"

using namespace miacode::mainwindow::shared;
using namespace miacode::mainwindow::preview_timeline_flow_detail;

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

void appendPreviewWaveformLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::audioDebugOutputEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/waveform"),
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

QVector<SimaiRawField> metadataExtraFieldsFromUi(
    const QTextEdit* metadataExtraEdit,
    const QVector<SimaiRawField>& fallbackFields
)
{
    if (metadataExtraEdit != nullptr) {
        return SimaiDocument::parseRawFields(metadataExtraEdit->toPlainText(), true);
    }
    return fallbackFields;
}

bool upsertMetadataField(QVector<SimaiRawField>* fields, const QString& key, const QString& value)
{
    if (fields == nullptr) {
        return false;
    }
    for (SimaiRawField& field : *fields) {
        if (field.key.compare(key, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (field.value == value) {
            return false;
        }
        field.value = value;
        return true;
    }
    fields->append(SimaiRawField{key, value});
    return true;
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
    const double previousTrackDurationSeconds = state_.previewTrackDurationSeconds_;
    state_.previewTrackDurationSeconds_ = waveformData ? qMax(0.0, waveformData->durationSeconds) : 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setWaveformData(waveformData);
    }
    updatePreviewSliderRange();
    const double chartDurationSeconds = state_.timelineQuickStateBridge_ != nullptr
        ? state_.timelineQuickStateBridge_->durationSeconds()
        : 0.0;
    const int sliderMinimum = ui_.previewSlider_ != nullptr ? ui_.previewSlider_->minimum() : 0;
    const int sliderMaximum = ui_.previewSlider_ != nullptr ? ui_.previewSlider_->maximum() : 0;
    const QString summary = waveformData
        ? miacode::waveform::waveformDataDebugSummary(*waveformData)
        : QStringLiteral("data=0");
    appendPreviewWaveformLog(
        QStringLiteral("apply"),
        QStringLiteral("generation=%1 current_track_id=%2 old_track_duration=%3 new_track_duration=%4 chart_duration=%5 preview_duration=%6 slider_min_ms=%7 slider_max_ms=%8 bridge=%9 %10")
            .arg(state_.waveformRefreshGeneration_)
            .arg(miacode::waveform::waveformTrackDebugId(
                miacode::waveform::normalizeTrackPath(state_.lastTrackPath_)))
            .arg(previousTrackDurationSeconds, 0, 'f', 6)
            .arg(state_.previewTrackDurationSeconds_, 0, 'f', 6)
            .arg(chartDurationSeconds, 0, 'f', 6)
            .arg(previewDurationSeconds(), 0, 'f', 6)
            .arg(sliderMinimum)
            .arg(sliderMaximum)
            .arg(state_.timelineQuickStateBridge_ != nullptr ? 1 : 0)
            .arg(summary));
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
    const QString normalizedTrackPath = miacode::waveform::normalizeTrackPath(trackPath);
    appendPreviewWaveformLog(
        QStringLiteral("refresh_start"),
        QStringLiteral("generation=%1 track_id=%2 track_empty=%3 known_duration=%4 chart_path_empty=%5")
            .arg(generation)
            .arg(miacode::waveform::waveformTrackDebugId(normalizedTrackPath))
            .arg(trackPath.isEmpty() ? 1 : 0)
            .arg(knownDurationSeconds, 0, 'f', 6)
            .arg(state_.currentFilePath_.isEmpty() ? 1 : 0));
    if (trackPath.isEmpty()) {
        appendPreviewWaveformLog(
            QStringLiteral("refresh_placeholder"),
            QStringLiteral("generation=%1 reason=track_empty").arg(generation));
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(0.0));
        return;
    }

    const QFileInfo trackInfo(trackPath);
    if (!trackInfo.exists() || !trackInfo.isFile()) {
        appendPreviewWaveformLog(
            QStringLiteral("refresh_placeholder"),
            QStringLiteral("generation=%1 reason=track_missing track_id=%2")
                .arg(generation)
                .arg(miacode::waveform::waveformTrackDebugId(normalizedTrackPath)));
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(0.0));
        return;
    }

    if (knownDurationSeconds > 0.0) {
        appendPreviewWaveformLog(
            QStringLiteral("refresh_placeholder"),
            QStringLiteral("generation=%1 reason=known_duration track_id=%2 duration=%3")
                .arg(generation)
                .arg(miacode::waveform::waveformTrackDebugId(normalizedTrackPath))
                .arg(knownDurationSeconds, 0, 'f', 6));
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(knownDurationSeconds));
    } else {
        appendPreviewWaveformLog(
            QStringLiteral("refresh_placeholder"),
            QStringLiteral("generation=%1 reason=await_worker track_id=%2")
                .arg(generation)
                .arg(miacode::waveform::waveformTrackDebugId(normalizedTrackPath)));
        applyWaveformData(miacode::waveform::makeWaveformPlaceholder(0.0));
    }

    const QString cacheDirectoryPath = miacode::waveform::waveformCacheDirectoryPath(
        miacode::waveform::projectDataDirectoryPathForFile(state_.currentFilePath_));
    owner_.ensureWaveformCacheService()->requestWaveform(
        trackPath,
        cacheDirectoryPath,
        [this, generation, trackPath](miacode::waveform::WaveformDataPtr waveformData) {
            if (generation != state_.waveformRefreshGeneration_ || state_.lastTrackPath_ != trackPath) {
                appendPreviewWaveformLog(
                    QStringLiteral("callback_discard"),
                    QStringLiteral("reason=stale generation=%1 current_generation=%2 callback_track_id=%3 current_track_id=%4")
                        .arg(generation)
                        .arg(state_.waveformRefreshGeneration_)
                        .arg(miacode::waveform::waveformTrackDebugId(
                            miacode::waveform::normalizeTrackPath(trackPath)))
                        .arg(miacode::waveform::waveformTrackDebugId(
                            miacode::waveform::normalizeTrackPath(state_.lastTrackPath_))));
                return;
            }

            const QFileInfo currentTrackInfo(trackPath);
            if (!currentTrackInfo.exists() || !currentTrackInfo.isFile()) {
                appendPreviewWaveformLog(
                    QStringLiteral("callback_discard"),
                    QStringLiteral("reason=track_missing generation=%1 track_id=%2")
                        .arg(generation)
                        .arg(miacode::waveform::waveformTrackDebugId(
                            miacode::waveform::normalizeTrackPath(trackPath))));
                return;
            }
            if (waveformData && waveformData->fileSize >= 0) {
                const qint64 currentLastModifiedMs = fileLastModifiedMs(currentTrackInfo);
                if (currentTrackInfo.size() != waveformData->fileSize
                    || currentLastModifiedMs != waveformData->lastModifiedMs) {
                    appendPreviewWaveformLog(
                        QStringLiteral("callback_discard"),
                        QStringLiteral("reason=file_stamp_mismatch generation=%1 track_id=%2 current_size=%3 data_size=%4 current_mtime_ms=%5 data_mtime_ms=%6")
                            .arg(generation)
                            .arg(miacode::waveform::waveformTrackDebugId(
                                miacode::waveform::normalizeTrackPath(trackPath)))
                            .arg(currentTrackInfo.size())
                            .arg(waveformData->fileSize)
                            .arg(currentLastModifiedMs)
                            .arg(waveformData->lastModifiedMs));
                    return;
                }
            }
            appendPreviewWaveformLog(
                QStringLiteral("callback_apply"),
                QStringLiteral("generation=%1 %2")
                    .arg(generation)
                    .arg(waveformData
                        ? miacode::waveform::waveformDataDebugSummary(*waveformData)
                        : QStringLiteral("data=0")));
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
    return miacode::timeline::offset::parsedFirstSeconds(rawValue, ok);
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

int MainWindow::TimelineSection::parsedClockCount() const
{
    const QVector<SimaiRawField> fields = metadataExtraFieldsFromUi(ui_.metadataExtraEdit_, state_.document_.extraFields);
    const int value = miacode::chart_clock::clockCountFromFields(fields);
    return value > 0 ? value : 4;
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
    QVector<SimaiRawField> fields = metadataExtraFieldsFromUi(ui_.metadataExtraEdit_, state_.document_.extraFields);
    const QString serializedBpm = QString::number(bpm, 'f', 3);
    upsertMetadataField(&fields, QStringLiteral("wholebpm"), serializedBpm);
    state_.document_.extraFields = fields;
    owner_.setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    state_.documentDirty_ = true;
    owner_.updateDirtyState();
}

void MainWindow::TimelineSection::applyLatencyDetectorClockCount(int clockCount)
{
    const int normalized = qMax(1, clockCount);
    QVector<SimaiRawField> fields = metadataExtraFieldsFromUi(ui_.metadataExtraEdit_, state_.document_.extraFields);
    upsertMetadataField(&fields, QStringLiteral("clock_count"), QString::number(normalized));
    state_.document_.extraFields = fields;
    owner_.setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    state_.documentDirty_ = true;
    owner_.updateDirtyState();
    refreshTimelineMetadata();
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
    // Relocate the startup beacon and op-chain shadow so they co-locate
    // with the runtime/export logs under this chart's .miacode/logs/.
    if (!projectDataDirectoryPath.isEmpty()) {
        miacode::oplog::relocateLogs(
            QDir(projectDataDirectoryPath).filePath(QStringLiteral("logs")));
    }
    // The runtime log directory just rebound to this chart's .miacode/logs/;
    // re-emit the P0/P2/P3 startup diagnostics (process identity / GPU hint /
    // resolved GPU policy) so the per-chart log a user collects is self-contained
    // rather than only holding them in the app-local boot log.
    if (!projectDataDirectoryPath.isEmpty()) {
        miacode::app::entry::logProcessStartupDiagnostics(QStringLiteral("log_dir_rebound"));
    }
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
        // Rebind the project-scoped mixer BEFORE the SFX reload / level
        // dispatch below, so the new chart's volumes are the ones handed to
        // reloadAssetsForChart and applyPreviewAudioSettingsToRuntime rather
        // than the outgoing chart's.
        owner_.loadProjectAudioPreferences();
    }
    owner_.syncPreviewStageMediaRouteChartPath(state_.currentFilePath_, state_.lastTrackPath_, state_.qtPreviewPauseSecond_, state_.document_.videoPath);  // Phase 4c &video= override
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setPlayheadSeconds(state_.qtPreviewPauseSecond_, false);
    }
    if (state_.previewSfxRuntime_ != nullptr && pathChanged) {
        // The next warm-up result submits one atomic path+asset reload. Mark
        // the old chart's completed (or pending) assets unusable immediately
        // so an early Play cannot start them while that preload is queued.
        state_.previewSfxRuntimePrepared_ = false;
        state_.previewSfxRuntimePreparationAssetGeneration_ = 0;
        state_.previewSfxRuntimePreparationSequence_ = 0;
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
    if (!hasActiveDifficulty()) {
        return;
    }
    ++state_.timelineRevision_;

    if (state_.timelineQuickStateBridge_ != nullptr) {
        refreshTimelineQuickModelFromCurrentText();
    }
    requestTimelineSlowRefresh();
}

void MainWindow::TimelineSection::refreshTimelineMetadata()
{
    scheduleTimelineRefresh();
}


void MainWindow::TimelineSection::onTimelineHeaderNavigateRequested(double second)
{
    navigateTimelineToSecond(second, true);
}

void MainWindow::TimelineSection::onTimelineUserInteractionStarted()
{
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("timeline.interaction.started"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("pointer")},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("timeline")}}},
        });
    }
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
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("timeline.interaction.updated"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("pointer")},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("drag")}, {QStringLiteral("second"), second}}},
        }, true);
    }
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
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("timeline.wheel"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("pointer")},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("second"), second}}},
        }, true);
    }
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
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("timeline.interaction.finished"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("pointer")},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("drag")}, {QStringLiteral("second"), second}}},
        });
    }
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
    if (!hasActiveDifficulty()) {
        owner_.clearPreviewFollowDecoration();
        return;
    }
    // Turning the option off stops the caret/viewport follow, not the highlight:
    // it stays as the on-screen cue for where the playhead is (and as the target
    // touch-pad click authoring writes to). Refresh it either way.
    const double second = qMax(0.0, owner_.currentPreviewAuthoritativeAudioClockSecond());
    syncEditorCursorToPreviewSecond(
        second,
        enabled && state_.qtPreviewPlaying_ && state_.previewViewportLockEnabled_,
        !state_.qtPreviewPlaying_);
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

    miacode::diag::MemoryStageScope memScope("preview/mem_stage", "preview_state_push");
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
    state_.pendingTimelineSlowRefresh_.validationLocale = uiValidationLocale();
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
        miacode::diag::MemoryStageScope memScope("preview/mem_stage", "slow_refresh_build");
        SimaiNativeParseResult parseResult;
        TimelinePreviewRefreshState previewState;
        {
            // beta7 probe 2.1 — tight core bracket excludes the invokeMethod result COPY below,
            // so (slow_refresh_build − slow_refresh_core) isolates the in-flight handoff cost.
            miacode::diag::MemoryStageScope memScopeCore(
                "preview/mem_stage", "slow_refresh_core");
            parseResult = SimaiNativeParser::parseForTimeline(
                request.chartText,
                request.timingMetadata);
            previewState = buildTimelinePreviewRefreshState(parseResult, request.firstSeconds);
        }
        if (guard.isNull()) {
            return;
        }
        miacode::diag::leak_gauge::noteInflightDispatch();
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, request, parseResult, previewState]() mutable {
                miacode::diag::leak_gauge::noteInflightApplied();
                if (guard.isNull()) {
                    return;
                }

                guard->state_.timelineSlowWorkerRunning_ = false;
                if (request.revision != guard->state_.timelineSlowRequestedRevision_
                    || request.revision != guard->state_.timelineRevision_
                    || !guard->hasActiveDifficulty()
                    || request.difficultyId != guard->activeDifficultyId()
                    || request.chartText != guard->activeChartText()
                    || request.timingMetadata != guard->currentTimingMetadata()) {
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


void MainWindow::TimelineSection::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    state_.muriStaticReferences_ = miacode::muri::buildStaticMuriReferences(
        noteMarkers,
        static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0);
    state_.muriStaticReferencesNoteMarkerSignature_ = state_.latestTimelineNoteMarkerSignature_;
    state_.muriStaticReferencesDifficultyId_ = hasActiveDifficulty() ? activeDifficultyId() : 0;
    state_.muriStaticReferencesTimelineRevision_ = state_.timelineRevision_;
    state_.muriStaticReferencesAvailable_ = !state_.muriStaticReferencesNoteMarkerSignature_.isEmpty();
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

bool MainWindow::TimelineSection::resolveTimelineSecondForCursor(int line, int col, double* second) const
{
    return state_.timelineQuickModel_.resolveTimelineSecondForCursor(line, col, second);
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

    const bool previewFollowOwnsPlaybackCursor =
        previewFollowHandled
        && (state_.qtPreviewPlaying_
            || state_.previewStartupSyncPending_
            || state_.previewLateVideoStartPending_);
    if (syncTimelineCursor && !previewFollowOwnsPlaybackCursor) {
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

void MainWindow::applyLatencyDetectorClockCount(int clockCount)
{
    timelineSection_->applyLatencyDetectorClockCount(clockCount);
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

void MainWindow::setTouchPadAuthoringAnchor(double seekSecond, double tokenSecond)
{
    timelineSection_->setTouchPadAuthoringAnchor(seekSecond, tokenSecond);
}

bool MainWindow::resolveTimelineSecondForCursor(int line, int col, double* second) const
{
    return timelineSection_->resolveTimelineSecondForCursor(line, col, second);
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
