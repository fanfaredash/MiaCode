#include "preview/quick_scene/PreviewQuickArcNodes.h"

#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGTexture>
#include <QSGTextureMaterial>
#include <QtMath>

namespace {

QSGGeometryNode* buildArcNode(
    const miacode::preview::scene::PreviewArcDescriptor& arc,
    PreviewTextureRepository* textures
)
{
    if (arc.image == nullptr || arc.image->isNull() || textures == nullptr) {
        return nullptr;
    }

    constexpr int kSegments = 48;
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), kSegments + 2);
    geometry->setDrawingMode(QSGGeometry::DrawTriangleFan);
    auto* vertices = geometry->vertexDataAsTexturedPoint2D();

    const float rx = static_cast<float>(arc.width * 0.5);
    const float ry = static_cast<float>(arc.height * 0.5);
    vertices[0].set(static_cast<float>(arc.center.x()), static_cast<float>(arc.center.y()), 0.5f, 0.5f);

    for (int index = 0; index <= kSegments; ++index) {
        const qreal t = static_cast<qreal>(index) / static_cast<qreal>(kSegments);
        const qreal angleDegrees = arc.startDegrees + arc.sweepDegrees * t;
        const qreal radians = qDegreesToRadians(-angleDegrees);
        const float x = rx * static_cast<float>(qCos(radians));
        const float y = ry * static_cast<float>(qSin(radians));
        const float px = static_cast<float>(arc.center.x()) + x;
        const float py = static_cast<float>(arc.center.y()) + y;
        const float u = 0.5f + (arc.width > 0.0 ? x / static_cast<float>(arc.width) : 0.0f);
        const float v = 0.5f + (arc.height > 0.0 ? y / static_cast<float>(arc.height) : 0.0f);
        vertices[index + 1].set(px, py, u, v);
    }

    auto* material = new QSGTextureMaterial();
    material->setTexture(textures->textureForImage(*arc.image, arc.cacheable));
    material->setFiltering(QSGTexture::Linear);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial, true);
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
        auto* opacityNode = new QSGOpacityNode();
        opacityNode->setOpacity(arc.opacity);
        if (QSGGeometryNode* node = buildArcNode(arc, textures)) {
            opacityNode->appendChildNode(node);
            root->appendChildNode(opacityNode);
        } else {
            delete opacityNode;
        }
    }
    return root;
}
