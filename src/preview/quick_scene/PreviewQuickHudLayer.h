#pragma once

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QQuickPaintedItem>

#include "preview/scene/PreviewLayerOrder.h"

class PreviewRuntime;
namespace miacode::preview::scene {
struct PreviewFrameState;
}

class PreviewQuickHudLayer : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* runtime READ runtimeObject WRITE setRuntimeObject NOTIFY runtimeChanged)

public:
    explicit PreviewQuickHudLayer(QQuickItem* parent = nullptr);

    void setRuntime(PreviewRuntime* runtime);
    QObject* runtimeObject() const;
    void setRuntimeObject(QObject* runtimeObject);
    void setFrameState(const miacode::preview::scene::PreviewFrameState* frameState);
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);
    void paint(QPainter* painter) override;

signals:
    void runtimeChanged();

private:
    void requestThrottledUpdate();

    QPointer<PreviewRuntime> runtime_;
    QMetaObject::Connection runtimeUpdateConnection_;
    const miacode::preview::scene::PreviewFrameState* frameState_ = nullptr;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
    // The HUD draws diagnostic text (FPS, max frame interval, stutter
    // counts, timestamps) — none of which the human eye benefits from at
    // 60Hz update rate. Each `update()` on a QQuickPaintedItem rasterises
    // the full-area backing texture and re-uploads it during the QSG sync
    // phase, which dominates frame time for an item the size of the
    // preview surface. We throttle to ~kHudUpdateIntervalMs so the HUD
    // refresh rate is decoupled from frameStateChanged firings.
    QElapsedTimer hudUpdateThrottleTimer_;
    qint64 lastHudUpdateMs_ = -1;
    bool hudUpdatePending_ = false;
};
