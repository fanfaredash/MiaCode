#include "preview/runtime/PreviewQuickItem.h"

#include "preview/runtime/PreviewRuntime.h"

#include <QPainter>
#include <QQuickWindow>

PreviewQuickItem::PreviewQuickItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(false);
    setAntialiasing(false);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setPerformanceHint(QQuickPaintedItem::FastFBOResizing, true);
}

void PreviewQuickItem::setRuntime(PreviewRuntime* runtime)
{
    if (runtime_ == runtime) {
        return;
    }
    runtime_ = runtime;
    if (runtime_ != nullptr) {
        runtime_->setFrameSize(boundingRect().size().toSize());
    }
    update();
}

void PreviewQuickItem::setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags)
{
    if (layerFlags_ == layerFlags) {
        return;
    }
    layerFlags_ = layerFlags;
    update();
}

void PreviewQuickItem::paint(QPainter* painter)
{
    if (painter == nullptr) {
        return;
    }
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->fillRect(boundingRect(), Qt::transparent);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (runtime_ == nullptr) {
        return;
    }
    const qreal devicePixelRatio =
        window() != nullptr ? qMax<qreal>(1.0, window()->devicePixelRatio()) : 1.0;
    runtime_->paintLegacyFrame(*painter, boundingRect().size().toSize(), devicePixelRatio, layerFlags_);
}

void PreviewQuickItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (runtime_ == nullptr || newGeometry.size() == oldGeometry.size()) {
        return;
    }
    runtime_->setFrameSize(newGeometry.size().toSize());
}
