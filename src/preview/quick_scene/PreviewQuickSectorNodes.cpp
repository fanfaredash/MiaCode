#include "preview/quick_scene/PreviewQuickSectorNodes.h"

#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGVertexColorMaterial>
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

QSGGeometryNode* buildSectorNode(const PreviewSectorDescriptor& sector)
{
    if (sector.outerRadius <= kSectorEpsilon
        || qAbs(sector.sweepDegrees) <= kSectorEpsilon
        || sector.color.alpha() <= 0) {
        return nullptr;
    }

    const int segmentCount = qMax(1, qCeil(qAbs(sector.sweepDegrees) / 8.0));
    const qreal innerRadius = qBound<qreal>(0.0, sector.innerRadius, sector.outerRadius);
    const qreal fadeOuterRadius = qBound<qreal>(innerRadius, sector.innerFadeOuterRadius, sector.outerRadius);
    const bool hasHole = innerRadius > kSectorEpsilon;
    const bool hasFadeBand = fadeOuterRadius > innerRadius + kSectorEpsilon;
    const int trianglesPerSegment =
        hasHole ? (hasFadeBand ? 4 : 2) : (hasFadeBand ? 3 : 1);

    auto* geometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_ColoredPoint2D(),
        segmentCount * trianglesPerSegment * 3
    );
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsColoredPoint2D();

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

    auto* material = new QSGVertexColorMaterial();

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
    for (const PreviewSectorDescriptor& sector : sectors) {
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
