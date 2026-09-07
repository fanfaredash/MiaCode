#include "runtime/Session.h"
#include "runtime/Shared.h"
#include "runtime/editor/EditorHost.h"
#include "runtime/media/MediaJobsHost.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/export/VideoExportHost.h"
#include "runtime/settings/SettingsHost.h"
#include "runtime/preview/StageMediaHost.h"
#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/validation/ValidationHost.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>

using namespace miacode::runtime::shared;

namespace {

void appendPreviewInteractionLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/interaction"),
        text
    );
}

void appendPreviewFramePacingDiagLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/frame_pacing"),
        text,
        true
    );
}

int previewFrameSwapWatchdogTimeoutMs(qint64 frameIntervalNs)
{
    return qMax(
        24,
        qMax(1, qRound(static_cast<double>(frameIntervalNs) / 1000000.0)) * 2
    );
}

}  // namespace

void Session::finishFrameBootstrap(const std::function<void(const QString&)>& logStartupStage)
{
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(kAutosaveIntervalMs);
    connect(autosaveTimer_, &QTimer::timeout, this, [this]() { runAutosaveCheck(true); });
    autosaveIdleTimer_ = new QTimer(this);
    autosaveIdleTimer_->setSingleShot(true);
    autosaveIdleTimer_->setInterval(kAutosaveLatestIdleMs);
    connect(autosaveIdleTimer_, &QTimer::timeout, this, [this]() { runAutosaveCheck(false); });

    qtPreviewTimer_ = new QChronoTimer(this);
    qtPreviewTimer_->setInterval(std::chrono::milliseconds(16));
    qtPreviewTimer_->setSingleShot(true);
    qtPreviewTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimer_, &QChronoTimer::timeout, this, [this]() {
        // Doc section 4.1/4.3: qtPreviewTimer_ is a watchdog for both modes — it no longer
        // carries the normal visual cadence. It only fires if the framePresented pipeline
        // has stalled long enough to warrant a kick.
        if (!playing_) {
            return;
        }
        const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
        const int stallTimeoutMs =
            previewFrameSwapWatchdogTimeoutMs(previewCanvasTargetFrameIntervalNs());
        if (previewCanvasUsesFrameSwappedPacing()) {
            if (!qtPreviewAwaitingFrameSwap_) {
                return;
            }
            if (qtPreviewAwaitingFrameSwapSinceMs_ >= 0
                && nowMs - qtPreviewAwaitingFrameSwapSinceMs_ >= stallTimeoutMs) {
                const qint64 waitMs = nowMs - qtPreviewAwaitingFrameSwapSinceMs_;
                if (state_.previewStageMediaHost_ != nullptr) {
                    state_.previewStageMediaHost_->noteFirstPlaybackRenderStall(
                        state_.activePreviewPlaybackTransactionId_,
                        waitMs,
                        qMax(0.0, pauseSecond_),
                        qMax(0.0, currentPreviewAuthoritativeAudioClockSecond()));
                }
                qtPreviewDisplayRefreshConsecutiveWatchdogs_ += 1;
                if (scene_ != nullptr) {
                    scene_->noteDisplayRefreshWatchdogTimeout();
                    scene_->noteDisplayRefreshTimerFallbackTick();
                }
                appendPreviewFramePacingDiagLog(
                    QStringLiteral("display_watchdog_timeout"),
                    QStringLiteral(
                        "wait_ms=%1 target_ms=%2 txn=%3 playhead=%4 audio_second=%5 consecutive_count=%6"
                    )
                        .arg(waitMs)
                        .arg(stallTimeoutMs)
                        .arg(state_.activePreviewPlaybackTransactionId_)
                        .arg(qMax(0.0, pauseSecond_), 0, 'f', 6)
                        .arg(qMax(0.0, currentPreviewAuthoritativeAudioClockSecond()), 0, 'f', 6)
                        .arg(qtPreviewDisplayRefreshConsecutiveWatchdogs_)
                );
                qtPreviewAwaitingFrameSwap_ = false;
                qtPreviewAwaitingFrameSwapSinceMs_ = -1;
                qtPreviewAwaitingFrameSwapSinceNs_ = -1;
                qtPreviewDisplayRefreshTickQueued_ = false;
                onQtPreviewTick();
                return;
            }
            scheduleNextQtPreviewTick();
            return;
        }
        // Fixed interval watchdog (doc 4.1): if no present in a reasonable window while we had
        // an update() request in flight, clear state and kick a new frame request.
        if (!qtPreviewFixedAwaitingFrame_) {
            return;
        }
        if (qtPreviewFixedAwaitingFrameSinceMs_ >= 0
            && nowMs - qtPreviewFixedAwaitingFrameSinceMs_ >= stallTimeoutMs) {
            const qint64 waitMs = nowMs - qtPreviewFixedAwaitingFrameSinceMs_;
            if (state_.previewStageMediaHost_ != nullptr) {
                state_.previewStageMediaHost_->noteFirstPlaybackRenderStall(
                    state_.activePreviewPlaybackTransactionId_,
                    waitMs,
                    qMax(0.0, pauseSecond_),
                    qMax(0.0, currentPreviewAuthoritativeAudioClockSecond()));
            }
            if (scene_ != nullptr) {
                scene_->noteFixedGateWatchdogKick();
            }
            appendPreviewFramePacingDiagLog(
                QStringLiteral("fixed_gate_watchdog_kick"),
                QStringLiteral(
                    "wait_ms=%1 target_ms=%2 txn=%3 playhead=%4 audio_second=%5 time_authority=%6"
                )
                    .arg(waitMs)
                    .arg(stallTimeoutMs)
                    .arg(state_.activePreviewPlaybackTransactionId_)
                    .arg(qMax(0.0, pauseSecond_), 0, 'f', 6)
                    .arg(qMax(0.0, currentPreviewAuthoritativeAudioClockSecond()), 0, 'f', 6)
                    .arg(previewSfxRuntime_ != nullptr
                        ? QStringLiteral("audio")
                        : QStringLiteral("elapsed_fallback"))
            );
            qtPreviewFixedAwaitingFrame_ = false;
            qtPreviewFixedAwaitingFrameSinceMs_ = -1;
            qtPreviewFixedAwaitingFrameSinceNs_ = -1;
            qtPreviewFixedFrameTickQueued_ = false;
            // Re-prime the present pump; do not advance visual time here — onQtPreviewTick will
            // run from the next real framePresented once it arrives.
            requestNextFixedIntervalPreviewFrame();
            return;
        }
        scheduleNextQtPreviewTick();
    });

    // Watchdog, not the visual cadence. The timeline's playback sampling is driven by
    // TimelineQuickItem's afterAnimating hook (bridge::renderCadenceTick, connected in
    // MainWindow.FrameBootstrap.cpp), which is phase-locked to the frame being synced.
    // Driving the sample from this free-running timer instead was the timeline judder: the
    // timer's phase drifts against vsync (~2.3us/frame measured), so sample->present latency
    // wandered across the whole frame interval and 16% of on-time frames were drawn for the
    // wrong moment. The timer stays at the frame interval so that if the cadence dies the
    // fallback runs at full rate exactly as before; onTimelineCadenceWatchdogTick() no-ops
    // while the cadence is alive.
    qtPreviewTimelineTimer_ = new QChronoTimer(this);
    qtPreviewTimelineTimer_->setInterval(std::chrono::nanoseconds(
        qMax<qint64>(
            1,
            playback_ != nullptr
                ? playback_->timelineTargetFrameIntervalNs()
                : previewCanvasTargetFrameIntervalNs())));
    qtPreviewTimelineTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimelineTimer_, &QChronoTimer::timeout, this, &Session::onTimelineCadenceWatchdogTick);

    previewStatsUiTimer_ = new QTimer(this);
    previewStatsUiTimer_->setInterval(67);
    previewStatsUiTimer_->setTimerType(Qt::PreciseTimer);
    connect(previewStatsUiTimer_, &QTimer::timeout, this, [this]() {
        if (!playing_) {
            if (previewStatsUiTimer_ != nullptr) {
                previewStatsUiTimer_->stop();
            }
            return;
        }
        updatePreviewObjectStats(pauseSecond_);
    });

    previewSeekDebounceTimer_ = new QTimer(this);
    previewSeekDebounceTimer_->setSingleShot(true);
    previewSeekDebounceTimer_->setInterval(33);
    connect(previewSeekDebounceTimer_, &QTimer::timeout, this, [this]() {
        maybeSubmitLatestPausedMediaSeek();
    });
    // Debounce persisting the bottom-tabs (timeline/validation/muri) divider
    // height. The drag funnels through setShellBottomTabsHeight() which updates
    // bottomTabsContentScale_ on every move tick; coalesce the synchronous
    // savePortableState() write so a drag doesn't hammer preferences.json. A
    // pending write is still covered by the close-time savePortableState().
    bottomTabsContentScalePersistTimer_ = new QTimer(this);
    bottomTabsContentScalePersistTimer_->setSingleShot(true);
    bottomTabsContentScalePersistTimer_->setInterval(500);
    connect(bottomTabsContentScalePersistTimer_, &QTimer::timeout, this, [this]() {
        savePortableState();
    });
    visualLayoutPersistTimer_ = new QTimer(this);
    visualLayoutPersistTimer_->setSingleShot(true);
    visualLayoutPersistTimer_->setInterval(500);
    connect(visualLayoutPersistTimer_, &QTimer::timeout, this, [this]() {
        savePortableState();
    });
    previewHeldSeekTimer_ = new QTimer(this);
    previewHeldSeekTimer_->setTimerType(Qt::PreciseTimer);
    previewHeldSeekTimer_->setInterval(miacode::preview_interaction::kSeekHoldTickIntervalMs);
    connect(previewHeldSeekTimer_, &QTimer::timeout, this, &Session::applyPreviewHeldSeekTick);

    timelineAnalysisIdleTimer_ = new QTimer(this);
    timelineAnalysisIdleTimer_->setSingleShot(true);
    connect(timelineAnalysisIdleTimer_, &QTimer::timeout, this, &Session::dispatchTimelineAnalysisRefresh);
    logStartupStage("timers_ready");

    loadPortableState();
    logStartupStage("portable_state_loaded");
    // beta4: the preview debug HUD ("显示预览调试信息") is NO LONGER force-enabled in
    // --debug / diagnostic builds. It used to default ON whenever runtime debug output was
    // enabled, so every diagnostic run paid the per-frame debug-overlay paint + stats cost
    // (which also skewed the frame-drop investigation). The HUD is now driven solely by the
    // user's render-settings toggle (previewShowDebugInfo_, default false), independent of
    // debug logging. See docs/PREVIEW_FRAMEDROP_DIAGNOSIS_AND_FIX_SPEC_ZH.md.
    if (previewUsesStageMediaHostRoute()) {
        ensurePreviewStageMediaRouteInitialized();
    }
    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_, "frame_bootstrap_finalize");
    applyPreviewStageMediaRouteVisualSettings();
    if (scene_ != nullptr) {
        applyEffectivePreviewOutlineVariantToCanvas();
        applyPreviewSkinDirectoryToSurfaces();
        scene_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        scene_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        scene_->setLayoutSquareScale(previewLayoutSquareScale_);
        scene_->setSmoothBrightness(previewSmoothBrightness_);
        scene_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        scene_->setTapFlowSpeed(previewTapFlowSpeed_);
        scene_->setTouchFlowSpeed(previewTouchFlowSpeed_);
        scene_->setSlideEarlierSecondAndTextOnTop(previewSlideEarlierSecondAndTextOnTop_);
        scene_->setTapJudgeTextDistance(previewTapJudgeTextDistance_);
        scene_->setJudgeEffectStyle(previewJudgeEffectStyle_);
        scene_->setShowDebugInfo(previewShowDebugInfo_);
        scene_->setShowTimestamp(previewShowTimestamp_);
        scene_->setShowObjectStatsHud(previewShowObjectStatsHud_);
        // Chart info HUD only ever activates inside the export-preview
        // dialog (gated by setSuppressDebugInfo + setChartInfo there); the
        // editor's main preview keeps it off.
        scene_->setShowChartInfoHud(false);
        scene_->setCenterDisplayMode(previewCenterDisplayMode_);
    }
    applyMuriRenderOptions();
    updatePauseButtonAppearance();
    const bool restoredStartupDocument = restoreLastSessionFile();
    if (!restoredStartupDocument) {
        applicationServices_.workspace().openSource(SimaiDocument::createEmpty().toText());
        loadDocument();
        logStartupStage("initial_empty_document_applied");
    } else {
        logStartupStage("initial_last_session_document_applied");
    }
    publishPreviewPlayhead();
    logStartupStage("initial_document_loaded");
    qtPreviewWatchdogElapsed_.start();
    logStartupStage("preview_media_controller_lazy_init_deferred");
    QTimer::singleShot(0, this, [this]() {
        schedulePreviewSubsystemWarmup();
    });
    logStartupStage("preview_subsystem_warmup_scheduled");
    logStartupStage("constructor_done");
}
