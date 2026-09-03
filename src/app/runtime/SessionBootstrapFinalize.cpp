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
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
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
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

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

void Session::finishFrameBootstrap(QToolBar* toolBar, const std::function<void(const QString&)>& logStartupStage)
{
    Q_UNUSED(toolBar);

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

    if (previewSlider_ != nullptr) {
        previewSlider_->setFocusPolicy(Qt::StrongFocus);
        previewSlider_->installEventFilter(this);
        connect(previewSlider_, &QSlider::sliderPressed, this, [this]() {
            stopPreviewHeldSeek();
            const quint64 interactionId = ++previewInteractionSequence_;
            const bool wasPlaying = playing_;
            const double authoritativeSecond = currentPreviewAuthoritativeAudioClockSecond();
            previewScrubInteractionId_ = interactionId;
            previewScrubMoveCount_ = 0;
            previewScrubStartSliderValue_ = previewSlider_ != nullptr ? previewSlider_->value() : 0;
            previewScrubLastSliderValue_ = previewScrubStartSliderValue_;
            appendPreviewInteractionLog(
                QStringLiteral("scrub_press"),
                QString("op=%1 slider_ms=%2 slider_second=%3 playing_before=%4 authoritative_second=%5")
                    .arg(interactionId)
                    .arg(previewScrubStartSliderValue_)
                    .arg(static_cast<double>(previewScrubStartSliderValue_) / 1000.0, 0, 'f', 6)
                    .arg(wasPlaying ? 1 : 0)
                    .arg(authoritativeSecond, 0, 'f', 6));
            if (previewSlider_ != nullptr) {
                previewSlider_->setFocus(Qt::MouseFocusReason);
            }
            if (previewFullscreenActive_) {
                showPreviewFullscreenControls(false);
            }
            if (playing_) {
                pauseQtPreviewPlaybackExact();
            }
            appendPreviewInteractionLog(
                QStringLiteral("scrub_pause_transition"),
                QString("op=%1 playing_after=%2 pause_second=%3 manual_generation=%4 manual_txn=%5 manual_sequence=%6")
                    .arg(interactionId)
                    .arg(playing_ ? 1 : 0)
                    .arg(pauseSecond_, 0, 'f', 6)
                    .arg(previewPendingManualPauseGeneration_)
                    .arg(previewPendingManualPauseTransactionId_)
                    .arg(previewPendingManualPauseSequence_));
            previewScrubDragging_ = true;
            previewScrubRenderElapsed_.invalidate();
            if (previewSlider_ != nullptr) {
                showPreviewSliderTimeHint(previewSlider_->value());
            }
        });
        connect(previewSlider_, &QSlider::sliderMoved, this, [this](int value) {
            if (previewSlider_ == nullptr) {
                return;
            }
            if (previewFullscreenActive_) {
                showPreviewFullscreenControls(false);
            }
            showPreviewSliderTimeHint(value);
            const double second = static_cast<double>(value) / 1000.0;
            const quint64 interactionId = previewScrubInteractionId_;
            const int moveIndex = ++previewScrubMoveCount_;
            const int previousValue = previewScrubLastSliderValue_;
            previewScrubLastSliderValue_ = value;
            // Negative-time intro region: a drag into [-duration, 0) renders a
            // static intro frame instead of a chart seek.
            if (handleExportIntroSliderSeek(second)) {
                appendPreviewInteractionLog(
                    QStringLiteral("scrub_move_intro"),
                    QString("op=%1 index=%2 from_ms=%3 target_ms=%4 target_second=%5")
                        .arg(interactionId)
                        .arg(moveIndex)
                        .arg(previousValue)
                        .arg(value)
                        .arg(second, 0, 'f', 6));
                return;
            }
            const bool shouldRenderNow = !previewScrubRenderElapsed_.isValid()
                || previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
            const quint64 generationBefore = pausedSeekGeneration_;
            if (shouldRenderNow) {
                requestPausedPreviewSeek(second, true, false, false);
                previewScrubRenderElapsed_.restart();
            } else {
                schedulePreviewSeek(second, true);
            }
            appendPreviewInteractionLog(
                QStringLiteral("scrub_move"),
                QString("op=%1 index=%2 from_ms=%3 target_ms=%4 target_second=%5 render_now=%6 "
                        "seek_generation_before=%7 seek_generation_after=%8 visual_pause_second=%9 "
                        "manual_pause_sequence=%10")
                    .arg(interactionId)
                    .arg(moveIndex)
                    .arg(previousValue)
                    .arg(value)
                    .arg(second, 0, 'f', 6)
                    .arg(shouldRenderNow ? 1 : 0)
                    .arg(generationBefore)
                    .arg(pausedSeekGeneration_)
                    .arg(pauseSecond_, 0, 'f', 6)
                    .arg(previewPendingManualPauseSequence_));
        });
        connect(previewSlider_, &QSlider::sliderReleased, this, [this]() {
            stopPreviewHeldSeek();
            previewScrubDragging_ = false;
            previewScrubRenderElapsed_.invalidate();
            if (previewSlider_ == nullptr) {
                return;
            }
            if (previewFullscreenActive_) {
                showPreviewFullscreenControls(false);
            }
            showPreviewSliderTimeHint(previewSlider_->value());
            if (previewSeekDebounceTimer_ != nullptr) {
                previewSeekDebounceTimer_->stop();
            }
            const double releasedSecond = static_cast<double>(previewSlider_->value()) / 1000.0;
            const quint64 interactionId = previewScrubInteractionId_;
            appendPreviewInteractionLog(
                QStringLiteral("scrub_release"),
                QString("op=%1 moves=%2 start_ms=%3 target_ms=%4 target_second=%5 pause_second=%6 "
                        "manual_generation=%7 manual_txn=%8 manual_sequence=%9")
                    .arg(interactionId)
                    .arg(previewScrubMoveCount_)
                    .arg(previewScrubStartSliderValue_)
                    .arg(previewSlider_->value())
                    .arg(releasedSecond, 0, 'f', 6)
                    .arg(pauseSecond_, 0, 'f', 6)
                    .arg(previewPendingManualPauseGeneration_)
                    .arg(previewPendingManualPauseTransactionId_)
                    .arg(previewPendingManualPauseSequence_));
            if (handleExportIntroSliderSeek(releasedSecond)) {
                appendPreviewInteractionLog(
                    QStringLiteral("scrub_release_intro"),
                    QString("op=%1 target_second=%2").arg(interactionId).arg(releasedSecond, 0, 'f', 6));
                previewScrubInteractionId_ = 0;
                return;
            }
            seekPreviewToSecond(releasedSecond, true);
            appendPreviewInteractionLog(
                QStringLiteral("scrub_release_dispatched"),
                QString("op=%1 target_second=%2 final_pause_second=%3 manual_sequence_after=%4")
                    .arg(interactionId)
                    .arg(releasedSecond, 0, 'f', 6)
                    .arg(pauseSecond_, 0, 'f', 6)
                    .arg(previewPendingManualPauseSequence_));
            previewScrubInteractionId_ = 0;
        });
    }
    if (previewControlCard_ != nullptr) {
        previewControlCard_->installEventFilter(this);
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->installEventFilter(this);
    }
    if (pausePreviewButton_ != nullptr) {
        pausePreviewButton_->installEventFilter(this);
    }
    if (previewSpeedButton_ != nullptr) {
        previewSpeedButton_->installEventFilter(this);
    }
    if (previewFullscreenButton_ != nullptr) {
        previewFullscreenButton_->installEventFilter(this);
    }
    if (QApplication::instance() != nullptr) {
        QApplication::instance()->installEventFilter(this);
    }
    shell_->updateEditorFindBarGeometry();
    shell_->applyFindOverlayInset();
    const auto applyEditorFontDelta = [this](int delta) {
        settings_->applyEditorTextFontSize(editorTextFontPointSize_ + delta, true);
        noteStatus(UiText::text(QStringLiteral("status.editor_text_display_updated")));
    };
    fontDecreaseAction_ = new QAction(QStringLiteral("Decrease Editor Font"), this);
    ShortcutRegistry::instance().applyShortcut(
        fontDecreaseAction_,
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Alt+-")));
    fontDecreaseAction_->setShortcutContext(Qt::ApplicationShortcut);
    connect(fontDecreaseAction_, &QAction::triggered, this, [applyEditorFontDelta]() {
        applyEditorFontDelta(-1);
    });
    fontIncreaseAction_ = new QAction(QStringLiteral("Increase Editor Font"), this);
    ShortcutRegistry::instance().applyShortcut(
        fontIncreaseAction_,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Alt+=")));
    fontIncreaseAction_->setShortcutContext(Qt::ApplicationShortcut);
    connect(fontIncreaseAction_, &QAction::triggered, this, [applyEditorFontDelta]() {
        applyEditorFontDelta(1);
    });
    noteStatus("Editor ready.");

    loadPortableState();
    applyWorkspacePanelArrangement();
    shell_->setOutlineDockCollapsed(outlineDockCollapsed_);
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
    shell_->applyUiTheme();
    updatePauseButtonAppearance();
    const bool restoredStartupDocument = restoreLastSessionFile();
    if (!restoredStartupDocument) {
        applicationServices_.workspace().openSource(SimaiDocument::createEmpty().toText());
        loadDocument();
        logStartupStage("initial_empty_document_applied");
    } else {
        logStartupStage("initial_last_session_document_applied");
    }
    updatePreviewSliderRange();
    updatePreviewSliderPosition(0.0);
    logStartupStage("initial_document_loaded");
    qtPreviewWatchdogElapsed_.start();
    logStartupStage("preview_media_controller_lazy_init_deferred");
    QTimer::singleShot(0, this, [this]() {
        schedulePreviewSubsystemWarmup();
    });
    logStartupStage("preview_subsystem_warmup_scheduled");
    logStartupStage("constructor_done");
}
