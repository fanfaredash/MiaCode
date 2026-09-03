#include "QmlCoverExportWindow.h"

#include "app/MainEntrypoints.h"
#include "app/qml_ui/QmlUiSettings.h"
#include "app/ui/UiNativeWindowTheme.h"
#include "common/DebugLog.h"
#include "tools/cover_export/CoverCompositeRenderer.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>

QmlCoverExportWindow::QmlCoverExportWindow(miacode::v2::ExportEngine& exportEngine,
                                           miacode::v2::PlaybackControl*& playbackControlSlot,
                                           QmlUiSettings& preferences,
                                           const QIcon& icon,
                                           QObject* parent)
    : QObject(parent)
    , preferences_(preferences)
    , icon_(icon)
    , requests_(this)
    , session_(exportEngine, requests_, playbackControlSlot, this)
{
    // Export pumps events while capturing. Finish the active operation before
    // deleting its session, including when the application is closing.
    connect(&session_, &QmlCoverExportSession::busyChanged, this, [this] {
        if (closePending_ && !session_.busy()) {
            close();
        }
    }, Qt::QueuedConnection);
    connect(&preferences_, &QmlUiSettings::themeChanged, this, [this] {
        if (window_) {
            UiNativeWindowTheme::applyToWindow(window_);
        }
    });
}

QmlCoverExportWindow::~QmlCoverExportWindow()
{
    session_.leave();
    // Destroy live scene nodes and the image provider before their borrowed
    // frame state and layout. Session destruction then releases the capture
    // window, skin repository, chart data and all layer images.
    engine_.reset();
}

bool QmlCoverExportWindow::show(QQuickWindow* owner, int difficultyId)
{
    engine_ = std::make_unique<QQmlApplicationEngine>();
    engine_->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    miacode::cover_export::registerCoverChartImageProvider(engine_.get(), session_.coverLayout());
    connect(engine_.get(), &QQmlApplicationEngine::warnings, this,
            [](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) {
            miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                QStringLiteral("cover_window"), warning.toString());
        }
    });
    engine_->setInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject*>(this))},
        {QStringLiteral("coverSession"), QVariant::fromValue(static_cast<QObject*>(&session_))},
        {QStringLiteral("preferences"), QVariant::fromValue(static_cast<QObject*>(&preferences_))},
    });
    engine_->loadFromModule(QStringLiteral("MiaCode.UI"), QStringLiteral("CoverExportWindow"));
    if (engine_->rootObjects().isEmpty()) {
        return false;
    }
    window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
    if (!window_) {
        return false;
    }
    window_->setTransientParent(owner);
    window_->setIcon(icon_);
    if (owner != nullptr) {
        window_->setScreen(owner->screen());
    }
    const QRect available = window_->screen()->availableGeometry();
    window_->resize(window_->size().boundedTo(available.size()));
    window_->setPosition(available.center() - QPoint(window_->width() / 2, window_->height() / 2));
    miacode::app::entry::bindHighPerformanceQuickGraphicsDevice(
        window_, QStringLiteral("cover_window"), /*preferVideoShareDevice=*/false);
    UiNativeWindowTheme::applyToWindow(window_);
    session_.enter(difficultyId);
    window_->show();
    window_->requestActivate();
    return true;
}

void QmlCoverExportWindow::raise()
{
    if (window_ && !closePending_) {
        if (window_->windowState() == Qt::WindowMinimized) {
            window_->showNormal();
        }
        window_->raise();
        window_->requestActivate();
    }
}

void QmlCoverExportWindow::close()
{
    closePending_ = true;
    if (session_.busy()) {
        return;
    }
    if (window_) {
        window_->hide();
    }
    session_.leave();
    deleteLater();
}
