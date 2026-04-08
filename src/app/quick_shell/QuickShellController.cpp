#include "QuickShellController.h"

#include "common/DebugLog.h"
#include "mainwindow/MainWindow.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"

#include <QDockWidget>
#include <QLayout>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace {

template <typename T>
bool assignIfChanged(T& target, const T& value)
{
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

void appendQuickShellControllerLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/controller"),
        text
    );
}

}  // namespace

QuickShellController::QuickShellController(MainWindow* backend, QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , refreshTimer_(new QTimer(this))
{
    if (backend_ != nullptr) {
        topChromeWindow_ = createForeignWindowForSurface(backend_->quickShellTopChromeSurfaceWidget_);
        workspaceWindow_ = createForeignWindowForSurface(backend_->quickShellWorkspaceSurfaceWidget_);
        previewControlsWindow_ = createForeignWindowForSurface(backend_->quickShellPreviewControlsSurfaceWidget_);
        statusWindow_ = createForeignWindowForSurface(backend_->quickShellStatusSurfaceWidget_);
    }
    refreshTimer_->setInterval(150);
    connect(refreshTimer_, &QTimer::timeout, this, &QuickShellController::refreshFromBackend);
    refreshTimer_->start();
    refreshFromBackend();
}

QString QuickShellController::windowTitle() const
{
    return windowTitle_;
}

bool QuickShellController::workspacePanelsSwapped() const
{
    return workspacePanelsSwapped_;
}

QString QuickShellController::previewSpeedLabel() const
{
    return previewSpeedLabel_;
}

bool QuickShellController::previewPlaying() const
{
    return previewPlaying_;
}

double QuickShellController::previewPositionSeconds() const
{
    return previewPositionSeconds_;
}

double QuickShellController::previewDurationSeconds() const
{
    return previewDurationSeconds_;
}

bool QuickShellController::previewFullscreen() const
{
    return previewFullscreen_;
}

QObject* QuickShellController::previewRuntime() const
{
    return backend_ != nullptr ? backend_->previewCanvas_ : nullptr;
}

QObject* QuickShellController::previewStageMediaHost() const
{
    return backend_ != nullptr ? backend_->previewStageMediaHost_ : nullptr;
}

QWindow* QuickShellController::topChromeWindow() const
{
    return topChromeWindow_;
}

QWindow* QuickShellController::workspaceWindow() const
{
    return workspaceWindow_;
}

QWindow* QuickShellController::previewControlsWindow() const
{
    return previewControlsWindow_;
}

QWindow* QuickShellController::statusWindow() const
{
    return statusWindow_;
}

void QuickShellController::setPreviewFullscreen(bool fullscreen)
{
    if (backend_ == nullptr) {
        return;
    }
    if (backend_->previewFullscreenActive_ == fullscreen) {
        return;
    }
    if (fullscreen) {
        backend_->enterPreviewFullscreen();
    } else {
        backend_->exitPreviewFullscreen();
    }
    refreshFromBackend();
}

void QuickShellController::refresh()
{
    refreshFromBackend();
}

bool QuickShellController::confirmClose()
{
    if (backend_ == nullptr) {
        return true;
    }
    if (!backend_->maybeSaveBeforeContinue()) {
        return false;
    }
    backend_->savePortableState();
    backend_->clearVideoExportWorkerState();
    return true;
}

void QuickShellController::togglePreviewPlayback()
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->onTogglePreviewPause();
    refreshFromBackend();
}

void QuickShellController::stopPreview()
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->onStopPreview();
    refreshFromBackend();
}

void QuickShellController::seekPreview(double second)
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->seekPreviewToSecond(second, true);
    refreshFromBackend();
}

void QuickShellController::setPreviewRate(double rate)
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->applyPreviewPlaybackRate(rate);
    refreshFromBackend();
}

void QuickShellController::syncTopChromeSurfaceSize(int width, int height)
{
    if (backend_ == nullptr || backend_->quickShellTopChromeSurfaceWidget_ == nullptr) {
        return;
    }
    const QSize nextSize(qMax(1, width), qMax(1, height));
    if (backend_->quickShellTopChromeSurfaceWidget_->size() != nextSize) {
        backend_->quickShellTopChromeSurfaceWidget_->resize(nextSize);
    }
    if (QLayout* layout = backend_->quickShellTopChromeSurfaceWidget_->layout(); layout != nullptr) {
        layout->activate();
    }
    backend_->quickShellTopChromeSurfaceWidget_->updateGeometry();
    backend_->quickShellTopChromeSurfaceWidget_->update();
    backend_->quickShellTopChromeSurfaceWidget_->show();
    appendQuickShellControllerLog(
        QStringLiteral("sync_top_chrome"),
            QString("size=%1x%2 handle=0x%3")
                .arg(nextSize.width())
                .arg(nextSize.height())
            .arg(static_cast<quintptr>(backend_->quickShellTopChromeSurfaceWidget_->winId()), 0, 16)
    );
}

void QuickShellController::syncWorkspaceSurfaceSize(int width, int height)
{
    if (backend_ == nullptr || backend_->quickShellWorkspaceSurfaceWidget_ == nullptr) {
        return;
    }
    const QSize nextSize(qMax(1, width), qMax(1, height));
    if (backend_->quickShellWorkspaceSurfaceWidget_->size() != nextSize) {
        backend_->quickShellWorkspaceSurfaceWidget_->resize(nextSize);
    }
    if (QLayout* layout = backend_->quickShellWorkspaceSurfaceWidget_->layout(); layout != nullptr) {
        layout->activate();
    }
    if (backend_->previewLeftColumn_ != nullptr) {
        backend_->previewLeftColumn_->updateGeometry();
        backend_->previewLeftColumn_->show();
    }
    if (backend_->outlineDock_ != nullptr) {
        backend_->outlineDock_->updateGeometry();
        backend_->outlineDock_->show();
    }
    backend_->quickShellWorkspaceSurfaceWidget_->updateGeometry();
    backend_->quickShellWorkspaceSurfaceWidget_->update();
    backend_->quickShellWorkspaceSurfaceWidget_->show();
    appendQuickShellControllerLog(
        QStringLiteral("sync_workspace"),
            QString("size=%1x%2 handle=0x%3")
                .arg(nextSize.width())
                .arg(nextSize.height())
            .arg(static_cast<quintptr>(backend_->quickShellWorkspaceSurfaceWidget_->winId()), 0, 16)
    );
}

void QuickShellController::syncPreviewControlsSurfaceSize(int width, int height)
{
    if (backend_ == nullptr || backend_->quickShellPreviewControlsSurfaceWidget_ == nullptr) {
        return;
    }
    const QSize nextSize(qMax(1, width), qMax(1, height));
    if (backend_->quickShellPreviewControlsSurfaceWidget_->size() != nextSize) {
        backend_->quickShellPreviewControlsSurfaceWidget_->resize(nextSize);
    }
    if (QLayout* layout = backend_->quickShellPreviewControlsSurfaceWidget_->layout(); layout != nullptr) {
        layout->activate();
    }
    if (backend_->previewControlCard_ != nullptr) {
        backend_->previewControlCard_->updateGeometry();
        backend_->previewControlCard_->show();
    }
    if (backend_->previewStatsCard_ != nullptr) {
        backend_->previewStatsCard_->updateGeometry();
        backend_->previewStatsCard_->show();
    }
    backend_->quickShellPreviewControlsSurfaceWidget_->updateGeometry();
    backend_->quickShellPreviewControlsSurfaceWidget_->update();
    backend_->quickShellPreviewControlsSurfaceWidget_->show();
    appendQuickShellControllerLog(
        QStringLiteral("sync_preview_controls"),
            QString("size=%1x%2 handle=0x%3")
                .arg(nextSize.width())
                .arg(nextSize.height())
            .arg(static_cast<quintptr>(backend_->quickShellPreviewControlsSurfaceWidget_->winId()), 0, 16)
    );
}

void QuickShellController::syncStatusSurfaceSize(int width, int height)
{
    if (backend_ == nullptr || backend_->quickShellStatusSurfaceWidget_ == nullptr) {
        return;
    }
    const QSize nextSize(qMax(1, width), qMax(1, height));
    if (backend_->quickShellStatusSurfaceWidget_->size() != nextSize) {
        backend_->quickShellStatusSurfaceWidget_->resize(nextSize);
    }
    if (QLayout* layout = backend_->quickShellStatusSurfaceWidget_->layout(); layout != nullptr) {
        layout->activate();
    }
    if (QStatusBar* bar = backend_->statusBar(); bar != nullptr) {
        bar->updateGeometry();
        bar->show();
    }
    backend_->quickShellStatusSurfaceWidget_->updateGeometry();
    backend_->quickShellStatusSurfaceWidget_->update();
    backend_->quickShellStatusSurfaceWidget_->show();
    appendQuickShellControllerLog(
        QStringLiteral("sync_status"),
        QString("size=%1x%2 handle=0x%3")
            .arg(nextSize.width())
            .arg(nextSize.height())
            .arg(static_cast<quintptr>(backend_->quickShellStatusSurfaceWidget_->winId()), 0, 16)
    );
}

QWindow* QuickShellController::createForeignWindowForSurface(QWidget* surface) const
{
    if (surface == nullptr) {
        return nullptr;
    }
    const WId wid = surface->winId();
    if (wid == 0) {
        return nullptr;
    }
    QWindow* window = QWindow::fromWinId(wid);
    if (window != nullptr) {
        window->QObject::setParent(const_cast<QuickShellController*>(this));
    }
    return window;
}

void QuickShellController::refreshFromBackend()
{
    if (backend_ == nullptr) {
        return;
    }

    bool stateChanged = false;
    stateChanged |= assignIfChanged(windowTitle_, backend_->windowTitle());
    stateChanged |= assignIfChanged(workspacePanelsSwapped_, backend_->workspacePanelsSwapped_);
    stateChanged |= assignIfChanged(
        previewSpeedLabel_,
        backend_->previewSpeedButton_ != nullptr ? backend_->previewSpeedButton_->text() : QStringLiteral("1x")
    );
    stateChanged |= assignIfChanged(previewPlaying_, backend_->qtPreviewPlaying_);
    stateChanged |= assignIfChanged(
        previewPositionSeconds_,
        backend_->previewSlider_ != nullptr
            ? static_cast<double>(backend_->previewSlider_->value()) / 1000.0
            : backend_->qtPreviewPauseSecond_
    );
    stateChanged |= assignIfChanged(previewDurationSeconds_, backend_->previewDurationSeconds());

    const bool nextPreviewFullscreen = backend_->previewFullscreenActive_;
    if (assignIfChanged(previewFullscreen_, nextPreviewFullscreen)) {
        stateChanged = true;
        emit previewFullscreenChanged();
    }

    if (stateChanged) {
        emit shellStateChanged();
    }
}
