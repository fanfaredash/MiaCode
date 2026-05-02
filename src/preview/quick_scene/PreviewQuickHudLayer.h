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

// Phase 4f — standalone HUD painter, used by both the legacy
// PreviewQuickHudLayer (via QQuickPaintedItem::paint) and the DComp
// path (which rasterises into an offscreen QImage and uploads it as
// a textured sprite). All inputs are read-only; painter must be set
// up to draw within the canvas's local coordinate space (top-left
// origin, size canvasSize).
void paintPreviewHudOverlay(
    QPainter& painter,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& canvasSize,
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags
        = miacode::preview::scene::kPreviewAllRenderLayers);

}  // namespace miacode::preview::hud

class PreviewQuickHudLayer : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* runtime READ runtimeObject WRITE setRuntimeObject NOTIFY runtimeChanged)
    // Issue #4 fix — pair to PreviewQuickSceneRoot::dcompFallbackActive.
    // QML sets this on the fullscreen instance so the HUD renders via
    // the legacy QQuickPaintedItem path inside the fullscreen window.
    Q_PROPERTY(bool dcompFallbackActive READ dcompFallbackActive
               WRITE setDCompFallbackActive NOTIFY dcompFallbackActiveChanged)

public:
    explicit PreviewQuickHudLayer(QQuickItem* parent = nullptr);

    void setRuntime(PreviewRuntime* runtime);
    QObject* runtimeObject() const;
    void setRuntimeObject(QObject* runtimeObject);
    void setFrameState(const miacode::preview::scene::PreviewFrameState* frameState);
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);
    void paint(QPainter* painter) override;

    bool dcompFallbackActive() const { return dcompFallbackActive_; }
    void setDCompFallbackActive(bool active);

signals:
    void runtimeChanged();
    void dcompFallbackActiveChanged();

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
    bool dcompFallbackActive_ = false;
};
