#include "preview/quick_scene/PreviewQuickArcNodes.h"

#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QVector>
#include <QtMath>

namespace {

using miacode::preview::scene::PreviewArcDescriptor;

constexpr int kArcClipSegments = 48;

QRectF arcRect(const PreviewArcDescriptor& arc)
{
    return QRectF(
        arc.center.x() - arc.width / 2.0,
        arc.center.y() - arc.height / 2.0,
        arc.width,
        arc.height
    );
}

bool isRenderableArc(const PreviewArcDescriptor& arc)
{
    return arc.image != nullptr
        && !arc.image->isNull()
        && arc.opacity > 0.0
        && arc.width > 0.0
        && arc.height > 0.0
        && !qFuzzyIsNull(arc.sweepDegrees);
}

void updateArcClipGeometry(
    QSGGeometry* geometry,
    const PreviewArcDescriptor& arc
)
{
    Q_ASSERT(geometry != nullptr);
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

    for (int index = 0; index < kArcClipSegments; ++index) {
        const qreal t0 = static_cast<qreal>(index) / static_cast<qreal>(kArcClipSegments);
        const qreal t1 = static_cast<qreal>(index + 1) / static_cast<qreal>(kArcClipSegments);
        const QPointF p0 = pointForAngleDegrees(arc.startDegrees + arc.sweepDegrees * t0);
        const QPointF p1 = pointForAngleDegrees(arc.startDegrees + arc.sweepDegrees * t1);
        const int vertexIndex = index * 3;
        vertices[vertexIndex + 0].set(static_cast<float>(arc.center.x()), static_cast<float>(arc.center.y()));
        vertices[vertexIndex + 1].set(static_cast<float>(p0.x()), static_cast<float>(p0.y()));
        vertices[vertexIndex + 2].set(static_cast<float>(p1.x()), static_cast<float>(p1.y()));
    }
    geometry->markVertexDataDirty();
}

class PreviewQuickArcEntryNode final : public QSGOpacityNode
{
public:
    PreviewQuickArcEntryNode()
    {
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kArcClipSegments * 3);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);

        clipNode_ = new QSGClipNode();
        clipNode_->setGeometry(geometry_);
        clipNode_->setFlag(QSGNode::OwnsGeometry, true);
        clipNode_->setIsRectangular(false);

        textureNode_ = new QSGSimpleTextureNode();
        textureNode_->setOwnsTexture(false);
        clipNode_->appendChildNode(textureNode_);
        appendChildNode(clipNode_);
    }

    void updateArc(const PreviewArcDescriptor& arc, QSGTexture* texture)
    {
        Q_ASSERT(texture != nullptr);

        updateArcClipGeometry(geometry_, arc);
        clipNode_->setClipRect(arcRect(arc));
        clipNode_->markDirty(QSGNode::DirtyGeometry);

        setOpacity(arc.opacity);

        textureNode_->setTexture(texture);
        textureNode_->setFiltering(QSGTexture::Linear);
        textureNode_->setRect(arcRect(arc));
    }

private:
    QSGGeometry* geometry_ = nullptr;
    QSGClipNode* clipNode_ = nullptr;
    QSGSimpleTextureNode* textureNode_ = nullptr;
};

struct ResolvedArc {
    PreviewArcDescriptor descriptor;
    QSGTexture* texture = nullptr;
};

}  // namespace

QSGNode* buildPreviewArcNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewArcDescriptors& arcs,
    QQuickWindow* window,
    PreviewTextureRepository* textures
)
{
    Q_UNUSED(window);

    if (textures == nullptr || arcs.isEmpty()) {
        return nullptr;
    }

    QVector<ResolvedArc> resolvedArcs;
    resolvedArcs.reserve(arcs.size());
    for (const PreviewArcDescriptor& arc : arcs) {
        if (!isRenderableArc(arc)) {
            continue;
        }

        QSGTexture* texture = textures->textureForImage(*arc.image, arc.cacheable);
        if (texture == nullptr) {
            continue;
        }
        texture->setFiltering(QSGTexture::Linear);
        resolvedArcs.append(ResolvedArc{arc, texture});
    }

    if (resolvedArcs.isEmpty()) {
        return nullptr;
    }

    auto* root = oldNode != nullptr ? oldNode : new QSGNode();
    QVector<QSGNode*> existingChildren;
    for (QSGNode* child = root->firstChild(); child != nullptr; child = child->nextSibling()) {
        existingChildren.append(child);
    }

    int nodeIndex = 0;
    for (const ResolvedArc& arc : resolvedArcs) {
        QSGNode* existing = nodeIndex < existingChildren.size() ? existingChildren.at(nodeIndex) : nullptr;
        auto* entryNode = dynamic_cast<PreviewQuickArcEntryNode*>(existing);
        if (entryNode == nullptr) {
            auto* newNode = new PreviewQuickArcEntryNode();
            if (existing != nullptr) {
                root->insertChildNodeBefore(newNode, existing);
                root->removeChildNode(existing);
                delete existing;
                existingChildren[nodeIndex] = newNode;
            } else {
                root->appendChildNode(newNode);
                existingChildren.append(newNode);
            }
            entryNode = newNode;
        }

        entryNode->updateArc(arc.descriptor, arc.texture);
        ++nodeIndex;
    }

    for (int index = existingChildren.size() - 1; index >= nodeIndex; --index) {
        QSGNode* child = existingChildren.at(index);
        root->removeChildNode(child);
        delete child;
    }
    return root;
}
