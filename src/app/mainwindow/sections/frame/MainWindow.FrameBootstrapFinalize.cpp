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
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

void MainWindow::finishFrameBootstrap(QToolBar* toolBar, const std::function<void(const QString&)>& logStartupStage)
{
    constexpr int kToolbarLeadingSpacerWidth = 6;
    auto* toolbarLeadingSpacer = new QWidget(toolBar);
    toolbarLeadingSpacer->setFixedWidth(kToolbarLeadingSpacerWidth);
    toolBar->addWidget(toolbarLeadingSpacer);
    toolBar->addAction(openAction_);
    toolBar->addAction(saveAction_);
    constexpr int kToolbarActionButtonWidth = 64;
    constexpr int kToolbarActionButtonHorizontalPadding = 20;
    const auto compactToolbarButtonWidth = [](const QFont& font, QAction* action) -> int {
        if (action == nullptr) {
            return kToolbarActionButtonWidth;
        }
        if (UiText::isChineseUi()) {
            return kToolbarActionButtonWidth;
        }
        const QFontMetrics metrics(font);
        return qMax(
            kToolbarActionButtonWidth,
            metrics.horizontalAdvance(action->text()) + kToolbarActionButtonHorizontalPadding
        );
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
        if (!qtPreviewPlaying_) {
            return;
        }
        const bool usingFrameSwapPacing =
            previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing();
        if (!usingFrameSwapPacing) {
            const qint64 frameIntervalNs = previewCanvasTargetFrameIntervalNs();
            if (qtPreviewNextFixedTickDueNs_ < 0 || qtPreviewFixedTickOriginNs_ < 0) {
                resetQtPreviewFixedFramePacing();
            }
            const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
            const qint64 latenessNs = qMax<qint64>(0, nowNs - qtPreviewNextFixedTickDueNs_);
            const bool canLogicalCatchup =
                !(previewSfxRuntime_ != nullptr
                    && previewSfxRuntime_->hasBackgroundTrack()
                    && previewSfxRuntime_->isBackgroundTrackRunning());
            int ticksToRun = 1;
            if (canLogicalCatchup && frameIntervalNs > 0 && latenessNs >= frameIntervalNs) {
                ticksToRun = qMin(2, 1 + static_cast<int>(latenessNs / frameIntervalNs));
            }
            int executedTicks = 0;
            for (; executedTicks < ticksToRun && qtPreviewPlaying_; ++executedTicks) {
                const qint64 scheduledDueNs = qtPreviewNextFixedTickDueNs_;
                if (canLogicalCatchup) {
                    timelineSection_->onQtPreviewTickAtSecond(
                        timelineSection_->fixedIntervalPreviewSecondForDeadlineNs(scheduledDueNs)
                    );
                } else {
                    onQtPreviewTick();
                }
                if (!qtPreviewPlaying_) {
                    return;
                }
                if (previewCanvas_ != nullptr && !previewCanvasUsesFrameSwappedPacing()) {
                    previewCanvas_->update();
                }
                qtPreviewNextFixedTickDueNs_ += frameIntervalNs;
            }
            const qint64 postTickNowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
            qint64 skippedIntervals = 0;
            if (frameIntervalNs > 0 && qtPreviewNextFixedTickDueNs_ <= postTickNowNs) {
                skippedIntervals =
                    qMax<qint64>(1, ((postTickNowNs - qtPreviewNextFixedTickDueNs_) / frameIntervalNs) + 1);
                qtPreviewNextFixedTickDueNs_ += skippedIntervals * frameIntervalNs;
            }
            if (previewCanvas_ != nullptr) {
                previewCanvas_->noteFixedTimerDeadlineMetrics(
                    latenessNs,
                    qMax(0, executedTicks - 1),
                    skippedIntervals
                );
            }
            scheduleNextQtPreviewTick();
            return;
        }
        if (!qtPreviewAwaitingFrameSwap_) {
            onQtPreviewTick();
            return;
        }
        const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
        const int stallTimeoutMs = qMax(
            24,
            qMax(1, qRound(static_cast<double>(previewCanvasTargetFrameIntervalNs()) / 1000000.0)) * 2
        );
        if (qtPreviewAwaitingFrameSwapSinceMs_ >= 0 && nowMs - qtPreviewAwaitingFrameSwapSinceMs_ >= stallTimeoutMs) {
            qtPreviewAwaitingFrameSwap_ = false;
            qtPreviewAwaitingFrameSwapSinceMs_ = -1;
            onQtPreviewTick();
            return;
        }
        scheduleNextQtPreviewTick();
    });

    qtPreviewTimelineTimer_ = new QTimer(this);
    qtPreviewTimelineTimer_->setInterval(kTimelineUiCadenceMs);
    qtPreviewTimelineTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimelineTimer_, &QTimer::timeout, this, &MainWindow::flushQtPreviewTimelinePosition);

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
                stopQtPreviewPlayback(true);
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
                if (previewSeekDebounceTimer_ != nullptr) {
                    previewSeekDebounceTimer_->stop();
                }
                seekPreviewToSecond(second, true);
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
    auto* fontDecreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
    fontDecreaseShortcut->setContext(Qt::WindowShortcut);
    connect(fontDecreaseShortcut, &QShortcut::activated, this, [this]() {
        applyEditorTextFontSize(editorTextFontPointSize_ - 1, true);
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    auto* fontIncreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+=")), this);
    fontIncreaseShortcut->setContext(Qt::WindowShortcut);
    connect(fontIncreaseShortcut, &QShortcut::activated, this, [this]() {
        applyEditorTextFontSize(editorTextFontPointSize_ + 1, true);
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    auto* fontIncreaseShiftShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this);
    fontIncreaseShiftShortcut->setContext(Qt::WindowShortcut);
    connect(fontIncreaseShiftShortcut, &QShortcut::activated, this, [this]() {
        applyEditorTextFontSize(editorTextFontPointSize_ + 1, true);
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
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
        previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
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
