#include "QuickShellBootstrap.h"

#include "app/WindowsIdleEventDiagnostics.h"
#include "app/WindowVisibilityDiagnostics.h"

#include "QuickShellNativeSurfaceHost.h"
#include "QuickShellController.h"
#include "QuickShellStyleBridge.h"
#include "MainEntrypoints.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "UiNativeWindowTheme.h"
#include "common/DebugOptions.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "mainwindow/MainWindow.h"
// Phase 4c — needed for the host pointer type in the bootstrap
// wiring lambda; the lambda passes it straight to setStageMediaHost.
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/quick/TimelineQuickItem.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QtQml>

#ifdef Q_OS_WIN
#include <QtCore/qabstractnativeeventfilter.h>
#include <windows.h>
#endif

namespace {

QString pointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

void appendQuickShellRuntimeLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell"),
        text
    );
}

void appendQuickShellFocusLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/focus"),
        text
    );
}

void appendQuickShellArrowDispatchLog(const QString& action, const QString& payload = QString())
{
    // Always-on tracing for the arrow-key dispatch path so we can
    // diagnose user reports of arrow-keys-hijacked-to-preview-seek
    // without requiring a debug-mode rerun. The log volume is small
    // (one line per mouse press and one per arrow press/release).
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/arrow_dispatch"),
        text
    );
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

QString applicationStateName(Qt::ApplicationState state)
{
    switch (state) {
    case Qt::ApplicationSuspended:
        return QStringLiteral("ApplicationSuspended");
    case Qt::ApplicationHidden:
        return QStringLiteral("ApplicationHidden");
    case Qt::ApplicationInactive:
        return QStringLiteral("ApplicationInactive");
    case Qt::ApplicationActive:
        return QStringLiteral("ApplicationActive");
    }
    return QStringLiteral("ApplicationState(%1)").arg(static_cast<int>(state));
}

#ifdef Q_OS_WIN
class QuickShellNativeCloseEventFilter final : public QAbstractNativeEventFilter
{
public:
    explicit QuickShellNativeCloseEventFilter(QuickShellBootstrap* bootstrap)
        : bootstrap_(bootstrap)
    {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QuickShellBootstrap* bootstrap_ = nullptr;
};
#endif

void applyNativeThemeToQuickWindow(QQuickWindow* window)
{
    UiNativeWindowTheme::applyToWindow(window);
}

void ensurePreviewQuickTypesRegisteredForQuickShell()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    qmlRegisterType<PreviewQuickSceneRoot>("MiaCode.Preview", 1, 0, "PreviewQuickSceneRoot");
   qmlRegisterType<PreviewQuickHudLayer>("MiaCode.Preview", 1, 0, "PreviewQuickHudLayer");
   qmlRegisterType<TimelineQuickItem>("MiaCode.Timeline", 1, 0, "TimelineQuickItem");
    registered = true;
}

}  // namespace

QuickShellBootstrap::QuickShellBootstrap(const QIcon& appIcon, QObject* parent)
    : QObject(parent)
    , appIcon_(appIcon)
{
#ifdef Q_OS_WIN
    windowsIdleEventMonitor_ =
        std::make_unique<miacode::app::windows_idle_diagnostics::WindowsIdleEventMonitor>();
#endif
}

QuickShellBootstrap::~QuickShellBootstrap()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (!rootWindow_.isNull()) {
        rootWindow_->removeEventFilter(this);
    }
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
#ifdef Q_OS_WIN
    if (windowsIdleEventMonitor_ != nullptr) {
        windowsIdleEventMonitor_->unregisterWindow();
    }
    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        if (nativeCloseEventFilter_ != nullptr) {
            app->removeNativeEventFilter(nativeCloseEventFilter_.get());
        }
    }
#endif
    if (backend_ != nullptr) {
        backend_->preparePreviewForShutdown();
        backend_->close();
    }
    const auto logResetTiming = [](const QString& step, auto& pointer) {
        if (!pointer) {
            return;
        }
        QElapsedTimer timer;
        timer.start();
        pointer.reset();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("app_shutdown/quick_shell_bootstrap"),
            step,
            timer.elapsed()
        );
    };
    logResetTiming(QStringLiteral("destroy_engine"), engine_);
    logResetTiming(QStringLiteral("destroy_style_bridge"), styleBridge_);
    logResetTiming(QStringLiteral("destroy_controller"), controller_);
    logResetTiming(QStringLiteral("destroy_surface_host"), surfaceHost_);
    logResetTiming(QStringLiteral("destroy_backend"), backend_);
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/quick_shell_bootstrap"),
        QStringLiteral("destructor_total"),
        totalTimer.elapsed()
    );
}

bool QuickShellBootstrap::start(const QString& startupOpenTarget)
{
    MC_OP("QuickShellBootstrap::start");
    miacode::oplog::appendStartupBeaconLine("qsb/start_enter");
    appendQuickShellRuntimeLog(QStringLiteral("start_enter"));
    miacode::oplog::appendStartupBeaconLine("qsb/before_mainwindow_ctor");
    backend_ = std::make_unique<MainWindow>(true);
    miacode::oplog::appendStartupBeaconLine("qsb/after_mainwindow_ctor");
    backend_->setQuickShellBackendActive(true);
    miacode::oplog::appendStartupBeaconLine("qsb/after_set_backend_active");
    backend_->hide();
    miacode::oplog::appendStartupBeaconLine("qsb/after_first_hide");
    backend_->setVisible(false);
    miacode::oplog::appendStartupBeaconLine("qsb/after_first_setvisible_false");
    appendQuickShellRuntimeLog(QStringLiteral("backend_ready"));
    miacode::oplog::appendStartupBeaconLine("qsb/backend_ready");
    surfaceHost_ = std::make_unique<QuickShellNativeSurfaceHost>(backend_.get(), backend_.get(), this);
    miacode::oplog::appendStartupBeaconLine("qsb/after_surface_host_ctor");
    backend_->hide();
    backend_->setVisible(false);
    appendQuickShellRuntimeLog(QStringLiteral("surface_host_ready"));
    miacode::oplog::appendStartupBeaconLine("qsb/surface_host_ready");
    controller_ = std::make_unique<QuickShellController>(backend_.get(), backend_.get(), surfaceHost_.get(), this);
    appendQuickShellRuntimeLog(QStringLiteral("controller_ready"));
    miacode::oplog::appendStartupBeaconLine("qsb/controller_ready");
    QObject::connect(
        controller_.get(),
        &QuickShellController::rootCloseAccepted,
        this,
        [this](const QString& source) {
            beginAcceptedRootWindowShutdown(source);
        }
    );
    styleBridge_ = std::make_unique<QuickShellStyleBridge>(backend_.get(), surfaceHost_.get(), this);
    appendQuickShellRuntimeLog(QStringLiteral("style_bridge_ready"));
    if (surfaceHost_ != nullptr && styleBridge_ != nullptr) {
        QObject::connect(
            styleBridge_.get(),
            &QuickShellStyleBridge::appearanceChanged,
            surfaceHost_.get(),
            &QuickShellNativeSurfaceHost::refreshSurfaceStyles
        );
    }
    miacode::oplog::appendStartupBeaconLine("qsb/before_qml_engine_ctor");
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    miacode::oplog::appendStartupBeaconLine("qsb/after_qml_engine_ctor");
    appendQuickShellRuntimeLog(QStringLiteral("engine_ready"));
    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }
#ifdef Q_OS_WIN
    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        nativeCloseEventFilter_ = std::make_unique<QuickShellNativeCloseEventFilter>(this);
        app->installNativeEventFilter(nativeCloseEventFilter_.get());
    }
#endif
    if (QApplication* app = qobject_cast<QApplication*>(qApp); app != nullptr) {
        QObject::connect(app, &QApplication::focusChanged, this, [this](QWidget* old, QWidget* now) {
            if (!shouldTraceFocusObject(old) && !shouldTraceFocusObject(now)) {
                return;
            }
            logFocusEvent(
                QStringLiteral("focus_changed_signal"),
                now,
                nullptr,
                QStringLiteral("old={%1} now={%2}")
                    .arg(describeFocusObject(old))
                    .arg(describeFocusObject(now))
            );
        });
    }
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        logFocusEvent(
            QStringLiteral("application_state_changed"),
            qApp != nullptr ? qApp->focusWindow() : nullptr,
            nullptr,
            QStringLiteral("state=%1").arg(applicationStateName(state))
        );
    });
    engine_->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    ensurePreviewQuickTypesRegisteredForQuickShell();

    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::warnings,
        this,
        [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
                appendQuickShellRuntimeLog(QStringLiteral("qml_warning"), warning.toString());
                QTextStream(stderr) << "[QuickShell QML] " << warning.toString() << '\n';
            }
            QTextStream(stderr).flush();
        }
    );
    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::objectCreationFailed,
        this,
        [](const QUrl& url) {
            appendQuickShellRuntimeLog(QStringLiteral("qml_object_creation_failed"), url.toString());
            QTextStream(stderr) << "[QuickShell QML] object creation failed for " << url.toString() << '\n';
            QTextStream(stderr).flush();
        }
    );

    qmlRegisterUncreatableType<QuickShellController>(
        "MiaCode.QuickShell",
        1,
        0,
        "QuickShellController",
        "Quick shell controller is provided by bootstrap."
    );
    qmlRegisterUncreatableType<QuickShellStyleBridge>(
        "MiaCode.QuickShell",
        1,
        0,
        "QuickShellStyleBridge",
        "Quick shell style bridge is provided by bootstrap."
    );

    engine_->rootContext()->setContextProperty(QStringLiteral("controller"), controller_.get());
    engine_->rootContext()->setContextProperty(QStringLiteral("styleBridge"), styleBridge_.get());
    const QUrl mainUrl(QStringLiteral("qrc:/quick_shell/qml/QuickShellMain.qml"));
    appendQuickShellRuntimeLog(QStringLiteral("load_begin"), mainUrl.toString());
    miacode::oplog::appendStartupBeaconLine("qsb/before_qml_load");
    engine_->load(mainUrl);
    miacode::oplog::appendStartupBeaconLine("qsb/after_qml_load");
    appendQuickShellRuntimeLog(
        QStringLiteral("load_end"),
        QString("root_objects=%1").arg(engine_->rootObjects().size())
    );
    if (engine_->rootObjects().isEmpty()) {
        appendQuickShellRuntimeLog(QStringLiteral("load_failed"), mainUrl.toString());
        QTextStream(stderr) << "[QuickShell QML] no root object created for " << mainUrl.toString() << '\n';
        QTextStream(stderr).flush();
        engine_.reset();
        styleBridge_.reset();
        controller_.reset();
        surfaceHost_.reset();
        backend_.reset();
        return false;
    }

    if (QQuickWindow* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst()); window != nullptr) {
        rootWindow_ = window;
#ifdef Q_OS_WIN
        rootWindowNativeHwnd_ = static_cast<quintptr>(window->winId());
        if (windowsIdleEventMonitor_ != nullptr) {
            windowsIdleEventMonitor_->registerWindow(rootWindowNativeHwnd_);
        }
#endif
        window->installEventFilter(this);

        // Occlusion / minimize awareness for the DEFAULT QSG path. The reported freeze
        // happens with the app minimized or covered by a video-playing browser, and that
        // condition is otherwise entirely absent from the logs.
        miacode::app::window_diag::installWindowVisibilityDiagnostics(
            window, QStringLiteral("root_window"));

        // P4 — bind the root window to the resolved high-performance DXGI
        // adapter BEFORE its scene graph initializes (still pre-event-loop
        // here; winId() above created only the HWND, not the RHI device).
        // No-op on single-GPU / explicit-RHI / non-Windows; falls through to
        // Qt's default adapter on any miss. The P1 probe below runs on first
        // render and logs the ACTUAL adapter, verifying whether the bind hit.
        miacode::app::entry::bindHighPerformanceQuickGraphicsDevice(
            window, QStringLiteral("quick_shell_root_window"), /*preferVideoShareDevice=*/false);

        // P1 — log the RHI adapter Qt Quick actually bound for the root window
        // (D3D11 DXGI adapter desc, or GL renderer string). Scheduled onto the
        // render thread; no-op unless --debug is active.
        miacode::app::entry::logQuickWindowGpuDevice(
            window, QStringLiteral("quick_shell_root_window"));

        if (surfaceHost_ != nullptr) {
            surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
        }
        appendQuickShellRuntimeLog(
            QStringLiteral("root_window_ready"),
            QString("visible=%1 width=%2 height=%3 title=%4 icon_null=%5")
                .arg(window->isVisible() ? 1 : 0)
                .arg(window->width())
                .arg(window->height())
                .arg(window->title())
                .arg(appIcon_.isNull() ? 1 : 0)
        );
        if (window->minimumWidth() > 0 && window->width() < window->minimumWidth()) {
            const int deltaWidth = window->minimumWidth() - window->width();
            window->setX(window->x() - deltaWidth / 2);
            window->setWidth(window->minimumWidth());
        }
        if (window->minimumHeight() > 0 && window->height() < window->minimumHeight()) {
            const int deltaHeight = window->minimumHeight() - window->height();
            window->setY(window->y() - deltaHeight / 2);
            window->setHeight(window->minimumHeight());
        }
        // macOS must keep the bundle icon from app.icns so Dock can apply the
        // system app-icon mask. Setting the QQuickWindow icon at runtime would
        // replace it with the unmasked PNG; other platforms still need the
        // explicit window icon supplied by the application bootstrap.
#ifndef Q_OS_MACOS
        if (!appIcon_.isNull()) {
            window->setIcon(appIcon_);
        }
#endif
        QObject::connect(window, &QQuickWindow::visibleChanged, this, [this, window]() {
            appendQuickShellRuntimeLog(
                QStringLiteral("root_window_visible_changed"),
                QStringLiteral("visible=%1 shutdown_started=%2")
                    .arg(window->isVisible() ? 1 : 0)
                    .arg(acceptedRootWindowShutdownStarted_ ? 1 : 0)
            );
            if (!window->isVisible()) {
                beginAcceptedRootWindowShutdown(QStringLiteral("root_window_hidden"));
            }
        });
#ifdef Q_OS_WIN
        QObject::connect(window, &QQuickWindow::activeChanged, this, [this, window]() {
            logFocusEvent(
                QStringLiteral("root_window_active_changed"),
                window,
                nullptr,
                QStringLiteral("active=%1").arg(window->isActive() ? 1 : 0)
            );
        });
        QObject::connect(window, &QQuickWindow::xChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
        QObject::connect(window, &QQuickWindow::yChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
        QObject::connect(window, &QQuickWindow::widthChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
        QObject::connect(window, &QQuickWindow::heightChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
#endif
        QPointer<QQuickWindow> windowGuard(window);
        QTimer::singleShot(0, this, [this, windowGuard]() {
            if (windowGuard.isNull()) {
                return;
            }
            auto* window = windowGuard.data();
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
            applyNativeThemeToQuickWindow(window);
#ifdef Q_OS_WIN
            QObject::connect(window, &QQuickWindow::activeChanged, this, [window]() {
                applyNativeThemeToQuickWindow(window);
            });
#endif
            if (styleBridge_ != nullptr) {
                QObject::connect(styleBridge_.get(), &QuickShellStyleBridge::appearanceChanged, this, [window]() {
                    applyNativeThemeToQuickWindow(window);
                });
            }
#ifdef Q_OS_WIN
            QObject::connect(qApp, &QGuiApplication::applicationStateChanged, this, [window](Qt::ApplicationState) {
                applyNativeThemeToQuickWindow(window);
            });
#endif
            appendQuickShellRuntimeLog(
                QStringLiteral("root_window_post_show"),
                QString("visible=%1 exposed=%2 active=%3 width=%4 height=%5")
                    .arg(window->isVisible() ? 1 : 0)
                    .arg(window->isExposed() ? 1 : 0)
                    .arg(window->isActive() ? 1 : 0)
                    .arg(window->width())
                    .arg(window->height())
            );
            // P1 — QuickShell hosting topology snapshot. Confirms the default
            // frontend is the QML root window + a hidden MainWindow backend +
            // N bridge/preview native surfaces, and that the preview stage media
            // route is the QuickShell host — the mixed-topology risk the GPU
            // device policy has to respect (see the plan's QuickShell专项).
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("quick_shell/topology"),
                QStringLiteral(
                    "frontend=quickshell hidden_mainwindow=%1 stage_media_route=QuickShellStageHost "
                    "root_window=1 top_level_windows=%2 top_level_widgets=%3")
                    .arg(backend_ != nullptr && !backend_->isVisible() ? 1 : 0)
                    .arg(QGuiApplication::topLevelWindows().size())
                    .arg(QApplication::topLevelWidgets().size()));
            if (surfaceHost_ != nullptr) {
                surfaceHost_->noteQuickShellUiReady();
            }
            // First-run welcome / initial-config dialog. Deferred one more
            // tick so the first frame is presented (and the workspace/theme
            // are settled) before the modal appears over it.
            if (showWelcomeDialogOnStartup_ && backend_ != nullptr) {
                showWelcomeDialogOnStartup_ = false;
                QPointer<MainWindow> welcomeBackendGuard(backend_.get());
                QTimer::singleShot(0, backend_.get(), [welcomeBackendGuard]() {
                    if (!welcomeBackendGuard.isNull()) {
                        welcomeBackendGuard->showWelcomeDialog();
                    }
                });
            }
        });
    }
    if (!startupOpenTarget.trimmed().isEmpty() && backend_ != nullptr) {
        QTimer::singleShot(0, backend_.get(), [backend = QPointer<MainWindow>(backend_.get()), startupOpenTarget]() {
            if (backend.isNull()) {
                return;
            }
            backend->openStartupTarget(startupOpenTarget);
        });
    }
    return true;
}

bool QuickShellBootstrap::eventFilter(QObject* watched, QEvent* event)
{
    if (controller_ == nullptr || event == nullptr) {
        return QObject::eventFilter(watched, event);
    }

    if (watched == rootWindow_ && event->type() == QEvent::Close) {
        if (rootWindowCloseRelayScheduled_
            || rootWindowCloseRelayActive_
            || UiDialogs::hasVisibleBlockingModalDialog()) {
            if (!rootWindowCloseRelayScheduled_
                && !rootWindowCloseRelayActive_
                && UiDialogs::hasVisibleBlockingModalDialog()) {
                scheduleRootWindowCloseRelay(QStringLiteral("qevent_close"));
            }
            event->ignore();
            return true;
        }
    }

    if (shouldTraceFocusObject(watched)) {
        switch (event->type()) {
        case QEvent::FocusIn:
        case QEvent::FocusOut:
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
        case QEvent::ActivationChange:
            logFocusEvent(QStringLiteral("event_filter"), watched, event);
            break;
        default:
            break;
        }
    }

    if (UiDialogs::hasVisibleProtectedPreviewDialog()
        && (event->type() == QEvent::ShortcutOverride
            || event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (UiDialogs::isPreviewShortcutEvent(keyEvent)) {
            return QObject::eventFilter(watched, event);
        }
    }

    if (!controller_->previewFullscreen() && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent != nullptr ? mouseEvent->globalPosition().toPoint() : QPoint();
        const bool previouslyArmed = previewSeekArmed_;
        previewSeekArmed_ = previewSeekHotRectContainsGlobalPoint(globalPos);
        // Always log mouse-press arming decisions so we can see exactly
        // which click left previewSeekArmed_ stuck on. Includes the
        // global pos, the live hot rect (if QML root is available), and
        // the watched widget that received the press.
        QRectF hotRect;
        if (engine_ != nullptr && !engine_->rootObjects().isEmpty()) {
            if (auto* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
                window != nullptr) {
                hotRect = window->property("previewSeekHotRect").toRectF();
            }
        }
        appendQuickShellArrowDispatchLog(
            QStringLiteral("mouse_press_arm"),
            QStringLiteral("global=%1,%2 hot_rect=%3,%4,%5x%6 prev_armed=%7 new_armed=%8 watched={%9} focus_widget={%10} button=%11")
                .arg(globalPos.x()).arg(globalPos.y())
                .arg(hotRect.x()).arg(hotRect.y()).arg(hotRect.width()).arg(hotRect.height())
                .arg(previouslyArmed ? 1 : 0)
                .arg(previewSeekArmed_ ? 1 : 0)
                .arg(describeFocusObject(watched))
                .arg(describeFocusObject(qobject_cast<QObject*>(QApplication::focusWidget())))
                .arg(mouseEvent != nullptr ? static_cast<int>(mouseEvent->button()) : -1)
        );
    }

    if (!controller_->previewFullscreen()) {
        if ((event->type() == QEvent::ShortcutOverride
                || event->type() == QEvent::KeyPress
                || event->type() == QEvent::KeyRelease)
            && previewSeekArmed_) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const bool isArrow =
                keyEvent != nullptr
                && keyEvent->modifiers() == Qt::NoModifier
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right);
            // The sticky `previewSeekArmed_` flag is purely geometric — it
            // only knows whether the last mouse press was inside the QML
            // previewPaneFrame's bounds. In QuickShell mode the editor's
            // native sub-window (QuickShellWorkspaceSurfaceWindow) overlays
            // part of that bounds, so a click that landed on the editor
            // (focus goes to PlainCodeEditor, cursor moves) still arms
            // the flag, and the next arrow keypress meant for cursor
            // navigation gets swallowed into preview seek. Gate the
            // hijack on the active focus widget NOT being a text input;
            // let normal arrow delivery handle text-cursor navigation.
            const QWidget* focusedWidget = QApplication::focusWidget();
            const bool focusIsTextInput = focusedWidget != nullptr
                && (qobject_cast<const QTextEdit*>(focusedWidget) != nullptr
                    || qobject_cast<const QPlainTextEdit*>(focusedWidget) != nullptr
                    || qobject_cast<const QLineEdit*>(focusedWidget) != nullptr);
            if (isArrow && focusIsTextInput) {
                appendQuickShellArrowDispatchLog(
                    QStringLiteral("arrow_text_input_passthrough"),
                    QStringLiteral("key=%1 event_type=%2 watched={%3} focus_widget={%4} armed=1")
                        .arg(keyEvent->key())
                        .arg(static_cast<int>(event->type()))
                        .arg(describeFocusObject(watched))
                        .arg(describeFocusObject(qobject_cast<QObject*>(QApplication::focusWidget())))
                );
                return QObject::eventFilter(watched, event);
            }
            if (isArrow) {
                if (event->type() == QEvent::ShortcutOverride) {
                    appendQuickShellArrowDispatchLog(
                        QStringLiteral("arrow_shortcut_override_swallow"),
                        QStringLiteral("key=%1 watched={%2} focus_widget={%3} armed=1")
                            .arg(keyEvent->key())
                            .arg(describeFocusObject(watched))
                            .arg(describeFocusObject(qobject_cast<QObject*>(QApplication::focusWidget())))
                    );
                    keyEvent->accept();
                    return true;
                }
                const int direction = keyEvent->key() == Qt::Key_Left ? -1 : 1;
                if (event->type() == QEvent::KeyPress) {
                    appendQuickShellArrowDispatchLog(
                        QStringLiteral("arrow_press_hijack"),
                        QStringLiteral("key=%1 direction=%2 autorepeat=%3 watched={%4} focus_widget={%5} focus_window={%6}")
                            .arg(keyEvent->key())
                            .arg(direction)
                            .arg(keyEvent->isAutoRepeat() ? 1 : 0)
                            .arg(describeFocusObject(watched))
                            .arg(describeFocusObject(qobject_cast<QObject*>(QApplication::focusWidget())))
                            .arg(describeFocusObject(qobject_cast<QObject*>(qApp != nullptr ? qApp->focusWindow() : nullptr)))
                    );
                    keyEvent->accept();
                    if (keyEvent->isAutoRepeat()) {
                        return true;
                    }
                    controller_->beginPreviewHeldSeek(direction, keyEvent->key());
                    controller_->stepPreviewBySeconds(
                        direction * controller_->previewSeekSingleStepSeconds(),
                        true
                    );
                    return true;
                }
                if (event->type() == QEvent::KeyRelease) {
                    appendQuickShellArrowDispatchLog(
                        QStringLiteral("arrow_release"),
                        QStringLiteral("key=%1 autorepeat=%2 watched={%3}")
                            .arg(keyEvent->key())
                            .arg(keyEvent->isAutoRepeat() ? 1 : 0)
                            .arg(describeFocusObject(watched))
                    );
                    keyEvent->accept();
                    if (!keyEvent->isAutoRepeat()) {
                        controller_->stopPreviewHeldSeek(keyEvent->key());
                    }
                    return true;
                }
            }
        }
        if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent != nullptr && !keyEvent->isAutoRepeat()) {
                const QKeySequence sequence(keyEvent->modifiers() | keyEvent->key());
                if (controller_->hasShortcut(sequence)) {
                    if (event->type() == QEvent::ShortcutOverride) {
                        keyEvent->accept();
                        return true;
                    }
                    if (controller_->triggerShortcut(sequence)) {
                        keyEvent->accept();
                        return true;
                    }
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent != nullptr
            && keyEvent->modifiers() == Qt::NoModifier
            && (keyEvent->key() == Qt::Key_Escape || keyEvent->key() == Qt::Key_Space)) {
            keyEvent->accept();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent != nullptr && !keyEvent->isAutoRepeat() && keyEvent->modifiers() == Qt::NoModifier) {
            if (keyEvent->key() == Qt::Key_Escape) {
                appendQuickShellRuntimeLog(QStringLiteral("global_fullscreen_key"), QStringLiteral("key=Esc"));
                controller_->setPreviewFullscreen(false);
                keyEvent->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Space) {
                appendQuickShellRuntimeLog(QStringLiteral("global_fullscreen_key"), QStringLiteral("key=Space"));
                controller_->togglePreviewPlayback();
                keyEvent->accept();
                return true;
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

#ifdef Q_OS_WIN
bool QuickShellNativeCloseEventFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    return bootstrap_ != nullptr ? bootstrap_->handleNativeCloseEvent(eventType, message, result) : false;
}

bool QuickShellBootstrap::handleNativeCloseEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (message == nullptr
        || (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")) {
        return false;
    }

    auto* msg = static_cast<MSG*>(message);
    if (msg == nullptr) {
        return false;
    }
    if (windowsIdleEventMonitor_ != nullptr) {
        windowsIdleEventMonitor_->observeNativeMessage(eventType, message);
    }
    if (msg->hwnd == nullptr) {
        return false;
    }

    const bool isCloseMessage = msg->message == WM_CLOSE;
    const bool isCloseSysCommand = msg->message == WM_SYSCOMMAND && ((msg->wParam & 0xFFF0) == SC_CLOSE);
    if (!isCloseMessage && !isCloseSysCommand) {
        return false;
    }

    if (rootWindow_.isNull() || rootWindowNativeHwnd_ == 0) {
        return false;
    }

    const HWND rootHwnd = reinterpret_cast<HWND>(rootWindowNativeHwnd_);
    if (msg->hwnd != rootHwnd) {
        return false;
    }

    if (!rootWindowCloseRelayScheduled_
        && !rootWindowCloseRelayActive_
        && !UiDialogs::hasVisibleBlockingModalDialog()) {
        return false;
    }

    if (!rootWindowCloseRelayScheduled_
        && !rootWindowCloseRelayActive_
        && UiDialogs::hasVisibleBlockingModalDialog()) {
        scheduleRootWindowCloseRelay(
            isCloseSysCommand ? QStringLiteral("native_syscommand_close") : QStringLiteral("native_wm_close")
        );
    }
    if (result != nullptr) {
        *result = 0;
    }
    return true;
}
#endif

void QuickShellBootstrap::scheduleRootWindowCloseRelay(const QString& source)
{
    if (controller_ == nullptr || rootWindow_.isNull() || rootWindowCloseRelayScheduled_ || rootWindowCloseRelayActive_) {
        return;
    }
    rootWindowCloseRelayScheduled_ = true;
    appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_schedule"), QStringLiteral("source=%1").arg(source));
    QTimer::singleShot(0, this, [this]() {
        processRootWindowCloseRelay();
    });
}

void QuickShellBootstrap::processRootWindowCloseRelay()
{
    QElapsedTimer relayTimer;
    relayTimer.start();
    rootWindowCloseRelayScheduled_ = false;
    if (controller_ == nullptr || rootWindow_.isNull() || rootWindowCloseRelayActive_) {
        return;
    }

    rootWindowCloseRelayActive_ = true;
    appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_begin"));
    QElapsedTimer closeDialogsTimer;
    closeDialogsTimer.start();
    const int closedDialogCount = UiDialogs::closeVisibleBlockingModalDialogs();
    const bool dialogsRemainVisible = UiDialogs::hasVisibleBlockingModalDialog();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("close_blocking_modal_dialogs"),
        closeDialogsTimer.elapsed(),
        QStringLiteral("closed_count=%1 dialogs_remaining=%2")
            .arg(closedDialogCount)
            .arg(dialogsRemainVisible ? 1 : 0)
    );
    if (dialogsRemainVisible) {
        appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_retry"));
        rootWindowCloseRelayActive_ = false;
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("process_root_window_close_relay"),
            relayTimer.elapsed(),
            QStringLiteral("result=retry_dialogs_visible")
        );
        scheduleRootWindowCloseRelay(QStringLiteral("dialogs_still_visible"));
        return;
    }

    QElapsedTimer confirmCloseTimer;
    confirmCloseTimer.start();
    const bool confirmed = controller_->confirmClose();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("confirm_close"),
        confirmCloseTimer.elapsed(),
        QStringLiteral("confirmed=%1").arg(confirmed ? 1 : 0)
    );
    if (!confirmed) {
        appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_cancelled"));
        rootWindowCloseRelayActive_ = false;
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("process_root_window_close_relay"),
            relayTimer.elapsed(),
            QStringLiteral("result=cancelled")
        );
        return;
    }

    rootWindowCloseRelayActive_ = false;
    controller_->markNextCloseConfirmedExternally();
    QElapsedTimer rootCloseTimer;
    rootCloseTimer.start();
    const bool rootWindowClosed = rootWindow_->close();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("root_window_close"),
        rootCloseTimer.elapsed(),
        QStringLiteral("accepted=%1").arg(rootWindowClosed ? 1 : 0)
    );
    if (!rootWindowClosed) {
        controller_->clearPendingExternalCloseConfirmation();
    } else if (backend_ != nullptr) {
        QElapsedTimer previewShutdownTimer;
        previewShutdownTimer.start();
        backend_->preparePreviewForShutdown();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("prepare_preview_for_shutdown"),
            previewShutdownTimer.elapsed()
        );
        beginAcceptedRootWindowShutdown(QStringLiteral("root_close_relay"));
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("process_root_window_close_relay"),
        relayTimer.elapsed(),
        QStringLiteral("result=%1").arg(rootWindowClosed ? QStringLiteral("forwarded") : QStringLiteral("close_rejected"))
    );
}

void QuickShellBootstrap::beginAcceptedRootWindowShutdown(const QString& source)
{
    if (acceptedRootWindowShutdownStarted_) {
        appendQuickShellRuntimeLog(
            QStringLiteral("accepted_close_shutdown_skip"),
            QStringLiteral("source=%1 started=1").arg(source)
        );
        return;
    }
    acceptedRootWindowShutdownStarted_ = true;

    QElapsedTimer totalTimer;
    totalTimer.start();
    appendQuickShellRuntimeLog(QStringLiteral("accepted_close_shutdown_begin"), QStringLiteral("source=%1").arg(source));

    if (qApp != nullptr) {
        qApp->setQuitOnLastWindowClosed(false);
    }

#ifdef Q_OS_WIN
    if (windowsIdleEventMonitor_ != nullptr) {
        windowsIdleEventMonitor_->unregisterWindow();
    }
    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        if (nativeCloseEventFilter_ != nullptr) {
            app->removeNativeEventFilter(nativeCloseEventFilter_.get());
            nativeCloseEventFilter_.reset();
        }
    }
    rootWindowNativeHwnd_ = 0;
#endif

    if (!rootWindow_.isNull()) {
        rootWindow_->removeEventFilter(this);
    }
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }

    if (backend_ != nullptr) {
        QElapsedTimer previewShutdownTimer;
        previewShutdownTimer.start();
        backend_->preparePreviewForShutdown();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("accepted_close_prepare_preview_for_shutdown"),
            previewShutdownTimer.elapsed()
        );
    }
    if (backend_ != nullptr) {
        QElapsedTimer backendCloseTimer;
        backendCloseTimer.start();
        backend_->close();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("accepted_close_backend_close"),
            backendCloseTimer.elapsed()
        );
    }

    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("accepted_close_shutdown_prepare_destroy"),
        totalTimer.elapsed(),
        QStringLiteral("source=%1").arg(source)
    );
    appendQuickShellRuntimeLog(
        QStringLiteral("accepted_close_shutdown_prepare_destroy"),
        QStringLiteral("source=%1 elapsed_ms=%2").arg(source).arg(totalTimer.elapsed())
    );
    scheduleAcceptedRootWindowDestroyAndQuit(source);
}

void QuickShellBootstrap::scheduleAcceptedRootWindowDestroyAndQuit(const QString& source)
{
    if (acceptedRootWindowDestroyScheduled_ || acceptedRootWindowDestroyStarted_) {
        appendQuickShellRuntimeLog(
            QStringLiteral("accepted_close_destroy_skip"),
            QStringLiteral("source=%1 scheduled=%2 started=%3")
                .arg(source)
                .arg(acceptedRootWindowDestroyScheduled_ ? 1 : 0)
                .arg(acceptedRootWindowDestroyStarted_ ? 1 : 0)
        );
        return;
    }

    acceptedRootWindowDestroyScheduled_ = true;
    appendQuickShellRuntimeLog(QStringLiteral("accepted_close_destroy_schedule"), QStringLiteral("source=%1").arg(source));
    QTimer::singleShot(0, this, [this, source]() {
        destroyAcceptedRootWindowResourcesAndQuit(source);
    });
}

void QuickShellBootstrap::destroyAcceptedRootWindowResourcesAndQuit(const QString& source)
{
    if (acceptedRootWindowDestroyStarted_) {
        return;
    }
    acceptedRootWindowDestroyScheduled_ = false;
    acceptedRootWindowDestroyStarted_ = true;

    QElapsedTimer totalTimer;
    totalTimer.start();
    appendQuickShellRuntimeLog(QStringLiteral("accepted_close_destroy_begin"), QStringLiteral("source=%1").arg(source));

    if (!rootWindow_.isNull()) {
        rootWindow_->removeEventFilter(this);
    }
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }

    const auto logResetTiming = [](const QString& step, auto& pointer) {
        if (!pointer) {
            return;
        }
        // Bracket-log around the destructor so that if .reset() crashes, we can tell which
        // pointer was being torn down from the last logged "enter" line.
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("action=%1_enter").arg(step),
            true
        );
        QElapsedTimer timer;
        timer.start();
        pointer.reset();
        // beta20-fix2 — force-log the elapsed-time line too. Without
        // --debug the prior `appendTimingLine` (force=false default)
        // would silently drop, leaving the runtime log with `_enter`
        // lines but no `_exit`/elapsed lines, making it impossible to
        // tell from a non-debug crash dump whether a particular
        // destructor finished or hung. Force-true matches the `_enter`
        // line's logging level so brackets are symmetric in any mode.
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            step,
            timer.elapsed(),
            QString(),
            /*force=*/true
        );
    };

    logResetTiming(QStringLiteral("accepted_close_destroy_engine"), engine_);
    rootWindow_ = nullptr;
#ifdef Q_OS_WIN
    rootWindowNativeHwnd_ = 0;
#endif
    logResetTiming(QStringLiteral("accepted_close_destroy_style_bridge"), styleBridge_);
    logResetTiming(QStringLiteral("accepted_close_destroy_controller"), controller_);
    logResetTiming(QStringLiteral("accepted_close_destroy_surface_host"), surfaceHost_);
    logResetTiming(QStringLiteral("accepted_close_destroy_backend"), backend_);

    // beta20-fix2 — force-log so non-debug crash dumps can tell whether
    // we got to the very end of the close sequence or hung partway. If
    // this line is absent in a future log but `accepted_close_destroy_backend_enter`
    // is present, the crash is in `~MainWindow()` (i.e. backend.reset()).
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("accepted_close_destroy_and_quit"),
        totalTimer.elapsed(),
        QStringLiteral("source=%1").arg(source),
        /*force=*/true
    );
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell"),
        QStringLiteral("action=accepted_close_destroy_quit_request source=%1 elapsed_ms=%2")
            .arg(source).arg(totalTimer.elapsed()),
        /*force=*/true
    );
    QCoreApplication::quit();
}

bool QuickShellBootstrap::previewSeekHotRectContainsGlobalPoint(const QPoint& globalPoint) const
{
    if (engine_ == nullptr || engine_->rootObjects().isEmpty()) {
        return false;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
    if (window == nullptr || !window->isVisible()) {
        return false;
    }
    const QVariant rectVariant = window->property("previewSeekHotRect");
    if (!rectVariant.isValid()) {
        return false;
    }
    const QRectF hotRect = rectVariant.toRectF();
    if (!hotRect.isValid() || hotRect.isEmpty()) {
        return false;
    }
    const QPoint localPos = window->mapFromGlobal(globalPoint);
    return hotRect.contains(QPointF(localPos));
}

QuickShellController* QuickShellBootstrap::controller() const
{
    return controller_.get();
}

QuickShellStyleBridge* QuickShellBootstrap::styleBridge() const
{
    return styleBridge_.get();
}

bool QuickShellBootstrap::shouldTraceFocusObject(QObject* watched) const
{
    if (watched == nullptr) {
        return false;
    }

    if (auto* widget = qobject_cast<QWidget*>(watched); widget != nullptr) {
        const auto matchesOrDescends = [widget](QWidget* root) {
            return root != nullptr && (widget == root || root->isAncestorOf(widget));
        };
        if (surfaceHost_ != nullptr
            && (matchesOrDescends(surfaceHost_->topChromeSurfaceWidget())
                || matchesOrDescends(surfaceHost_->sidebarSurfaceWidget())
                || matchesOrDescends(surfaceHost_->workspaceSurfaceWidget())
                || matchesOrDescends(surfaceHost_->statusSurfaceWidget()))) {
            return true;
        }
        return qobject_cast<QTextEdit*>(widget) != nullptr
            || qobject_cast<QLineEdit*>(widget) != nullptr;
    }

    if (auto* window = qobject_cast<QWindow*>(watched); window != nullptr) {
        if (engine_ != nullptr && !engine_->rootObjects().isEmpty() && engine_->rootObjects().constFirst() == window) {
            return true;
        }
        if (qApp != nullptr && qApp->focusWindow() == window) {
            return true;
        }
    }

    return false;
}

QString QuickShellBootstrap::describeFocusObject(QObject* object) const
{
    if (object == nullptr) {
        return QStringLiteral("null");
    }

    QStringList parts;
    parts.append(QStringLiteral("class=%1").arg(object->metaObject() != nullptr ? object->metaObject()->className() : "unknown"));
    parts.append(QStringLiteral("ptr=%1").arg(pointerHex(object)));
    if (!object->objectName().isEmpty()) {
        parts.append(QStringLiteral("name=%1").arg(object->objectName()));
    }

    if (auto* widget = qobject_cast<QWidget*>(object); widget != nullptr) {
        parts.append(QStringLiteral("widget=1"));
        parts.append(QStringLiteral("visible=%1").arg(widget->isVisible() ? 1 : 0));
        parts.append(QStringLiteral("enabled=%1").arg(widget->isEnabled() ? 1 : 0));
        parts.append(QStringLiteral("focus=%1").arg(widget->hasFocus() ? 1 : 0));
        parts.append(QStringLiteral("active_win=%1").arg(widget->isActiveWindow() ? 1 : 0));
        parts.append(QStringLiteral("policy=%1").arg(focusPolicyName(widget->focusPolicy())));
        if (QWidget* parent = widget->parentWidget(); parent != nullptr) {
            parts.append(QStringLiteral("parent=%1").arg(pointerHex(parent)));
        }
        if (surfaceHost_ != nullptr) {
            const auto matchesOrDescends = [widget](QWidget* root) {
                return root != nullptr && (widget == root || root->isAncestorOf(widget));
            };
            parts.append(QStringLiteral("in_top=%1").arg(matchesOrDescends(surfaceHost_->topChromeSurfaceWidget()) ? 1 : 0));
            parts.append(QStringLiteral("in_sidebar=%1").arg(matchesOrDescends(surfaceHost_->sidebarSurfaceWidget()) ? 1 : 0));
            parts.append(QStringLiteral("in_workspace=%1").arg(matchesOrDescends(surfaceHost_->workspaceSurfaceWidget()) ? 1 : 0));
            parts.append(QStringLiteral("in_status=%1").arg(matchesOrDescends(surfaceHost_->statusSurfaceWidget()) ? 1 : 0));
        }
        if (auto* textEdit = qobject_cast<QTextEdit*>(widget); textEdit != nullptr) {
            const QTextCursor cursor = textEdit->textCursor();
            parts.append(QStringLiteral("cursor_anchor=%1").arg(cursor.anchor()));
            parts.append(QStringLiteral("cursor_pos=%1").arg(cursor.position()));
        }
        if (auto* lineEdit = qobject_cast<QLineEdit*>(widget); lineEdit != nullptr) {
            parts.append(QStringLiteral("text_len=%1").arg(lineEdit->text().size()));
        }
    }

    if (auto* window = qobject_cast<QWindow*>(object); window != nullptr) {
        parts.append(QStringLiteral("window=1"));
        parts.append(QStringLiteral("visible=%1").arg(window->isVisible() ? 1 : 0));
        parts.append(QStringLiteral("active=%1").arg(window->isActive() ? 1 : 0));
        parts.append(QStringLiteral("focus_obj=%1").arg(pointerHex(window->focusObject())));
    }

    return parts.join(' ');
}

QString QuickShellBootstrap::focusReasonName(Qt::FocusReason reason) const
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

void QuickShellBootstrap::logFocusEvent(const QString& action, QObject* watched, QEvent* event, const QString& detail) const
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }

    QString payload;
    if (event != nullptr) {
        payload += QStringLiteral("event_type=%1").arg(static_cast<int>(event->type()));
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            auto* focusEvent = static_cast<QFocusEvent*>(event);
            payload += QStringLiteral(" focus_reason=%1").arg(focusReasonName(focusEvent != nullptr ? focusEvent->reason() : Qt::NoFocusReason));
        }
        payload += QStringLiteral(" spontaneous=%1").arg(event->spontaneous() ? 1 : 0);
    }
    const QString watchedDescription = describeFocusObject(watched);
    const QString focusWidgetDescription =
        describeFocusObject(qobject_cast<QObject*>(QApplication::focusWidget()));
    const QString focusWindowDescription =
        describeFocusObject(qobject_cast<QObject*>(qApp != nullptr ? qApp->focusWindow() : nullptr));

    // `event_filter` is raw event tracing: it fires for every focus-related
    // event the filter sees, and in practice most of those repeat the previous
    // line's focus state exactly. Keep every real transition, drop the
    // repeats. The other actions (focus_changed_signal,
    // root_window_active_changed, application_state_changed) are genuine state
    // changes and are never suppressed.
    if (action == QLatin1String("event_filter")) {
        const QString signature = watchedDescription + QLatin1Char('|')
            + focusWidgetDescription + QLatin1Char('|') + focusWindowDescription;
        if (signature == lastFocusFilterSignature_) {
            ++suppressedFocusFilterCount_;
            return;
        }
        lastFocusFilterSignature_ = signature;
    } else {
        // A non-filter transition changes the state the filter signature
        // describes, so drop the memo rather than let it mask the next
        // event_filter line.
        lastFocusFilterSignature_.clear();
    }

    payload += QStringLiteral(" watched={%1}").arg(watchedDescription);
    payload += QStringLiteral(" app_focus_widget={%1}").arg(focusWidgetDescription);
    payload += QStringLiteral(" app_focus_window={%1}").arg(focusWindowDescription);
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" detail=%1").arg(detail.trimmed());
    }
    // Attach and reset the run of drops this line ends, so the collapsed volume is
    // still readable instead of erased. Omitted when zero, so an unthrottled stream
    // stays byte-identical to what it looked like before the dedup existed.
    if (suppressedFocusFilterCount_ > 0) {
        payload += QStringLiteral(" deduped=%1").arg(suppressedFocusFilterCount_);
        suppressedFocusFilterCount_ = 0;
    }
    appendQuickShellFocusLog(action, payload);
}
