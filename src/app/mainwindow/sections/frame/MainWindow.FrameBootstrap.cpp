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
#include "audio/PreviewAudioDeviceWatcher.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
#include "BusySpinner.h"
#include "UiText.h"
#include "UiTheme.h"
#include "WindowParityMetrics.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/AdoptedSurfaceDragAutoScroll.h"
#include "common/AdoptedWidgetCoordinates.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "app/qml_ui/export/QmlExportSession.h"
#include "app/v2/JobProgressService.h"
#include "app/v2/UiRequestService.h"
#include "tools/latency/LatencySandboxController.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <algorithm>
#include <functional>

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

}  // namespace

MainWindow::MainWindow(miacode::v2::ApplicationServices& services, QWidget* parent)
    : QMainWindow(parent)
    , applicationServices_(services)
{
    // Borrowed, not created: these services outlive the window and are owned by
    // the non-Widget assembly (stage 3.5 item 1). The window only connects to
    // them.
    editorSyncController_ = &applicationServices_.editorSync();
    chartDropImportService_ = &applicationServices_.chartDropImport();
    connect(editorSyncController_, &miacode::v2::EditorSyncController::editorContextChanged,
            this, &MainWindow::refreshEditorAuthoringContext);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::caretLocationPublished,
            this, [this](int difficultyId, qulonglong, int line, int column) {
                if (difficultyId == activeDifficultyId_) {
                    publishEditorCaret(difficultyId, line, column);
                }
            });
    connect(editorSyncController_, &miacode::v2::EditorSyncController::pointerInteractionStarted,
            this, &MainWindow::handleEditorPointerInteraction);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::touchPadControlHoldChanged,
            this, &MainWindow::setTouchPadAuthoringCtrlHold);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::touchPadPreviewAnchorPublished,
            this, &MainWindow::applyTouchPadAuthoringPreviewAnchor);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::previewSeekPublished,
            this, &MainWindow::seekPreviewToEditorLocation);

    // The preview appearance values live in the application assembly; this
    // window owns the live surfaces and the settings file, so it is what reacts
    // when one of them moves. Restore paths write through
    // PreviewAppearanceState::values() instead, which stays silent — reloading
    // a document must not look like a user edit and must not rewrite settings.
    miacode::v2::PreviewAppearanceState& previewAppearance =
        applicationServices_.previewAppearance();
    connect(&previewAppearance, &miacode::v2::PreviewAppearanceState::skinChanged,
            this, [this] {
                applyPreviewSkinDirectoryToSurfaces();
                savePortableState();
            });
    connect(&previewAppearance, &miacode::v2::PreviewAppearanceState::judgeEffectStyleChanged,
            this, [this, &previewAppearance] {
                if (previewCanvas_ != nullptr) {
                    previewCanvas_->setJudgeEffectStyle(previewAppearance.judgeEffectStyle());
                }
                savePortableState();
            });
    connect(&previewAppearance, &miacode::v2::PreviewAppearanceState::introSoundChanged,
            this, [this] {
                applyPreviewSfxLevels(/*reloadAssets=*/true);
                savePortableState();
            });

    QElapsedTimer startupStageTimer;
    startupStageTimer.start();
    qint64 startupLastMs = 0;
    const auto logStartupStage = [&](const QString& stageName) {
        const qint64 nowMs = startupStageTimer.elapsed();
        const qint64 deltaMs = nowMs - startupLastMs;
        startupLastMs = nowMs;
        appendStartupTimingStage(QString("mainwindow/%1").arg(stageName), nowMs, deltaMs);
    };

    configureRuntimeDebugOutput();
    logStartupStage("configure_runtime_debug_output");
    quickShellStartupStageMediaLoadDeferred_ = true;
    setProperty("miacode.dialog_parentless", true);
    logStartupStage("dialog_parentless_property_ready");
    setAttribute(Qt::WA_DontShowOnScreen);
    logStartupStage("dont_show_on_screen_ready");
    setAttribute(Qt::WA_NativeWindow);
    logStartupStage("native_window_attribute_ready");
    winId();
    logStartupStage("native_window_ready");

    editorSection_ = std::make_unique<EditorSection>(*this, ui_, state_);
    documentSection_ = std::make_unique<DocumentSection>(*this, ui_, state_);
    dialogsSection_ = std::make_unique<DialogsSection>(*this, ui_, state_);
    exportSection_ = std::make_unique<ExportSection>(*this, ui_, state_);
    // The export page reaches the engine through the assembly's slot, never
    // through this window's member. Cleared at the top of ~MainWindow so the
    // page cannot call into a half-destroyed section.
    applicationServices_.setExportEngine(exportSection_.get());
    // Page routing takes the same door: the QML page host asks the router, not
    // this window.
    applicationServices_.setEditorPageRouter(this);
    applicationServices_.setMediaToolsEngine(this);
    applicationServices_.setLatencyEngine(this);
    applicationServices_.setTimelineSurface(this);
    applicationServices_.setPreviewSurface(this);
    applicationServices_.setPreferencesStore(this);
    applicationServices_.setDocumentBridge(this);

    // Relay the window's push notifications into the assembly so the QML models
    // can subscribe without holding a MainWindow&. Nothing waits on a result
    // here, so a relay changes nothing about the contract — unlike the handler
    // hooks on DocumentBridge, which need an answer back.
    {
        miacode::v2::ShellNotifications& notify = applicationServices_.shellNotifications();
        connect(this, &MainWindow::shellPresentationChanged,
                &notify, &miacode::v2::ShellNotifications::presentationChanged);
        connect(this, &MainWindow::shellPreviewPlayheadChanged,
                &notify, &miacode::v2::ShellNotifications::previewPlayheadChanged);
        connect(this, &MainWindow::previewSkinDirectoryChanged,
                &notify, &miacode::v2::ShellNotifications::previewSkinDirectoryChanged);
        connect(this, &MainWindow::documentReplaced,
                &notify, &miacode::v2::ShellNotifications::documentReplaced);
        connect(this, &MainWindow::editorPreferencesChanged,
                &notify, &miacode::v2::ShellNotifications::editorPreferencesChanged);
        connect(this, &MainWindow::muriPromptPreferenceChanged,
                &notify, &miacode::v2::ShellNotifications::muriPromptPreferenceChanged);
        connect(this, &MainWindow::videoExportWorkerRunningChanged,
                &notify, &miacode::v2::ShellNotifications::videoExportWorkerRunningChanged);
        connect(this, &MainWindow::normalizeWholeChartRequested,
                &notify, &miacode::v2::ShellNotifications::normalizeWholeChartRequested);
        connect(this, &MainWindow::mediaToolsRequested,
                &notify, &miacode::v2::ShellNotifications::mediaToolsRequested);
        connect(this, &MainWindow::preferencesRequested,
                &notify, &miacode::v2::ShellNotifications::preferencesRequested);
        connect(this, &MainWindow::coverExportRequested,
                &notify, &miacode::v2::ShellNotifications::coverExportRequested);
    }
    preferencesSection_ = std::make_unique<PreferencesSection>(*this, ui_, state_);
    previewSection_ = std::make_unique<PreviewSection>(*this, ui_, state_);
    validationSection_ = std::make_unique<ValidationSection>(*this, ui_, state_);
    windowSection_ = std::make_unique<WindowSection>(*this, ui_, state_);
    frameSection_ = std::make_unique<FrameSection>(*this, ui_, state_);
    timelineSection_ = std::make_unique<TimelineSection>(*this, ui_, state_);
    connect(&applicationServices_.workspace(), &miacode::v2::ChartWorkspace::changed,
            this, [this](quint64) {
                documentSection_->syncRuntimeFromWorkspace();
            });
    // unique_ptr owns it; pass no QObject parent to avoid double-delete.
    latencySandboxController_ = std::make_unique<miacode::latency::LatencySandboxController>(this, nullptr);
    logStartupStage("sections_ready");

    previewWarmupPool_ = new QThreadPool(this);
    previewWarmupPool_->setObjectName(QStringLiteral("PreviewWarmupPool"));
    previewWarmupPool_->setMaxThreadCount(2);
    previewWarmupPool_->setExpiryTimeout(-1);
    logStartupStage("preview_warmup_pool_ready");

    timelineSlowRefreshPool_ = new QThreadPool(this);
    timelineSlowRefreshPool_->setObjectName(QStringLiteral("TimelineSlowRefreshPool"));
    timelineSlowRefreshPool_->setMaxThreadCount(1);
    timelineSlowRefreshPool_->setExpiryTimeout(-1);

    timelineAnalysisPool_ = new QThreadPool(this);
    timelineAnalysisPool_->setObjectName(QStringLiteral("TimelineAnalysisPool"));
    timelineAnalysisPool_->setMaxThreadCount(1);
    timelineAnalysisPool_->setExpiryTimeout(-1);
    logStartupStage("timeline_analysis_pools_ready");

    setWindowModified(false);
    updateWindowTitle();
    windowSection_->setupInitialWindowGeometry();
    if (QGuiApplication* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance()); guiApp != nullptr) {
        if (QStyleHints* styleHints = guiApp->styleHints(); styleHints != nullptr) {
            connect(styleHints, &QStyleHints::colorSchemeChanged, this, [this]() {
                windowSection_->applyUiTheme();
            });
        }
    }
    auto* central = new QWidget(this);
    central->setObjectName("EditorShell");
    workspaceContentWidget_ = central;
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    editorStack_ = new QStackedWidget(central);
    editorStack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    welcomePage_ = new QWidget(editorStack_);
    metadataPage_ = new QWidget(editorStack_);
    chartPage_ = new QWidget(editorStack_);
    ui_.latencyPlaceholderPage_ = new QWidget(editorStack_);
    ui_.exportPlaceholderPage_ = new QWidget(editorStack_);
    editorStack_->addWidget(welcomePage_);
    editorStack_->addWidget(metadataPage_);
    editorStack_->addWidget(ui_.latencyPlaceholderPage_);
    editorStack_->addWidget(ui_.exportPlaceholderPage_);
    ui_.uiRequests_ = &applicationServices_.uiRequests();
    ui_.jobProgress_ = &applicationServices_.jobProgress();
    connect(ui_.jobProgress_, &miacode::v2::JobProgressService::cancellationRequested,
            this, [this](quint64 token) {
                if (exportSection_ != nullptr && token == videoExportJobToken_
                    && videoExportJobToken_ != 0) {
                    exportSection_->cancelVideoExportWorker();
                }
            });
    ui_.qmlExportSession_ = new QmlExportSession(
        applicationServices_.shellNotifications(), applicationServices_.uiRequests(),
        applicationServices_.jobProgress(), applicationServices_.previewAppearance(),
        applicationServices_.exportEngineSlot(), applicationServices_.previewSurfaceSlot(),
        this);
    applicationServices_.setExportPageSession(ui_.qmlExportSession_);
    editorStack_->addWidget(chartPage_);
    centralLayout->addWidget(editorStack_, 1);
    logStartupStage("runtime_pages_ready");

    previewPanel_ = new QWidget(this);
    previewPanel_->setObjectName("PreviewPanel");
    previewPanel_->setStyleSheet(UiTheme::previewPanelStyleSheet());
    previewPanel_->setMinimumWidth(kEmbeddedPreviewPanelMinWidth);
    previewPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    previewCanvas_ = new PreviewRuntime(this);
    connect(previewCanvas_, &PreviewRuntime::touchPadAuthoringClicked, this, [this](const QString& pad, bool backtickSeparator) {
        if (editorSyncController_ != nullptr) {
            editorSyncController_->requestTouchPadAuthoring(pad, backtickSeparator);
        }
    });
    if (editorStack_ != nullptr) {
        connect(editorStack_, &QStackedWidget::currentChanged, this, [this](int) {
            applyEffectivePreviewOutlineVariantToCanvas();
        });
    }
    logStartupStage("preview_canvas_created");
    applyEffectivePreviewOutlineVariantToCanvas();
    applyPreviewSkinDirectoryToSurfaces();
    updatePreviewStageMediaPresentationMode(false);
    if (previewUsesStageMediaHostRoute()) {
        ensurePreviewStageMediaRouteInitialized();
    }
    logStartupStage("preview_skin_async_dispatched");
    previewCanvasFrame_ = new QFrame(previewPanel_);
    previewCanvasFrame_->setObjectName("PreviewCanvasFrame");
    previewCanvasFrame_->setMinimumSize(QSize(1, 1));
    previewCanvasFrame_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_ = new QWidget(previewCanvasFrame_);
    previewCanvasContainer_->setMinimumSize(QSize(1, 1));
    previewCanvasContainer_->setFocusPolicy(Qt::StrongFocus);
    previewPanel_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_->hide();
    logStartupStage("preview_canvas_container_ready");

    previewControlCard_ = nullptr;
    previewControlsLayout_ = nullptr;
    stopPreviewButton_ = nullptr;
    pausePreviewButton_ = nullptr;
    previewSlider_ = nullptr;
    previewSpeedButton_ = nullptr;
    previewFullscreenButton_ = nullptr;

    auto* previewStatsCard = new QFrame(previewPanel_);
    previewStatsCard_ = previewStatsCard;
    previewStatsCard->setObjectName("PreviewStatsCard");
    previewStatsCard->setMinimumWidth(kPreviewControlStatsCardMinWidth);
    previewStatsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* previewStatsCardLayout = new QVBoxLayout(previewStatsCard);
    previewStatsCardLayout->setContentsMargins(8, 8, 8, 8);
    previewStatsCardLayout->setSpacing(0);

    auto* previewStats = new QFrame(previewStatsCard);
    previewStats->setObjectName("PreviewStats");
    auto* previewStatsLayout = new QGridLayout(previewStats);
    previewStatsGridLayout_ = previewStatsLayout;
    previewStatsLayout->setContentsMargins(2, 2, 2, 2);
    previewStatsLayout->setHorizontalSpacing(10);
    previewStatsLayout->setVerticalSpacing(6);

    const auto addStatsChip = [previewStats, previewStatsLayout](const QString& labelText) -> QLabel* {
        auto* label = new QLabel(labelText, previewStats);
        label->setObjectName("PreviewStatChip");
        label->setFont(uiMonoFont(10, QFont::DemiBold));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setFixedHeight(30);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        previewStatsLayout->addWidget(label);
        return label;
    };

    previewTapStatsLabel_ = addStatsChip("Tap    0/0");
    previewHoldStatsLabel_ = addStatsChip("Hold   0/0");
    previewSlideStatsLabel_ = addStatsChip("Slide  0/0");
    previewTouchStatsLabel_ = addStatsChip("Touch  0/0");
    previewBreakStatsLabel_ = addStatsChip("Break  0/0");
    previewTotalStatsLabel_ = addStatsChip("Total  0/0");
    previewTotalStatsLabel_->setObjectName("PreviewStatChipTotal");
    previewStatsChips_.clear();
    previewStatsChips_ << previewTapStatsLabel_
                       << previewHoldStatsLabel_
                       << previewSlideStatsLabel_
                       << previewTouchStatsLabel_
                       << previewBreakStatsLabel_
                       << previewTotalStatsLabel_;
    previewStatsCardLayout->addWidget(previewStats, 0);
    previewStatsCardLayout->addStretch(1);
    updatePreviewStatsLayoutMode();
    logStartupStage("preview_controls_and_stats_ready");

    previewSfxRuntime_ = new QtPreviewSfxRuntime(this);
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::commandCompleted,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                using namespace miacode::preview_audio;
                if (completion.kind != CommandKind::ReloadAssets
                    || completion.identity.sequence != state_.previewSfxRuntimePreparationSequence_
                    || !acceptsAssetCompletion(
                        state_.previewSfxRuntimePreparationAssetGeneration_, completion)) {
                    return;
                }
                state_.previewSfxRuntimePrepared_ = completion.success
                    && previewSfxRuntime_ != nullptr
                    && previewSfxRuntime_->audioEngineInitialized();
                state_.previewSfxRuntimePreparationAssetGeneration_ = 0;
                state_.previewSfxRuntimePreparationSequence_ = 0;
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::previewPrepared,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->handlePreviewAudioPrepared(completion);
                }
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::retainedPlaybackCompleted,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->handlePreviewRetainedPlaybackCompleted(completion);
                }
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::previewPlaybackPaused,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->handlePreviewRetainedPlaybackCompleted(completion);
                }
            });
    logStartupStage("preview_sfx_runtime_created");
#ifdef MIACODE_HAS_BASS_AUDIO
    // BASS-only on purpose. docs/audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md fixes the
    // device-change desync (问题 3) to the BASS transport's anchor model on Windows and
    // macOS. Linux runs MiniaudioPreviewAudioBackend, a different seek/clock
    // implementation with a separate, unproven report (问题 4), so auto-pausing there
    // would interrupt playback on no evidence.
    previewAudioDeviceWatcher_ = new PreviewAudioDeviceWatcher(this);
    previewAudioDeviceWatcher_->setDirectCutoffHandler(
        [runtime = previewSfxRuntime_](PreviewAudioDeviceWatcher::Change) {
            return runtime != nullptr
                ? runtime->requestDeviceChangeCutoff()
                : miacode::preview_audio::PreviewAudioDeviceCutoff{};
        });
    connect(previewAudioDeviceWatcher_,
            &PreviewAudioDeviceWatcher::deviceCutoffRequested,
            this,
            [this](const PreviewAudioDeviceWatcher::DeviceCutoff& cutoff) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->applyPreviewAudioDeviceCutoff(cutoff);
                }
            });
    logStartupStage("preview_audio_device_watcher_created");
#endif
    connect(previewCanvas_, &PreviewRuntime::framePresented, this, [this]() {
        timelineSection_->handlePreviewStartupCanvasPresented();
        if (!qtPreviewPlaying_) {
            return;
        }
        if (previewCanvasUsesFrameSwappedPacing()) {
            const bool matchedRequest = qtPreviewAwaitingFrameSwap_;
            const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
            const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
            const qint64 waitNs =
                matchedRequest && qtPreviewAwaitingFrameSwapSinceNs_ >= 0
                    ? qMax<qint64>(0, nowNs - qtPreviewAwaitingFrameSwapSinceNs_)
                    : 0;
            qtPreviewDisplayRefreshFramePresentSeq_ += 1;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->noteDisplayRefreshFramePresentation(waitNs, matchedRequest);
            }
            if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
                const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
                if (!matchedRequest
                    || qtPreviewFramePacingDiagLastPresentLogMs_ < 0
                    || nowMs - qtPreviewFramePacingDiagLastPresentLogMs_ >= sampleMs) {
                    qtPreviewFramePacingDiagLastPresentLogMs_ = nowMs;
                    appendPreviewFramePacingDiagLog(
                        matchedRequest ? QStringLiteral("display_present") : QStringLiteral("display_orphan_present"),
                        QStringLiteral("request_seq=%1 present_seq=%2 wait_ms=%3 queued=%4")
                            .arg(qtPreviewDisplayRefreshFrameRequestSeq_)
                            .arg(qtPreviewDisplayRefreshFramePresentSeq_)
                            .arg(static_cast<double>(waitNs) / 1000000.0, 0, 'f', 3)
                            .arg(qtPreviewDisplayRefreshTickQueued_ ? 1 : 0)
                    );
                }
            }
            if (!matchedRequest) {
                return;
            }
            qtPreviewAwaitingFrameSwap_ = false;
            qtPreviewAwaitingFrameSwapSinceMs_ = -1;
            qtPreviewAwaitingFrameSwapSinceNs_ = -1;
            qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
            if (qtPreviewDisplayRefreshTickQueued_) {
                return;
            }
            qtPreviewDisplayRefreshTickQueued_ = true;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->noteDisplayRefreshQueuedTick();
            }
            // Call onQtPreviewTick synchronously inside the framePresented callback. Every extra
            // event-loop hop between a present and the next update() is ~1-3ms of latency, and
            // on systems where the render pipeline takes ~14-15ms per frame that latency pushes
            // completion past the next vsync boundary, doubling the effective cycle time.
            // The doc's advice against synchronous tick here was cautionary, not load-bearing —
            // the tick body completes in ~0.5ms and cannot re-enter this callback (update() only
            // schedules a render; the next framePresented fires on the next vsync).
            qtPreviewDisplayRefreshTickQueued_ = false;
            if (qtPreviewPlaying_
                && previewCanvasUsesFrameSwappedPacing()
                && !qtPreviewAwaitingFrameSwap_) {
                onQtPreviewTick();
            }
            return;
        }
        // Fixed interval mode: present-driven gate (doc section 4.1). Each present clears the
        // awaiting flag and runs the FPS gate synchronously — same reasoning as DisplayRefresh
        // branch above (avoid event-loop latency that costs vsync alignment).
        const bool matchedRequest = qtPreviewFixedAwaitingFrame_;
        const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
        const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
        const qint64 waitNs =
            matchedRequest && qtPreviewFixedAwaitingFrameSinceNs_ >= 0
                ? qMax<qint64>(0, nowNs - qtPreviewFixedAwaitingFrameSinceNs_)
                : 0;
        qtPreviewFixedGateFramePresentSeq_ += 1;
        if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
            const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
            if (!matchedRequest
                || qtPreviewFramePacingDiagLastPresentLogMs_ < 0
                || nowMs - qtPreviewFramePacingDiagLastPresentLogMs_ >= sampleMs) {
                qtPreviewFramePacingDiagLastPresentLogMs_ = nowMs;
                appendPreviewFramePacingDiagLog(
                    matchedRequest
                        ? QStringLiteral("fixed_gate_present")
                        : QStringLiteral("fixed_gate_orphan_present"),
                    QStringLiteral("request_seq=%1 present_seq=%2 wait_ms=%3 queued=%4")
                        .arg(qtPreviewFixedGateFrameRequestSeq_)
                        .arg(qtPreviewFixedGateFramePresentSeq_)
                        .arg(static_cast<double>(waitNs) / 1000000.0, 0, 'f', 3)
                        .arg(qtPreviewFixedFrameTickQueued_ ? 1 : 0)
                );
            }
        }
        if (!matchedRequest) {
            return;
        }
        qtPreviewFixedAwaitingFrame_ = false;
        qtPreviewFixedAwaitingFrameSinceMs_ = -1;
        qtPreviewFixedAwaitingFrameSinceNs_ = -1;
        if (qtPreviewFixedFrameTickQueued_) {
            return;
        }
        qtPreviewFixedFrameTickQueued_ = true;
        // Synchronous call — skip the event-loop hop. See the DisplayRefresh branch above for the
        // rationale. advanceFixedIntervalGateAfterPresent internally re-checks playing / mode /
        // awaiting-frame state, so running it straight from this lambda is safe even if another
        // queued event (e.g. pause) is sitting behind the current signal.
        qtPreviewFixedFrameTickQueued_ = false;
        advanceFixedIntervalGateAfterPresent();
    });
    logStartupStage("preview_runtime_connections_ready");
    logStartupStage("preview_runtime_ready");

    bottomTabs_ = new QTabWidget(central);
    bottomTabs_->installEventFilter(this);
    if (QTabBar* bottomTabBar = bottomTabs_->tabBar(); bottomTabBar != nullptr) {
        bottomTabBar->installEventFilter(this);
    }
    quickShellBottomTabsProxy_ = new QTabWidget(this);
    if (QTabBar* proxyTabBar = quickShellBottomTabsProxy_->tabBar(); proxyTabBar != nullptr) {
        proxyTabBar->installEventFilter(this);
    }
    timelineQuickStateBridge_ = new TimelineQuickStateBridge(this);
    timelineQuickStateBridge_->setHeaderLineNumberFont(timelineHeaderLineNumberFont());
    timelineQuickStateBridge_->setShowSlideTracks(true);
    timelineQuickStateBridge_->setSkinDirectory(resolvePreviewSkinDir());
    timelineQuickStateBridge_->setViewportLockEnabled(previewViewportLockEnabled_);
    timelineQuickStateBridge_->setFollowProgressEnabled(previewProgressFollowEnabled_);
    timelineQuickStateBridge_->setTimelineSyncEnabled(timelineSyncEnabled_);
    timelineQuickStateBridge_->setZoomWheelShortcuts(
        ShortcutRegistry::instance().shortcutTexts(
            QStringLiteral("timeline.zoom_in"),
            {QStringLiteral("Ctrl+WheelUp")}),
        ShortcutRegistry::instance().shortcutTexts(
            QStringLiteral("timeline.zoom_out"),
            {QStringLiteral("Ctrl+WheelDown")}));
    timelineSection_->refreshTimelineWaveformPhaseCompensation();
    // Phase-locked playback sampling for the timeline. Fires once per timeline frame from
    // TimelineQuickItem::bindRenderCadence (QQuickWindow::afterAnimating, GUI thread), so the
    // second we sample is the one that frame renders. qtPreviewTimelineTimer_ remains armed as
    // a watchdog behind this; see MainWindow.FrameBootstrapFinalize.cpp.
    connect(timelineQuickStateBridge_,
            &TimelineQuickStateBridge::renderCadenceTick,
            this,
            &MainWindow::onTimelineRenderCadenceTick);
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::zoomScaleChanged, this, [this](double) {
        savePortableState();
    });
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::waveformBrightnessChanged, this, [this](double) {
        savePortableState();
    });
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::measureLineBrightnessChanged, this, [this](double) {
        savePortableState();
    });
    outputView_ = nullptr;

    errorList_ = new QListWidget(bottomTabs_);
    errorList_->setFont(uiOutputFont());
    errorList_->setUniformItemSizes(false);
    errorList_->setWordWrap(true);
    errorList_->setTextElideMode(Qt::ElideNone);
    errorList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (QScrollBar* vbar = errorList_->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    errorList_->setContextMenuPolicy(Qt::CustomContextMenu);
    errorList_->viewport()->installEventFilter(this);
    connect(errorList_, &QListWidget::itemActivated, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::itemClicked, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showIssueListContextMenu(errorList_, pos, false);
    });
    bottomTabs_->addTab(
        errorList_,
        UiText::text(QStringLiteral("window.syntax"))
    );

    muriList_ = new QListWidget(bottomTabs_);
    muriList_->setFont(uiOutputFont());
    muriList_->setUniformItemSizes(false);
    muriList_->setWordWrap(true);
    muriList_->setTextElideMode(Qt::ElideNone);
    muriList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (QScrollBar* vbar = muriList_->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    muriList_->setContextMenuPolicy(Qt::CustomContextMenu);
    muriList_->viewport()->installEventFilter(this);
    connect(muriList_, &QListWidget::itemActivated, this, &MainWindow::onMuriItemActivated);
    connect(muriList_, &QListWidget::itemClicked, this, &MainWindow::onMuriItemActivated);
    connect(muriList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showIssueListContextMenu(muriList_, pos, true);
    });
    bottomTabs_->addTab(
        muriList_,
        UiText::text(QStringLiteral("window.muri"))
    );
    connect(bottomTabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (!quickShellBottomTabsProxyActive()) {
            if (bottomTabs_->currentWidget() == errorList_) {
                currentBottomTabsTabId_ = BottomTabsTabId::Validation;
            } else if (bottomTabs_->currentWidget() == muriList_) {
                currentBottomTabsTabId_ = BottomTabsTabId::Muri;
            }
        }
        if (validationSection_ != nullptr && bottomTabs_->currentWidget() == muriList_) {
            validationSection_->flushPendingMuriDiagnosticsPanelRefresh();
        }
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    });
    connect(quickShellBottomTabsProxy_, &QTabWidget::currentChanged, this, [this](int) {
        if (quickShellBottomTabsProxy_->currentWidget() == errorList_) {
            currentBottomTabsTabId_ = BottomTabsTabId::Validation;
        } else if (quickShellBottomTabsProxy_->currentWidget() == muriList_) {
            currentBottomTabsTabId_ = BottomTabsTabId::Muri;
        }
        if (validationSection_ != nullptr && quickShellBottomTabsProxy_->currentWidget() == muriList_) {
            validationSection_->flushPendingMuriDiagnosticsPanelRefresh();
        }
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    });

    windowSection_->updateBottomTabsDeviceHeight();
    logStartupStage("timeline_and_tabs_ready");

    previewLeftColumn_ = new QWidget(this);
    // Content-column floor = export-page design-width budget (spec). Mirrors the
    // QuickShell content WindowContainer's Layout.minimumWidth.
    previewLeftColumn_->setMinimumWidth(miacode::window_parity::kWorkspaceContentMinWidth);
    previewLeftColumn_->setProperty("baseMinimumWidth", previewLeftColumn_->minimumWidth());
    previewLeftColumn_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* leftColumnLayout = new QVBoxLayout(previewLeftColumn_);
    leftColumnLayout->setContentsMargins(0, 0, 0, 0);
    leftColumnLayout->setSpacing(0);
    leftColumnLayout->addWidget(central, 1);
    leftColumnLayout->addWidget(bottomTabs_, 0);

    workspaceSplitter_ = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(0);
    workspaceSplitter_->addWidget(previewLeftColumn_);
    workspaceSplitter_->addWidget(previewPanel_);
    workspaceSplitter_->setStretchFactor(0, 1);
    workspaceSplitter_->setStretchFactor(1, 0);
    if (QSplitterHandle* handle = workspaceSplitter_->handle(1); handle != nullptr) {
        handle->setEnabled(false);
        handle->hide();
    }
    setCentralWidget(workspaceSplitter_);
    syncEditorHeaderMinimumWidth();
    applyWorkspacePanelArrangement();
    updatePreviewWorkspaceLayout();
    logStartupStage("workspace_and_central_widget_ready");

    finishFrameBootstrap(nullptr, logStartupStage);
}

miacode::latency::LatencySandboxController* MainWindow::latencySandboxController() const
{
    return latencySandboxController_.get();
}
