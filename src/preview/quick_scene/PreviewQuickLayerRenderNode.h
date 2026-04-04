#pragma once

#include <QSGRenderNode>
#include <QSize>

#include "preview/scene/PreviewLayerOrder.h"

class PreviewRuntime;

class PreviewQuickLayerRenderNode : public QSGRenderNode
{
public:
    void configure(
        PreviewRuntime* runtime,
        const QSize& logicalSize,
        qreal devicePixelRatio,
        miacode::preview::scene::PreviewRenderLayerFlags layerFlags
    );

    RenderingFlags flags() const override;
    StateFlags changedStates() const override;
    QRectF rect() const override;
    void render(const RenderState* state) override;
    void releaseResources() override;

private:
    PreviewRuntime* runtime_ = nullptr;
    QSize logicalSize_;
    qreal devicePixelRatio_ = 1.0;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ = 0;
};
