#include "tools/cover_export/CoverFrameSceneBinder.h"

namespace miacode::cover_export {

CoverFrameSceneBinder::CoverFrameSceneBinder(QObject* parent)
    : QObject(parent)
{
}

void CoverFrameSceneBinder::setFrameState(
    const miacode::preview::scene::PreviewFrameState* frameState)
{
    const bool oldBound = liveChartSceneBound();
    if (frameState_ == frameState) {
        return;
    }
    frameState_ = frameState;
    emitBindingChanges(liveChartScene_.data(), oldBound);
}

void CoverFrameSceneBinder::bindLiveChartScene(QObject* scene)
{
    const bool oldBound = liveChartSceneBound();
    QObject* const oldScene = liveChartScene_.data();
    if (oldScene == scene) {
        return;
    }

    if (destroyedConnection_) {
        QObject::disconnect(destroyedConnection_);
        destroyedConnection_ = QMetaObject::Connection();
    }
    liveChartScene_ = scene;
    if (scene != nullptr) {
        destroyedConnection_ = QObject::connect(
            scene,
            &QObject::destroyed,
            this,
            [this](QObject* destroyedObject) { clearDestroyedScene(destroyedObject); });
    }
    emitBindingChanges(oldScene, oldBound);
}

void CoverFrameSceneBinder::unbindLiveChartScene(QObject* scene)
{
    if (scene == nullptr || liveChartScene_.data() != scene) {
        return;
    }
    bindLiveChartScene(nullptr);
}

void CoverFrameSceneBinder::detachLiveChartScene()
{
    const bool oldBound = liveChartSceneBound();
    QObject* const oldScene = liveChartScene_.data();
    if (destroyedConnection_) {
        QObject::disconnect(destroyedConnection_);
        destroyedConnection_ = QMetaObject::Connection();
    }
    liveChartScene_.clear();
    frameState_ = nullptr;
    emitBindingChanges(oldScene, oldBound);
}

void CoverFrameSceneBinder::clearDestroyedScene(QObject* scene)
{
    // QPointer has already nulled the scene before QObject::destroyed reaches
    // this callback, so derive the former bound state from the borrowed state
    // rather than from liveChartSceneBound(). QML needs the bound notification
    // to reveal the cached still when the live root disappears.
    const bool oldBound = frameState_ != nullptr;
    liveChartScene_.clear();
    frameState_ = nullptr;
    destroyedConnection_ = QMetaObject::Connection();
    // QPointer is cleared by QObject destruction before this callback runs, so
    // notify the property change explicitly. A disconnected old connection
    // cannot reach this slot after a replacement scene is bound.
    Q_UNUSED(scene);
    emit liveChartSceneChanged();
    if (oldBound) {
        emit liveChartSceneBoundChanged();
    }
}

void CoverFrameSceneBinder::emitBindingChanges(QObject* oldScene, bool oldBound)
{
    if (oldScene != liveChartScene_.data()) {
        emit liveChartSceneChanged();
    }
    if (oldBound != liveChartSceneBound()) {
        emit liveChartSceneBoundChanged();
    }
}

}  // namespace miacode::cover_export
