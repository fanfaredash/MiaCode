#include "app/WindowVisibilityDiagnostics.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QStringList>

#include <utility>

namespace miacode::app::window_diag {

namespace {

const char* const kObserverObjectName = "MiaCodeWindowVisibilityObserver";

void appendVisibilityLine(const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("window/visibility"),
        payload);
}

// One observer per window, parented to it so it dies with the window. Holds the little
// bit of state a transition log needs: the previous visibility (so each line records an
// edge, not just a level) and when the surface entered its current occluded state.
class WindowVisibilityObserver final : public QObject
{
public:
    WindowVisibilityObserver(QWindow* window, QString surface)
        : QObject(window)
        , window_(window)
        , surface_(std::move(surface))
    {
        setObjectName(QLatin1String(kObserverObjectName));
        previousVisibility_ = window_->visibility();
        sinceOccluded_.start();
        occluded_ = isOccludedVisibility(previousVisibility_);

        QObject::connect(
            window_, &QWindow::visibilityChanged, this,
            [this](QWindow::Visibility visibility) { noteVisibility(visibility); });
        window_->installEventFilter(this);

        appendVisibilityLine(
            QStringLiteral("action=installed %1")
                .arg(visibilityTransitionPayload(
                    surface_,
                    previousVisibility_,
                    previousVisibility_,
                    window_->isExposed(),
                    window_->isActive(),
                    window_->windowStates(),
                    /*occludedForMs=*/-1)));
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == window_ && event != nullptr) {
            switch (event->type()) {
            case QEvent::Expose:
                // Expose fires on occlusion/uncover as well as on minimize/restore, so it
                // catches the "browser covered us" case that visibilityChanged does not.
                noteEdge(QStringLiteral("expose"));
                break;
            case QEvent::WindowStateChange:
                // Carries the FULL state mask (Qt::WindowMinimized included), which the
                // windowStateChanged signal's single-value argument does not.
                noteEdge(QStringLiteral("window_state"));
                break;
            case QEvent::Hide:
                noteEdge(QStringLiteral("hide"));
                break;
            case QEvent::Show:
                noteEdge(QStringLiteral("show"));
                break;
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void noteVisibility(QWindow::Visibility visibility)
    {
        if (visibility == previousVisibility_ && exposedLatch_ == window_->isExposed()) {
            return;
        }
        emitTransition(QStringLiteral("visibility_changed"), visibility);
    }

    void noteEdge(const QString& action)
    {
        const QWindow::Visibility visibility = window_->visibility();
        const bool exposed = window_->isExposed();
        // Suppress no-op repeats: Qt delivers Expose freely, and this must stay
        // transition-triggered rather than turning into a per-frame log.
        if (visibility == previousVisibility_ && exposed == exposedLatch_
            && window_->windowStates() == statesLatch_) {
            return;
        }
        emitTransition(action, visibility);
    }

    void emitTransition(const QString& action, QWindow::Visibility visibility)
    {
        const bool nowOccluded = isOccludedVisibility(visibility) || !window_->isExposed();
        qint64 occludedForMs = -1;
        if (occluded_ && !nowOccluded) {
            occludedForMs = sinceOccluded_.isValid() ? sinceOccluded_.elapsed() : -1;
        }
        if (nowOccluded != occluded_) {
            occluded_ = nowOccluded;
            sinceOccluded_.restart();
        }
        appendVisibilityLine(
            QStringLiteral("action=%1 %2")
                .arg(action)
                .arg(visibilityTransitionPayload(
                    surface_,
                    previousVisibility_,
                    visibility,
                    window_->isExposed(),
                    window_->isActive(),
                    window_->windowStates(),
                    occludedForMs)));
        previousVisibility_ = visibility;
        exposedLatch_ = window_->isExposed();
        statesLatch_ = window_->windowStates();
    }

    QWindow* window_ = nullptr;
    QString surface_;
    QWindow::Visibility previousVisibility_ = QWindow::Hidden;
    Qt::WindowStates statesLatch_ = Qt::WindowNoState;
    bool exposedLatch_ = false;
    bool occluded_ = false;
    QElapsedTimer sinceOccluded_;
};

}  // namespace

QString visibilityName(QWindow::Visibility visibility)
{
    switch (visibility) {
    case QWindow::Hidden:
        return QStringLiteral("Hidden");
    case QWindow::AutomaticVisibility:
        return QStringLiteral("AutomaticVisibility");
    case QWindow::Windowed:
        return QStringLiteral("Windowed");
    case QWindow::Minimized:
        return QStringLiteral("Minimized");
    case QWindow::Maximized:
        return QStringLiteral("Maximized");
    case QWindow::FullScreen:
        return QStringLiteral("FullScreen");
    }
    return QStringLiteral("Visibility(%1)").arg(static_cast<int>(visibility));
}

QString windowStatesText(Qt::WindowStates states)
{
    QStringList parts;
    if (states.testFlag(Qt::WindowMinimized)) {
        parts.append(QStringLiteral("Minimized"));
    }
    if (states.testFlag(Qt::WindowMaximized)) {
        parts.append(QStringLiteral("Maximized"));
    }
    if (states.testFlag(Qt::WindowFullScreen)) {
        parts.append(QStringLiteral("FullScreen"));
    }
    if (states.testFlag(Qt::WindowActive)) {
        parts.append(QStringLiteral("Active"));
    }
    if (parts.isEmpty()) {
        return QStringLiteral("NoState");
    }
    return parts.join(QLatin1Char('|'));
}

bool isOccludedVisibility(QWindow::Visibility visibility)
{
    return visibility == QWindow::Minimized || visibility == QWindow::Hidden;
}

QString visibilityTransitionPayload(
    const QString& surface,
    QWindow::Visibility previous,
    QWindow::Visibility current,
    bool exposed,
    bool active,
    Qt::WindowStates states,
    qint64 occludedForMs)
{
    QString payload =
        QStringLiteral("surface=%1 from=%2 to=%3 exposed=%4 active=%5 state=%6 minimized=%7 occluded=%8")
            .arg(surface.isEmpty() ? QStringLiteral("(unnamed)") : surface)
            .arg(visibilityName(previous))
            .arg(visibilityName(current))
            .arg(exposed ? 1 : 0)
            .arg(active ? 1 : 0)
            .arg(windowStatesText(states))
            .arg(states.testFlag(Qt::WindowMinimized) ? 1 : 0)
            .arg((isOccludedVisibility(current) || !exposed) ? 1 : 0);
    if (occludedForMs >= 0) {
        payload += QStringLiteral(" occluded_for_ms=%1").arg(occludedForMs);
    }
    return payload;
}

void installWindowVisibilityDiagnostics(QWindow* window, const QString& surface)
{
    if (window == nullptr || !miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    if (window->findChild<QObject*>(
            QLatin1String(kObserverObjectName), Qt::FindDirectChildrenOnly) != nullptr) {
        return;
    }
    new WindowVisibilityObserver(window, surface);
}

void logSurfaceGraphicsPersistence(
    const QString& surface, bool persistentGraphics, bool persistentSceneGraph)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    appendVisibilityLine(
        QStringLiteral("action=graphics_persistence surface=%1 persistent_graphics=%2 "
                       "persistent_scene_graph=%3 releases_gpu_on_hide=%4")
            .arg(surface.isEmpty() ? QStringLiteral("(unnamed)") : surface)
            .arg(persistentGraphics ? 1 : 0)
            .arg(persistentSceneGraph ? 1 : 0)
            .arg((persistentGraphics || persistentSceneGraph) ? 0 : 1));
}

}  // namespace miacode::app::window_diag
