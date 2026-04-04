#include "preview/quick_scene/PreviewQuickLayerRenderNode.h"

#include "preview/runtime/PreviewRuntime.h"

#include <QOpenGLPaintDevice>
#include <QPainter>

void PreviewQuickLayerRenderNode::configure(
    PreviewRuntime* runtime,
    const QSize& logicalSize,
    qreal devicePixelRatio,
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags
)
{
    runtime_ = runtime;
    logicalSize_ = logicalSize;
    devicePixelRatio_ = qMax<qreal>(1.0, devicePixelRatio);
    layerFlags_ = layerFlags;
    markDirty(QSGNode::DirtyMaterial);
}

QSGRenderNode::RenderingFlags PreviewQuickLayerRenderNode::flags() const
{
    return QSGRenderNode::BoundedRectRendering;
}

QSGRenderNode::StateFlags PreviewQuickLayerRenderNode::changedStates() const
{
    return QSGRenderNode::DepthState
        | QSGRenderNode::StencilState
        | QSGRenderNode::ScissorState
        | QSGRenderNode::ColorState
        | QSGRenderNode::BlendState
        | QSGRenderNode::CullState
        | QSGRenderNode::ViewportState
        | QSGRenderNode::RenderTargetState;
}

QRectF PreviewQuickLayerRenderNode::rect() const
{
    return QRectF(QPointF(0.0, 0.0), QSizeF(logicalSize_));
}

void PreviewQuickLayerRenderNode::render(const RenderState* state)
{
    Q_UNUSED(state);
    if (runtime_ == nullptr || logicalSize_.isEmpty() || layerFlags_ == 0) {
        return;
    }

    const QSize pixelSize(
        qMax(1, qRound(static_cast<qreal>(logicalSize_.width()) * devicePixelRatio_)),
        qMax(1, qRound(static_cast<qreal>(logicalSize_.height()) * devicePixelRatio_))
    );
    QOpenGLPaintDevice device(pixelSize);
    device.setDevicePixelRatio(devicePixelRatio_);

    QPainter painter(&device);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    runtime_->paintLegacyFrame(
        painter,
        logicalSize_,
        devicePixelRatio_,
        layerFlags_,
        true,
        false
    );
    painter.end();
}

void PreviewQuickLayerRenderNode::releaseResources()
{
}
