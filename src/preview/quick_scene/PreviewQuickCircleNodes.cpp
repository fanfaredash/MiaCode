#include "preview/quick_scene/PreviewQuickCircleNodes.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QtMath>

namespace {

constexpr int kCircleSegments = 48;
constexpr qreal kCircleEpsilon = 0.001;

QPointF ellipsePoint(const QPointF& center, qreal radiusX, qreal radiusY, qreal angleDegrees)
{
    const qreal radians = qDegreesToRadians(-angleDegrees);
    return QPointF(
        center.x() + radiusX * qCos(radians),
        center.y() + radiusY * qSin(radians)
    );
}

QSGGeometryNode* buildFilledEllipseNode(
    const QPointF& center,
    qreal radiusX,
    qreal radiusY,
    const QColor& color
)
{
    if (radiusX <= kCircleEpsilon || radiusY <= kCircleEpsilon || color.alpha() <= 0) {
        return nullptr;
    }

    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kCircleSegments * 3);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsPoint2D();
    for (int segmentIndex = 0; segmentIndex < kCircleSegments; ++segmentIndex) {
        const qreal t0 = static_cast<qreal>(segmentIndex) / static_cast<qreal>(kCircleSegments);
        const qreal t1 = static_cast<qreal>(segmentIndex + 1) / static_cast<qreal>(kCircleSegments);
        const QPointF p0 = ellipsePoint(center, radiusX, radiusY, t0 * 360.0);
        const QPointF p1 = ellipsePoint(center, radiusX, radiusY, t1 * 360.0);
        const int vertexIndex = segmentIndex * 3;
        vertices[vertexIndex + 0].set(static_cast<float>(center.x()), static_cast<float>(center.y()));
        vertices[vertexIndex + 1].set(static_cast<float>(p0.x()), static_cast<float>(p0.y()));
        vertices[vertexIndex + 2].set(static_cast<float>(p1.x()), static_cast<float>(p1.y()));
    }

    auto* material = new QSGFlatColorMaterial();
    material->setColor(color);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setFlag(QSGNode::OwnsMaterial, true);
    return node;
}

QSGGeometryNode* buildStrokeEllipseNode(
    const QPointF& center,
    qreal radiusX,
    qreal radiusY,
    qreal strokeWidth,
    const QColor& color
)
{
    if (radiusX <= kCircleEpsilon || radiusY <= kCircleEpsilon || strokeWidth <= kCircleEpsilon || color.alpha() <= 0) {
        return nullptr;
    }

    const qreal outerRadiusX = radiusX + strokeWidth * 0.5;
    const qreal outerRadiusY = radiusY + strokeWidth * 0.5;
    const qreal innerRadiusX = qMax<qreal>(0.0, radiusX - strokeWidth * 0.5);
    const qreal innerRadiusY = qMax<qreal>(0.0, radiusY - strokeWidth * 0.5);
    if (innerRadiusX <= kCircleEpsilon || innerRadiusY <= kCircleEpsilon) {
        return buildFilledEllipseNode(center, outerRadiusX, outerRadiusY, color);
    }

    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kCircleSegments * 6);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsPoint2D();
    for (int segmentIndex = 0; segmentIndex < kCircleSegments; ++segmentIndex) {
        const qreal t0 = static_cast<qreal>(segmentIndex) / static_cast<qreal>(kCircleSegments);
        const qreal t1 = static_cast<qreal>(segmentIndex + 1) / static_cast<qreal>(kCircleSegments);
        const QPointF outer0 = ellipsePoint(center, outerRadiusX, outerRadiusY, t0 * 360.0);
        const QPointF outer1 = ellipsePoint(center, outerRadiusX, outerRadiusY, t1 * 360.0);
        const QPointF inner0 = ellipsePoint(center, innerRadiusX, innerRadiusY, t0 * 360.0);
        const QPointF inner1 = ellipsePoint(center, innerRadiusX, innerRadiusY, t1 * 360.0);
        const int vertexIndex = segmentIndex * 6;
        vertices[vertexIndex + 0].set(static_cast<float>(outer0.x()), static_cast<float>(outer0.y()));
        vertices[vertexIndex + 1].set(static_cast<float>(outer1.x()), static_cast<float>(outer1.y()));
        vertices[vertexIndex + 2].set(static_cast<float>(inner0.x()), static_cast<float>(inner0.y()));
        vertices[vertexIndex + 3].set(static_cast<float>(outer1.x()), static_cast<float>(outer1.y()));
        vertices[vertexIndex + 4].set(static_cast<float>(inner1.x()), static_cast<float>(inner1.y()));
        vertices[vertexIndex + 5].set(static_cast<float>(inner0.x()), static_cast<float>(inner0.y()));
    }

    auto* material = new QSGFlatColorMaterial();
    material->setColor(color);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setFlag(QSGNode::OwnsMaterial, true);
    return node;
}

}  // namespace

QSGNode* buildPreviewCircleNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewCircleDescriptors& circles
)
{
    delete oldNode;
    if (circles.isEmpty()) {
        return nullptr;
    }

    auto* root = new QSGNode();
    for (const miacode::preview::scene::PreviewCircleDescriptor& circle : circles) {
        if (QSGGeometryNode* fillNode = buildFilledEllipseNode(
                circle.center,
                circle.radiusX,
                circle.radiusY,
                circle.fillColor
            )) {
            root->appendChildNode(fillNode);
        }
        if (QSGGeometryNode* strokeNode = buildStrokeEllipseNode(
                circle.center,
                circle.radiusX,
                circle.radiusY,
                circle.strokeWidth,
                circle.strokeColor
            )) {
            root->appendChildNode(strokeNode);
        }
    }

    if (root->firstChild() == nullptr) {
        delete root;
        return nullptr;
    }
    return root;
}
