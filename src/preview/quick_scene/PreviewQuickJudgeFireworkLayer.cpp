#include "preview/quick_scene/PreviewQuickJudgeFireworkLayer.h"

#include "preview/quick_scene/PreviewQuickSectorNodes.h"
#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewJudgeFireworkLayerState.h"
#include "preview/scene/PreviewSceneGeometry.h"

#include <QSGClipNode>
#include <QSGGeometry>
#include <QtMath>

namespace {

QSGClipNode* buildEllipseClipNode(const QPointF& center, qreal radius)
{
    if (radius <= 0.0) {
        return nullptr;
    }

    constexpr int kSegments = 64;
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kSegments * 3);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsPoint2D();

    for (int segmentIndex = 0; segmentIndex < kSegments; ++segmentIndex) {
        const qreal t0 = static_cast<qreal>(segmentIndex) / static_cast<qreal>(kSegments);
        const qreal t1 = static_cast<qreal>(segmentIndex + 1) / static_cast<qreal>(kSegments);
        const qreal angle0 = -t0 * 360.0;
        const qreal angle1 = -t1 * 360.0;
        const QPointF p0(
            center.x() + radius * qCos(qDegreesToRadians(angle0)),
            center.y() + radius * qSin(qDegreesToRadians(angle0))
        );
        const QPointF p1(
            center.x() + radius * qCos(qDegreesToRadians(angle1)),
            center.y() + radius * qSin(qDegreesToRadians(angle1))
        );
        const int vertexIndex = segmentIndex * 3;
        vertices[vertexIndex + 0].set(static_cast<float>(center.x()), static_cast<float>(center.y()));
        vertices[vertexIndex + 1].set(static_cast<float>(p0.x()), static_cast<float>(p0.y()));
        vertices[vertexIndex + 2].set(static_cast<float>(p1.x()), static_cast<float>(p1.y()));
    }

    auto* node = new QSGClipNode();
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setIsRectangular(false);
    node->setClipRect(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0));
    return node;
}

}  // namespace

QSGNode* PreviewQuickJudgeFireworkLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    delete oldNode;
    if (window == nullptr || textures == nullptr) {
        return nullptr;
    }

    const miacode::preview::scene::PreviewJudgeFireworkLayerState layerState =
        miacode::preview::scene::buildPreviewJudgeFireworkLayerState(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        );
    if (layerState.sectors.isEmpty() && layerState.sprites.isEmpty()) {
        return nullptr;
    }

    auto* root = new QSGNode();
    QSGNode* contentRoot = root;
    if (QSGClipNode* clipNode = buildEllipseClipNode(layerState.clipCenter, layerState.clipRadius)) {
        root->appendChildNode(clipNode);
        contentRoot = clipNode;
    }
    if (QSGNode* sectorNode = buildPreviewSectorNodeTree(nullptr, layerState.sectors)) {
        contentRoot->appendChildNode(sectorNode);
    }
    if (QSGNode* spriteNode = buildPreviewSpriteNodeTree(nullptr, layerState.sprites, window, textures)) {
        contentRoot->appendChildNode(spriteNode);
    }

    if (root->firstChild() == nullptr || contentRoot->firstChild() == nullptr) {
        delete root;
        return nullptr;
    }
    return root;
}
