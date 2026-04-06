#include "preview/quick_scene/PreviewQuickSectorNodes.h"

#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGVertexColorMaterial>
#include <QVector>
#include <QtMath>

namespace {

using miacode::preview::scene::PreviewSectorDescriptor;

constexpr qreal kSectorEpsilon = 0.001;

QPointF circlePoint(const QPointF& center, qreal radius, qreal angleDegrees)
{
    const qreal radians = qDegreesToRadians(-angleDegrees);
    return QPointF(
        center.x() + radius * qCos(radians),
        center.y() + radius * qSin(radians)
    );
}

struct PremultipliedVertexColor {
    uchar r = 0;
    uchar g = 0;
    uchar b = 0;
    uchar a = 0;
};

PremultipliedVertexColor premultipliedVertexColor(const QColor& color, qreal alphaScale)
{
    const qreal alpha01 = qBound<qreal>(0.0, color.alphaF() * alphaScale, 1.0);
    return {
        static_cast<uchar>(qBound(0, qRound(color.redF() * alpha01 * 255.0), 255)),
        static_cast<uchar>(qBound(0, qRound(color.greenF() * alpha01 * 255.0), 255)),
        static_cast<uchar>(qBound(0, qRound(color.blueF() * alpha01 * 255.0), 255)),
        static_cast<uchar>(qBound(0, qRound(alpha01 * 255.0), 255))
    };
}

void setVertex(
    QSGGeometry::ColoredPoint2D* vertices,
    int index,
    const QPointF& point,
    const PremultipliedVertexColor& color
)
{
    vertices[index].set(
        static_cast<float>(point.x()),
        static_cast<float>(point.y()),
        color.r,
        color.g,
        color.b,
        color.a
    );
}

bool isRenderableSector(const PreviewSectorDescriptor& sector)
{
    return sector.outerRadius > kSectorEpsilon
        && qAbs(sector.sweepDegrees) > kSectorEpsilon
        && sector.color.alpha() > 0;
}

int requiredSectorVertexCount(const PreviewSectorDescriptor& sector)
{
    const int segmentCount = qMax(1, qCeil(qAbs(sector.sweepDegrees) / 8.0));
    const qreal innerRadius = qBound<qreal>(0.0, sector.innerRadius, sector.outerRadius);
    const qreal fadeOuterRadius = qBound<qreal>(innerRadius, sector.innerFadeOuterRadius, sector.outerRadius);
    const bool hasHole = innerRadius > kSectorEpsilon;
    const bool hasFadeBand = fadeOuterRadius > innerRadius + kSectorEpsilon;
    const int trianglesPerSegment =
        hasHole ? (hasFadeBand ? 4 : 2) : (hasFadeBand ? 3 : 1);
    return segmentCount * trianglesPerSegment * 3;
}

void updateSectorVertices(
    QSGGeometry::ColoredPoint2D* vertices,
    const PreviewSectorDescriptor& sector
)
{
    const int segmentCount = qMax(1, qCeil(qAbs(sector.sweepDegrees) / 8.0));
    const qreal innerRadius = qBound<qreal>(0.0, sector.innerRadius, sector.outerRadius);
    const qreal fadeOuterRadius = qBound<qreal>(innerRadius, sector.innerFadeOuterRadius, sector.outerRadius);
    const bool hasHole = innerRadius > kSectorEpsilon;
    const bool hasFadeBand = fadeOuterRadius > innerRadius + kSectorEpsilon;
    const int trianglesPerSegment =
        hasHole ? (hasFadeBand ? 4 : 2) : (hasFadeBand ? 3 : 1);

    const PremultipliedVertexColor solidColor = premultipliedVertexColor(sector.color, 1.0);
    const PremultipliedVertexColor transparentColor;

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

        int vertexIndex = segmentIndex * trianglesPerSegment * 3;
        if (!hasHole) {
            const QPointF center = sector.center;
            if (hasFadeBand) {
                const QPointF fade0 = circlePoint(
                    sector.center,
                    fadeOuterRadius,
                    sector.startDegrees + sector.sweepDegrees * t0
                );
                const QPointF fade1 = circlePoint(
                    sector.center,
                    fadeOuterRadius,
                    sector.startDegrees + sector.sweepDegrees * t1
                );

                setVertex(vertices, vertexIndex + 0, center, transparentColor);
                setVertex(vertices, vertexIndex + 1, fade0, solidColor);
                setVertex(vertices, vertexIndex + 2, fade1, solidColor);
                setVertex(vertices, vertexIndex + 3, outer0, solidColor);
                setVertex(vertices, vertexIndex + 4, outer1, solidColor);
                setVertex(vertices, vertexIndex + 5, fade0, solidColor);
                setVertex(vertices, vertexIndex + 6, outer1, solidColor);
                setVertex(vertices, vertexIndex + 7, fade1, solidColor);
                setVertex(vertices, vertexIndex + 8, fade0, solidColor);
            } else {
                setVertex(vertices, vertexIndex + 0, center, solidColor);
                setVertex(vertices, vertexIndex + 1, outer0, solidColor);
                setVertex(vertices, vertexIndex + 2, outer1, solidColor);
            }
            continue;
        }

        const QPointF inner0 = circlePoint(
            sector.center,
            innerRadius,
            sector.startDegrees + sector.sweepDegrees * t0
        );
        const QPointF inner1 = circlePoint(
            sector.center,
            innerRadius,
            sector.startDegrees + sector.sweepDegrees * t1
        );

        if (hasFadeBand) {
            const QPointF fade0 = circlePoint(
                sector.center,
                fadeOuterRadius,
                sector.startDegrees + sector.sweepDegrees * t0
            );
            const QPointF fade1 = circlePoint(
                sector.center,
                fadeOuterRadius,
                sector.startDegrees + sector.sweepDegrees * t1
            );

            setVertex(vertices, vertexIndex + 0, fade0, solidColor);
            setVertex(vertices, vertexIndex + 1, fade1, solidColor);
            setVertex(vertices, vertexIndex + 2, inner0, transparentColor);
            setVertex(vertices, vertexIndex + 3, fade1, solidColor);
            setVertex(vertices, vertexIndex + 4, inner1, transparentColor);
            setVertex(vertices, vertexIndex + 5, inner0, transparentColor);
            vertexIndex += 6;

            setVertex(vertices, vertexIndex + 0, outer0, solidColor);
            setVertex(vertices, vertexIndex + 1, outer1, solidColor);
            setVertex(vertices, vertexIndex + 2, fade0, solidColor);
            setVertex(vertices, vertexIndex + 3, outer1, solidColor);
            setVertex(vertices, vertexIndex + 4, fade1, solidColor);
            setVertex(vertices, vertexIndex + 5, fade0, solidColor);
            continue;
        }

        setVertex(vertices, vertexIndex + 0, outer0, solidColor);
        setVertex(vertices, vertexIndex + 1, outer1, solidColor);
        setVertex(vertices, vertexIndex + 2, inner0, solidColor);
        setVertex(vertices, vertexIndex + 3, outer1, solidColor);
        setVertex(vertices, vertexIndex + 4, inner1, solidColor);
        setVertex(vertices, vertexIndex + 5, inner0, solidColor);
    }
}

void fillDegenerateTriangles(
    QSGGeometry::ColoredPoint2D* vertices,
    int startVertex,
    int totalVertexCount,
    const QPointF& point
)
{
    const PremultipliedVertexColor transparentColor;
    for (int vertexIndex = startVertex; vertexIndex < totalVertexCount; vertexIndex += 3) {
        setVertex(vertices, vertexIndex + 0, point, transparentColor);
        setVertex(vertices, vertexIndex + 1, point, transparentColor);
        setVertex(vertices, vertexIndex + 2, point, transparentColor);
    }
}

class PreviewQuickSectorNode final : public QSGGeometryNode
{
public:
    PreviewQuickSectorNode()
    {
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);

        material_ = new QSGVertexColorMaterial();
        setMaterial(material_);
        setFlag(QSGNode::OwnsMaterial, true);
    }

    void updateSector(const PreviewSectorDescriptor& sector)
    {
        const int requiredVertexCount = requiredSectorVertexCount(sector);
        if (requiredVertexCount > geometry_->vertexCount()) {
            geometry_->allocate(requiredVertexCount);
        }

        auto* vertices = geometry_->vertexDataAsColoredPoint2D();
        updateSectorVertices(vertices, sector);
        if (requiredVertexCount < geometry_->vertexCount()) {
            fillDegenerateTriangles(vertices, requiredVertexCount, geometry_->vertexCount(), sector.center);
        }
        geometry_->markVertexDataDirty();
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry* geometry_ = nullptr;
    QSGVertexColorMaterial* material_ = nullptr;
};

}  // namespace

QSGNode* buildPreviewSectorNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewSectorDescriptors& sectors
)
{
    if (sectors.isEmpty()) {
        return nullptr;
    }

    auto* root = oldNode != nullptr ? oldNode : new QSGNode();
    const bool reusedRoot = root == oldNode;
    QVector<QSGNode*> existingChildren;
    for (QSGNode* child = root->firstChild(); child != nullptr; child = child->nextSibling()) {
        existingChildren.append(child);
    }

    int nodeIndex = 0;
    for (const PreviewSectorDescriptor& sector : sectors) {
        if (!isRenderableSector(sector)) {
            continue;
        }

        QSGNode* existing = nodeIndex < existingChildren.size() ? existingChildren.at(nodeIndex) : nullptr;
        auto* sectorNode = dynamic_cast<PreviewQuickSectorNode*>(existing);
        if (sectorNode == nullptr) {
            auto* newNode = new PreviewQuickSectorNode();
            if (existing != nullptr) {
                root->insertChildNodeBefore(newNode, existing);
                root->removeChildNode(existing);
                delete existing;
                existingChildren[nodeIndex] = newNode;
            } else {
                root->appendChildNode(newNode);
                existingChildren.append(newNode);
            }
            sectorNode = newNode;
        }

        sectorNode->updateSector(sector);
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
