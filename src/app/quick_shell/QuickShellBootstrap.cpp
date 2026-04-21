#include "QuickShellBootstrap.h"

#include "QuickShellNativeSurfaceHost.h"
#include "QuickShellController.h"
#include "QuickShellStyleBridge.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugOptions.h"
#include "common/DebugLog.h"
#include "mainwindow/MainWindow.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "timeline/quick/TimelineQuickItem.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
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

constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr DWORD kDwmwaTextColor = 36;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmwaMicaEffect = 1029;
constexpr int kDwmsbtNone = 1;
constexpr int kDwmsbtMainWindow = 2;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

bool setDwmWindowAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size)
{
    if (hwnd == nullptr || value == nullptr || size == 0) {
        return false;
    }
    static HMODULE dwmapiModule = ::LoadLibraryW(L"dwmapi.dll");
    if (dwmapiModule == nullptr) {
        return false;
    }
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        ::GetProcAddress(dwmapiModule, "DwmSetWindowAttribute")
    );
    if (setWindowAttribute == nullptr) {
        return false;
    }
    return SUCCEEDED(setWindowAttribute(hwnd, attribute, value, size));
}

COLORREF colorRefForDwm(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}

void applySystemBackdropToQuickWindow(QQuickWindow* window)
{
    if (window == nullptr) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr) {
        return;
    }

    const BOOL darkMode = UiTheme::isDarkTheme() ? TRUE : FALSE;
    setDwmWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode, sizeof(darkMode));

    if (UiText::preferredTheme() == UiText::ThemePreference::System) {
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
    } else {
        const UiTheme::Colors& themeColors = UiTheme::colors();
        const bool active = window->isActive();
        const COLORREF borderColor = colorRefForDwm(active ? themeColors.borderStrong : themeColors.borderSoft);
        const COLORREF captionColor = colorRefForDwm(active ? themeColors.toolbarBg : themeColors.windowAltBg);
        const COLORREF textColor = colorRefForDwm(active ? themeColors.textPrimary : themeColors.textSecondary);
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &borderColor, sizeof(borderColor));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &captionColor, sizeof(captionColor));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &textColor, sizeof(textColor));
    }

    const int backdropType = kDwmsbtMainWindow;
    if (!setDwmWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType))) {
        const BOOL micaEnabled = TRUE;
        setDwmWindowAttribute(hwnd, kDwmwaMicaEffect, &micaEnabled, sizeof(micaEnabled));
    }
}
#endif

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
}

QuickShellBootstrap::~QuickShellBootstrap()
{
    if (!rootWindow_.isNull()) {
        rootWindow_->removeEventFilter(this);
    }
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
#ifdef Q_OS_WIN
    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        if (nativeCloseEventFilter_ != nullptr) {
            app->removeNativeEventFilter(nativeCloseEventFilter_.get());
        }
    }
#endif
}

bool QuickShellBootstrap::start()
{
    appendQuickShellRuntimeLog(QStringLiteral("start_enter"));
    backend_ = std::make_unique<MainWindow>(true);
    backend_->setQuickShellBackendActive(true);
    backend_->hide();
    backend_->setVisible(false);
    appendQuickShellRuntimeLog(QStringLiteral("backend_ready"));
    surfaceHost_ = std::make_unique<QuickShellNativeSurfaceHost>(backend_.get(), backend_.get(), this);
    backend_->hide();
    backend_->setVisible(false);
    appendQuickShellRuntimeLog(QStringLiteral("surface_host_ready"));
    controller_ = std::make_unique<QuickShellController>(backend_.get(), backend_.get(), surfaceHost_.get(), this);
    appendQuickShellRuntimeLog(QStringLiteral("controller_ready"));
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
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
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
    engine_->load(mainUrl);
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
        window->installEventFilter(this);
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
        if (!appIcon_.isNull()) {
            window->setIcon(appIcon_);
        }
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
#ifdef Q_OS_WIN
            applySystemBackdropToQuickWindow(window);
            QObject::connect(window, &QQuickWindow::activeChanged, this, [window]() {
                applySystemBackdropToQuickWindow(window);
            });
            if (styleBridge_ != nullptr) {
                QObject::connect(styleBridge_.get(), &QuickShellStyleBridge::appearanceChanged, this, [window]() {
                    applySystemBackdropToQuickWindow(window);
                });
            }
            QObject::connect(qApp, &QGuiApplication::applicationStateChanged, this, [window](Qt::ApplicationState) {
                applySystemBackdropToQuickWindow(window);
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
            if (surfaceHost_ != nullptr) {
                surfaceHost_->noteQuickShellUiReady();
            }
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
        previewSeekArmed_ = previewSeekHotRectContainsGlobalPoint(globalPos);
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
            if (isArrow) {
                if (event->type() == QEvent::ShortcutOverride) {
                    keyEvent->accept();
                    return true;
                }
                const int direction = keyEvent->key() == Qt::Key_Left ? -1 : 1;
                if (event->type() == QEvent::KeyPress) {
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
    if (rootWindow_.isNull()
        || message == nullptr
        || (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")) {
        return false;
    }

    auto* msg = static_cast<MSG*>(message);
    if (msg == nullptr || msg->hwnd == nullptr) {
        return false;
    }

    const HWND rootHwnd = reinterpret_cast<HWND>(rootWindow_->winId());
    if (msg->hwnd != rootHwnd) {
        return false;
    }

    const bool isCloseMessage = msg->message == WM_CLOSE;
    const bool isCloseSysCommand = msg->message == WM_SYSCOMMAND && ((msg->wParam & 0xFFF0) == SC_CLOSE);
    if (!isCloseMessage && !isCloseSysCommand) {
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
    rootWindowCloseRelayScheduled_ = false;
    if (controller_ == nullptr || rootWindow_.isNull() || rootWindowCloseRelayActive_) {
        return;
    }

    rootWindowCloseRelayActive_ = true;
    appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_begin"));
    UiDialogs::closeVisibleBlockingModalDialogs();
    if (UiDialogs::hasVisibleBlockingModalDialog()) {
        appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_retry"));
        rootWindowCloseRelayActive_ = false;
        scheduleRootWindowCloseRelay(QStringLiteral("dialogs_still_visible"));
        return;
    }

    const bool confirmed = controller_->confirmClose();
    if (!confirmed) {
        appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_cancelled"));
        rootWindowCloseRelayActive_ = false;
        return;
    }

    rootWindowCloseRelayActive_ = false;
    controller_->markNextCloseConfirmedExternally();
    if (!rootWindow_->close()) {
        controller_->clearPendingExternalCloseConfirmation();
    }
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
    payload += QStringLiteral(" watched={%1}").arg(describeFocusObject(watched));
    payload += QStringLiteral(" app_focus_widget={%1}").arg(describeFocusObject(qobject_cast<QObject*>(QApplication::focusWidget())));
    payload += QStringLiteral(" app_focus_window={%1}").arg(describeFocusObject(qobject_cast<QObject*>(qApp != nullptr ? qApp->focusWindow() : nullptr)));
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" detail=%1").arg(detail.trimmed());
    }
    appendQuickShellFocusLog(action, payload);
}
