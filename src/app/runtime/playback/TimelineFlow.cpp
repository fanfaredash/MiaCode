#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Shared.h"
#include "runtime/media/MediaJobsHost.h"
#include "runtime/document/DocumentSessionHost.h"

#include "app/v2/ApplicationServices.h"
#include "app/v2/LatencyEngine.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
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

#include <QtCore>
#include <QtGui>
#include <QToolTip>

#include "runtime/playback/TimelineFlow.Internal.h"

using namespace miacode::runtime::shared;
using namespace miacode::runtime::preview_timeline_detail;

miacode::runtime::PlaybackCoordinator::PlaybackCoordinator(
    QObject& owner,
    miacode::v2::ApplicationServices& services,
    RuntimeContext::Ui& ui,
    RuntimeContext::State& state,
    RuntimeContext::PlaybackState& playbackState,
    miacode::v2::PlaybackPreferencesPort& preferences,
    miacode::v2::PlaybackValidationPort& validation,
    miacode::v2::PlaybackDocumentPort& documents,
    miacode::v2::PlaybackPreviewPort& preview,
    quint64 sessionGeneration)
    : owner_(owner)
    , services_(services)
    , ui_(ui)
    , state_(state)
    , playbackState_(playbackState)
    , preferences_(preferences)
    , validation_(validation)
    , documents_(documents)
    , preview_(preview)
    , identity_(sessionGeneration)
{}

bool miacode::runtime::PlaybackCoordinator::timelineTabIsForeground() const
{
    return bottomTabsTabVisibleFromState(RuntimeContext::BottomTabsTabId::Timeline)
        && state_.currentBottomTabsTabId_ == RuntimeContext::BottomTabsTabId::Timeline;
}

bool miacode::runtime::PlaybackCoordinator::quickTimelineBridgeReady() const
{
    return !state_.uiFocusBridgeMode_ || state_.timelineReady_;
}

void miacode::runtime::PlaybackCoordinator::queueTimelineCursorBridgeUpdate(double second, bool centerView)
{
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    state_.pendingQuickTimelineCursorSync_ = true;
    state_.pendingQuickTimelineCursorSecond_ = second;
    state_.pendingQuickTimelineCursorCenterView_ =
        state_.pendingQuickTimelineCursorCenterView_ || centerView;
}

void miacode::runtime::PlaybackCoordinator::scheduleDeferredTimelineBridgeFlush()
{
    const quint64 generation = ++state_.deferredTimelineBridgeFlushGeneration_;
    QTimer::singleShot(0, &owner_, [this, generation]() {
        if (generation != state_.deferredTimelineBridgeFlushGeneration_) {
            return;
        }
        flushDeferredTimelineBridgeState();
    });
}

void miacode::runtime::PlaybackCoordinator::deferTimelineCursorBridgeUpdate(double second, bool centerView)
{
    queueTimelineCursorBridgeUpdate(second, centerView);
    scheduleDeferredTimelineBridgeFlush();
}

void miacode::runtime::PlaybackCoordinator::flushDeferredTimelineBridgeState()
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

void miacode::runtime::PlaybackCoordinator::invalidatePreviewFollowBindingCache()
{
    state_.previewFollowBindingCacheValid_ = false;
    state_.previewFollowBindingCache_ = TimelineQuickModel::PreviewFollowBinding();
}

bool miacode::runtime::PlaybackCoordinator::cachedPreviewFollowBindingContainsSecond(double second) const
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

void miacode::runtime::PlaybackCoordinator::cachePreviewFollowBinding(
    const TimelineQuickModel::PreviewFollowBinding& binding)
{
    if (!binding.resolved) {
        invalidatePreviewFollowBindingCache();
        return;
    }
    state_.previewFollowBindingCacheValid_ = true;
    state_.previewFollowBindingCache_ = binding;
}

void miacode::runtime::PlaybackCoordinator::resetPreviewTrackTimelineOffsets()
{
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setBackgroundTrackOffsetSeconds(0.0);
    }
    resetPreviewStageMediaRouteTimelineOffset();
}

void miacode::runtime::PlaybackCoordinator::applyWaveformData(
    const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData)
{
    const double previousTrackDurationSeconds = state_.previewTrackDurationSeconds_;
    state_.previewTrackDurationSeconds_ = waveformData ? qMax(0.0, waveformData->durationSeconds) : 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setWaveformData(waveformData);
    }
    const double chartDurationSeconds = state_.timelineQuickStateBridge_ != nullptr
        ? state_.timelineQuickStateBridge_->durationSeconds()
        : 0.0;
    const QString summary = waveformData
        ? miacode::waveform::waveformDataDebugSummary(*waveformData)
        : QStringLiteral("data=0");
    appendPreviewWaveformLog(
        QStringLiteral("apply"),
        QStringLiteral("generation=%1 current_track_id=%2 old_track_duration=%3 new_track_duration=%4 chart_duration=%5 preview_duration=%6 bridge=%7 %8")
            .arg(state_.waveformRefreshGeneration_)
            .arg(miacode::waveform::waveformTrackDebugId(
                miacode::waveform::normalizeTrackPath(state_.lastTrackPath_)))
            .arg(previousTrackDurationSeconds, 0, 'f', 6)
            .arg(state_.previewTrackDurationSeconds_, 0, 'f', 6)
            .arg(chartDurationSeconds, 0, 'f', 6)
            .arg(previewDurationSeconds(), 0, 'f', 6)
            .arg(state_.timelineQuickStateBridge_ != nullptr ? 1 : 0)
            .arg(summary));
}

void miacode::runtime::PlaybackCoordinator::refreshWaveformCache()
{
    refreshWaveformCache(-1.0);
}

void miacode::runtime::PlaybackCoordinator::refreshWaveformCache(double knownDurationSeconds)
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
    ensureWaveformCacheService()->requestWaveform(
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

bool miacode::runtime::PlaybackCoordinator::hasActiveDifficulty() const
{
    return state_.activeDifficultyId_ > 0 && services_.workspace().document().difficulty(state_.activeDifficultyId_) != nullptr;
}

bool miacode::runtime::PlaybackCoordinator::hasPreviewableChart() const
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

int miacode::runtime::PlaybackCoordinator::activeDifficultyId() const
{
    return state_.activeDifficultyId_;
}

QString miacode::runtime::PlaybackCoordinator::activeChartText() const
{
    if (!hasActiveDifficulty()) {
        return QString();
    }
    const SimaiDifficultyData* difficultyData = services_.workspace().document().difficulty(state_.activeDifficultyId_);
    return difficultyData != nullptr ? difficultyData->chart : QString();
}

miacode::simai::SimaiTimingMetadata miacode::runtime::PlaybackCoordinator::currentTimingMetadata() const
{
    return miacode::simai::buildTimingMetadata(services_.workspace().document());
}

double miacode::runtime::PlaybackCoordinator::parsedRawFirstSeconds(bool* ok) const
{
    return miacode::timeline::offset::parsedFirstSeconds(
        services_.workspace().document().first, ok);
}

double miacode::runtime::PlaybackCoordinator::parsedFirstSeconds(bool* ok) const
{
    return parsedRawFirstSeconds(ok);
}

double miacode::runtime::PlaybackCoordinator::parsedWholeBpm(bool* ok) const
{
    for (const SimaiRawField& field : services_.workspace().document().extraFields) {
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

int miacode::runtime::PlaybackCoordinator::parsedClockCount() const
{
    const int value = miacode::chart_clock::clockCountFromDocument(
        services_.workspace().document());
    return value > 0 ? value : 4;
}

QString miacode::runtime::PlaybackCoordinator::parsedLatencyMeterId() const
{
    return miacode::simai::latencyMeterIdForTimingMetadata(currentTimingMetadata());
}

void miacode::runtime::PlaybackCoordinator::applyLatencyDetectorOffset(double seconds)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    const QString serialized = QString::number(normalized, 'f', 3);
    miacode::v2::ChartWorkspace& workspace = services_.workspace();
    workspace.updateDocumentField(miacode::v2::ChartWorkspaceDocumentField::First, serialized);
    state_.documentDirty_ = workspace.snapshot().dirty;
    documents_.updateDirtyState();
    resetPreviewTrackTimelineOffsets();
    refreshTimelineMetadata();
}

void miacode::runtime::PlaybackCoordinator::applyLatencyDetectorBpm(double bpm)
{
    if (!qIsFinite(bpm) || bpm <= 0.0) {
        return;
    }
    miacode::v2::ChartWorkspace& workspace = services_.workspace();
    const QString serializedBpm = QString::number(bpm, 'f', 3);
    workspace.upsertExtraField(QStringLiteral("wholebpm"), serializedBpm);
    state_.documentDirty_ = workspace.snapshot().dirty;
    documents_.updateDirtyState();
}

void miacode::runtime::PlaybackCoordinator::applyLatencyDetectorClockCount(int clockCount)
{
    const int normalized = qMax(1, clockCount);
    miacode::v2::ChartWorkspace& workspace = services_.workspace();
    workspace.upsertExtraField(QStringLiteral("clock_count"), QString::number(normalized));
    state_.documentDirty_ = workspace.snapshot().dirty;
    documents_.updateDirtyState();
    refreshTimelineMetadata();
}

void miacode::runtime::PlaybackCoordinator::setCurrentFilePath(const QString& path, bool suppressImmediateRefresh)
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
        validation_.clearValidationCache();
        validation_.clearValidationDecorations();
        stopQtPreviewPlayback(false);
        if (auto* latency = services_.latencyEngine(); latency != nullptr) {
            latency->exitSandboxIfActive();
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
        preferences_.setLastOpenDirectory(state_.currentFilePath_);

        if (!nextTrackPath.isEmpty()) {
            // Keep preview audio in sync with the currently opened chart directory.
            state_.lastTrackPath_ = nextTrackPath;
        } else {
            state_.lastTrackPath_.clear();
        }
    } else {
        state_.lastTrackPath_.clear();
    }
    if (state_.scene_ != nullptr) {
#ifdef HAVE_QT_MULTIMEDIA
        state_.scene_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(state_.currentFilePath_));
#else
        state_.scene_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(state_.currentFilePath_, false));
#endif
    }
    updateWindowTitle();
    if (pathChanged) {
        preferences_.loadProjectRenderState();
        // Rebind the project-scoped mixer BEFORE the SFX reload / level
        // dispatch below, so the new chart's volumes are the ones handed to
        // reloadAssetsForChart and applyPreviewAudioSettingsToRuntime rather
        // than the outgoing chart's.
        preview_.loadProjectAudioPreferences();
    }
    preview_.syncPreviewStageMediaRouteChartPath(state_.currentFilePath_, state_.lastTrackPath_, state_.pauseSecond_, services_.workspace().document().videoPath);  // Phase 4c &video= override
    if (state_.scene_ != nullptr) {
        state_.scene_->setPlayheadSeconds(state_.pauseSecond_, false);
    }
    if (state_.previewSfxRuntime_ != nullptr && pathChanged) {
        // The next warm-up result submits one atomic path+asset reload. Mark
        // the old chart's completed (or pending) assets unusable immediately
        // so an early Play cannot start them while that preload is queued.
        state_.previewSfxRuntimePrepared_ = false;
        state_.previewSfxRuntimePreparationAssetGeneration_ = 0;
        state_.previewSfxRuntimePreparationSequence_ = 0;
    }
    preview_.applyPreviewAudioSettingsToRuntime();
    if (!suppressImmediateRefresh) {
        refreshWaveformCache();
        refreshTimelineMetadata();
    }
    if (pathChanged && state_.previewWarmupGeneration_ > 0) {
        preview_.schedulePreviewSubsystemWarmup();
    }
}

void miacode::runtime::PlaybackCoordinator::updateWindowTitle()
{
    QString titleText = services_.workspace().document().title;
    if (titleText.trimmed().isEmpty()) {
        titleText = state_.currentFilePath_.isEmpty()
            ? QString("Untitled.simai")
            : QFileInfo(state_.currentFilePath_).fileName();
    }
    const QFontMetrics metrics(QGuiApplication::font());
    const QString elided = metrics.elidedText(titleText, Qt::ElideRight, 420);
    const bool dirty = state_.documentDirty_ || state_.currentFieldDirty_;
    state_.titleText_ = QString("MiaCode - %1%2").arg(elided, dirty ? QStringLiteral("[*]") : QString());
}

QString miacode::runtime::PlaybackCoordinator::editorText() const
{
    return activeChartText();
}

void miacode::runtime::PlaybackCoordinator::scheduleTimelineRefresh()
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

void miacode::runtime::PlaybackCoordinator::refreshTimelineMetadata()
{
    scheduleTimelineRefresh();
}


void miacode::runtime::PlaybackCoordinator::onTimelineHeaderNavigateRequested(double second)
{
    navigateTimelineToSecond(second, true);
}

void miacode::runtime::PlaybackCoordinator::onTimelineUserInteractionStarted()
{
    const bool pauseForViewportLock =
        state_.previewFollowEnabled_ && state_.previewViewportLockEnabled_;
    if (!state_.previewProgressFollowEnabled_ && !pauseForViewportLock) {
        return;
    }
    if (state_.playing_) {
        pauseQtPreviewPlaybackExact();
        updatePauseButtonAppearance();
        if (pauseForViewportLock) {
            const double second = qMax(0.0, state_.pauseSecond_);
            QTimer::singleShot(0, &owner_, [this, second]() {
                if (state_.previewFollowEnabled_ && state_.previewViewportLockEnabled_) {
                    syncEditorCursorToPreviewSecond(second, true, false);
                }
            });
        }
    }
}

void miacode::runtime::PlaybackCoordinator::onTimelineDragStarted()
{
    appendTimelineInteractionLog(
        QStringLiteral("drag_scrub_begin"),
        QString("playing=%1 current_second=%2")
            .arg(state_.playing_ ? 1 : 0)
            .arg(state_.pauseSecond_, 0, 'f', 6));
    stopPreviewHeldSeek();
    QToolTip::hideText();
    state_.previewScrubRenderElapsed_.invalidate();
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
}

void miacode::runtime::PlaybackCoordinator::onTimelineCenterNavigateRequested(double second)
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
        preview_.ensurePreviewStageMediaRouteInitialized();
        ensurePreviewSfxRuntimePrepared(state_);
        requestPausedPreviewSeek(clampedSecond, false, false, false);
        state_.previewScrubRenderElapsed_.restart();
    } else {
        schedulePreviewSeek(clampedSecond, false);
    }
}

void miacode::runtime::PlaybackCoordinator::onTimelineWheelNavigateRequested(double second)
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

void miacode::runtime::PlaybackCoordinator::onTimelineDragFinished(double second)
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

void miacode::runtime::PlaybackCoordinator::onTimelineFollowPreviewToggled(bool enabled)
{
    state_.previewFollowEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setFollowPreviewEnabled(enabled);
    }
    invalidatePreviewFollowBindingCache();
    preferences_.savePortableState();
    if (!hasActiveDifficulty()) {
        clearPreviewFollowDecoration();
        return;
    }
    // Turning the option off stops the caret/viewport follow, not the highlight:
    // it stays as the on-screen cue for where the playhead is (and as the target
    // touch-pad click authoring writes to). Refresh it either way.
    const double second = qMax(0.0, authoritativeAudioClockSecond());
    syncEditorCursorToPreviewSecond(
        second,
        enabled && state_.playing_ && state_.previewViewportLockEnabled_,
        !state_.playing_);
}

void miacode::runtime::PlaybackCoordinator::onTimelineViewportLockToggled(bool enabled)
{
    state_.previewViewportLockEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setViewportLockEnabled(enabled);
    }
    preferences_.savePortableState();
    if (!enabled || !hasActiveDifficulty()) {
        return;
    }
    if (state_.playing_ && state_.previewFollowEnabled_) {
        const double second = qMax(0.0, authoritativeAudioClockSecond());
        syncEditorCursorToPreviewSecond(second, true, false);
    }
}

void miacode::runtime::PlaybackCoordinator::onTimelineFollowProgressToggled(bool enabled)
{
    state_.previewProgressFollowEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setFollowProgressEnabled(enabled);
    }
    preferences_.savePortableState();
}

void miacode::runtime::PlaybackCoordinator::onTimelineSyncToggled(bool enabled)
{
    state_.timelineSyncEnabled_ = enabled;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setTimelineSyncEnabled(enabled);
    }
    preferences_.savePortableState();
}

void miacode::runtime::PlaybackCoordinator::applyLatestTimelinePreviewStateToPausedPreview()
{
    if (state_.playing_) {
        return;
    }

    miacode::diag::MemoryStageScope memScope("preview/mem_stage", "preview_state_push");
    const bool noteMarkersChanged = state_.latestTimelineNoteMarkerSignature_ != state_.lastPreviewNoteMarkerSignature_;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->applyPausedPreviewState(
            state_.latestTimelineNoteMarkers_,
            noteMarkersChanged,
            state_.pauseSecond_,
            state_.previewPlaybackRate_,
            state_.previewTimingSettings_);
    }

    refreshPreviewObjectStatsTotals(state_.latestTimelineNoteMarkers_);
    if (state_.scene_ != nullptr && noteMarkersChanged) {
        state_.scene_->setNoteMarkers(state_.latestTimelineNoteMarkers_);
    }
    validation_.applyAlignedMuriAnalysisReportToViews();
    state_.lastPreviewNoteMarkerSignature_ = state_.latestTimelineNoteMarkerSignature_;
}

void miacode::runtime::PlaybackCoordinator::requestTimelineSlowRefresh()
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

void miacode::runtime::PlaybackCoordinator::dispatchTimelineSlowRefresh()
{
    if (state_.timelineSlowWorkerRunning_ || state_.pendingTimelineSlowRefresh_.revision == 0) {
        return;
    }

    const TimelineSlowRefreshRequest request = state_.pendingTimelineSlowRefresh_;
    state_.pendingTimelineSlowRefresh_ = TimelineSlowRefreshRequest();
    state_.timelineSlowWorkerRunning_ = true;
    state_.timelineSlowRunningRevision_ = request.revision;
    QPointer<QObject> guard(&owner_);
    QThreadPool* const pool = state_.timelineSlowRefreshPool_ != nullptr
        ? state_.timelineSlowRefreshPool_
        : QThreadPool::globalInstance();
    pool->start([this, guard, request]() {
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
            [this, guard, request, parseResult, previewState]() mutable {
                miacode::diag::leak_gauge::noteInflightApplied();
                if (guard.isNull()) {
                    return;
                }

                state_.timelineSlowWorkerRunning_ = false;
                if (request.revision != state_.timelineSlowRequestedRevision_
                    || request.revision != state_.timelineRevision_
                    || !hasActiveDifficulty()
                    || request.difficultyId != activeDifficultyId()
                    || request.chartText != activeChartText()
                    || request.timingMetadata != currentTimingMetadata()) {
                    dispatchTimelineSlowRefresh();
                    return;
                }

                state_.lastTimelineParseDifficultyId_ = request.difficultyId;
                state_.lastTimelineParseChartText_ = request.chartText;
                state_.lastTimelineParseTimingMetadata_ = request.timingMetadata;
                state_.lastTimelineParseResult_ = parseResult;
                state_.latestTimelineNoteMarkers_ = previewState.shiftedNoteMarkers;
                state_.latestTimelineNoteMarkerSignature_ = previewState.noteMarkerSignature;
                state_.latestTimelinePreviewRevision_ = request.revision;
                state_.latestTimelinePreviewSnapshotReady_ = true;
                if (!state_.playing_) {
                    applyLatestTimelinePreviewStateToPausedPreview();
                }
                if (state_.pendingDifficultySwitchPreviewRestore_
                    && state_.pendingDifficultySwitchPreviewRestoreRevision_ == request.revision
                    && state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ == request.difficultyId) {
                    const double restoreSecond = state_.pendingDifficultySwitchPreviewRestoreSecond_;
                    state_.pendingDifficultySwitchPreviewRestore_ = false;
                    state_.pendingDifficultySwitchPreviewRestoreRevision_ = 0;
                    state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ = 0;
                    state_.pendingDifficultySwitchPreviewRestoreSecond_ = 0.0;
                    seekPreviewDiscreteToSecond(restoreSecond, false);
                    deferTimelineCursorBridgeUpdate(restoreSecond, false);
                }
                if (state_.previewFollowEnabled_ && hasActiveDifficulty()) {
                    const double followSecond = state_.playing_
                        ? authoritativeAudioClockSecond()
                        : state_.pauseSecond_;
                    // Chart edits rebuild the timeline. Refresh the follow span
                    // only — reveal would yank the editor off the caret when the
                    // playhead and the caret cannot share one viewport.
                    syncEditorCursorToPreviewSecond(
                        qMax(0.0, followSecond),
                        false,
                        false);
                }
                scheduleTimelineAnalysisRefresh(request, parseResult, previewState);
                if (state_.pendingPreviewPlaybackStart_
                    && !state_.playing_
                    && state_.pendingPreviewPlaybackRevision_ == request.revision
                    && state_.pendingPreviewPlaybackDifficultyId_ == request.difficultyId) {
                    const double pendingSecond = state_.pendingPreviewPlaybackSecond_;
                    const bool resumeFromPause = state_.pendingPreviewPlaybackResumeFromPause_;
                    state_.pendingPreviewPlaybackStart_ = false;
                    startQtPreviewPlayback(pendingSecond, resumeFromPause);
                }
                dispatchTimelineSlowRefresh();
            },
            Qt::QueuedConnection
        );
    });
}


void miacode::runtime::PlaybackCoordinator::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    state_.muriStaticReferences_ = miacode::muri::buildStaticMuriReferences(
        noteMarkers,
        static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0);
    state_.muriStaticReferencesNoteMarkerSignature_ = state_.latestTimelineNoteMarkerSignature_;
    state_.muriStaticReferencesDifficultyId_ = hasActiveDifficulty() ? activeDifficultyId() : 0;
    state_.muriStaticReferencesTimelineRevision_ = state_.timelineRevision_;
    state_.muriStaticReferencesAvailable_ = !state_.muriStaticReferencesNoteMarkerSignature_.isEmpty();
}

double miacode::runtime::PlaybackCoordinator::timelineSecondForCursor(int line, int col) const
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

bool miacode::runtime::PlaybackCoordinator::resolveTimelineSecondForCursor(int line, int col, double* second) const
{
    return state_.timelineQuickModel_.resolveTimelineSecondForCursor(line, col, second);
}

void miacode::runtime::PlaybackCoordinator::updateTimelineCursorFromEditorLocation(
    int line, int col, bool centerView)
{
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    const bool bridgeCenterView = !state_.playing_ && centerView;
    if (state_.timelineQuickStateBridge_ != nullptr) {
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

void miacode::runtime::PlaybackCoordinator::navigateTimelineToSecond(double second, bool focusEditor)
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
}

bool miacode::runtime::PlaybackCoordinator::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    return state_.timelineQuickModel_.resolveNearestTimelineNote(second, lane, line, col, noteSecond);
}

bool miacode::runtime::PlaybackCoordinator::moveEditorCursorToTimelineLocation(
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

    Q_UNUSED(suppressSignals);
    int startColumn = col;
    int endColumn = col;
    if (selectToken) {
        state_.timelineQuickModel_.resolvePreviewFollowSelectionRange(
            line, col, &startColumn, &endColumn);
    }
    return documents_.requestEditorNavigation(
        line, startColumn, line, endColumn,
        selectToken, focusEditor, centerView);
}


// Session::resetPreviewTrackTimelineOffsets moved to
// SessionForwarding.TimelineFlow.cpp (stage 4.9d-6: TU boundary split).


miacode::waveform::WaveformCacheService* miacode::runtime::PlaybackCoordinator::ensureWaveformCacheService()
{
    if (state_.waveformCacheService_ == nullptr) {
        state_.waveformCacheService_ = new miacode::waveform::WaveformCacheService(&owner_);
    }
    state_.waveformCacheService_->setThreadPool(
        state_.previewWarmupPool_ != nullptr ? state_.previewWarmupPool_ : QThreadPool::globalInstance());
    return state_.waveformCacheService_;
}

// Session::applyWaveformData, Session::refreshWaveformCache (both overloads),
// Session::applyLatencyDetectorOffset, Session::latencyDocumentWholeBpm,
// Session::latencyDocumentOffsetSeconds, Session::latencyDocumentClockCount,
// Session::latencyTrackPath, Session::applyLatencyDetectorBpm,
// Session::applyLatencyDetectorClockCount, Session::setCurrentFilePath,
// Session::updateWindowTitle, Session::editorText,
// Session::scheduleTimelineRefresh, Session::refreshTimelineMetadata,
// Session::refreshTimelineQuickModelFromCurrentText,
// Session::applyLatestTimelinePreviewStateToPausedPreview,
// Session::requestTimelineSlowRefresh, and Session::dispatchTimelineSlowRefresh
// moved to SessionForwarding.TimelineFlow.cpp (stage 4.9d-6: TU boundary split so
// this file holds only Coordinator:: methods).





















// Session::scheduleTimelineAnalysisRefresh,
// Session::scheduleTimelineAnalysisRefreshFromLatestPreviewState,
// Session::requestTimelineAnalysisDispatch, Session::dispatchTimelineAnalysisRefresh,
// Session::rebuildStaticMuriReferences, Session::timelineSecondForCursor,
// Session::setTouchPadAuthoringAnchor, Session::resolveTimelineSecondForCursor,
// Session::publishEditorCaret, Session::handleEditorPointerInteraction,
// Session::editorSyncController (both overloads),
// Session::applyTouchPadAuthoringPreviewAnchor, Session::seekPreviewToEditorLocation,
// Session::editorAuthoringContextActive, Session::navigateTimelineToSecond,
// Session::deferTimelineCursorBridgeUpdate, Session::resolveNearestTimelineNote,
// Session::moveEditorCursorToTimelineLocation, and
// Session::syncEditorCursorToPreviewSecond moved to SessionForwarding.TimelineFlow.cpp
// (stage 4.9d-6: TU boundary split so this file holds only Coordinator:: methods).
