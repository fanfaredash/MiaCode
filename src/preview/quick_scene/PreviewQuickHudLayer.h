#pragma once

#include <QQuickPaintedItem>

#include "preview/scene/PreviewLayerOrder.h"

class PreviewRuntime;
namespace miacode::preview::scene {
struct PreviewFrameState;
}

class PreviewQuickHudLayer : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit PreviewQuickHudLayer(QQuickItem* parent = nullptr);

    void setRuntime(PreviewRuntime* runtime);
    void setFrameState(const miacode::preview::scene::PreviewFrameState* frameState);
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);
    void paint(QPainter* painter) override;

private:
    PreviewRuntime* runtime_ = nullptr;
    const miacode::preview::scene::PreviewFrameState* frameState_ = nullptr;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
};
