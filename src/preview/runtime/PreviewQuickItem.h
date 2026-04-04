#pragma once

#include <QQuickPaintedItem>

#include "preview/scene/PreviewLayerOrder.h"

class PreviewRuntime;

class PreviewQuickItem : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit PreviewQuickItem(QQuickItem* parent = nullptr);

    void setRuntime(PreviewRuntime* runtime);
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);

    void paint(QPainter* painter) override;

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    PreviewRuntime* runtime_ = nullptr;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewLegacyBridgeLayers;
};
