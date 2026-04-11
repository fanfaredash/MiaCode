#include "QuickShellController.h"

#include "QuickShellNativeSurfaceHost.h"

#include "common/DebugLog.h"
#include "preview/runtime/PreviewStageMediaHost.h"

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

QuickShellController::QuickShellController(
    QuickShellCommandSink* commandSink,
    QuickShellStateSource* stateSource,
    QuickShellNativeSurfaceHost* surfaceHost,
    QObject* parent)
    : QObject(parent)
    , commandSink_(commandSink)
    , stateSource_(stateSource)
    , surfaceHost_(surfaceHost)
    , refreshTimer_(new QTimer(this))
{
    if (stateSource_ != nullptr) {
        if (auto* mediaHost =
                qobject_cast<PreviewStageMediaHost*>(stateSource_->shellPreviewStageMediaHostObject());
            mediaHost != nullptr) {
            connect(mediaHost, &PreviewStageMediaHost::mediaStateChanged, this, [this]() {
                refreshFromStateSource();
            });
        }
    }
    refreshTimer_->setInterval(150);
    connect(refreshTimer_, &QTimer::timeout, this, &QuickShellController::refreshFromStateSource);
    refreshTimer_->start();
    refreshFromStateSource();
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
    return stateSource_ != nullptr ? stateSource_->shellPreviewRuntimeObject() : nullptr;
}

QObject* QuickShellController::previewStageMediaHost() const
{
    return stateSource_ != nullptr ? stateSource_->shellPreviewStageMediaHostObject() : nullptr;
}

QWindow* QuickShellController::previewCompositeWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().previewCompositeWindow : nullptr;
}

bool QuickShellController::previewUsesSeparateSurface() const
{
    return previewUsesSeparateSurface_;
}

QWindow* QuickShellController::topChromeWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().topChrome : nullptr;
}

QWindow* QuickShellController::sidebarWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().sidebar : nullptr;
}

QWindow* QuickShellController::workspaceWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().workspace : nullptr;
}

QWindow* QuickShellController::previewControlsWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().previewControls : nullptr;
}

QWindow* QuickShellController::statusWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().status : nullptr;
}

void QuickShellController::setPreviewFullscreen(bool fullscreen)
{
    if (commandSink_ == nullptr || stateSource_ == nullptr) {
        return;
    }
    if (stateSource_->shellPreviewFullscreen() == fullscreen) {
        return;
    }
    commandSink_->setShellPreviewFullscreen(fullscreen);
    refreshFromStateSource();
}

void QuickShellController::refresh()
{
    refreshFromStateSource();
}

bool QuickShellController::confirmClose()
{
    return commandSink_ != nullptr ? commandSink_->confirmShellClose() : true;
}

void QuickShellController::togglePreviewPlayback()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->toggleShellPreviewPlayback();
    refreshFromStateSource();
}

void QuickShellController::stopPreview()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->stopShellPreview();
    refreshFromStateSource();
}

void QuickShellController::seekPreview(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->seekShellPreview(second);
    refreshFromStateSource();
}

void QuickShellController::setPreviewRate(double rate)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->setShellPreviewRate(rate);
    refreshFromStateSource();
}

void QuickShellController::syncTopChromeSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncTopChromeSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->topChromeSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_top_chrome"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncSidebarSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncSidebarSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->sidebarSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_sidebar"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncWorkspaceSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncWorkspaceSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->workspaceSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_workspace"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncPreviewControlsSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncPreviewControlsSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->previewControlsSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_preview_controls"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncStatusSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncStatusSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->statusSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_status"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

bool QuickShellController::hasShortcut(const QKeySequence& sequence) const
{
    return commandSink_ != nullptr ? commandSink_->shellHasShortcut(sequence) : false;
}

bool QuickShellController::triggerShortcut(const QKeySequence& sequence)
{
    if (commandSink_ == nullptr) {
        return false;
    }
    const bool triggered = commandSink_->shellTriggerShortcut(sequence);
    if (triggered) {
        refreshFromStateSource();
    }
    return triggered;
}

void QuickShellController::refreshFromStateSource()
{
    if (stateSource_ == nullptr) {
        return;
    }

    bool stateChanged = false;
    stateChanged |= assignIfChanged(windowTitle_, stateSource_->shellWindowTitle());
    stateChanged |= assignIfChanged(workspacePanelsSwapped_, stateSource_->shellWorkspacePanelsSwapped());
    stateChanged |= assignIfChanged(previewSpeedLabel_, stateSource_->shellPreviewSpeedLabel());
    stateChanged |= assignIfChanged(previewPlaying_, stateSource_->shellPreviewPlaying());
    stateChanged |= assignIfChanged(previewPositionSeconds_, stateSource_->shellPreviewPositionSeconds());
    stateChanged |= assignIfChanged(previewDurationSeconds_, stateSource_->shellPreviewDurationSeconds());
    stateChanged |= assignIfChanged(previewUsesSeparateSurface_, stateSource_->shellPreviewUsesSeparateSurface());

    const bool nextPreviewFullscreen = stateSource_->shellPreviewFullscreen();
    if (assignIfChanged(previewFullscreen_, nextPreviewFullscreen)) {
        stateChanged = true;
        emit previewFullscreenChanged();
    }

    if (stateChanged) {
        emit shellStateChanged();
    }
}
