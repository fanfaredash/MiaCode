#include "timeline/quick/TimelineQuickLayerUtils.h"

#include <QPainter>
#include <QPainterPath>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <QtMath>

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
