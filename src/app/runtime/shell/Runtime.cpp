#include "runtime/shell/ShellHost.h"
#include "runtime/Shared.h"

#include "runtime/media/MediaJobsHost.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/editor/EditorHost.h"
#include "runtime/export/VideoExportHost.h"
#include "runtime/settings/SettingsHost.h"
#include "runtime/preview/StageMediaHost.h"
#include "runtime/playback/PlaybackHost.h"
#include "runtime/playback/PlaybackControlAdapter.h"
#include "runtime/validation/ValidationHost.h"
#include "audio/PreviewAudioDeviceWatcher.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "common/AssetPaths.h"
#include "common/PreviewSfxAssets.h"
#include "common/CrashRecovery.h"
#include "common/DebugOptions.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/UiHangWatchdog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/latency/LatencySandboxController.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <QSoundEffect>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace miacode::runtime::shared;

namespace {

QString pointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

constexpr qint64 kRehostLayoutStepSlowMs = 50;
constexpr qint64 kRehostLayoutTotalSlowMs = 80;

QString rehostWidgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("(null)");
    }
    return QStringLiteral("class=%1 name=%2 ptr=%3 size=%4x%5 visible=%6 parent=%7")
        .arg(QString::fromUtf8(widget->metaObject()->className()))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(empty)") : widget->objectName())
        .arg(pointerHex(widget))
        .arg(widget->width())
        .arg(widget->height())
        .arg(widget->isVisible() ? 1 : 0)
        .arg(pointerHex(widget->parentWidget()));
}

void appendRehostLayoutDiag(
    const QString& action,
    const QString& step,
    QWidget* widget,
    qint64 elapsedMs,
    miacode::debug_log::Level level = miacode::debug_log::Level::Info)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("layout/rehosted_widget"),
        QStringLiteral("action=%1 step=%2 elapsed_ms=%3 widget=\"%4\"")
            .arg(action, step)
            .arg(elapsedMs)
            .arg(rehostWidgetSummary(widget)),
        /*force=*/false,
        level);
}

// Cascade-close every visible top-level popup besides the main window.
// Backed by Qt's QApplication::closeAllWindows() semantics: each popup
// receives a QCloseEvent and can decline if it has unsaved state.
//
// MUST be called only AFTER the close is confirmed (i.e. after
// maybeSaveBeforeContinue() returns true). The sweep is indiscriminate —
// it also closes the quick-shell's native bridge/compositing surfaces,
// which host the editor/timeline/preview as top-level QWidgets — so
// running it on a cancellable path and then declining the close strands
// the window with its content windows gone. Gating it behind the confirmed
// branch keeps a cancelled close fully side-effect free.
//
// Note: this helper only runs once Session's own closeEvent has fired.
// Qt's modal gate (Qt::ApplicationModal + dialog.exec()) drops close
// events targeted at non-modal windows for the duration of the nested
// event loop, so a taskbar "Close window" issued while a modal popup is
// open will not reach here. The fix for that path is to convert those
// popups from exec() to open()+finished — see comments in the export
// flow callers.
int dismissOpenChildPopupDialogs()
{
    int closed = 0;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevels) {
        if (widget == nullptr) {
            continue;
        }
        if (!widget->isWindow() || !widget->isVisible()) {
            continue;
        }
        if (widget->close()) {
            ++closed;
        }
    }
    return closed;
}

QString qEventTypeName(QEvent::Type type)
{
    switch (type) {
    case QEvent::None:
        return QStringLiteral("None");
    case QEvent::Show:
        return QStringLiteral("Show");
    case QEvent::Hide:
        return QStringLiteral("Hide");
    case QEvent::Close:
        return QStringLiteral("Close");
    case QEvent::Move:
        return QStringLiteral("Move");
    case QEvent::Resize:
        return QStringLiteral("Resize");
    case QEvent::Polish:
        return QStringLiteral("Polish");
    case QEvent::PolishRequest:
        return QStringLiteral("PolishRequest");
    case QEvent::UpdateRequest:
        return QStringLiteral("UpdateRequest");
    case QEvent::WinIdChange:
        return QStringLiteral("WinIdChange");
    case QEvent::PlatformSurface:
        return QStringLiteral("PlatformSurface");
    case QEvent::Expose:
        return QStringLiteral("Expose");
    case QEvent::WindowActivate:
        return QStringLiteral("WindowActivate");
    case QEvent::WindowDeactivate:
        return QStringLiteral("WindowDeactivate");
    case QEvent::ActivationChange:
        return QStringLiteral("ActivationChange");
    case QEvent::FocusIn:
        return QStringLiteral("FocusIn");
    case QEvent::FocusOut:
        return QStringLiteral("FocusOut");
    case QEvent::ParentChange:
        return QStringLiteral("ParentChange");
    case QEvent::ShowToParent:
        return QStringLiteral("ShowToParent");
    case QEvent::HideToParent:
        return QStringLiteral("HideToParent");
    case QEvent::WindowStateChange:
        return QStringLiteral("WindowStateChange");
    case QEvent::WindowBlocked:
        return QStringLiteral("WindowBlocked");
    case QEvent::WindowUnblocked:
        return QStringLiteral("WindowUnblocked");
    case QEvent::Destroy:
        return QStringLiteral("Destroy");
    default:
        break;
    }
    return QStringLiteral("Type(%1)").arg(static_cast<int>(type));
}

QString platformSurfaceEventTypeName(QPlatformSurfaceEvent::SurfaceEventType type)
{
    switch (type) {
    case QPlatformSurfaceEvent::SurfaceCreated:
        return QStringLiteral("SurfaceCreated");
    case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed:
        return QStringLiteral("SurfaceAboutToBeDestroyed");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(type));
}

QString windowVisibilityName(QWindow::Visibility visibility)
{
    switch (visibility) {
    case QWindow::Hidden:
        return QStringLiteral("Hidden");
    case QWindow::AutomaticVisibility:
        return QStringLiteral("AutomaticVisibility");
    case QWindow::Windowed:
        return QStringLiteral("Windowed");
    case QWindow::Minimized:
        return QStringLiteral("Minimized");
    case QWindow::Maximized:
        return QStringLiteral("Maximized");
    case QWindow::FullScreen:
        return QStringLiteral("FullScreen");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(visibility));
}

QString focusPolicyName(Qt::FocusPolicy policy)
{
    switch (policy) {
    case Qt::NoFocus:
        return QStringLiteral("NoFocus");
    case Qt::TabFocus:
        return QStringLiteral("TabFocus");
    case Qt::ClickFocus:
        return QStringLiteral("ClickFocus");
    case Qt::StrongFocus:
        return QStringLiteral("StrongFocus");
    case Qt::WheelFocus:
        return QStringLiteral("WheelFocus");
    }
    return QStringLiteral("FocusPolicy(%1)").arg(static_cast<int>(policy));
}

QString platformSurfaceDetail(const QEvent* event)
{
    const auto* surfaceEvent = dynamic_cast<const QPlatformSurfaceEvent*>(event);
    if (surfaceEvent == nullptr) {
        return QString();
    }
    return QStringLiteral("surface_event=%1")
        .arg(platformSurfaceEventTypeName(surfaceEvent->surfaceEventType()));
}

bool shouldTracePreviewHostWindowEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Expose:
    case QEvent::PlatformSurface:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::Destroy:
        return true;
    default:
        return false;
    }
}

QString resolveInvalidStarPreviewReverseSoundPath()
{
    QStringList candidates;
    const auto appendCandidate = [&candidates](const QString& candidate) {
        if (candidate.isEmpty()) {
            return;
        }
        const QString cleanPath = QDir::cleanPath(candidate);
        if (!candidates.contains(cleanPath)) {
            candidates.append(cleanPath);
        }
    };

    const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
    if (!sfxDir.isEmpty()) {
        appendCandidate(QDir(sfxDir).filePath(QStringLiteral("answer_reverse.wav")));
    }

    appendCandidate(miacode::assets::assetPath(QStringLiteral("SFX/answer_reverse.wav")));
    appendCandidate(miacode::assets::assetPath(QStringLiteral("sfx/answer_reverse.wav")));

    const QDir appDir(QCoreApplication::applicationDirPath());
    appendCandidate(appDir.filePath(QStringLiteral("assets/SFX/answer_reverse.wav")));
    appendCandidate(appDir.filePath(QStringLiteral("SFX/answer_reverse.wav")));
    appendCandidate(appDir.filePath(QStringLiteral("sfx/answer_reverse.wav")));
    appendCandidate(appDir.filePath(QStringLiteral("../Resources/assets/SFX/answer_reverse.wav")));

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString timestampLine(const QString& title)
{
    return miacode::debug_log::formatTitleLine(title);
}

}  // namespace

Session::~Session()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    // beta20-fix2 — first action in the destructor must be a force-logged
    // marker WITH NO MEMBER ACCESS, so we can tell from the runtime log
    // whether the destructor body even started executing. The previous
    // implementation jumped straight into a 10-arg .arg() chain that
    // touched many members (`isVisible()`, `previewWarmupPool_->...`,
    // `videoExportWorkerProcess_->...`); a crash in any of those would
    // leave no trace. The user-reported beta20 crash dump has the
    // runtime log ending at `accepted_close_destroy_backend_enter` with
    // no `destructor_enter` line — meaning either the destructor body
    // never ran OR its first .arg() chain crashed. This bare line
    // disambiguates the next crash.
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("step=destructor_body_entered"),
        /*force=*/true);

    // Withdraw the export engine before anything starts being torn down. The
    // assembly outlives this window, and the export page is a QObject child so
    // it dies AFTER videoExport_ — this is what stops a late call from
    // reaching a destroyed section.
    applicationServices_.setExportEngine(nullptr);
    applicationServices_.setEditorPageRouter(nullptr);
    applicationServices_.setLatencyEngine(nullptr);
    applicationServices_.setTimelineSurface(nullptr);
    applicationServices_.setPreviewSurface(nullptr);
    if (playbackControl_ != nullptr) {
        playbackControl_->invalidateSession();
    }
    applicationServices_.setPlaybackControl(nullptr);
    applicationServices_.setPreferencesStore(nullptr);
    applicationServices_.setDocumentBridge(nullptr);
    applicationServices_.setExportPageSession(nullptr);

    // Original diagnostic with member-touching .arg() chain — kept for
    // completeness but now safe because the bare marker above already
    // proved the destructor entered. force=true so it lands even when
    // debug mode is off and the next abnormal-exit dump can locate it.
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("step=destructor_enter "
            "visible=%1 top_level_widgets=%2 current_file=%3 preview_canvas=%4 preview_sfx=%5 waveform_cache=%6 "
            "warmup_active=%7 slow_refresh_active=%8 analysis_active=%9 export_worker_state=%10")
            .arg(0)
            .arg(QApplication::topLevelWidgets().size())
            .arg(currentFilePath_.trimmed().isEmpty() ? QStringLiteral("(empty)") : currentFilePath_)
            .arg(scene_ != nullptr ? 1 : 0)
            .arg(previewSfxRuntime_ != nullptr ? 1 : 0)
            .arg(state_.waveformCacheService_ != nullptr ? 1 : 0)
            .arg(previewWarmupPool_ != nullptr ? previewWarmupPool_->activeThreadCount() : -1)
            .arg(timelineSlowRefreshPool_ != nullptr ? timelineSlowRefreshPool_->activeThreadCount() : -1)
            .arg(timelineAnalysisPool_ != nullptr ? timelineAnalysisPool_->activeThreadCount() : -1)
            .arg(videoExportWorkerProcess_ != nullptr ? static_cast<int>(videoExportWorkerProcess_->state()) : -1),
        /*force=*/true);

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("step=before_disable_high_res_timer"),
        /*force=*/true);
    setPreviewFixedTimerHighResolutionActive(false);

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("step=before_shutdown_preview_stage_media_host"),
        /*force=*/true);
    QElapsedTimer stageMediaTimer;
    stageMediaTimer.start();
    shutdownPreviewStageMediaHost();
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("step=after_shutdown_preview_stage_media_host elapsed_ms=%1")
            .arg(stageMediaTimer.elapsed()),
        /*force=*/true);

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("step=destructor_body_exit elapsed_ms=%1 top_level_widgets=%2")
            .arg(totalTimer.elapsed())
            .arg(QApplication::topLevelWidgets().size()),
        /*force=*/true);
}

void Session::preparePreviewForShutdown()
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    if (quickShellPreviewCompositeSurface_ != nullptr) {
        quickShellPreviewCompositeSurface_->setMediaHost(nullptr);
        quickShellPreviewCompositeSurface_->setActive(false);
    }

    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    if (qtPreviewTimelineTimer_ != nullptr) {
        qtPreviewTimelineTimer_->stop();
    }
    if (previewStatsUiTimer_ != nullptr) {
        previewStatsUiTimer_->stop();
    }
    if (previewHeldSeekTimer_ != nullptr) {
        previewHeldSeekTimer_->stop();
    }
    setPreviewFixedTimerHighResolutionActive(false);
    setPreviewPlayingFlag(false);
    if (scene_ != nullptr) {
        scene_->setActivePlaybackProfilingEnabled(false);
    }
    // Doc 4.1: clear fixed-gate awaiting/queued state so lingering singleShot tick callbacks
    // cannot re-enter the request path during shutdown teardown.
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    qtPreviewDisplayRefreshTickQueued_ = false;
    qtPreviewFixedAwaitingFrame_ = false;
    qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    qtPreviewFixedFrameTickQueued_ = false;
    qtPreviewLastVisualTickNs_ = -1;
    previewStartupSyncPending_ = false;
    previewLateVideoStartPending_ = false;
    state_.previewStartupVideoPrepareStarted_ = false;
    pausedPreviewMediaSeekPending_ = false;
    pendingPreviewPlaybackStart_ = false;
    state_.activePreviewPlaybackTransactionId_ = 0;

    if (previewAudioDeviceWatcher_ != nullptr) {
        previewAudioDeviceWatcher_->disconnect(this);
        delete previewAudioDeviceWatcher_;
        previewAudioDeviceWatcher_ = nullptr;
    }

    QElapsedTimer audioTimer;
    audioTimer.start();
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->prepareForShutdown();
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("prepare_preview_audio"),
        audioTimer.elapsed(),
        QStringLiteral("preview_sfx=%1").arg(previewSfxRuntime_ != nullptr ? 1 : 0)
    );

    QElapsedTimer stageMediaTimer;
    stageMediaTimer.start();
    shutdownPreviewStageMediaHost();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("prepare_preview_stage_media_host"),
        stageMediaTimer.elapsed()
    );

    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/mainwindow"),
        QStringLiteral("prepare_preview_for_shutdown"),
        totalTimer.elapsed()
    );
}

void miacode::runtime::ShellHost::configureRuntimeDebugOutput()
{
    session_.runtimeDebugOutputEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
}

void miacode::runtime::ShellHost::setupInitialWindowGeometry()
{
}

QString miacode::runtime::ShellHost::formatWindowStateFlags(Qt::WindowStates states) const
{
    QStringList flags;
    if (states.testFlag(Qt::WindowMinimized)) {
        flags.append("minimized");
    }
    if (states.testFlag(Qt::WindowMaximized)) {
        flags.append("maximized");
    }
    if (states.testFlag(Qt::WindowFullScreen)) {
        flags.append("fullscreen");
    }
    if (states.testFlag(Qt::WindowActive)) {
        flags.append("active");
    }
    if (flags.isEmpty()) {
        flags.append("normal");
    }
    return flags.join('|');
}

void miacode::runtime::ShellHost::logWindowGeometryDebug(const QString& tag, const QString& detail)
{
    if (!session_.runtimeDebugOutputEnabled_) {
        return;
    }

    QRect clientRect;
    QRect frameRect;
    Qt::WindowStates states = Qt::WindowNoState;
    bool active = false;
    bool visible = false;
    if (session_.rootWindow_ != nullptr) {
        frameRect = session_.rootWindow_->frameGeometry();
        clientRect = session_.rootWindow_->geometry();
        states = session_.rootWindow_->windowStates();
        active = session_.rootWindow_->isActive();
        visible = session_.rootWindow_->isVisible();
    }

    QString payload = QString(
        "seq=%1 tag=%2 geom=[%3,%4 %5x%6] frame=[%7,%8 %9x%10] state=%11 "
        "active=%12 visible=%13 minimized=%14 maximized=%15 fullscreen=%16 "
        "suspend_depth=%17 arrange_gen=%18 arrange_retry=%19"
    )
        .arg(++session_.windowEventDebugSequence_)
        .arg(tag)
        .arg(clientRect.left())
        .arg(clientRect.top())
        .arg(clientRect.width())
        .arg(clientRect.height())
        .arg(frameRect.left())
        .arg(frameRect.top())
        .arg(frameRect.width())
        .arg(frameRect.height())
        .arg(this->formatWindowStateFlags(states))
        .arg(active ? 1 : 0)
        .arg(visible ? 1 : 0)
        .arg(states.testFlag(Qt::WindowMinimized) ? 1 : 0)
        .arg(states.testFlag(Qt::WindowMaximized) ? 1 : 0)
        .arg(states.testFlag(Qt::WindowFullScreen) ? 1 : 0)
        .arg(0)
        .arg(0)
        .arg(0);

    if (!detail.isEmpty()) {
        payload += " detail=" + detail;
    }

#ifdef Q_OS_WIN
    const HWND selfHwnd = nullptr;
    const HWND foregroundHwnd = GetForegroundWindow();
    const HWND foregroundOwner = foregroundHwnd != nullptr ? GetWindow(foregroundHwnd, GW_OWNER) : nullptr;
    const HWND foregroundRootOwner = foregroundHwnd != nullptr ? GetAncestor(foregroundHwnd, GA_ROOTOWNER) : nullptr;
    payload += QString(" self=0x%1 fg=0x%2 fg_owner=0x%3 fg_root_owner=0x%4 zoomed=%5 iconic=%6")
                   .arg(reinterpret_cast<quintptr>(selfHwnd), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundHwnd), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundOwner), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundRootOwner), 0, 16)
                   .arg(selfHwnd != nullptr ? (IsZoomed(selfHwnd) ? 1 : 0) : -1)
                   .arg(selfHwnd != nullptr ? (IsIconic(selfHwnd) ? 1 : 0) : -1);
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    if (selfHwnd != nullptr && GetWindowPlacement(selfHwnd, &placement)) {
        payload += QString(" wp_show=%1 wp_normal=[%2,%3 %4x%5]")
                       .arg(static_cast<int>(placement.showCmd))
                       .arg(placement.rcNormalPosition.left)
                       .arg(placement.rcNormalPosition.top)
                       .arg(placement.rcNormalPosition.right - placement.rcNormalPosition.left)
                       .arg(placement.rcNormalPosition.bottom - placement.rcNormalPosition.top);
    } else {
        payload += " wp_show=-1";
    }
#endif

    this->appendOutput("window/event", payload);
}

void miacode::runtime::ShellHost::logTopLevelWindowSnapshot(const QString& tag)
{
    if (!session_.runtimeDebugOutputEnabled_) {
        return;
    }

    QStringList lines;
    const auto topLevels = QApplication::topLevelWidgets();
    lines.reserve(topLevels.size() + 1);
    lines.append(QString("tag=%1 count=%2").arg(tag).arg(topLevels.size()));
    int index = 0;
    for (QWidget* window : topLevels) {
        if (window == nullptr) {
            continue;
        }
        const QRect geom = window->geometry();
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        const HWND owner = hwnd != nullptr ? GetWindow(hwnd, GW_OWNER) : nullptr;
        const HWND rootOwner = hwnd != nullptr ? GetAncestor(hwnd, GA_ROOTOWNER) : nullptr;
        const QString nativeDetail = QString(" wid=0x%1 owner=0x%2 root_owner=0x%3 zoomed=%4 iconic=%5")
                                         .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
                                         .arg(reinterpret_cast<quintptr>(owner), 0, 16)
                                         .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
                                         .arg(hwnd != nullptr ? (IsZoomed(hwnd) ? 1 : 0) : -1)
                                         .arg(hwnd != nullptr ? (IsIconic(hwnd) ? 1 : 0) : -1);
#else
        const QString nativeDetail;
#endif
        lines.append(
            QString("[%1] class=%2 title=%3 vis=%4 active=%5 modal=%6 state=%7 geom=[%8,%9 %10x%11]%12")
                .arg(index++)
                .arg(window->metaObject() != nullptr ? window->metaObject()->className() : "unknown")
                .arg(window->windowTitle().isEmpty() ? "(empty)" : window->windowTitle())
                .arg(window->isVisible() ? 1 : 0)
                .arg(window->isActiveWindow() ? 1 : 0)
                .arg(window->isModal() ? 1 : 0)
                .arg(this->formatWindowStateFlags(window->windowState()))
                .arg(geom.left())
                .arg(geom.top())
                .arg(geom.width())
                .arg(geom.height())
                .arg(nativeDetail)
        );
    }
    this->appendOutput("window/top_levels", lines.join('\n'));
}

void miacode::runtime::ShellHost::closeEvent(QCloseEvent* event)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    this->logWindowGeometryDebug("close_event_enter");
    QElapsedTimer maybeSaveTimer;
    maybeSaveTimer.start();
    const bool canClose = session_.maybeSaveBeforeContinue();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("legacy_close_event_maybe_save_before_continue"),
        maybeSaveTimer.elapsed(),
        QStringLiteral("result=%1").arg(canClose ? QStringLiteral("continue") : QStringLiteral("cancel"))
    );
    if (canClose) {
        // Close is confirmed. Dismiss any open child popup dialogs
        // (Preferences, Keyboard Shortcuts, etc.) only now — a cancelled
        // close (the else branch) must not tear down sibling windows.
        const int dismissed = dismissOpenChildPopupDialogs();
        if (dismissed > 0) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("close_timing/window"),
                QStringLiteral("legacy_close_event_dismissed_child_dialogs=%1").arg(dismissed)
            );
        }
        QElapsedTimer autosaveTimer;
        autosaveTimer.start();
        session_.runAutosaveCheck(false);
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/window"),
            QStringLiteral("legacy_close_event_autosave_check"),
            autosaveTimer.elapsed(),
            QStringLiteral("allow_history=0")
        );
        // Clean exit: drop the crash-recovery snapshot + delete any
        // recovery file so we don't prompt the user on next open about
        // a "crash" that was actually a clean shutdown. The session
        // marker goes with them — its whole job is to survive ONLY
        // abnormal exits.
        session_.documents_->cleanupCrashRecoveryForCleanExit();
        miacode::crash_recovery::clearSessionMarker();

        QElapsedTimer savePortableTimer;
        savePortableTimer.start();
        session_.savePortableState();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/window"),
            QStringLiteral("legacy_close_event_save_portable_state"),
            savePortableTimer.elapsed()
        );

        QElapsedTimer exportCleanupTimer;
        exportCleanupTimer.start();
        session_.videoExport_->clearVideoExportWorkerState();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/window"),
            QStringLiteral("legacy_close_event_clear_video_export_worker_state"),
            exportCleanupTimer.elapsed()
        );

        QElapsedTimer previewShutdownTimer;
        previewShutdownTimer.start();
        session_.preparePreviewForShutdown();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/window"),
            QStringLiteral("legacy_close_event_prepare_preview_for_shutdown"),
            previewShutdownTimer.elapsed()
        );
        event->accept();
        this->logWindowGeometryDebug("close_event_accept");
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/window"),
            QStringLiteral("legacy_close_event_total"),
            totalTimer.elapsed(),
            QStringLiteral("result=accepted")
        );
    } else {
        event->ignore();
        this->logWindowGeometryDebug("close_event_ignore");
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/window"),
            QStringLiteral("legacy_close_event_total"),
            totalTimer.elapsed(),
            QStringLiteral("result=ignored")
        );
    }
}

void miacode::runtime::ShellHost::appendOutput(const QString& title, const QString& payload)
{
    if (!session_.runtimeDebugOutputEnabled_) {
        return;
    }
    miacode::debug_log::appendText(
        miacode::debug_log::Channel::Runtime,
        timestampLine(title) + QStringLiteral("\n") + payload + QStringLiteral("\n")
    );
    if (session_.outputView_ == nullptr) {
        return;
    }
    session_.outputView_->appendPlainText(timestampLine(title));
    session_.outputView_->appendPlainText(payload);
    session_.outputView_->appendPlainText(QString());
}

QString miacode::runtime::ShellHost::describeFocusWidget(QWidget* widget) const
{
    if (widget == nullptr) {
        return QStringLiteral("null");
    }

    const auto matchesOrDescendsFrom = [](QWidget* candidate, QWidget* root) {
        return candidate != nullptr && root != nullptr && (candidate == root || root->isAncestorOf(candidate));
    };

    QStringList parts;
    parts.append(QStringLiteral("class=%1").arg(widget->metaObject() != nullptr ? widget->metaObject()->className() : "unknown"));
    parts.append(QStringLiteral("ptr=%1").arg(pointerHex(widget)));
    if (!widget->objectName().isEmpty()) {
        parts.append(QStringLiteral("name=%1").arg(widget->objectName()));
    }
    parts.append(QStringLiteral("vis=%1").arg(widget->isVisible() ? 1 : 0));
    parts.append(QStringLiteral("enabled=%1").arg(widget->isEnabled() ? 1 : 0));
    parts.append(QStringLiteral("focus=%1").arg(widget->hasFocus() ? 1 : 0));
    parts.append(QStringLiteral("policy=%1").arg(focusPolicyName(widget->focusPolicy())));
    parts.append(QStringLiteral("in_owner=%1").arg(0));
    parts.append(QStringLiteral("in_metadata=%1").arg(matchesOrDescendsFrom(widget, session_.metadataExtraEdit_) ? 1 : 0));
    parts.append(QStringLiteral("in_preview=%1").arg(matchesOrDescendsFrom(widget, session_.previewPanel_) ? 1 : 0));

    if (QWidget* focusProxy = widget->focusProxy(); focusProxy != nullptr) {
        parts.append(QStringLiteral("focus_proxy=%1").arg(pointerHex(focusProxy)));
    }

    QTextEdit* restorableTextEdit = this->resolveRestorableTextEdit(widget);
    if (restorableTextEdit != nullptr) {
        const bool chartEditor = false;
        const QTextCursor cursor = restorableTextEdit->textCursor();
        const int selectionLength = qAbs(cursor.position() - cursor.anchor());
        parts.append(QStringLiteral("restorable=1"));
        parts.append(QStringLiteral("restorable_id=%1").arg(chartEditor ? "chart_editor" : "metadata_extra"));
        parts.append(QStringLiteral("cursor_anchor=%1").arg(cursor.anchor()));
        parts.append(QStringLiteral("cursor_pos=%1").arg(cursor.position()));
        parts.append(QStringLiteral("selection_len=%1").arg(selectionLength));
        if (QTextDocument* document = restorableTextEdit->document(); document != nullptr) {
            parts.append(QStringLiteral("doc_chars=%1").arg(qMax(0, document->characterCount() - 1)));
        }
    }

    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget); lineEdit != nullptr) {
        parts.append(QStringLiteral("line_text_len=%1").arg(lineEdit->text().size()));
    }

    return parts.join(' ');
}

QString miacode::runtime::ShellHost::formatFocusReason(Qt::FocusReason reason) const
{
    switch (reason) {
    case Qt::MouseFocusReason:
        return QStringLiteral("MouseFocusReason");
    case Qt::TabFocusReason:
        return QStringLiteral("TabFocusReason");
    case Qt::BacktabFocusReason:
        return QStringLiteral("BacktabFocusReason");
    case Qt::ActiveWindowFocusReason:
        return QStringLiteral("ActiveWindowFocusReason");
    case Qt::PopupFocusReason:
        return QStringLiteral("PopupFocusReason");
    case Qt::ShortcutFocusReason:
        return QStringLiteral("ShortcutFocusReason");
    case Qt::MenuBarFocusReason:
        return QStringLiteral("MenuBarFocusReason");
    case Qt::OtherFocusReason:
        return QStringLiteral("OtherFocusReason");
    case Qt::NoFocusReason:
        return QStringLiteral("NoFocusReason");
    }
    return QStringLiteral("FocusReason(%1)").arg(static_cast<int>(reason));
}

void miacode::runtime::ShellHost::logFocusDebug(const QString& reason, QWidget* oldWidget, QWidget* nowWidget, const QString& detail)
{
    if (!session_.runtimeDebugOutputEnabled_) {
        return;
    }

    QString payload = QStringLiteral("seq=%1 reason=%2 active=%3")
        .arg(++session_.windowEventDebugSequence_)
        .arg(reason)
        .arg(session_.rootWindow_ != nullptr && session_.rootWindow_->isActive() ? 1 : 0);
    payload += QStringLiteral(" old={%1}").arg(this->describeFocusWidget(oldWidget));
    payload += QStringLiteral(" now={%1}").arg(this->describeFocusWidget(nowWidget));
    payload += QStringLiteral(" app_focus={%1}").arg(this->describeFocusWidget(QApplication::focusWidget()));
    payload += QStringLiteral(" owner_focus={%1}").arg(this->describeFocusWidget(QApplication::focusWidget()));
    if (!pendingTextFocusWidget_.isNull()) {
        payload += QStringLiteral(" pending={%1 saved_anchor=%2 saved_pos=%3}")
            .arg(this->describeFocusWidget(pendingTextFocusWidget_.data()))
            .arg(pendingTextCursorAnchor_)
            .arg(pendingTextCursorPosition_);
    } else {
        payload += QStringLiteral(" pending={null}");
    }
    if (!detail.isEmpty()) {
        payload += QStringLiteral(" detail=%1").arg(detail);
    }
    this->appendOutput(QStringLiteral("window/focus"), payload);
}

void miacode::runtime::ShellHost::refreshQuickShellRehostedWidgetParent(QWidget* widget)
{
    MC_OP("miacode::runtime::ShellHost::refreshQuickShellRehostedWidgetParent");
    if (widget == nullptr) {
        return;
    }
    QElapsedTimer totalTimer;
    totalTimer.start();
    MIACODE_HANG_PHASE(
        "WindowSection::refreshQuickShellRehostedWidgetParent",
        rehostWidgetSummary(widget));
    if (auto* dock = qobject_cast<QDockWidget*>(widget); dock != nullptr) {
        if (QWidget* dockWidget = dock->widget(); dockWidget != nullptr) {
            dockWidget->updateGeometry();
        }
    }
    if (QLayout* layout = widget->layout(); layout != nullptr) {
        QElapsedTimer stepTimer;
        stepTimer.start();
        MIACODE_HANG_PHASE(
            "WindowSection::refreshQuickShellRehostedWidgetParent.widgetLayout",
            rehostWidgetSummary(widget));
        layout->activate();
        const qint64 elapsedMs = stepTimer.elapsed();
        if (elapsedMs >= kRehostLayoutStepSlowMs) {
            appendRehostLayoutDiag(
                QStringLiteral("rehost_refresh_step_slow"),
                QStringLiteral("widget_layout_activate"),
                widget,
                elapsedMs,
                miacode::debug_log::Level::Warn);
        }
    }
    widget->updateGeometry();
    widget->update();
    if (QWidget* parentWidget = widget->parentWidget(); parentWidget != nullptr) {
        if (QLayout* parentLayout = parentWidget->layout(); parentLayout != nullptr) {
            QElapsedTimer stepTimer;
            stepTimer.start();
            MIACODE_HANG_PHASE(
                "WindowSection::refreshQuickShellRehostedWidgetParent.parentLayout",
                rehostWidgetSummary(parentWidget));
            parentLayout->activate();
            const qint64 elapsedMs = stepTimer.elapsed();
            if (elapsedMs >= kRehostLayoutStepSlowMs) {
                appendRehostLayoutDiag(
                    QStringLiteral("rehost_refresh_step_slow"),
                    QStringLiteral("parent_layout_activate"),
                    parentWidget,
                    elapsedMs,
                    miacode::debug_log::Level::Warn);
            }
        }
        parentWidget->updateGeometry();
        parentWidget->update();
    }
    const qint64 totalMs = totalTimer.elapsed();
    if (totalMs >= kRehostLayoutTotalSlowMs) {
        appendRehostLayoutDiag(
            QStringLiteral("rehost_refresh_total_slow"),
            QStringLiteral("total"),
            widget,
            totalMs,
            miacode::debug_log::Level::Warn);
    }
}

void miacode::runtime::ShellHost::setInvalidStarPreviewEasterEggEnabled(bool enabled)
{
    if (session_.invalidStarPreviewEasterEggEnabled_ == enabled) {
        return;
    }
    session_.invalidStarPreviewEasterEggEnabled_ = enabled;
    SimaiNativeParser::setInvalidStarPreviewEnabled(enabled);
    session_.refreshTimelineMetadata();
    this->playInvalidStarPreviewEasterEggSound(enabled);
}

void miacode::runtime::ShellHost::ensureInvalidStarPreviewEasterEggSounds()
{
    if (session_.invalidStarPreviewEnableSound_ == nullptr) {
        session_.invalidStarPreviewEnableSound_ = new QSoundEffect(&session_);
        session_.invalidStarPreviewEnableSound_->setLoopCount(1);
        session_.invalidStarPreviewEnableSound_->setVolume(0.45f);
    }
    if (session_.invalidStarPreviewDisableSound_ == nullptr) {
        session_.invalidStarPreviewDisableSound_ = new QSoundEffect(&session_);
        session_.invalidStarPreviewDisableSound_->setLoopCount(1);
        session_.invalidStarPreviewDisableSound_->setVolume(0.45f);
    }

    const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
    const QString forwardPath = miacode::preview_sfx::assetFilePathForKind(sfxDir, QStringLiteral("answer"));
    const QString reversePath = resolveInvalidStarPreviewReverseSoundPath();

    const QUrl forwardUrl = QFileInfo::exists(forwardPath) ? QUrl::fromLocalFile(forwardPath) : QUrl();
    const QUrl reverseUrl = QFileInfo::exists(reversePath) ? QUrl::fromLocalFile(reversePath) : QUrl();

    if (session_.invalidStarPreviewDisableSound_->source() != forwardUrl) {
        session_.invalidStarPreviewDisableSound_->setSource(forwardUrl);
    }
    if (session_.invalidStarPreviewEnableSound_->source() != reverseUrl) {
        session_.invalidStarPreviewEnableSound_->setSource(reverseUrl);
    }
}

void miacode::runtime::ShellHost::playInvalidStarPreviewEasterEggSound(bool enabled)
{
    this->ensureInvalidStarPreviewEasterEggSounds();
    QSoundEffect* effect = enabled ? session_.invalidStarPreviewEnableSound_ : session_.invalidStarPreviewDisableSound_;
    if (effect == nullptr || effect->source().isEmpty()) {
        return;
    }
    effect->stop();
    effect->play();
}
