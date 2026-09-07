#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>

namespace miacode::preview::scene {
struct PreviewFrameState;
}

namespace miacode::cover_export {

// Owns the lifetime-safe relationship between the visible cover chart scene
// and the borrowed frame state produced by SceneFrameRenderer. The scene is
// created by QML; the state is owned by the export session, so neither side
// may retain the other beyond its current binding.
class CoverFrameSceneBinder final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* liveChartScene READ liveChartScene NOTIFY liveChartSceneChanged)
    Q_PROPERTY(bool liveChartSceneBound READ liveChartSceneBound NOTIFY liveChartSceneBoundChanged)

public:
    explicit CoverFrameSceneBinder(QObject* parent = nullptr);

    QObject* liveChartScene() const { return liveChartScene_.data(); }
    bool liveChartSceneBound() const
    {
        return liveChartScene_ != nullptr && frameState_ != nullptr;
    }

    void setFrameState(const miacode::preview::scene::PreviewFrameState* frameState);
    Q_INVOKABLE void bindLiveChartScene(QObject* scene);
    Q_INVOKABLE void unbindLiveChartScene(QObject* scene);
    void detachLiveChartScene();

signals:
    void liveChartSceneChanged();
    void liveChartSceneBoundChanged();

private:
    void clearDestroyedScene(QObject* scene);
    void emitBindingChanges(QObject* oldScene, bool oldBound);

    QPointer<QObject> liveChartScene_;
    const miacode::preview::scene::PreviewFrameState* frameState_ = nullptr;
    QMetaObject::Connection destroyedConnection_;
};

}  // namespace miacode::cover_export
