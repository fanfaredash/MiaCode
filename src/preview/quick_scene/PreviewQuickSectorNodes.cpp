#include "preview/quick_scene/PreviewQuickSectorNodes.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QtMath>

namespace {

constexpr qreal kSectorEpsilon = 0.001;

QPointF circlePoint(const QPointF& center, qreal radius, qreal angleDegrees)
{
    const qreal radians = qDegreesToRadians(-angleDegrees);
    return QPointF(
        center.x() + radius * qCos(radians),
        center.y() + radius * qSin(radians)
    );
}

QSGGeometryNode* buildSectorNode(const miacode::preview::scene::PreviewSectorDescriptor& sector)
{
    if (sector.outerRadius <= kSectorEpsilon
        || qAbs(sector.sweepDegrees) <= kSectorEpsilon
        || sector.color.alpha() <= 0) {
        return nullptr;
    }

    const int segmentCount = qMax(1, qCeil(qAbs(sector.sweepDegrees) / 8.0));
    const bool annulus = sector.innerRadius > kSectorEpsilon;
    const int vertexCount = segmentCount * (annulus ? 6 : 3);
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertexCount);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsPoint2D();

    for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        const qreal t0 = static_cast<qreal>(segmentIndex) / static_cast<qreal>(segmentCount);
        const qreal t1 = static_cast<qreal>(segmentIndex + 1) / static_cast<qreal>(segmentCount);
        const QPointF outer0 = circlePoint(
            sector.center,
            sector.outerRadius,
            sector.startDegrees + sector.sweepDegrees * t0
        );
        const QPointF outer1 = circlePoint(
            sector.center,
            sector.outerRadius,
            sector.startDegrees + sector.sweepDegrees * t1
        );

        if (!annulus) {
            const int vertexIndex = segmentIndex * 3;
            vertices[vertexIndex + 0].set(static_cast<float>(sector.center.x()), static_cast<float>(sector.center.y()));
            vertices[vertexIndex + 1].set(static_cast<float>(outer0.x()), static_cast<float>(outer0.y()));
            vertices[vertexIndex + 2].set(static_cast<float>(outer1.x()), static_cast<float>(outer1.y()));
            continue;
        }

        const QPointF inner0 = circlePoint(
            sector.center,
            sector.innerRadius,
            sector.startDegrees + sector.sweepDegrees * t0
        );
        const QPointF inner1 = circlePoint(
            sector.center,
            sector.innerRadius,
            sector.startDegrees + sector.sweepDegrees * t1
        );
        const int vertexIndex = segmentIndex * 6;
        vertices[vertexIndex + 0].set(static_cast<float>(outer0.x()), static_cast<float>(outer0.y()));
        vertices[vertexIndex + 1].set(static_cast<float>(outer1.x()), static_cast<float>(outer1.y()));
        vertices[vertexIndex + 2].set(static_cast<float>(inner0.x()), static_cast<float>(inner0.y()));
        vertices[vertexIndex + 3].set(static_cast<float>(outer1.x()), static_cast<float>(outer1.y()));
        vertices[vertexIndex + 4].set(static_cast<float>(inner1.x()), static_cast<float>(inner1.y()));
        vertices[vertexIndex + 5].set(static_cast<float>(inner0.x()), static_cast<float>(inner0.y()));
    }

    auto* material = new QSGFlatColorMaterial();
    material->setColor(sector.color);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setFlag(QSGNode::OwnsMaterial, true);
    return node;
}

}  // namespace

QSGNode* buildPreviewSectorNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewSectorDescriptors& sectors
)
{
    delete oldNode;
    if (sectors.isEmpty()) {
        return nullptr;
    }

    auto* root = new QSGNode();
    for (const miacode::preview::scene::PreviewSectorDescriptor& sector : sectors) {
        if (QSGGeometryNode* node = buildSectorNode(sector)) {
            root->appendChildNode(node);
        }
    }

    if (root->firstChild() == nullptr) {
        delete root;
        return nullptr;
    }
    return root;
}
