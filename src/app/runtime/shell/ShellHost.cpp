#include "runtime/shell/ShellHost.h"

#include "runtime/Session.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/export/VideoExportHost.h"
#include "runtime/playback/PlaybackCoordinator.h"

#include "app/ui/ShortcutRegistry.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>

namespace {

constexpr char kReservesPlainSpaceProperty[] = "reservesPlainSpace";

struct PauseDisplayHoldKey {
    int key = Qt::Key_Alt;
    Qt::KeyboardModifiers pressModifiers = Qt::AltModifier;
};

PauseDisplayHoldKey pauseDisplayHoldKey()
{
    const QKeySequence sequence = ShortcutRegistry::instance().sequence(
        QStringLiteral("preview.pause_display_hold"), QKeySequence(Qt::Key_Alt));
    const QKeyCombination combination = sequence[0];
    PauseDisplayHoldKey hold;
    hold.key = combination.key();
    switch (hold.key) {
    case Qt::Key_Alt:
        hold.pressModifiers = Qt::AltModifier;
        break;
    case Qt::Key_Control:
        hold.pressModifiers = Qt::ControlModifier;
        break;
    case Qt::Key_Shift:
        hold.pressModifiers = Qt::ShiftModifier;
        break;
    case Qt::Key_Meta:
        hold.pressModifiers = Qt::MetaModifier;
        break;
    default:
        hold.pressModifiers = combination.keyboardModifiers();
        break;
    }
    return hold;
}

bool objectReservesPlainSpace(QObject* object)
{
    for (QObject* current = object; current != nullptr; current = current->parent()) {
        if (current->property(kReservesPlainSpaceProperty).toBool()) {
            return true;
        }
    }

    auto* item = qobject_cast<QQuickItem*>(object);
    for (QQuickItem* current = item; current != nullptr; current = current->parentItem()) {
        if (current->property(kReservesPlainSpaceProperty).toBool()) {
            return true;
        }
    }
    return false;
}

bool rootWindowReservesPlainSpace(QQuickWindow* window)
{
    return window->property("sourceEditorFocused").toBool()
        || objectReservesPlainSpace(QGuiApplication::focusObject())
        || objectReservesPlainSpace(window->activeFocusItem());
}

}  // namespace

miacode::runtime::ShellHost::ShellHost(::Session& session)
    : session_(session)
{}

void Session::attachRootWindow(QWindow* window)
{
    QCoreApplication::instance()->removeEventFilter(this);
    setPauseDisplayAltHoldActive(false);
    rootWindow_ = window;

    if (window != nullptr) {
        QCoreApplication::instance()->installEventFilter(this);
    }
}

bool Session::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::ApplicationDeactivate) {
        setPauseDisplayAltHoldActive(false);
        return false;
    }

    if (event->type() == QEvent::ShortcutOverride
        || event->type() == QEvent::KeyPress
        || event->type() == QEvent::KeyRelease) {
        const auto* keyEvent = static_cast<const QKeyEvent*>(event);
        auto* rootWindow = qobject_cast<QQuickWindow*>(rootWindow_.data());
        if (rootWindow != nullptr
            && QGuiApplication::focusWindow() == rootWindow
            && keyEvent->key() == Qt::Key_Space
            && keyEvent->modifiers() == Qt::NoModifier
            && !rootWindowReservesPlainSpace(rootWindow)) {
            if (event->type() == QEvent::KeyPress
                && !keyEvent->isAutoRepeat()
                && playback_ != nullptr) {
                playback_->togglePlayback();
            }
            event->accept();
            return true;
        }

        const PauseDisplayHoldKey hold = pauseDisplayHoldKey();
        if (keyEvent->key() == hold.key) {
            if (event->type() == QEvent::KeyPress
                && !keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == hold.pressModifiers) {
                setPauseDisplayAltHoldActive(true);
            } else if (event->type() == QEvent::KeyRelease && !keyEvent->isAutoRepeat()) {
                setPauseDisplayAltHoldActive(false);
            }
        }
    }

    return false;
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
