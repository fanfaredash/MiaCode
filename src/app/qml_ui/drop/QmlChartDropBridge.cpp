#include "QmlChartDropBridge.h"

#include "common/ChartAssetPaths.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace miacode::qml_ui {

namespace {

constexpr int kDragLeaveDelayMs = 160;

}

QmlChartDropBridge::QmlChartDropBridge(QObject& window,
                                       std::function<void()> enableDropTarget,
                                       Submit submit,
                                       Completion completion,
                                       QObject* parent)
    : QObject(parent)
    , window_(&window)
    , enableDropTarget_(std::move(enableDropTarget))
    , submit_(std::move(submit))
    , completion_(std::move(completion))
    , leaveTimer_(new QTimer(this))
{
    leaveTimer_->setSingleShot(true);
    leaveTimer_->setInterval(kDragLeaveDelayMs);
    connect(leaveTimer_, &QTimer::timeout, this, [this]() {
        setDragState(false);
    });
    if (enableDropTarget_) {
        enableDropTarget_();
    }
    window.installEventFilter(this);
}

QmlChartDropBridge::~QmlChartDropBridge()
{
    release();
}

QStringList QmlChartDropBridge::supportedPaths(const QMimeData* mimeData) const
{
    QStringList paths;
    if (mimeData == nullptr || !mimeData->hasUrls()) {
        return paths;
    }
    const QStringList extensions = miacode::chart_assets::supportedTrackFileExtensions();
    for (const QUrl& url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile() || !extensions.contains(info.suffix().toLower())) {
            continue;
        }
        const QString path = info.absoluteFilePath();
        if (!paths.contains(path, Qt::CaseInsensitive)) {
            paths.append(path);
        }
    }
    return paths;
}

void QmlChartDropBridge::setDragState(bool active, const QStringList& paths)
{
    const bool pathsChanged = acceptedPaths_ != paths;
    const bool activeChanged = dragActive_ != active;
    dragActive_ = active;
    acceptedPaths_ = paths;
    if (pathsChanged) {
        emit acceptedPathsChanged();
    }
    if (activeChanged) {
        emit dragActiveChanged();
    }
}

void QmlChartDropBridge::scheduleDragLeave()
{
    if (leaveTimer_ != nullptr) {
        leaveTimer_->start();
    }
}

void QmlChartDropBridge::clearDragLeaveTimer()
{
    if (leaveTimer_ != nullptr) {
        leaveTimer_->stop();
    }
}

void QmlChartDropBridge::submitDrop(const QStringList& paths)
{
    clearDragLeaveTimer();
    setDragState(false);
    if (busy_) {
        emit dropBusy();
        return;
    }

    busy_ = true;
    emit busyChanged();
    const quint64 requestId = ++nextRequestId_;
    const quint64 requestGeneration = generation_;
    activeRequestId_ = requestId;
    const auto finished = [this, requestId, requestGeneration](const QmlChartDropResult& result) {
        if (released_ || !busy_ || activeRequestId_ != requestId || generation_ != requestGeneration) {
            return;
        }
        busy_ = false;
        activeRequestId_ = 0;
        emit busyChanged();
        if (completion_) {
            completion_(result);
        }
        emit dropCompleted(result);
    };

    if (!submit_) {
        finished(QmlChartDropResult{requestId, requestGeneration, true, true, false, 0, 1, {}});
        return;
    }
    submit_(paths, requestId, requestGeneration, finished);
}

bool QmlChartDropBridge::eventFilter(QObject* watched, QEvent* event)
{
    if (released_ || watched != window_ || event == nullptr) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* window = qobject_cast<QQuickWindow*>(window_.data());
        if (window != nullptr && window->property("sourceEditorFocused").toBool()
            && !window->property("sourceEditorOverlayHeld").toBool()) {
            if (QQuickItem* focus = window->activeFocusItem()) {
                const QPointF scene = static_cast<QMouseEvent*>(event)->scenePosition();
                if (!focus->contains(focus->mapFromScene(scene))) {
                    focus->setFocus(false);
                }
            }
        }
        return false;
    }
    case QEvent::DragLeave:
        scheduleDragLeave();
        return false;
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        const auto* dropEvent = static_cast<const QDropEvent*>(event);
        const QStringList paths = supportedPaths(dropEvent->mimeData());
        if (paths.isEmpty()) {
            scheduleDragLeave();
            return false;
        }
        clearDragLeaveTimer();
        setDragState(true, paths);
        static_cast<QDropEvent*>(event)->acceptProposedAction();
        return true;
    }
    case QEvent::Drop: {
        const auto* dropEvent = static_cast<const QDropEvent*>(event);
        const QStringList paths = supportedPaths(dropEvent->mimeData());
        if (paths.isEmpty()) {
            scheduleDragLeave();
            return false;
        }
        if (busy_) {
            setDragState(false);
            emit dropBusy();
            event->ignore();
            return true;
        }
        static_cast<QDropEvent*>(event)->acceptProposedAction();
        submitDrop(paths);
        return true;
    }
    default:
        return QObject::eventFilter(watched, event);
    }
}

void QmlChartDropBridge::release()
{
    if (released_) {
        return;
    }
    released_ = true;
    ++generation_;
    clearDragLeaveTimer();
    if (window_ != nullptr) {
        window_->removeEventFilter(this);
    }
    setDragState(false);
    if (busy_) {
        busy_ = false;
        activeRequestId_ = 0;
        emit busyChanged();
    }
    submit_ = {};
    completion_ = {};
}

} // namespace miacode::qml_ui
