#include "preview/quick_scene/PreviewQuickArcNodes.h"

#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtMath>

namespace {

QSGClipNode* buildArcClipNode(
    const miacode::preview::scene::PreviewArcDescriptor& arc,
    QSGTexture* texture
)
{
    if (arc.image == nullptr || arc.image->isNull() || texture == nullptr) {
        return nullptr;
    }

    constexpr int kSegments = 48;
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kSegments * 3);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsPoint2D();

    const float rx = static_cast<float>(arc.width * 0.5);
    const float ry = static_cast<float>(arc.height * 0.5);
    const auto pointForAngleDegrees = [&](qreal angleDegrees) {
        const qreal radians = qDegreesToRadians(-angleDegrees);
        return QPointF(
            arc.center.x() + rx * static_cast<float>(qCos(radians)),
            arc.center.y() + ry * static_cast<float>(qSin(radians))
        );
    };

    for (int index = 0; index < kSegments; ++index) {
        const qreal t0 = static_cast<qreal>(index) / static_cast<qreal>(kSegments);
        const qreal t1 = static_cast<qreal>(index + 1) / static_cast<qreal>(kSegments);
        const QPointF p0 = pointForAngleDegrees(arc.startDegrees + arc.sweepDegrees * t0);
        const QPointF p1 = pointForAngleDegrees(arc.startDegrees + arc.sweepDegrees * t1);
        const int vertexIndex = index * 3;
        vertices[vertexIndex + 0].set(static_cast<float>(arc.center.x()), static_cast<float>(arc.center.y()));
        vertices[vertexIndex + 1].set(static_cast<float>(p0.x()), static_cast<float>(p0.y()));
        vertices[vertexIndex + 2].set(static_cast<float>(p1.x()), static_cast<float>(p1.y()));
    }

    auto* node = new QSGClipNode();
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setIsRectangular(false);
    node->setClipRect(QRectF(
        arc.center.x() - arc.width / 2.0,
        arc.center.y() - arc.height / 2.0,
        arc.width,
        arc.height
    ));

    auto* textureNode = new QSGSimpleTextureNode();
    textureNode->setOwnsTexture(false);
    textureNode->setTexture(texture);
    textureNode->setFiltering(QSGTexture::Linear);
    textureNode->setRect(QRectF(
        arc.center.x() - arc.width / 2.0,
        arc.center.y() - arc.height / 2.0,
        arc.width,
        arc.height
    ));
    node->appendChildNode(textureNode);
    return node;
}

}  // namespace

QSGNode* buildPreviewArcNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewArcDescriptors& arcs,
    QQuickWindow* window,
    PreviewTextureRepository* textures
)
{
    delete oldNode;
    if (window == nullptr || textures == nullptr || arcs.isEmpty()) {
        return nullptr;
    }

    auto* root = new QSGNode();
    for (const miacode::preview::scene::PreviewArcDescriptor& arc : arcs) {
        if (arc.image == nullptr || arc.image->isNull() || arc.opacity <= 0.0 || arc.width <= 0.0 || arc.height <= 0.0 || qFuzzyIsNull(arc.sweepDegrees)) {
            continue;
        }
        QSGTexture* texture = textures->textureForImage(*arc.image, arc.cacheable);
        if (texture == nullptr) {
            continue;
        }
        auto* opacityNode = new QSGOpacityNode();
        opacityNode->setOpacity(arc.opacity);
        if (QSGClipNode* node = buildArcClipNode(arc, texture)) {
            opacityNode->appendChildNode(node);
            root->appendChildNode(opacityNode);
        } else {
            delete opacityNode;
        }
    }
    return root;
}
