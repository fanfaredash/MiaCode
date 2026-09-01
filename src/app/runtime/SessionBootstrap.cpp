#include "runtime/Session.h"
#include "runtime/Shared.h"
#include "runtime/editor/EditorHost.h"
#include "runtime/media/MediaJobsHost.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/export/VideoExportHost.h"
#include "runtime/settings/SettingsHost.h"
#include "runtime/preview/StageMediaHost.h"
#include "runtime/playback/PlaybackHost.h"
#include "runtime/playback/PlaybackControlAdapter.h"
#include "runtime/timeline/TimelineHost.h"
#include "runtime/preview/PreviewHost.h"
#include "app/v2/SessionGeneration.h"
#include "runtime/validation/ValidationHost.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "audio/PreviewAudioDeviceWatcher.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
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

using namespace miacode::runtime::shared;

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

Session::Session(miacode::v2::ApplicationServices& services, QObject* parent)
    : QObject(parent)
    , applicationServices_(services)
{
    // Borrowed, not created: these services outlive the window and are owned by
    // the non-Widget assembly (stage 3.5 item 1). The window only connects to
    // them.
    editorSyncController_ = &applicationServices_.editorSync();
    chartDropImportService_ = &applicationServices_.chartDropImport();
    connect(editorSyncController_, &miacode::v2::EditorSyncController::editorContextChanged,
            this, &Session::refreshEditorAuthoringContext);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::caretLocationPublished,
            this, [this](int difficultyId, qulonglong, int line, int column) {
                if (difficultyId == activeDifficultyId_) {
                    publishEditorCaret(difficultyId, line, column);
                }
            });
    connect(editorSyncController_, &miacode::v2::EditorSyncController::pointerInteractionStarted,
            this, &Session::handleEditorPointerInteraction);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::touchPadControlHoldChanged,
            this, &Session::setTouchPadAuthoringCtrlHold);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::touchPadPreviewAnchorPublished,
            this, &Session::applyTouchPadAuthoringPreviewAnchor);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::previewSeekPublished,
            this, &Session::seekPreviewToEditorLocation);

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
                if (scene_ != nullptr) {
                    scene_->setJudgeEffectStyle(previewAppearance.judgeEffectStyle());
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
        appendStartupTimingStage(QString("runtime/%1").arg(stageName), nowMs, deltaMs);
    };

    configureRuntimeDebugOutput();
    logStartupStage("configure_runtime_debug_output");
    quickShellStartupStageMediaLoadDeferred_ = true;
    setProperty("miacode.dialog_parentless", true);
    logStartupStage("dialog_parentless_property_ready");

    editor_ = std::make_unique<miacode::runtime::EditorHost>(*this, ui_, state_);
    documents_ = std::make_unique<miacode::runtime::DocumentSessionHost>(*this, ui_, state_);
    mediaJobs_ = std::make_unique<miacode::runtime::MediaJobsHost>(*this, ui_, state_);
    videoExport_ = std::make_unique<miacode::runtime::VideoExportHost>(*this, ui_, state_);
    // The export page reaches the engine through the assembly's slot, never
    // through this window's member. Cleared at the top of ~Session so the
    // page cannot call into a half-destroyed section.
    applicationServices_.setExportEngine(videoExport_.get());

    // Relay the window's push notifications into the assembly so the QML models
    // can subscribe without holding a Session&. Nothing waits on a result
    // here, so a relay changes nothing about the contract — unlike the handler
    // hooks on DocumentBridge, which need an answer back.
    {
        miacode::v2::ShellNotifications& notify = applicationServices_.shellNotifications();
        connect(this, &Session::presentationChanged,
                &notify, &miacode::v2::ShellNotifications::presentationChanged);
        connect(this, &Session::previewPlayheadChanged,
                &notify, &miacode::v2::ShellNotifications::previewPlayheadChanged);
        connect(this, &Session::previewSkinDirectoryChanged,
                &notify, &miacode::v2::ShellNotifications::previewSkinDirectoryChanged);
        connect(this, &Session::documentReplaced,
                &notify, &miacode::v2::ShellNotifications::documentReplaced);
        connect(this, &Session::editorPreferencesChanged,
                &notify, &miacode::v2::ShellNotifications::editorPreferencesChanged);
        connect(this, &Session::muriPromptPreferenceChanged,
                &notify, &miacode::v2::ShellNotifications::muriPromptPreferenceChanged);
        connect(this, &Session::videoExportWorkerRunningChanged,
                &notify, &miacode::v2::ShellNotifications::videoExportWorkerRunningChanged);
        connect(this, &Session::normalizeWholeChartRequested,
                &notify, &miacode::v2::ShellNotifications::normalizeWholeChartRequested);
        connect(this, &Session::mediaToolsRequested,
                &notify, &miacode::v2::ShellNotifications::mediaToolsRequested);
        connect(this, &Session::preferencesRequested,
                &notify, &miacode::v2::ShellNotifications::preferencesRequested);
        connect(this, &Session::coverExportRequested,
                &notify, &miacode::v2::ShellNotifications::coverExportRequested);
    }
    settings_ = std::make_unique<miacode::runtime::SettingsHost>(*this, ui_, state_);
    stageMedia_ = std::make_unique<miacode::runtime::StageMediaHost>(*this, ui_, state_);
    validation_ = std::make_unique<miacode::runtime::ValidationHost>(*this, ui_, state_);
    shell_ = std::make_unique<miacode::runtime::ShellHost>(*this, ui_, state_);
    playback_ = std::make_unique<miacode::runtime::PlaybackHost>(*this, ui_, state_);
    const quint64 sessionGeneration = miacode::v2::nextSessionGeneration();
    playbackControl_ = std::make_unique<miacode::runtime::PlaybackControlAdapter>(
        *playback_, sessionGeneration);
    timelineHost_ = std::make_unique<miacode::runtime::TimelineHost>(*playback_, sessionGeneration);
    previewHost_ = std::make_unique<miacode::runtime::PreviewHost>(
        *playback_, *playbackControl_, *playbackControl_);
    const quint64 initialWorkspaceRevision = applicationServices_.workspace().snapshot().revision;
    playbackControl_->setDocumentRevision(initialWorkspaceRevision);
    timelineHost_->setDocumentRevision(initialWorkspaceRevision);
    applicationServices_.setPlaybackControl(playbackControl_.get());
    connect(&applicationServices_.workspace(), &miacode::v2::ChartWorkspace::changed,
            this, [this](quint64 revision) {
                documents_->syncRuntimeFromWorkspace();
                playbackControl_->setDocumentRevision(revision);
                timelineHost_->setDocumentRevision(revision);
            });
    // unique_ptr owns it; pass no QObject parent to avoid double-delete.
    latencySandboxController_ = std::make_unique<miacode::latency::LatencySandboxController>(this, nullptr);
    applicationServices_.setEditorPageRouter(documents_.get());
    applicationServices_.setMediaToolsEngine(mediaJobs_.get());
    applicationServices_.setLatencyEngine(latencySandboxController_.get());
    applicationServices_.setTimelineSurface(timelineHost_.get());
    applicationServices_.setPreviewSurface(previewHost_.get());
    applicationServices_.setPreferencesStore(settings_.get());
    applicationServices_.setDocumentBridge(documents_.get());
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

    updateWindowTitle();
    if (QGuiApplication* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance()); guiApp != nullptr) {
        if (QStyleHints* styleHints = guiApp->styleHints(); styleHints != nullptr) {
            connect(styleHints, &QStyleHints::colorSchemeChanged, this, [this]() {
                shell_->applyUiTheme();
            });
        }
    }

    ui_.uiRequests_ = &applicationServices_.uiRequests();
    ui_.jobProgress_ = &applicationServices_.jobProgress();
    connect(ui_.jobProgress_, &miacode::v2::JobProgressService::cancellationRequested,
            this, [this](quint64 token) {
                if (videoExport_ != nullptr && token == videoExportJobToken_
                    && videoExportJobToken_ != 0) {
                    videoExport_->cancelVideoExportWorker();
                }
            });
    ui_.qmlExportSession_ = new QmlExportSession(
        applicationServices_.shellNotifications(), applicationServices_.uiRequests(),
        applicationServices_.jobProgress(), applicationServices_.previewAppearance(),
        applicationServices_.exportEngineSlot(), applicationServices_.previewSurfaceSlot(),
        this);
    applicationServices_.setExportPageSession(ui_.qmlExportSession_);
    logStartupStage("runtime_pages_ready");

    scene_ = new PreviewRuntime(this);
    connect(scene_, &PreviewRuntime::touchPadAuthoringClicked, this, [this](const QString& pad, bool backtickSeparator) {
        if (editorSyncController_ != nullptr) {
            editorSyncController_->requestTouchPadAuthoring(pad, backtickSeparator);
        }
    });
    logStartupStage("preview_canvas_created");
    applyEffectivePreviewOutlineVariantToCanvas();
    applyPreviewSkinDirectoryToSurfaces();
    updatePreviewStageMediaPresentationMode(false);
    if (previewUsesStageMediaHostRoute()) {
        ensurePreviewStageMediaRouteInitialized();
    }
    logStartupStage("preview_skin_async_dispatched");

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
                if (playback_ != nullptr) {
                    playback_->handlePreviewAudioPrepared(completion);
                }
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::retainedPlaybackCompleted,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (playback_ != nullptr) {
                    playback_->handlePreviewRetainedPlaybackCompleted(completion);
                }
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::previewPlaybackPaused,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (playback_ != nullptr) {
                    playback_->handlePreviewRetainedPlaybackCompleted(completion);
                }
            });
    logStartupStage("preview_sfx_runtime_created");
#ifdef MIACODE_HAS_BASS_AUDIO
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
                if (playback_ != nullptr) {
                    playback_->applyPreviewAudioDeviceCutoff(cutoff);
                }
            });
    logStartupStage("preview_audio_device_watcher_created");
#endif
    connect(scene_, &PreviewRuntime::framePresented, this, [this]() {
        playback_->handlePreviewStartupCanvasPresented();
        if (!playing_) {
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
            if (scene_ != nullptr) {
                scene_->noteDisplayRefreshFramePresentation(waitNs, matchedRequest);
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
            if (scene_ != nullptr) {
                scene_->noteDisplayRefreshQueuedTick();
            }
            qtPreviewDisplayRefreshTickQueued_ = false;
            if (playing_
                && previewCanvasUsesFrameSwappedPacing()
                && !qtPreviewAwaitingFrameSwap_) {
                onQtPreviewTick();
            }
            return;
        }
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
        qtPreviewFixedFrameTickQueued_ = false;
        advanceFixedIntervalGateAfterPresent();
    });
    logStartupStage("preview_runtime_ready");

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
    playback_->refreshTimelineWaveformPhaseCompensation();
    connect(timelineQuickStateBridge_,
            &TimelineQuickStateBridge::renderCadenceTick,
            this,
            &Session::onTimelineRenderCadenceTick);
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::zoomScaleChanged, this, [this](double) {
        savePortableState();
    });
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::waveformBrightnessChanged, this, [this](double) {
        savePortableState();
    });
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::measureLineBrightnessChanged, this, [this](double) {
        savePortableState();
    });
    logStartupStage("timeline_ready");

    finishFrameBootstrap(nullptr, logStartupStage);
}

miacode::latency::LatencySandboxController* Session::latencySandboxController() const
{
    return latencySandboxController_.get();
}
