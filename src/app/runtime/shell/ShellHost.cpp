#include "runtime/shell/ShellHost.h"
#include "runtime/document/DocumentSessionHost.h"

#include "common/DebugOptions.h"

#include <QApplication>

miacode::runtime::ShellHost::ShellHost(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        QObject::connect(app, &QApplication::focusChanged, &session_, [this](QWidget* old, QWidget* now) {
            this->handleApplicationFocusChanged(old, now);
        });
        QObject::connect(app, &QGuiApplication::applicationStateChanged, &session_, [this](Qt::ApplicationState state) {
            this->handleApplicationStateChanged(state);
        });
    }
}

bool Session::rootWindowFrameGeometryAvailable() const
{
    return shell_->rootWindowFrameGeometryAvailable();
}

QRect Session::rootWindowFrameGeometry() const
{
    return shell_->rootWindowFrameGeometry();
}

void Session::attachRootWindow(QWindow* window)
{
    shell_->attachRootWindow(window);
}

void Session::setPreviewPlayingFlag(bool playing)
{
    if (state_.playing_ == playing) {
        return;
    }
    state_.playing_ = playing;
    state_.previewTransportState_ = playing
        ? miacode::v2::PlaybackTransportState::Playing
        : (state_.previewTransportState_ == miacode::v2::PlaybackTransportState::Stopped
               ? miacode::v2::PlaybackTransportState::Stopped
               : miacode::v2::PlaybackTransportState::Paused);
    QMetaObject::invokeMethod(
        this, [this]() { emit presentationChanged(); }, Qt::QueuedConnection);
}

void Session::setRootWindowFrameGeometry(const QRect& geometry)
{
    shell_->setRootWindowFrameGeometry(geometry);
}

void Session::noteRootWindowReady()
{
    shell_->noteRootWindowReady();
}

void Session::configureRuntimeDebugOutput()
{
    if (shell_ == nullptr) {
        runtimeDebugOutputEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
        return;
    }
    shell_->configureRuntimeDebugOutput();
}

QString Session::windowTitle() const
{
    return titleText_;
}

void Session::noteStatus(const QString&)
{
}

void Session::onToggleFindReplace()
{
    shell_->onToggleFindReplace();
}

void Session::onFindNext()
{
    shell_->onFindNext();
}

void Session::onFindPrevious()
{
    shell_->onFindPrevious();
}

void Session::onReplaceOne()
{
    shell_->onReplaceOne();
}

void Session::onReplaceAll()
{
    shell_->onReplaceAll();
}

void Session::refreshQuickShellRehostedWidgetParent(QWidget* widget)
{
    shell_->refreshQuickShellRehostedWidgetParent(widget);
}

bool Session::eventFilter(QObject* watched, QEvent* event)
{
    if (shell_ == nullptr) {
        return QObject::eventFilter(watched, event);
    }
    return shell_->eventFilter(watched, event);
}

bool Session::event(QEvent* event)
{
    if (shell_ == nullptr) {
        return QObject::event(event);
    }
    return shell_->event(event);
}
