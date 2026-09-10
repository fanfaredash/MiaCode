#include "Bootstrap.h"

#include "ApplicationContext.h"
#include "preview/NoteImageProvider.h"
#include "export/CoverExportWindow.h"
#include "chrome/PlatformChrome.h"
#include "chrome/WindowChrome.h"
#include "MainEntrypoints.h"
#include "runtime/Session.h"
#include "app/services/ApplicationServices.h"
#include "chrome/NativeWindowTheme.h"
#include "ui/preferences/LocaleService.h"
#include "drop/ChartDropBridge.h"
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


namespace miacode::ui {
namespace {

void ensurePreviewQuickTypesRegistered()
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

void appendUiRuntimeLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui"),
        text);
}

} // namespace

Bootstrap::Bootstrap(const QIcon& appIcon, QObject* parent)
    : QObject(parent)
    , appIcon_(appIcon)
{
}

Bootstrap::~Bootstrap()
{
    delete coverWindow_.data();
    releaseRootWindowResources();
    engine_.reset();
    windowChrome_.reset();
    applicationContext_.reset();
    backend_.reset();
    // Last: the services outlive everything that borrows them.
    applicationServices_.reset();
}

bool Bootstrap::start(const QString& startupOpenTarget)
{
    miacode::oplog::appendStartupBeaconLine("ui/start_enter");
    appendUiRuntimeLog(QStringLiteral("start_enter"));
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    applicationServices_ = std::make_unique<miacode::ApplicationServices>();
    backend_ = std::make_unique<Session>(*applicationServices_);
    backend_->setBackendActive(true);
    appendUiRuntimeLog(QStringLiteral("backend_ready"));

    applicationContext_ = std::make_unique<ApplicationContext>(*applicationServices_, this);
    QObject::connect(static_cast<PageHost*>(applicationContext_->pages()),
        &PageHost::coverWindowRequested, this, &Bootstrap::openCoverExportWindow);
    QObject::connect(
        static_cast<miacode::ui::ShellLifecycle*>(applicationContext_->shell()),
        &miacode::ui::ShellLifecycle::rootCloseAccepted,
        this,
        [this](const QString& source) {
            beginAcceptedRootWindowShutdown(source);
        });
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    miacode::LocaleService::instance().setQmlEngine(engine_.get());
    engine_->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    registerNoteImageProvider(
        engine_.get(), static_cast<PreviewModel*>(applicationContext_->preview()));
    ensurePreviewQuickTypesRegistered();

    windowChrome_ = std::make_unique<WindowChrome>(this);
    applicationContext_->setWindowChrome(windowChrome_.get());

    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::warnings,
        this,
        [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
                appendUiRuntimeLog(QStringLiteral("qml_warning"), warning.toString());
                QTextStream(stderr) << "[QmlUi QML] " << warning.toString() << '\n';
            }
            QTextStream(stderr).flush();
        });
    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::objectCreationFailed,
        this,
        [](const QUrl& url) {
            appendUiRuntimeLog(QStringLiteral("qml_object_creation_failed"), url.toString());
            QTextStream(stderr) << "[QmlUi QML] object creation failed for " << url.toString() << '\n';
            QTextStream(stderr).flush();
        });


    engine_->setInitialProperties({
        {QStringLiteral("applicationContext"),
         QVariant::fromValue(static_cast<QObject*>(applicationContext_.get()))},
    });

    appendUiRuntimeLog(QStringLiteral("load_begin"), QStringLiteral("MiaCode.UI/Main"));
    miacode::oplog::appendStartupBeaconLine("ui/before_qml_load");
    engine_->loadFromModule(QStringLiteral("MiaCode.UI"), QStringLiteral("Main"));
    miacode::oplog::appendStartupBeaconLine("ui/after_qml_load");

    if (engine_->rootObjects().isEmpty()) {
        appendUiRuntimeLog(QStringLiteral("load_failed"));
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
        chartDropBridge_ = std::make_unique<miacode::ui::ChartDropBridge>(
            *window,
            [window]() {
                if (QQuickItem* contentItem = window->contentItem(); contentItem != nullptr) {
                    contentItem->setFlag(QQuickItem::ItemAcceptsDrops, true);
                }
            },
            [this](const QStringList& paths, quint64 requestId, quint64 generation,
                   std::function<void(const miacode::ui::ChartDropResult&)> done) {
                applicationServices_->documentBridge()->importDroppedAudio(
                    paths,
                    requestId,
                    generation,
                    [done = std::move(done)](const miacode::ChartDropImportResult& result) mutable {
                        if (done) {
                            done({result.requestId, result.generation, result.accepted,
                                  result.completed, result.cancelled, result.createdCount,
                                  result.failedCount, result.targetPath});
                        }
                    });
            },
            [](const miacode::ui::ChartDropResult&) {},
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
        miacode::app::entry::logQuickWindowGpuDevice(
            window, QStringLiteral("qml_ui_root_window"));

        auto* platform = qobject_cast<PlatformChrome*>(applicationContext_->platform());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        // Timing comes from applicationContext.platform (hide before / attach after show).
        if (platform != nullptr && platform->hideBeforeChromeAttach()) {
            window->setVisible(false);
            windowChrome_->attach(window);
            appendUiRuntimeLog(QStringLiteral("window_chrome_attached"));
        }
#endif
        NativeWindowTheme::applyToWindow(window);
        // The native frame is applied, not bound: without re-applying it the
        // titlebar keeps the palette it was born with while every QML surface
        // and the QSG timeline follow the new theme. Same call, repeated.
        if (auto* settings = qobject_cast<WorkbenchSettings*>(applicationContext_->preferences());
            settings != nullptr) {
            QObject::connect(settings, &WorkbenchSettings::themeChanged, this, [this]() {
                if (rootWindow_ != nullptr) {
                    NativeWindowTheme::applyToWindow(rootWindow_);
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
            appendUiRuntimeLog(QStringLiteral("window_chrome_attached"));
        }
#endif

        // The stage-media route defers its first chart-path load until the
        // frontend window is ready. UIv2 has no native surface host to forward
        // that readiness notification, so release the shared backend gate here
        // after the QML root window has been created and shown.
        backend_->noteRootWindowReady();
    }

    if (!startupOpenTarget.trimmed().isEmpty() && applicationContext_ != nullptr) {
        auto* document = qobject_cast<DocumentModel*>(applicationContext_->document());
        if (document != nullptr) {
            document->openFile(QUrl::fromLocalFile(startupOpenTarget.trimmed()));
        }
    }

    appendUiRuntimeLog(QStringLiteral("start_ok"));
    miacode::oplog::appendStartupBeaconLine("ui/start_ok");
    return true;
}

void Bootstrap::openCoverExportWindow(int difficultyId)
{
    if (coverWindow_) {
        coverWindow_->raise();
        return;
    }
    if (rootWindow_.isNull() || applicationServices_->exportEngine() == nullptr) {
        return;
    }
    auto* preferences = static_cast<WorkbenchSettings*>(applicationContext_->preferences());
    auto* window = new CoverExportWindow(*applicationServices_->exportEngine(),
        applicationServices_->uiRequests(),
        applicationServices_->playbackControlSlot(),
        *preferences, appIcon_, this);
    coverWindow_ = window;
    if (!window->show(rootWindow_, difficultyId)) {
        delete window;
        applicationServices_->uiRequests().postNotice(miacode::NoticeSeverity::Error,
            qtTrId("cover.export_cover"),
            qtTrId("cover.cover_export_failed_1")
                .arg(QStringLiteral("Failed to create the cover export window.")));
    }
}

void Bootstrap::beginAcceptedRootWindowShutdown(const QString& source)
{
    if (acceptedRootWindowShutdownStarted_) {
        return;
    }
    acceptedRootWindowShutdownStarted_ = true;
    appendUiRuntimeLog(QStringLiteral("shutdown_begin"), source);

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

void Bootstrap::destroyAcceptedRootWindowResourcesAndQuit(const QString& source)
{
    if (acceptedRootWindowDestroyStarted_) {
        return;
    }
    // Capture/export can be inside a nested event loop. Keep the application
    // services alive until the independent window finishes and destroys itself.
    if (coverWindow_) {
        QObject::connect(coverWindow_, &QObject::destroyed, this, [this, source] {
            destroyAcceptedRootWindowResourcesAndQuit(source);
        }, Qt::QueuedConnection);
        coverWindow_->close();
        return;
    }
    acceptedRootWindowDestroyStarted_ = true;
    appendUiRuntimeLog(QStringLiteral("shutdown_destroy"), source);

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

void Bootstrap::releaseRootWindowResources()
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

} // namespace miacode::ui
