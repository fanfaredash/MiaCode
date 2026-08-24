#include "../../MainWindow.h"
#include "../../MainWindowShared.h"
#include "MainWindow.FrameSection.h"
#include "../editor/MainWindow.EditorSection.h"
#include "../dialogs/MainWindow.DialogsSection.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../export/MainWindow.ExportSection.h"
#include "../preferences/MainWindow.PreferencesSection.h"
#include "../preview/MainWindow.PreviewSection.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "../validation/MainWindow.ValidationSection.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
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
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

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

void MainWindow::finishFrameBootstrap(QToolBar* toolBar, const std::function<void(const QString&)>& logStartupStage)
{
    constexpr int kToolbarLeadingSpacerWidth = 6;
    auto* toolbarLeadingSpacer = new QWidget(toolBar);
    toolbarLeadingSpacer->setFixedWidth(kToolbarLeadingSpacerWidth);
    toolBar->addWidget(toolbarLeadingSpacer);
    toolBar->addAction(openAction_);
    toolBar->addAction(saveAction_);
    // Beta20-fix / post-rename — we used to clamp every custom toolbar
    // button to a 64-px floor with 12-px synthetic side padding. That
    // floor was wider than the natural width of Qt's own
    // addAction()-managed buttons next to it (Open / Save), so after
    // settings labels changed width over time, so the
    // pair sat in a visibly oversized cell with empty air on each side
    // of the text. We now let `sizeHint()` produce the natural width
    // exactly the way `addAction()` would, and only sync Audio/Video
    // to each other (so the pair stays equal width even when the two
    // labels happen to differ slightly in glyph advance).
    constexpr int kToolbarActionButtonWidth = 64;
    const auto makeCompactToolbarButton = [toolBar](QAction* action) -> QToolButton* {
        auto* button = new QToolButton(toolBar);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
        button->ensurePolished();
        return button;
    };

    previewAudioSettingsButton_ = makeCompactToolbarButton(previewAudioSettingsAction_);
    previewVideoSettingsButton_ = makeCompactToolbarButton(previewVideoSettingsAction_);
    // 皮肤 sits between 预览设置 and 导出 but matches the 导出 button width (computed
    // below), NOT the wider 音频设置/预览设置 equal-width pair.
    skinSettingsButton_ = makeCompactToolbarButton(skinSettingsAction_);
    int settingsButtonWidth = 1;
    if (previewAudioSettingsButton_ != nullptr) {
        settingsButtonWidth = qMax(settingsButtonWidth, previewAudioSettingsButton_->sizeHint().width());
    }
    if (previewVideoSettingsButton_ != nullptr) {
        settingsButtonWidth = qMax(settingsButtonWidth, previewVideoSettingsButton_->sizeHint().width());
    }
    // Shared by the 皮肤 + 导出 buttons = the Open toolbar button's width.
    int actionButtonWidth = kToolbarActionButtonWidth;
    if (QWidget* openWidget = toolBar->widgetForAction(openAction_); openWidget != nullptr) {
        actionButtonWidth = qMax(1, openWidget->sizeHint().width());
    }
    if (previewAudioSettingsButton_ != nullptr) {
        previewAudioSettingsButton_->setFixedWidth(settingsButtonWidth);
        toolBar->addWidget(previewAudioSettingsButton_);
    }
    if (previewVideoSettingsButton_ != nullptr) {
        previewVideoSettingsButton_->setFixedWidth(settingsButtonWidth);
        toolBar->addWidget(previewVideoSettingsButton_);
    }
    if (skinSettingsButton_ != nullptr) {
        skinSettingsButton_->setFixedWidth(actionButtonWidth);
        toolBar->addWidget(skinSettingsButton_);
    }
    settingsPlaceholderAction_ = toolBar->addAction(
        makeSettingsGearIcon(QColor("#5D6E83")),
        UiText::text(QStringLiteral("action.preferences"))
    );
    settingsPlaceholderAction_->setToolTip(UiText::text(QStringLiteral("action.preferences")));
    connect(settingsPlaceholderAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    // The toolbar Export button jumps straight to the Export hub page (the
    // sidebar "export" item equivalent) — the old dropdown menu + 250ms
    // hover-open timer are gone; the page itself hosts the four entries.
    // Deliberately NOT bound to exportVideoAction_ (difficulty-scoped): the
    // page is reachable without an active difficulty and greys its own cards.
    exportVideoButton_ = makeCompactToolbarButton(nullptr);
    if (exportVideoButton_ != nullptr) {
        exportVideoButton_->setText(UiText::text(QStringLiteral("toolbar.export")));
        exportVideoButton_->setToolTip(UiText::text(QStringLiteral("document.open_the_export_page_video")));
        exportVideoButton_->setFixedWidth(actionButtonWidth);
        toolBar->insertWidget(settingsPlaceholderAction_, exportVideoButton_);
        connect(exportVideoButton_, &QToolButton::clicked, this, [this]() {
            switchToExportField();
        });
    }

    statusBar()->setSizeGripEnabled(false);
    statusBar()->addPermanentWidget(new QLabel("Current File:", this));
    currentFileLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(currentFileLabel_, 1);
    updateCurrentFileLabel();

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
        if (!qtPreviewPlaying_) {
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
                        qMax(0.0, qtPreviewPauseSecond_),
                        qMax(0.0, currentPreviewAuthoritativeAudioClockSecond()));
                }
                qtPreviewDisplayRefreshConsecutiveWatchdogs_ += 1;
                if (previewCanvas_ != nullptr) {
                    previewCanvas_->noteDisplayRefreshWatchdogTimeout();
                    previewCanvas_->noteDisplayRefreshTimerFallbackTick();
                }
                appendPreviewFramePacingDiagLog(
                    QStringLiteral("display_watchdog_timeout"),
                    QStringLiteral(
                        "wait_ms=%1 target_ms=%2 txn=%3 playhead=%4 audio_second=%5 consecutive_count=%6"
                    )
                        .arg(waitMs)
                        .arg(stallTimeoutMs)
                        .arg(state_.activePreviewPlaybackTransactionId_)
                        .arg(qMax(0.0, qtPreviewPauseSecond_), 0, 'f', 6)
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
                    qMax(0.0, qtPreviewPauseSecond_),
                    qMax(0.0, currentPreviewAuthoritativeAudioClockSecond()));
            }
            if (previewCanvas_ != nullptr) {
                previewCanvas_->noteFixedGateWatchdogKick();
            }
            appendPreviewFramePacingDiagLog(
                QStringLiteral("fixed_gate_watchdog_kick"),
                QStringLiteral(
                    "wait_ms=%1 target_ms=%2 txn=%3 playhead=%4 audio_second=%5 time_authority=%6"
                )
                    .arg(waitMs)
                    .arg(stallTimeoutMs)
                    .arg(state_.activePreviewPlaybackTransactionId_)
                    .arg(qMax(0.0, qtPreviewPauseSecond_), 0, 'f', 6)
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
            timelineSection_ != nullptr
                ? timelineSection_->timelineTargetFrameIntervalNs()
                : previewCanvasTargetFrameIntervalNs())));
    qtPreviewTimelineTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimelineTimer_, &QChronoTimer::timeout, this, &MainWindow::onTimelineCadenceWatchdogTick);

    previewStatsUiTimer_ = new QTimer(this);
    previewStatsUiTimer_->setInterval(67);
    previewStatsUiTimer_->setTimerType(Qt::PreciseTimer);
    connect(previewStatsUiTimer_, &QTimer::timeout, this, [this]() {
        if (!qtPreviewPlaying_) {
            if (previewStatsUiTimer_ != nullptr) {
                previewStatsUiTimer_->stop();
            }
            return;
        }
        updatePreviewObjectStats(qtPreviewPauseSecond_);
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
    connect(previewHeldSeekTimer_, &QTimer::timeout, this, &MainWindow::applyPreviewHeldSeekTick);

    previewPlaybackRateToast_ = new QWidget(this);
    previewPlaybackRateToast_->setObjectName(QStringLiteral("previewPlaybackRateToast"));
    previewPlaybackRateToast_->setFocusPolicy(Qt::NoFocus);
    previewPlaybackRateToast_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    previewPlaybackRateToast_->setAttribute(Qt::WA_ShowWithoutActivating, true);
    previewPlaybackRateToast_->setStyleSheet(previewPlaybackRateToastStyleSheet());
    auto* previewPlaybackRateToastLayout = new QVBoxLayout(previewPlaybackRateToast_);
    previewPlaybackRateToastLayout->setContentsMargins(24, 18, 24, 18);
    previewPlaybackRateToastLayout->setSpacing(0);
    previewPlaybackRateToastLabel_ = new QLabel(previewPlaybackRateToast_);
    previewPlaybackRateToastLabel_->setAlignment(Qt::AlignCenter);
    previewPlaybackRateToastLabel_->setFocusPolicy(Qt::NoFocus);
    previewPlaybackRateToastLabel_->setTextFormat(Qt::RichText);
    previewPlaybackRateToastLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
    previewPlaybackRateToastLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    previewPlaybackRateToastLayout->addWidget(previewPlaybackRateToastLabel_);
    previewPlaybackRateToastOpacityEffect_ = new QGraphicsOpacityEffect(previewPlaybackRateToast_);
    previewPlaybackRateToastOpacityEffect_->setOpacity(1.0);
    previewPlaybackRateToast_->setGraphicsEffect(previewPlaybackRateToastOpacityEffect_);
    previewPlaybackRateToastOpacityAnimation_ =
        new QPropertyAnimation(previewPlaybackRateToastOpacityEffect_, "opacity", previewPlaybackRateToast_);
    previewPlaybackRateToastOpacityAnimation_->setDuration(240);
    previewPlaybackRateToastOpacityAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        previewPlaybackRateToastOpacityAnimation_,
        &QPropertyAnimation::finished,
        this,
        [this]() {
            if (previewPlaybackRateToastOpacityEffect_ != nullptr
                && previewPlaybackRateToastOpacityEffect_->opacity() <= 0.0) {
                hidePreviewPlaybackRateToast();
            }
        }
    );
    previewPlaybackRateToastTimer_ = new QTimer(this);
    previewPlaybackRateToastTimer_->setSingleShot(true);
    previewPlaybackRateToastTimer_->setInterval(900);
    connect(previewPlaybackRateToastTimer_, &QTimer::timeout, this, [this]() {
        if (previewPlaybackRateToast_ == nullptr || previewPlaybackRateToastOpacityAnimation_ == nullptr) {
            return;
        }
        if (previewPlaybackRateToastOpacityEffect_ != nullptr) {
            previewPlaybackRateToastOpacityEffect_->setOpacity(1.0);
        }
        previewPlaybackRateToastOpacityAnimation_->stop();
        previewPlaybackRateToastOpacityAnimation_->setStartValue(1.0);
        previewPlaybackRateToastOpacityAnimation_->setEndValue(0.0);
        previewPlaybackRateToastOpacityAnimation_->start();
    });
    previewPlaybackRateToast_->hide();

    timelineAnalysisIdleTimer_ = new QTimer(this);
    timelineAnalysisIdleTimer_->setSingleShot(true);
    connect(timelineAnalysisIdleTimer_, &QTimer::timeout, this, &MainWindow::dispatchTimelineAnalysisRefresh);
    logStartupStage("timers_ready");

    if (previewSlider_ != nullptr) {
        previewSlider_->setFocusPolicy(Qt::StrongFocus);
        previewSlider_->installEventFilter(this);
        connect(previewSlider_, &QSlider::sliderPressed, this, [this]() {
            stopPreviewHeldSeek();
            const quint64 interactionId = ++previewInteractionSequence_;
            const bool wasPlaying = qtPreviewPlaying_;
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
            if (qtPreviewPlaying_) {
                pauseQtPreviewPlaybackExact();
            }
            appendPreviewInteractionLog(
                QStringLiteral("scrub_pause_transition"),
                QString("op=%1 playing_after=%2 pause_second=%3 manual_generation=%4 manual_txn=%5 manual_sequence=%6")
                    .arg(interactionId)
                    .arg(qtPreviewPlaying_ ? 1 : 0)
                    .arg(qtPreviewPauseSecond_, 0, 'f', 6)
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
                    .arg(qtPreviewPauseSecond_, 0, 'f', 6)
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
                    .arg(qtPreviewPauseSecond_, 0, 'f', 6)
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
                    .arg(qtPreviewPauseSecond_, 0, 'f', 6)
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
    if (previewCanvasContainer_ != nullptr) {
        previewCanvasContainer_->setMouseTracking(true);
        previewCanvasContainer_->installEventFilter(this);
    }
    if (previewCanvasFrame_ != nullptr) {
        previewCanvasFrame_->setMouseTracking(true);
        previewCanvasFrame_->installEventFilter(this);
    }
    if (previewPanel_ != nullptr) {
        previewPanel_->setMouseTracking(true);
        previewPanel_->installEventFilter(this);
    }
    // Watch the rehosted workspace content for the async, QML-driven surface resize
    // that lands AFTER a page switch (export page aspect change + bottom-tabs
    // collapse). The filter re-runs the layout finalize so the just-switched page
    // settles + repaints instead of staying composited at its stale arrangement
    // (see WindowSection::eventFilter + armWorkspaceSurfaceSettleRelayout).
    if (workspaceContentWidget_ != nullptr) {
        workspaceContentWidget_->installEventFilter(this);
    }
    editorViewport_ = qobject_cast<PlainCodeEditor*>(editorWidget_)->viewport();
    if (editorViewport_ != nullptr) {
        editorViewport_->installEventFilter(this);
    }
    if (editorFindEdit_ != nullptr) {
        editorFindEdit_->installEventFilter(this);
        connect(editorFindEdit_, &QLineEdit::returnPressed, this, &MainWindow::onFindNext);
    }
    if (editorReplaceEdit_ != nullptr) {
        editorReplaceEdit_->installEventFilter(this);
    }
    if (QApplication::instance() != nullptr) {
        QApplication::instance()->installEventFilter(this);
    }
    if (editorFindPrevButton_ != nullptr) {
        connect(editorFindPrevButton_, &QToolButton::clicked, this, &MainWindow::onFindPrevious);
    }
    if (editorFindNextButton_ != nullptr) {
        connect(editorFindNextButton_, &QToolButton::clicked, this, &MainWindow::onFindNext);
    }
    if (editorFindCloseButton_ != nullptr) {
        connect(editorFindCloseButton_, &QToolButton::clicked, this, [this]() {
            windowSection_->hideFindReplaceBar();
        });
    }
    if (editorReplaceButton_ != nullptr) {
        connect(editorReplaceButton_, &QPushButton::clicked, this, &MainWindow::onReplaceOne);
    }
    if (editorReplaceAllButton_ != nullptr) {
        connect(editorReplaceAllButton_, &QPushButton::clicked, this, &MainWindow::onReplaceAll);
    }
    if (editorFindBar_ != nullptr) {
        auto* toggleFindBarShortcut = new QShortcut(editorFindBar_);
        ShortcutRegistry::instance().applyShortcut(
            toggleFindBarShortcut,
            QStringLiteral("editor.find_bar.toggle"),
            QKeySequence::Find);
        toggleFindBarShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(toggleFindBarShortcut, &QShortcut::activated, this, &MainWindow::onToggleFindReplace);
        auto* closeFindBarShortcut = new QShortcut(editorFindBar_);
        ShortcutRegistry::instance().applyShortcut(
            closeFindBarShortcut,
            QStringLiteral("editor.find_bar.close"),
            QKeySequence(Qt::Key_Escape));
        closeFindBarShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(closeFindBarShortcut, &QShortcut::activated, this, [this]() {
            windowSection_->hideFindReplaceBar();
        });
    }
    windowSection_->updateEditorFindBarGeometry();
    windowSection_->applyFindOverlayInset();
    const auto applyEditorFontDelta = [this](int delta) {
        applyEditorTextFontSize(editorTextFontPointSize_ + delta, true);
        statusBar()->showMessage(UiText::text(QStringLiteral("status.editor_text_display_updated")));
    };
    fontDecreaseAction_ = new QAction(QStringLiteral("Decrease Editor Font"), this);
    ShortcutRegistry::instance().applyShortcut(
        fontDecreaseAction_,
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Alt+-")));
    fontDecreaseAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(fontDecreaseAction_);
    connect(fontDecreaseAction_, &QAction::triggered, this, [applyEditorFontDelta]() {
        applyEditorFontDelta(-1);
    });
    fontIncreaseAction_ = new QAction(QStringLiteral("Increase Editor Font"), this);
    ShortcutRegistry::instance().applyShortcut(
        fontIncreaseAction_,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Alt+=")));
    fontIncreaseAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(fontIncreaseAction_);
    connect(fontIncreaseAction_, &QAction::triggered, this, [applyEditorFontDelta]() {
        applyEditorFontDelta(1);
    });
    statusBar()->showMessage("PlainCodeEditor ready.");

    loadPortableState();
    applyAppBackgroundSettings(appBackgroundSettings_, false);
    applyWorkspacePanelArrangement();
    windowSection_->setOutlineDockCollapsed(outlineDockCollapsed_);
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
    if (previewCanvas_ != nullptr) {
        applyEffectivePreviewOutlineVariantToCanvas();
        applyPreviewSkinDirectoryToSurfaces();
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        previewCanvas_->setTapFlowSpeed(previewTapFlowSpeed_);
        previewCanvas_->setTouchFlowSpeed(previewTouchFlowSpeed_);
        previewCanvas_->setSlideEarlierSecondAndTextOnTop(previewSlideEarlierSecondAndTextOnTop_);
        previewCanvas_->setTapJudgeTextDistance(previewTapJudgeTextDistance_);
        previewCanvas_->setJudgeEffectStyle(previewJudgeEffectStyle_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        previewCanvas_->setShowTimestamp(previewShowTimestamp_);
        previewCanvas_->setShowObjectStatsHud(previewShowObjectStatsHud_);
        // Chart info HUD only ever activates inside the export-preview
        // dialog (gated by setSuppressDebugInfo + setChartInfo there); the
        // editor's main preview keeps it off.
        previewCanvas_->setShowChartInfoHud(false);
        previewCanvas_->setCenterDisplayMode(previewCenterDisplayMode_);
    }
    applyMuriRenderOptions();
    windowSection_->applyUiTheme();
    updatePauseButtonAppearance();
    const bool restoredStartupDocument = restoreLastSessionFile();
    if (!restoredStartupDocument) {
        loadDocument(SimaiDocument::createEmpty());
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
