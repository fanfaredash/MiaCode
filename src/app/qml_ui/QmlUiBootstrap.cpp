#include "QmlUiBootstrap.h"

#include "QmlApplicationContext.h"
#include "QmlNoteImageProvider.h"
#include "export/QmlCoverExportSession.h"
#include "QmlUiPlatformChrome.h"
#include "QmlUiWindowChrome.h"
#include "MainEntrypoints.h"
#include "runtime/Session.h"
#include "app/v2/ApplicationServices.h"
#include "UiNativeWindowTheme.h"
#include "drop/QmlChartDropBridge.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "timeline/quick/TimelineQuickItem.h"
#include "tools/cover_export/CoverCompositeRenderer.h"

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
    backend_.reset();
    // Last: the services outlive everything that borrows them.
    applicationServices_.reset();
}

bool QmlUiBootstrap::start(const QString& startupOpenTarget)
{
    miacode::oplog::appendStartupBeaconLine("qml_ui/start_enter");
    appendQmlUiRuntimeLog(QStringLiteral("start_enter"));
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    applicationServices_ = std::make_unique<miacode::v2::ApplicationServices>();
    backend_ = std::make_unique<Session>(*applicationServices_);
    backend_->setBackendActive(true);
    appendQmlUiRuntimeLog(QStringLiteral("backend_ready"));

    applicationContext_ = std::make_unique<QmlApplicationContext>(*applicationServices_, this);
    QObject::connect(
        static_cast<miacode::qml_ui::QmlShellLifecycle*>(applicationContext_->shell()),
        &miacode::qml_ui::QmlShellLifecycle::rootCloseAccepted,
        this,
        [this](const QString& source) {
            beginAcceptedRootWindowShutdown(source);
        });
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    engine_->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    registerQmlNoteImageProvider(
        engine_.get(), static_cast<QmlPreviewModel*>(applicationContext_->preview()));
    // CoverExportPage shows chart-frame layers through image://coverchart/<key>.
    // The off-screen export renderer registers this provider on its own private
    // engine; without the same registration here the live page renders every
    // chart frame blank while the exported image still contains it.
    miacode::cover_export::registerCoverChartImageProvider(
        engine_.get(),
        static_cast<QmlCoverExportSession*>(applicationContext_->coverExport())->coverLayout());
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
            backend_.reset();
        return false;
    }

    if (QQuickWindow* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
        window != nullptr) {
        rootWindow_ = window;
        // The QML root owns the visual drop surface. The bridge is only a
        // window-level event adapter and the sole owner of the OS drag route.
        if (!rootLifecycle_.registerRoot()) {
            releaseRootWindowResources();
            return false;
        }
        backend_->attachRootWindow(window);
        if (QQuickItem* rootItem = window->contentItem(); rootItem != nullptr) {
            rootItem->setFlag(QQuickItem::ItemAcceptsDrops, true);
        }
        if (!rootLifecycle_.installRootEventFilter()) {
            releaseRootWindowResources();
            return false;
        }
        chartDropBridge_ = std::make_unique<miacode::qml_ui::QmlChartDropBridge>(
            *window,
            [window]() {
                if (QQuickItem* contentItem = window->contentItem(); contentItem != nullptr) {
                    contentItem->setFlag(QQuickItem::ItemAcceptsDrops, true);
                }
            },
            [this](const QStringList& paths, quint64 requestId, quint64 generation,
                   std::function<void(const miacode::qml_ui::QmlChartDropResult&)> done) {
                applicationServices_->documentBridge()->importDroppedAudio(
                    paths,
                    requestId,
                    generation,
                    [done = std::move(done)](const miacode::v2::ChartDropImportResult& result) mutable {
                        if (done) {
                            done({result.requestId, result.generation, result.accepted,
                                  result.completed, result.cancelled, result.createdCount,
                                  result.failedCount, result.targetPath});
                        }
                    });
            },
            [](const miacode::qml_ui::QmlChartDropResult&) {},
            this);
        applicationContext_->setChartDropBridge(chartDropBridge_.get());
        if (!rootLifecycle_.installDropBridge()) {
            releaseRootWindowResources();
            return false;
        }
        QObject::connect(window, &QObject::destroyed, this, [this]() {
            // QObject destruction can arrive outside the accepted-close path;
            // never touch the dying QQuickWindow while releasing its overlay.
            rootWindow_ = nullptr;
            releaseRootWindowResources();
        });
        backend_->setRootWindowFrameGeometry(window->frameGeometry());
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
        // The native frame is applied, not bound: without re-applying it the
        // titlebar keeps the palette it was born with while every QML surface
        // and the QSG timeline follow the new theme. Same call, repeated.
        if (auto* settings = qobject_cast<QmlUiSettings*>(applicationContext_->preferences());
            settings != nullptr) {
            QObject::connect(settings, &QmlUiSettings::themeChanged, this, [this]() {
                if (rootWindow_ != nullptr) {
                    UiNativeWindowTheme::applyToWindow(rootWindow_);
                }
            });
        }
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
        backend_->noteRootWindowReady();
    }

    if (!startupOpenTarget.trimmed().isEmpty() && applicationContext_ != nullptr) {
        auto* document = qobject_cast<QmlDocumentModel*>(applicationContext_->document());
        if (document != nullptr) {
            document->openFile(QUrl::fromLocalFile(startupOpenTarget.trimmed()));
        }
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
    if (applicationServices_ != nullptr && applicationServices_->documentBridge() != nullptr
        && applicationServices_->previewSurface() != nullptr) {
        applicationServices_->documentBridge()->releaseChartDropImport();
        applicationServices_->previewSurface()->prepareForShutdown();
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
    backend_.reset();
    // Last: the services outlive everything that borrows them.
    applicationServices_.reset();

    if (qApp != nullptr) {
        qApp->quit();
    }
}

void QmlUiBootstrap::releaseRootWindowResources()
{
    if (!rootLifecycle_.beginRelease()) {
        return;
    }
    if (applicationServices_ != nullptr
        && applicationServices_->documentBridge() != nullptr) {
        applicationServices_->documentBridge()->releaseChartDropImport();
    }
    if (chartDropBridge_ != nullptr) {
        chartDropBridge_->release();
    }
    if (backend_ != nullptr) {
        backend_->attachRootWindow(nullptr);
    }
    if (applicationContext_ != nullptr) {
        applicationContext_->setChartDropBridge(nullptr);
    }
    chartDropBridge_.reset();
    rootWindow_ = nullptr;
}
