#include "runtime/shell/ShellHost.h"

#include "runtime/Session.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/export/VideoExportHost.h"

#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QString>

miacode::runtime::ShellHost::ShellHost(::Session& session)
    : session_(session)
{}

void Session::attachRootWindow(QWindow* window)
{
    rootWindow_ = window;
}

void Session::setRootWindowFrameGeometry(const QRect& geometry)
{
    rootWindowFrameGeometry_ = geometry;
    setProperty("miacode.quick_root_window_frame_geometry", geometry);
}

bool Session::rootWindowFrameGeometryAvailable() const
{
    return rootWindowFrameGeometry_.isValid();
}

QRect Session::rootWindowFrameGeometry() const
{
    return rootWindowFrameGeometry_;
}

void Session::noteRootWindowReady()
{
    noteQuickShellStartupUiReady();
}

void Session::configureRuntimeDebugOutput()
{
    runtimeDebugOutputEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
}

void miacode::runtime::ShellHost::requestShellClose(std::function<void(bool)> onDecided)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    session_.documents_->requestLeaveDocument(
        [this, onDecided = std::move(onDecided), totalTimer](bool canClose) mutable {
            const bool confirmed = canClose && finishShellClose(totalTimer);
            if (onDecided) {
                onDecided(confirmed);
            }
        });
}

bool miacode::runtime::ShellHost::finishShellClose(QElapsedTimer totalTimer)
{
    session_.documents_->cleanupCrashRecoveryForCleanExit();
    miacode::crash_recovery::clearSessionMarker();

    QElapsedTimer savePortableTimer;
    savePortableTimer.start();
    session_.savePortableState();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("save_portable_state"),
        savePortableTimer.elapsed());

    QElapsedTimer exportCleanupTimer;
    exportCleanupTimer.start();
    session_.videoExport_->clearVideoExportWorkerState();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("clear_video_export_worker_state"),
        exportCleanupTimer.elapsed());

    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("confirm_shell_close"),
        totalTimer.elapsed(),
        QStringLiteral("result=confirmed"));
    return true;
}

void miacode::runtime::ShellHost::appendOutput(const QString& scope, const QString& payload) const
{
    if (!session_.runtimeDebugOutputEnabled_) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        scope,
        payload);
}
