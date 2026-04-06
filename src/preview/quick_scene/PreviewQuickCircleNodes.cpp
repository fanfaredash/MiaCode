#include "preview/quick_scene/PreviewQuickCircleNodes.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QVector>
#include <QtMath>

namespace {

using miacode::preview::scene::PreviewCircleDescriptor;

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

bool hasRenderableFill(const PreviewCircleDescriptor& circle)
{
    return circle.radiusX > kCircleEpsilon
        && circle.radiusY > kCircleEpsilon
        && circle.fillColor.alpha() > 0;
}

bool hasRenderableStroke(const PreviewCircleDescriptor& circle)
{
    return circle.radiusX > kCircleEpsilon
        && circle.radiusY > kCircleEpsilon
        && circle.strokeWidth > kCircleEpsilon
        && circle.strokeColor.alpha() > 0;
}

class PreviewQuickFilledEllipseNode final : public QSGGeometryNode
{
public:
    PreviewQuickFilledEllipseNode()
    {
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kCircleSegments * 3);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);

        material_ = new QSGFlatColorMaterial();
        setMaterial(material_);
        setFlag(QSGNode::OwnsMaterial, true);
    }

    void updateFill(
        const QPointF& center,
        qreal radiusX,
        qreal radiusY,
        const QColor& color
    )
    {
        auto* vertices = geometry_->vertexDataAsPoint2D();
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
        geometry_->markVertexDataDirty();

        material_->setColor(color);
        markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }

private:
    QSGGeometry* geometry_ = nullptr;
    QSGFlatColorMaterial* material_ = nullptr;
};

class PreviewQuickStrokeEllipseNode final : public QSGGeometryNode
{
public:
    PreviewQuickStrokeEllipseNode()
    {
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kCircleSegments * 6);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);

        material_ = new QSGFlatColorMaterial();
        setMaterial(material_);
        setFlag(QSGNode::OwnsMaterial, true);
    }

    void updateStroke(
        const QPointF& center,
        qreal radiusX,
        qreal radiusY,
        qreal strokeWidth,
        const QColor& color
    )
    {
        const qreal outerRadiusX = radiusX + strokeWidth * 0.5;
        const qreal outerRadiusY = radiusY + strokeWidth * 0.5;
        const qreal innerRadiusX = qMax<qreal>(0.0, radiusX - strokeWidth * 0.5);
        const qreal innerRadiusY = qMax<qreal>(0.0, radiusY - strokeWidth * 0.5);

        auto* vertices = geometry_->vertexDataAsPoint2D();
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
        geometry_->markVertexDataDirty();

        material_->setColor(color);
        markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }

private:
    QSGGeometry* geometry_ = nullptr;
    QSGFlatColorMaterial* material_ = nullptr;
};

class PreviewQuickCircleEntryNode final : public QSGNode
{
public:
    void updateCircle(const PreviewCircleDescriptor& circle)
    {
        if (hasRenderableFill(circle)) {
            auto* fillNode = ensureFillNode();
            fillNode->updateFill(circle.center, circle.radiusX, circle.radiusY, circle.fillColor);
        } else {
            removeFillNode();
        }

        if (hasRenderableStroke(circle)) {
            auto* strokeNode = ensureStrokeNode();
            strokeNode->updateStroke(
                circle.center,
                circle.radiusX,
                circle.radiusY,
                circle.strokeWidth,
                circle.strokeColor
            );
        } else {
            removeStrokeNode();
        }
    }

private:
    PreviewQuickFilledEllipseNode* ensureFillNode()
    {
        if (fillNode_ == nullptr) {
            fillNode_ = new PreviewQuickFilledEllipseNode();
            if (strokeNode_ != nullptr) {
                insertChildNodeBefore(fillNode_, strokeNode_);
            } else {
                appendChildNode(fillNode_);
            }
        }
        return fillNode_;
    }

    PreviewQuickStrokeEllipseNode* ensureStrokeNode()
    {
        if (strokeNode_ == nullptr) {
            strokeNode_ = new PreviewQuickStrokeEllipseNode();
            appendChildNode(strokeNode_);
        }
        return strokeNode_;
    }

    void removeFillNode()
    {
        if (fillNode_ == nullptr) {
            return;
        }
        removeChildNode(fillNode_);
        delete fillNode_;
        fillNode_ = nullptr;
    }

    void removeStrokeNode()
    {
        if (strokeNode_ == nullptr) {
            return;
        }
        removeChildNode(strokeNode_);
        delete strokeNode_;
        strokeNode_ = nullptr;
    }

    PreviewQuickFilledEllipseNode* fillNode_ = nullptr;
    PreviewQuickStrokeEllipseNode* strokeNode_ = nullptr;
};

bool isRenderableCircle(const PreviewCircleDescriptor& circle)
{
    return hasRenderableFill(circle) || hasRenderableStroke(circle);
}

}  // namespace

QSGNode* buildPreviewCircleNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewCircleDescriptors& circles
)
{
    if (circles.isEmpty()) {
        return nullptr;
    }

    auto* root = oldNode != nullptr ? oldNode : new QSGNode();
    const bool reusedRoot = root == oldNode;
    QVector<QSGNode*> existingChildren;
    for (QSGNode* child = root->firstChild(); child != nullptr; child = child->nextSibling()) {
        existingChildren.append(child);
    }

    int nodeIndex = 0;
    for (const PreviewCircleDescriptor& circle : circles) {
        if (!isRenderableCircle(circle)) {
            continue;
        }

        QSGNode* existing = nodeIndex < existingChildren.size() ? existingChildren.at(nodeIndex) : nullptr;
        auto* entryNode = dynamic_cast<PreviewQuickCircleEntryNode*>(existing);
        if (entryNode == nullptr) {
            auto* newNode = new PreviewQuickCircleEntryNode();
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

        entryNode->updateCircle(circle);
        ++nodeIndex;
    }

    if (nodeIndex == 0) {
        if (!reusedRoot) {
            delete root;
        }
        return nullptr;
    }

    for (int index = existingChildren.size() - 1; index >= nodeIndex; --index) {
        QSGNode* child = existingChildren.at(index);
        root->removeChildNode(child);
        delete child;
    }
    return root;
}
