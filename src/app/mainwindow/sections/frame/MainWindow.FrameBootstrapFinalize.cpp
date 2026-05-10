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
    constexpr int kToolbarActionButtonWidth = 64;
    // Beta20-fix — reduced from 20 → 12 (6 px on each side) so the
    // explicit-fixed-width buttons (Audio Settings / Video Settings)
    // hug their text the same way Qt's native `addAction()`-managed
    // toolbar buttons (Open / Save / Export) do. The previous 20 px
    // produced ~10 px of empty space on each side of the label text,
    // which read as gratuitous extra margin compared to the native
    // buttons next to them.
    constexpr int kToolbarActionButtonHorizontalPadding = 12;
    const auto compactToolbarButtonWidth = [](const QFont& font, QAction* action) -> int {
        if (action == nullptr) {
            return kToolbarActionButtonWidth;
        }
        const QFontMetrics metrics(font);
        const int textBoundedWidth =
            metrics.horizontalAdvance(action->text())
            + kToolbarActionButtonHorizontalPadding;
        // Chinese UI — short labels (设置, 导出) are far narrower than
        // the 64 px floor, so always clamp to the floor in Chinese to
        // keep the toolbar visually balanced. English UI — let labels
        // grow naturally beyond the floor when needed.
        if (UiText::isChineseUi()) {
            return qMax(kToolbarActionButtonWidth, textBoundedWidth);
        }
        return qMax(kToolbarActionButtonWidth, textBoundedWidth);
    };
    const auto makeCompactToolbarButton = [toolBar, compactToolbarButtonWidth](QAction* action) -> QToolButton* {
        auto* button = new QToolButton(toolBar);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        const int buttonWidth = compactToolbarButtonWidth(button->font(), action);
        button->setFixedWidth(buttonWidth);
        button->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
        return button;
    };

    previewAudioSettingsButton_ = makeCompactToolbarButton(previewAudioSettingsAction_);
    previewVideoSettingsButton_ = makeCompactToolbarButton(previewVideoSettingsAction_);
    int settingsButtonWidth = kToolbarActionButtonWidth;
    if (previewAudioSettingsButton_ != nullptr) {
        settingsButtonWidth = qMax(settingsButtonWidth, previewAudioSettingsButton_->width());
    }
    if (previewVideoSettingsButton_ != nullptr) {
        settingsButtonWidth = qMax(settingsButtonWidth, previewVideoSettingsButton_->width());
    }
    if (previewAudioSettingsButton_ != nullptr) {
        previewAudioSettingsButton_->setFixedWidth(settingsButtonWidth);
        toolBar->addWidget(previewAudioSettingsButton_);
    }
    if (previewVideoSettingsButton_ != nullptr) {
        previewVideoSettingsButton_->setFixedWidth(settingsButtonWidth);
        toolBar->addWidget(previewVideoSettingsButton_);
    }
    settingsPlaceholderAction_ = toolBar->addAction(
        makeSettingsGearIcon(QColor("#5D6E83")),
        uiText("action.preferences", "Preferences...")
    );
    settingsPlaceholderAction_->setToolTip(uiText("action.preferences", "Preferences..."));
    connect(settingsPlaceholderAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    exportVideoButton_ = makeCompactToolbarButton(exportVideoAction_);
    if (exportVideoButton_ != nullptr) {
        const auto syncExportToolbarButton = [this]() {
            if (exportVideoButton_ == nullptr) {
                return;
            }
            exportVideoButton_->setText(uiText("toolbar.export", "Export"));
            exportVideoButton_->setToolTip(exportVideoAction_ != nullptr ? exportVideoAction_->text() : QString());
        };
        syncExportToolbarButton();
        int openButtonWidth = kToolbarActionButtonWidth;
        if (QWidget* openWidget = toolBar->widgetForAction(openAction_); openWidget != nullptr) {
            openButtonWidth = qMax(1, openWidget->sizeHint().width());
        }
        exportVideoButton_->setFixedWidth(openButtonWidth);
        toolBar->insertWidget(settingsPlaceholderAction_, exportVideoButton_);
        exportVideoMenu_ = new QMenu(exportVideoButton_);
        styleRoundedMenu(*exportVideoMenu_);
        if (exportVideoAction_ != nullptr) {
            exportVideoMenu_->addAction(exportVideoAction_);
        }
        QAction* toolbarBatchExportAction = exportVideoMenu_->addAction(
            uiText("action.batch_export", "Batch Export")
        );
        connect(toolbarBatchExportAction, &QAction::triggered, this, &MainWindow::onBatchExportPreviewVideo);
        exportVideoButton_->installEventFilter(this);
        exportVideoButton_->setMouseTracking(true);
        if (exportVideoAction_ != nullptr) {
            connect(exportVideoAction_, &QAction::changed, this, syncExportToolbarButton);
        }
    }

    exportVideoHoverMenuTimer_ = new QTimer(this);
    exportVideoHoverMenuTimer_->setSingleShot(true);
    exportVideoHoverMenuTimer_->setInterval(250);
    connect(exportVideoHoverMenuTimer_, &QTimer::timeout, this, [this]() {
        exportSection_->showExportToolbarMenu();
    });
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addPermanentWidget(new QLabel("Current File:", this));
    currentFileLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(currentFileLabel_, 1);
    updateCurrentFileLabel();
    dialogsSection_->updateLatencyDetectorAvailability();

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

    qtPreviewTimelineTimer_ = new QChronoTimer(this);
    qtPreviewTimelineTimer_->setInterval(std::chrono::nanoseconds(
        qMax<qint64>(
            1,
            timelineSection_ != nullptr
                ? timelineSection_->timelineTargetFrameIntervalNs()
                : previewCanvasTargetFrameIntervalNs())));
    qtPreviewTimelineTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimelineTimer_, &QChronoTimer::timeout, this, &MainWindow::flushQtPreviewTimelinePosition);

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
            if (previewSlider_ != nullptr) {
                previewSlider_->setFocus(Qt::MouseFocusReason);
            }
            if (previewFullscreenActive_) {
                showPreviewFullscreenControls(false);
            }
            if (qtPreviewPlaying_) {
                pauseQtPreviewPlaybackExact();
            }
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
            const bool shouldRenderNow = !previewScrubRenderElapsed_.isValid()
                || previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
            if (shouldRenderNow) {
                requestPausedPreviewSeek(second, true, false, false);
                previewScrubRenderElapsed_.restart();
            } else {
                schedulePreviewSeek(second, true);
            }
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
            seekPreviewToSecond(static_cast<double>(previewSlider_->value()) / 1000.0, true);
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
        auto* toggleFindBarShortcut = new QShortcut(QKeySequence::Find, editorFindBar_);
        toggleFindBarShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(toggleFindBarShortcut, &QShortcut::activated, this, &MainWindow::onToggleFindReplace);
        auto* closeFindBarShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), editorFindBar_);
        closeFindBarShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(closeFindBarShortcut, &QShortcut::activated, this, [this]() {
            windowSection_->hideFindReplaceBar();
        });
    }
    windowSection_->updateEditorFindBarGeometry();
    windowSection_->applyFindOverlayInset();
    const auto applyEditorFontDelta = [this](int delta) {
        applyEditorTextFontSize(editorTextFontPointSize_ + delta, true);
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    };
    fontDecreaseAction_ = new QAction(QStringLiteral("Decrease Editor Font"), this);
    fontDecreaseAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+-")));
    fontDecreaseAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(fontDecreaseAction_);
    connect(fontDecreaseAction_, &QAction::triggered, this, [applyEditorFontDelta]() {
        applyEditorFontDelta(-1);
    });
    fontIncreaseAction_ = new QAction(QStringLiteral("Increase Editor Font"), this);
    fontIncreaseAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+=")));
    fontIncreaseAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(fontIncreaseAction_);
    connect(fontIncreaseAction_, &QAction::triggered, this, [applyEditorFontDelta]() {
        applyEditorFontDelta(1);
    });
    fontIncreaseShiftAction_ = new QAction(QStringLiteral("Increase Editor Font Shift"), this);
    fontIncreaseShiftAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl++")));
    fontIncreaseShiftAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(fontIncreaseShiftAction_);
    connect(fontIncreaseShiftAction_, &QAction::triggered, this, [applyEditorFontDelta]() {
        applyEditorFontDelta(1);
    });

    statusBar()->showMessage("PlainCodeEditor ready.");

    loadPortableState();
    applyWorkspacePanelArrangement();
    logStartupStage("portable_state_loaded");
    if (runtimeDebugOutputEnabled_) {
        previewShowDebugInfo_ = true;
    }
    if (previewUsesStageMediaHostRoute()) {
        ensurePreviewStageMediaRouteInitialized();
    }
    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    applyPreviewStageMediaRouteVisualSettings();
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
        logStartupStage("preview_sfx_set_chart_path_done");
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        logStartupStage("preview_sfx_set_playback_rate_done");
    }
    if (previewCanvas_ != nullptr) {
        applyEffectivePreviewOutlineVariantToCanvas();
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
    previewCanvas_->setTapFlowSpeed(previewTapFlowSpeed_);
    previewCanvas_->setTouchFlowSpeed(previewTouchFlowSpeed_);
        previewCanvas_->setSlideEarlierSecondAndTextOnTop(previewSlideEarlierSecondAndTextOnTop_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        previewCanvas_->setShowTimestamp(previewShowTimestamp_);
        previewCanvas_->setShowObjectStatsHud(previewShowObjectStatsHud_);
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
