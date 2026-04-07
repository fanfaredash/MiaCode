#include "QuickShellBootstrap.h"

#include "IssueListModel.h"
#include "LegacyChartEditorSurface.h"
#include "LegacyTimelineSurface.h"
#include "OutlineListModel.h"
#include "QuickShellController.h"
#include "mainwindow/MainWindow.h"

#include <QCoreApplication>
#include <QIcon>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTextStream>
#include <QtQml>

QuickShellBootstrap::QuickShellBootstrap(const QIcon& appIcon, QObject* parent)
    : QObject(parent)
    , appIcon_(appIcon)
{
}

QuickShellBootstrap::~QuickShellBootstrap() = default;

bool QuickShellBootstrap::start()
{
    backend_ = std::make_unique<MainWindow>(MainWindow::FrontendHostMode::QuickShellBackend);
    controller_ = std::make_unique<QuickShellController>(backend_.get(), this);
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    engine_->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));

    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::warnings,
        this,
        [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
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
    qmlRegisterUncreatableType<OutlineListModel>(
        "MiaCode.QuickShell",
        1,
        0,
        "OutlineListModel",
        "Outline model is provided by the controller."
    );
    qmlRegisterUncreatableType<IssueListModel>(
        "MiaCode.QuickShell",
        1,
        0,
        "IssueListModel",
        "Issue models are provided by the controller."
    );
    qmlRegisterUncreatableType<LegacyChartEditorSurface>(
        "MiaCode.QuickShell",
        1,
        0,
        "LegacyChartEditorSurface",
        "Chart editor surface is provided by the controller."
    );
    qmlRegisterUncreatableType<LegacyTimelineSurface>(
        "MiaCode.QuickShell",
        1,
        0,
        "LegacyTimelineSurface",
        "Timeline surface is provided by the controller."
    );

    engine_->rootContext()->setContextProperty(QStringLiteral("controller"), controller_.get());
    const QUrl mainUrl(QStringLiteral("qrc:/quick_shell/qml/QuickShellMain.qml"));
    engine_->load(mainUrl);
    if (engine_->rootObjects().isEmpty()) {
        QTextStream(stderr) << "[QuickShell QML] no root object created for " << mainUrl.toString() << '\n';
        QTextStream(stderr).flush();
        engine_.reset();
        controller_.reset();
        backend_.reset();
        return false;
    }

    if (!appIcon_.isNull()) {
        if (QQuickWindow* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst()); window != nullptr) {
            window->setIcon(appIcon_);
        }
    }
    return true;
}

QuickShellController* QuickShellBootstrap::controller() const
{
    return controller_.get();
}
