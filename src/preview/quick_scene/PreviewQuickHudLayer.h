#pragma once

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QSize>

#include "core/scene/PreviewLayerOrder.h"

class QPainter;
class PreviewRuntime;
namespace miacode::preview::scene {
struct PreviewFrameState;
}

namespace miacode::preview::hud {

// Phase 4f — standalone HUD painter, used by PreviewQuickHudLayer via
// QQuickPaintedItem::paint. All inputs are read-only; painter must be
// set up to draw within the canvas's local coordinate space (top-left
// origin, size canvasSize).
void paintPreviewHudOverlay(
    QPainter& painter,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& canvasSize,
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags
        = miacode::preview::scene::kPreviewAllRenderLayers);

void paintCenterDisplay(
    QPainter& painter,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& canvasSize);

}  // namespace miacode::preview::hud

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
