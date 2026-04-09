#include "QuickShellBootstrap.h"

#include "QuickShellController.h"
#include "QuickShellStyleBridge.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "mainwindow/MainWindow.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>
#include <QEventLoop>
#include <QtQml>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

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

#ifdef Q_OS_WIN
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
    registered = true;
}

}  // namespace

QuickShellBootstrap::QuickShellBootstrap(const QIcon& appIcon, QObject* parent)
    : QObject(parent)
    , appIcon_(appIcon)
{
}

QuickShellBootstrap::~QuickShellBootstrap() = default;

bool QuickShellBootstrap::start()
{
    appendQuickShellRuntimeLog(QStringLiteral("start_enter"));
    backend_ = std::make_unique<MainWindow>(MainWindow::FrontendHostMode::QuickShellBackend);
    appendQuickShellRuntimeLog(QStringLiteral("backend_ready"));
    controller_ = std::make_unique<QuickShellController>(backend_.get(), this);
    appendQuickShellRuntimeLog(QStringLiteral("controller_ready"));
    styleBridge_ = std::make_unique<QuickShellStyleBridge>(backend_.get(), this);
    appendQuickShellRuntimeLog(QStringLiteral("style_bridge_ready"));
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    appendQuickShellRuntimeLog(QStringLiteral("engine_ready"));
    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }
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
        backend_.reset();
        return false;
    }

    if (!appIcon_.isNull()) {
        if (QQuickWindow* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst()); window != nullptr) {
            appendQuickShellRuntimeLog(
                QStringLiteral("root_window_ready"),
                QString("visible=%1 width=%2 height=%3 title=%4")
                    .arg(window->isVisible() ? 1 : 0)
                    .arg(window->width())
                    .arg(window->height())
                    .arg(window->title())
            );
            if (styleBridge_ != nullptr && window->width() > 0 && window->height() > 0) {
                styleBridge_->syncWindowSize(window->width(), window->height());
                styleBridge_->refreshNow();
            } else if (styleBridge_ != nullptr) {
                styleBridge_->refreshNow();
            }
            if (controller_ != nullptr) {
                controller_->refresh();
            }
            const auto warmupStartupSurfaceLayout = [this, window]() {
                if (backend_ == nullptr || controller_ == nullptr || styleBridge_ == nullptr || window == nullptr) {
                    return;
                }
                const QVariantMap metrics = styleBridge_->metrics();
                const int topChromeHeight = qMax(0, metrics.value(QStringLiteral("topChromeHeight")).toInt());
                const int statusHeight = qMax(0, metrics.value(QStringLiteral("statusHeight")).toInt());
                const int handleWidth = qMax(0, metrics.value(QStringLiteral("previewSplitterHandleWidth")).toInt());
                const int previewPaneWidth = qMax(0, qRound(window->property("previewPaneWidth").toReal()));
                const int workspaceHeight = qMax(1, window->height() - topChromeHeight - statusHeight);
                const int workspaceWidth = qMax(1, window->width() - handleWidth - previewPaneWidth);
                controller_->syncWorkspaceSurfaceSize(workspaceWidth, workspaceHeight);

                const int previewControlsWidth = qMax(1, previewPaneWidth - 16);
                int previewControlsHeight = qMax(180, metrics.value(QStringLiteral("previewControlsHeight"), 180).toInt());
                if (backend_->previewControlCard_ != nullptr) {
                    const int controlCardHeight = qMax(
                        backend_->previewControlCard_->minimumSizeHint().height(),
                        backend_->previewControlCard_->sizeHint().height()
                    );
                    previewControlsHeight = qMax(
                        previewControlsHeight,
                        controlCardHeight + backend_->previewStatsMinimumHeightForPanelWidth(previewPaneWidth) + 34
                    );
                }
                controller_->syncPreviewControlsSurfaceSize(previewControlsWidth, previewControlsHeight);
                styleBridge_->refreshNow();
                controller_->refresh();
            };
            for (int iteration = 0; iteration < 3; ++iteration) {
                warmupStartupSurfaceLayout();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                if (styleBridge_ != nullptr) {
                    styleBridge_->refreshNow();
                }
                if (controller_ != nullptr) {
                    controller_->refresh();
                }
            }
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
            window->setIcon(appIcon_);
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
            window->setVisible(true);
            window->show();
            window->raise();
            window->requestActivate();
            QTimer::singleShot(0, this, [this, window]() {
                appendQuickShellRuntimeLog(
                    QStringLiteral("root_window_post_show"),
                    QString("visible=%1 exposed=%2 active=%3 width=%4 height=%5")
                        .arg(window->isVisible() ? 1 : 0)
                        .arg(window->isExposed() ? 1 : 0)
                        .arg(window->isActive() ? 1 : 0)
                        .arg(window->width())
                        .arg(window->height())
                );
                if (backend_ != nullptr) {
                    backend_->noteQuickShellStartupUiReady();
                }
            });
        }
    }
    return true;
}

bool QuickShellBootstrap::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (controller_ == nullptr || !controller_->previewFullscreen() || event == nullptr) {
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

QuickShellController* QuickShellBootstrap::controller() const
{
    return controller_.get();
}

QuickShellStyleBridge* QuickShellBootstrap::styleBridge() const
{
    return styleBridge_.get();
}
