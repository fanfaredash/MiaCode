#include "timeline/quick/TimelineQuickLayerUtils.h"

#include <QPainter>
#include <QPainterPath>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGSimpleRectNode>
#include <QSGTextureMaterial>
#include <QSGTransformNode>
#include <QtMath>

#include <cstring>

QImage makeTimelineGlyphImage(const miacode::timeline::TimelineSceneGlyph& glyph)
{
    const QSize size(qMax(1, qCeil(glyph.rect.width())), qMax(1, qCeil(glyph.rect.height())));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(glyph.fillColor);
    painter.setPen(glyph.strokeWidth > 0.0 ? QPen(glyph.strokeColor, glyph.strokeWidth) : Qt::NoPen);
    const QRectF localRect(0.5, 0.5, size.width() - 1.0, size.height() - 1.0);
    switch (glyph.shape) {
    case miacode::timeline::TimelineSceneGlyphShape::Circle:
        painter.drawEllipse(localRect);
        break;
    case miacode::timeline::TimelineSceneGlyphShape::Diamond: {
        QPainterPath path;
        path.moveTo(localRect.center().x(), localRect.top());
        path.lineTo(localRect.right(), localRect.center().y());
        path.lineTo(localRect.center().x(), localRect.bottom());
        path.lineTo(localRect.left(), localRect.center().y());
        path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case miacode::timeline::TimelineSceneGlyphShape::RoundedRect:
        painter.drawRoundedRect(localRect, glyph.radius, glyph.radius);
        break;
    }
    return image;
}

TimelineQuickRasterizedImage makeTimelineTextImage(
    const miacode::timeline::TimelineSceneTextLabel& label,
    qreal devicePixelRatio)
{
    const qreal dpr = qMax<qreal>(1.0, devicePixelRatio);
    const QFontMetricsF metrics(label.font);
    const QSizeF logicalSize(
        qMax<qreal>(1.0, qCeil(metrics.horizontalAdvance(label.text) + 4.0)),
        qMax<qreal>(1.0, qCeil(metrics.height() + 2.0)));
    const QSize pixelSize(
        qMax(1, qCeil(logicalSize.width() * dpr)),
        qMax(1, qCeil(logicalSize.height() * dpr)));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(label.color);
    painter.setFont(label.font);
    painter.scale(dpr, dpr);
    painter.drawText(QPointF(2.0, metrics.ascent() + 1.0), label.text);
    image.setDevicePixelRatio(dpr);
    return TimelineQuickRasterizedImage{image, logicalSize};
}

QSGNode* buildTimelineLineNode(const miacode::timeline::TimelineSceneLine& line)
{
    const qreal dx = line.end.x() - line.start.x();
    const qreal dy = line.end.y() - line.start.y();
    const qreal length = qSqrt(dx * dx + dy * dy);
    if (length <= 0.0) {
        return new QSGNode();
    }

    const qreal lineWidth = qMax<qreal>(1.0, line.width);
    const bool vertical = qAbs(dx) <= 0.001;
    const bool horizontal = qAbs(dy) <= 0.001;
    if (vertical) {
        const qreal x = line.start.x() - (lineWidth * 0.5);
        const qreal top = qMin(line.start.y(), line.end.y());
        return new QSGSimpleRectNode(
            QRectF(x, top, lineWidth, qAbs(dy)),
            line.color);
    }
    if (horizontal) {
        const qreal y = line.start.y() - (lineWidth * 0.5);
        const qreal left = qMin(line.start.x(), line.end.x());
        return new QSGSimpleRectNode(
            QRectF(left, y, qAbs(dx), lineWidth),
            line.color);
    }

    auto* transformNode = new QSGTransformNode();
    auto* rectNode = new QSGSimpleRectNode(QRectF(0.0, -lineWidth / 2.0, length, lineWidth), line.color);
    QMatrix4x4 matrix;
    matrix.translate(line.start.x(), line.start.y());
    matrix.rotate(qRadiansToDegrees(qAtan2(dy, dx)), 0.0f, 0.0f, 1.0f);
    transformNode->setMatrix(matrix);
    transformNode->appendChildNode(rectNode);
    return transformNode;
}

QSGNode* buildTimelineRectNode(const miacode::timeline::TimelineSceneRect& rect)
{
    return new QSGSimpleRectNode(rect.rect, rect.color);
}

QSGNode* buildTimelineTriangleNode(const miacode::timeline::TimelineSceneTriangle& triangle)
{
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 3);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    QSGGeometry::Point2D* vertices = geometry->vertexDataAsPoint2D();
    vertices[0].set(static_cast<float>(triangle.a.x()), static_cast<float>(triangle.a.y()));
    vertices[1].set(static_cast<float>(triangle.b.x()), static_cast<float>(triangle.b.y()));
    vertices[2].set(static_cast<float>(triangle.c.x()), static_cast<float>(triangle.c.y()));

    auto* material = new QSGFlatColorMaterial();
    material->setColor(triangle.color);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// ---------------------------------------------------------------------
// TimelineQuickFlatColorBatchBuilder
//
// Coalesces consecutive axis-aligned rects sharing a color into a
// single QSGGeometryNode with N*6 vertices. Sibling of the sprite
// builder; same triangle-list layout, but Point2D (no UV) and a
// QSGFlatColorMaterial. Used by grid-line and waveform-bar emit paths
// where data has long same-color runs but per-line/per-bar QSG node
// allocation was the prior bottleneck.

TimelineQuickFlatColorBatchBuilder::TimelineQuickFlatColorBatchBuilder(QSGNode* parent)
    : parent_(parent)
{
    pendingVertices_.reserve(4096);
}

void TimelineQuickFlatColorBatchBuilder::appendRect(const QColor& color, const QRectF& rect)
{
    if (parent_ == nullptr || !rect.isValid() || rect.width() <= 0.0 || rect.height() <= 0.0) {
        return;
    }
    // Color rgba() comparison — themes with slightly-different colours
    // don't merge, which is correct: each color needs its own material
    // anyway. Equal-rgba colors definitely merge.
    if (currentColorValid_ && currentColor_.rgba() != color.rgba()) {
        flush();
    }
    currentColor_ = color;
    currentColorValid_ = true;

    const float xL = static_cast<float>(rect.left());
    const float xR = static_cast<float>(rect.right());
    const float yT = static_cast<float>(rect.top());
    const float yB = static_cast<float>(rect.bottom());
    QSGGeometry::Point2D v;
    v.set(xL, yT); pendingVertices_.append(v);
    v.set(xR, yT); pendingVertices_.append(v);
    v.set(xL, yB); pendingVertices_.append(v);
    v.set(xL, yB); pendingVertices_.append(v);
    v.set(xR, yT); pendingVertices_.append(v);
    v.set(xR, yB); pendingVertices_.append(v);
    ++totalQuads_;
}

bool TimelineQuickFlatColorBatchBuilder::tryAppendOrthogonalLine(
    const miacode::timeline::TimelineSceneLine& line)
{
    const qreal dx = line.end.x() - line.start.x();
    const qreal dy = line.end.y() - line.start.y();
    const bool vertical = qAbs(dx) <= 0.001;
    const bool horizontal = qAbs(dy) <= 0.001;
    if (!vertical && !horizontal) {
        return false;  // diagonal — caller falls back to legacy path.
    }
    const qreal lineWidth = qMax<qreal>(1.0, line.width);
    if (vertical) {
        const qreal x = line.start.x() - (lineWidth * 0.5);
        const qreal top = qMin(line.start.y(), line.end.y());
        appendRect(line.color, QRectF(x, top, lineWidth, qAbs(dy)));
    } else {
        const qreal y = line.start.y() - (lineWidth * 0.5);
        const qreal left = qMin(line.start.x(), line.end.x());
        appendRect(line.color, QRectF(left, y, qAbs(dx), lineWidth));
    }
    return true;
}

void TimelineQuickFlatColorBatchBuilder::flush()
{
    if (parent_ == nullptr || pendingVertices_.isEmpty() || !currentColorValid_) {
        pendingVertices_.clear();
        currentColorValid_ = false;
        return;
    }

    auto* geometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_Point2D(),
        pendingVertices_.size());
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    QSGGeometry::Point2D* dst = geometry->vertexDataAsPoint2D();
    std::memcpy(dst,
                pendingVertices_.constData(),
                static_cast<size_t>(pendingVertices_.size())
                    * sizeof(QSGGeometry::Point2D));

    auto* material = new QSGFlatColorMaterial();
    material->setColor(currentColor_);
    // Beta21-fix6 — force the translucent rendering path even for
    // alpha-255 colors. QSGFlatColorMaterial's setColor() clears the
    // Blending flag whenever alpha == 255, which puts the geometry
    // into Qt 6's opaque batch path. The opaque path is permitted to
    // reorder draws for early-Z rejection, and on D3D11 the result is
    // that opaque grid bars (alpha 255) can be drawn BEFORE the
    // opaque waveform bars even though the scene graph has them at a
    // higher z-position than the waveform layer. Setting Blending=true
    // forces strict scene-graph order (back-to-front translucent
    // batch), so a bar line emitted after the waveform genuinely lands
    // on top of waveform pixels. The cost is negligible — at most a
    // few grid-line batches go through the alpha-blend pipeline once
    // per gridRevision change.
    material->setFlag(QSGMaterial::Blending, true);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setFlag(QSGNode::OwnsMaterial);
    parent_->appendChildNode(node);

    ++totalBatches_;
    pendingVertices_.clear();
    currentColorValid_ = false;
}

// ---------------------------------------------------------------------
// TimelineQuickSpriteBatchBuilder
//
// Coalesces consecutive textured quads sharing a texture into a single
// QSGGeometryNode with N*6 vertices (triangle list — 2 triangles per
// quad). On a texture transition, finishes the current batch as a node
// and starts a new accumulator. Mirrors PreviewDCompSpritePipeline's
// DrawRun coalescing on the new pipeline side.

TimelineQuickSpriteBatchBuilder::TimelineQuickSpriteBatchBuilder(QSGNode* parent)
    : parent_(parent)
{
    // Reserve a generous starting capacity so the typical chart's hot
    // batch (e.g. all track-segment sprites of one type) never re-
    // allocates mid-fill. 4096 verts = 682 quads; charts tend to have
    // dense same-texture runs of a few thousand notes, so this avoids
    // the small-buffer doubling cascade.
    pendingVertices_.reserve(4096);
}

void TimelineQuickSpriteBatchBuilder::appendQuad(QSGTexture* texture, const QRectF& rect)
{
    appendQuad(texture, rect, QRectF(0.0, 0.0, 1.0, 1.0));
}

void TimelineQuickSpriteBatchBuilder::appendQuad(QSGTexture* texture,
                                                  const QRectF& rect,
                                                  const QRectF& uvRect)
{
    if (parent_ == nullptr || texture == nullptr || !rect.isValid()) {
        return;
    }
    // Texture changed → flush previous batch before adding this quad.
    if (currentTexture_ != nullptr && currentTexture_ != texture) {
        flush();
    }
    currentTexture_ = texture;

    // Triangle-list layout: 2 triangles per quad, 6 verts total.
    // Vertex order: TL, TR, BL, BL, TR, BR.
    // No index buffer — the redundancy is small (6 vs 4) and skipping
    // index management keeps the streaming append fast.
    const float xL = static_cast<float>(rect.left());
    const float xR = static_cast<float>(rect.right());
    const float yT = static_cast<float>(rect.top());
    const float yB = static_cast<float>(rect.bottom());
    const float uL = static_cast<float>(uvRect.left());
    const float uR = static_cast<float>(uvRect.right());
    const float vT = static_cast<float>(uvRect.top());
    const float vB = static_cast<float>(uvRect.bottom());

    QSGGeometry::TexturedPoint2D v;
    v.set(xL, yT, uL, vT); pendingVertices_.append(v);
    v.set(xR, yT, uR, vT); pendingVertices_.append(v);
    v.set(xL, yB, uL, vB); pendingVertices_.append(v);
    v.set(xL, yB, uL, vB); pendingVertices_.append(v);
    v.set(xR, yT, uR, vT); pendingVertices_.append(v);
    v.set(xR, yB, uR, vB); pendingVertices_.append(v);

    ++totalQuads_;
}

void TimelineQuickSpriteBatchBuilder::flush()
{
    if (parent_ == nullptr || currentTexture_ == nullptr || pendingVertices_.isEmpty()) {
        currentTexture_ = nullptr;
        pendingVertices_.clear();
        return;
    }

    auto* geometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_TexturedPoint2D(),
        pendingVertices_.size());
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    QSGGeometry::TexturedPoint2D* dst = geometry->vertexDataAsTexturedPoint2D();
    std::memcpy(dst,
                pendingVertices_.constData(),
                static_cast<size_t>(pendingVertices_.size())
                    * sizeof(QSGGeometry::TexturedPoint2D));

    // QSGTextureMaterial honours per-pixel alpha (chart sprites have
    // transparent borders, hold caps overlap their bodies on the alpha
    // channel). QSGOpaqueTextureMaterial would be wrong even for sprite
    // types whose interior is opaque, because the TRANSITION pixels
    // along the border are partial alpha.
    auto* material = new QSGTextureMaterial();
    material->setTexture(currentTexture_);
    material->setFiltering(QSGTexture::Linear);
    material->setMipmapFiltering(QSGTexture::None);
    material->setHorizontalWrapMode(QSGTexture::ClampToEdge);
    material->setVerticalWrapMode(QSGTexture::ClampToEdge);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setFlag(QSGNode::OwnsMaterial);
    parent_->appendChildNode(node);

    ++totalBatches_;
    pendingVertices_.clear();
    currentTexture_ = nullptr;
}
