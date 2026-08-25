#include "QmlUiBootstrap.h"

#include "QmlApplicationContext.h"
#include "QmlNoteImageProvider.h"
#include "QmlUiPlatformChrome.h"
#include "QmlUiWindowChrome.h"
#include "MainEntrypoints.h"
#include "mainwindow/MainWindow.h"
#include "QuickShellController.h"
#include "UiNativeWindowTheme.h"
#include "ui/ChartDropOverlay.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "timeline/quick/TimelineQuickItem.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>
#include <QtQml>

namespace {

void ensurePreviewQuickTypesRegisteredForQmlUi()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    qmlRegisterType<PreviewQuickSceneRoot>("MiaCode.Preview", 1, 0, "PreviewQuickSceneRoot");
    qmlRegisterType<PreviewQuickHudLayer>("MiaCode.Preview", 1, 0, "PreviewQuickHudLayer");
    qmlRegisterType<TimelineQuickItem>("MiaCode.Timeline", 1, 0, "TimelineQuickItem");
}

void appendQmlUiRuntimeLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("qml_ui"),
        text);
}

} // namespace

QmlUiBootstrap::QmlUiBootstrap(const QIcon& appIcon, QObject* parent)
    : QObject(parent)
    , appIcon_(appIcon)
{
}

QmlUiBootstrap::~QmlUiBootstrap()
{
    releaseRootWindowResources();
    engine_.reset();
    windowChrome_.reset();
    applicationContext_.reset();
    controller_.reset();
    backend_.reset();
}

bool QmlUiBootstrap::start(const QString& startupOpenTarget)
{
    miacode::oplog::appendStartupBeaconLine("qml_ui/start_enter");
    appendQmlUiRuntimeLog(QStringLiteral("start_enter"));

    backend_ = std::make_unique<MainWindow>(true);
    backend_->setQuickShellBackendActive(true);
    backend_->setQmlExportCenterActive(true);
    backend_->hide();
    backend_->setVisible(false);
    appendQmlUiRuntimeLog(QStringLiteral("backend_ready"));

    controller_ = std::make_unique<QuickShellController>(
        backend_.get(), backend_.get(), this);
    QObject::connect(
        controller_.get(),
        &QuickShellController::rootCloseAccepted,
        this,
        [this](const QString& source) {
            beginAcceptedRootWindowShutdown(source);
        });

    applicationContext_ = std::make_unique<QmlApplicationContext>(
        *backend_, *controller_, this);
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    engine_->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    registerQmlNoteImageProvider(
        engine_.get(), static_cast<QmlPreviewModel*>(applicationContext_->preview()));
    ensurePreviewQuickTypesRegisteredForQmlUi();

    windowChrome_ = std::make_unique<QmlUiWindowChrome>(this);
    applicationContext_->setWindowChrome(windowChrome_.get());

    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::warnings,
        this,
        [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
                appendQmlUiRuntimeLog(QStringLiteral("qml_warning"), warning.toString());
                QTextStream(stderr) << "[QmlUi QML] " << warning.toString() << '\n';
            }
            QTextStream(stderr).flush();
        });
    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::objectCreationFailed,
        this,
        [](const QUrl& url) {
            appendQmlUiRuntimeLog(QStringLiteral("qml_object_creation_failed"), url.toString());
            QTextStream(stderr) << "[QmlUi QML] object creation failed for " << url.toString() << '\n';
            QTextStream(stderr).flush();
        });

    qmlRegisterUncreatableType<QuickShellController>(
        "MiaCode.QuickShell",
        1,
        0,
        "QuickShellController",
        "Quick shell controller is provided by bootstrap.");

    engine_->setInitialProperties({
        {QStringLiteral("applicationContext"),
         QVariant::fromValue(static_cast<QObject*>(applicationContext_.get()))},
    });

    appendQmlUiRuntimeLog(QStringLiteral("load_begin"), QStringLiteral("MiaCode.UI/Main"));
    miacode::oplog::appendStartupBeaconLine("qml_ui/before_qml_load");
    engine_->loadFromModule(QStringLiteral("MiaCode.UI"), QStringLiteral("Main"));
    miacode::oplog::appendStartupBeaconLine("qml_ui/after_qml_load");

    if (engine_->rootObjects().isEmpty()) {
        appendQmlUiRuntimeLog(QStringLiteral("load_failed"));
        QTextStream(stderr) << "[QmlUi QML] no root object created for MiaCode.UI/Main\n";
        QTextStream(stderr).flush();
        releaseRootWindowResources();
        engine_.reset();
        applicationContext_.reset();
        controller_.reset();
        backend_.reset();
        return false;
    }

    if (QQuickWindow* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
        window != nullptr) {
        rootWindow_ = window;
        // Keep UIv2 on the same drag/drop ownership chain as QuickShell: the
        // hidden MainWindow owns filtering, supported-audio selection, and the
        // deferred handleAudioDrop() call. Register before creating the native
        // overlay so dialog transient-parent selection has a live root.
        if (!rootLifecycle_.registerRoot()) {
            releaseRootWindowResources();
            return false;
        }
        backend_->setQuickShellRootWindow(window);
        if (QQuickItem* rootItem = window->contentItem(); rootItem != nullptr) {
            rootItem->setFlag(QQuickItem::ItemAcceptsDrops, true);
        }
        window->installEventFilter(backend_.get());
        if (!rootLifecycle_.installRootEventFilter()) {
            releaseRootWindowResources();
            return false;
        }
        chartDropOverlay_ = std::make_unique<ChartDropOverlay>();
        chartDropOverlay_->installEventFilter(backend_.get());
        if (!rootLifecycle_.createChartDropOverlay()) {
            releaseRootWindowResources();
            return false;
        }
        chartDropOverlayMonitorTimer_ = std::make_unique<QTimer>();
        chartDropOverlayMonitorTimer_->setInterval(16);
        chartDropOverlayMonitorTimer_->setTimerType(Qt::PreciseTimer);
        QObject::connect(chartDropOverlayMonitorTimer_.get(), &QTimer::timeout, this, [this]() {
            syncChartDropOverlay();
        });
        QObject::connect(
            backend_.get(),
            &MainWindow::chartDropOverlayVisibleChanged,
            this,
            [this](bool visible) {
                rootLifecycle_.setChartDropOverlayVisible(visible);
                if (chartDropOverlay_ == nullptr) {
                    if (chartDropOverlayMonitorTimer_ != nullptr) {
                        chartDropOverlayMonitorTimer_->stop();
                    }
                    return;
                }
                if (!rootLifecycle_.shouldMonitorChartDropOverlay()) {
                    chartDropOverlay_->hideOverlay();
                    if (chartDropOverlayMonitorTimer_ != nullptr) {
                        chartDropOverlayMonitorTimer_->stop();
                    }
                    return;
                }
                syncChartDropOverlay();
                chartDropOverlayMonitorTimer_->start();
            });
        QObject::connect(window, &QObject::destroyed, this, [this]() {
            // QObject destruction can arrive outside the accepted-close path;
            // never touch the dying QQuickWindow while releasing its overlay.
            rootWindow_ = nullptr;
            releaseRootWindowResources();
        });
        backend_->shellSetRootWindowFrameGeometry(window->frameGeometry());
        if (!appIcon_.isNull()) {
            window->setIcon(appIcon_);
        }
        miacode::app::entry::bindHighPerformanceQuickGraphicsDevice(
            window, QStringLiteral("qml_ui_root_window"), /*preferVideoShareDevice=*/false);

        auto* platform = qobject_cast<QmlUiPlatformChrome*>(applicationContext_->platform());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        // Timing comes from applicationContext.platform (hide before / attach after show).
        if (platform != nullptr && platform->hideBeforeChromeAttach()) {
            window->setVisible(false);
            windowChrome_->attach(window);
            appendQmlUiRuntimeLog(QStringLiteral("window_chrome_attached"));
        }
#endif
        UiNativeWindowTheme::applyToWindow(window);
        if (!rootLifecycle_.canShowRoot()) {
            releaseRootWindowResources();
            return false;
        }
        window->show();
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        if (platform != nullptr && platform->attachChromeAfterShow()) {
            windowChrome_->attach(window);
            appendQmlUiRuntimeLog(QStringLiteral("window_chrome_attached"));
        }
#endif

        // The stage-media route defers its first chart-path load until the
        // frontend window is ready. UIv2 has no native surface host to forward
        // that readiness notification, so release the shared backend gate here
        // after the QML root window has been created and shown.
        backend_->shellNoteQuickUiReady();
    }

    if (!startupOpenTarget.trimmed().isEmpty() && backend_ != nullptr) {
        backend_->openStartupTarget(startupOpenTarget.trimmed());
    }

    if (showWelcomeDialogOnStartup_ && backend_ != nullptr) {
        QTimer::singleShot(0, backend_.get(), [backend = backend_.get()]() {
            if (backend != nullptr) {
                backend->showWelcomeDialog();
            }
        });
    }

    appendQmlUiRuntimeLog(QStringLiteral("start_ok"));
    miacode::oplog::appendStartupBeaconLine("qml_ui/start_ok");
    return true;
}

void QmlUiBootstrap::beginAcceptedRootWindowShutdown(const QString& source)
{
    if (acceptedRootWindowShutdownStarted_) {
        return;
    }
    acceptedRootWindowShutdownStarted_ = true;
    appendQmlUiRuntimeLog(QStringLiteral("shutdown_begin"), source);

    if (qApp != nullptr) {
        qApp->setQuitOnLastWindowClosed(false);
    }
    if (!rootWindow_.isNull()) {
        rootWindow_->hide();
    }
    if (backend_ != nullptr) {
        backend_->cancelChartAudioDrop();
        backend_->preparePreviewForShutdown();
    }

    QTimer::singleShot(0, this, [this, source]() {
        destroyAcceptedRootWindowResourcesAndQuit(source);
    });
}

void QmlUiBootstrap::destroyAcceptedRootWindowResourcesAndQuit(const QString& source)
{
    if (acceptedRootWindowDestroyStarted_) {
        return;
    }
    acceptedRootWindowDestroyStarted_ = true;
    appendQmlUiRuntimeLog(QStringLiteral("shutdown_destroy"), source);

    releaseRootWindowResources();
    engine_.reset();
    windowChrome_.reset();
    applicationContext_.reset();
    controller_.reset();
    backend_.reset();

    if (qApp != nullptr) {
        qApp->quit();
    }
}

void QmlUiBootstrap::releaseRootWindowResources()
{
    if (!rootLifecycle_.beginRelease()) {
        return;
    }
    if (chartDropOverlayMonitorTimer_ != nullptr) {
        chartDropOverlayMonitorTimer_->stop();
    }
    if (backend_ != nullptr) {
        backend_->cancelChartAudioDrop();
    }
    if (!rootWindow_.isNull() && backend_ != nullptr) {
        rootWindow_->removeEventFilter(backend_.get());
    }
    if (chartDropOverlay_ != nullptr) {
        if (backend_ != nullptr) {
            chartDropOverlay_->removeEventFilter(backend_.get());
        }
        chartDropOverlay_->clearTransientParent();
    }
    if (backend_ != nullptr) {
        backend_->setQuickShellRootWindow(nullptr);
    }
    chartDropOverlayMonitorTimer_.reset();
    chartDropOverlay_.reset();
    rootWindow_ = nullptr;
}

void QmlUiBootstrap::syncChartDropOverlay()
{
    if (chartDropOverlay_ == nullptr || rootWindow_.isNull()) {
        return;
    }
    if (backend_ != nullptr) {
        backend_->shellSetRootWindowFrameGeometry(rootWindow_->frameGeometry());
    }
    if (!rootLifecycle_.shouldMonitorChartDropOverlay()) {
        return;
    }
    chartDropOverlay_->showForWindow(rootWindow_);
}
