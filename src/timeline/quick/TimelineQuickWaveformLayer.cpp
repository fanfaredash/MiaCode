#include "timeline/quick/TimelineQuickWaveformLayer.h"

#include <QMatrix4x4>

#include <cmath>
#include <limits>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QVector>

namespace {

struct WaveformEnvelopeSample {
    qreal x = 0.0;
    qreal top = 0.0;
    qreal bottom = 0.0;
    QColor color;
    bool valid = false;
};

struct PremultipliedVertexColor {
    uchar r = 0;
    uchar g = 0;
    uchar b = 0;
    uchar a = 0;
};

PremultipliedVertexColor waveformVertexColor(const QColor& color, qreal alphaScale)
{
    const qreal alpha = qBound<qreal>(0.0, color.alphaF() * alphaScale, 1.0);
    return {
        static_cast<uchar>(qBound(0, qRound(color.redF() * alpha * 255.0), 255)),
        static_cast<uchar>(qBound(0, qRound(color.greenF() * alpha * 255.0), 255)),
        static_cast<uchar>(qBound(0, qRound(color.blueF() * alpha * 255.0), 255)),
        static_cast<uchar>(qBound(0, qRound(alpha * 255.0), 255)),
    };
}

void setWaveformVertex(
    QSGGeometry::ColoredPoint2D* vertices,
    int index,
    qreal x,
    qreal y,
    const PremultipliedVertexColor& color)
{
    vertices[index].set(
        static_cast<float>(x),
        static_cast<float>(y),
        color.r,
        color.g,
        color.b,
        color.a);
}

QSGGeometryNode* buildWaveformEnvelopeNode(
    const miacode::timeline::TimelineSceneState& state)
{
    constexpr int kVerticesPerSample = 2;
    constexpr int kIndicesPerSegment = 6;
    if (state.waveformBars.isEmpty()) {
        return nullptr;
    }

    qreal firstX = state.waveformBars.constFirst().rect.left();
    qreal lastX = state.waveformBars.constFirst().rect.right();
    for (const auto& bar : state.waveformBars) {
        firstX = qMin(firstX, bar.rect.left());
        lastX = qMax(lastX, bar.rect.right());
    }

    // One envelope sample per logical pixel preserves the visible extrema. Scaling the
    // geometry count by DPR duplicates silhouette data on the shared render thread.
    const qint64 firstPixel = static_cast<qint64>(std::floor(firstX));
    const qint64 lastPixel = static_cast<qint64>(std::ceil(lastX)) - 1;
    const qint64 sampleCount64 = lastPixel - firstPixel + 1;
    const qint64 maximumSampleCount = qMin<qint64>(
        std::numeric_limits<int>::max() / kVerticesPerSample,
        std::numeric_limits<int>::max() / kIndicesPerSegment);
    if (sampleCount64 <= 0 || sampleCount64 > maximumSampleCount) {
        return nullptr;
    }

    const qreal centerY = state.timelineTop + state.timelineHeight * 0.5;
    const int displaySampleCount = static_cast<int>(sampleCount64);
    bool hasSourceGap = false;
    for (int barIndex = 1; barIndex < state.waveformBars.size(); ++barIndex) {
        if (state.waveformBars.at(barIndex).rect.left()
            > state.waveformBars.at(barIndex - 1).rect.right() + 1.0) {
            hasSourceGap = true;
            break;
        }
    }

    QVector<WaveformEnvelopeSample> samples;
    if (!hasSourceGap && state.waveformBars.size() + 2 < displaySampleCount) {
        samples.reserve(state.waveformBars.size() + 2);
        const auto appendBarSample = [&samples](qreal x, const miacode::timeline::TimelineSceneRect& bar) {
            samples.append(WaveformEnvelopeSample{
                x,
                bar.rect.top(),
                bar.rect.bottom(),
                bar.color,
                true,
            });
        };
        appendBarSample(state.waveformBars.constFirst().rect.left(), state.waveformBars.constFirst());
        for (const auto& bar : state.waveformBars) {
            appendBarSample(bar.rect.center().x(), bar);
        }
        appendBarSample(state.waveformBars.constLast().rect.right(), state.waveformBars.constLast());
    } else {
        samples.resize(displaySampleCount);
        for (int sampleIndex = 0; sampleIndex < displaySampleCount; ++sampleIndex) {
            samples[sampleIndex].x = static_cast<qreal>(firstPixel + sampleIndex) + 0.5;
            samples[sampleIndex].top = centerY;
            samples[sampleIndex].bottom = centerY;
        }

        // Match Mixxx's display-space reduction: every display column receives
        // the extrema of every visual frame that overlaps it. Narrow transients survive
        // zooming out, while the vertex count stays proportional to the viewport.
        for (const auto& bar : state.waveformBars) {
            const qint64 barFirstPixel = static_cast<qint64>(std::floor(bar.rect.left()));
            const qint64 barLastPixel = static_cast<qint64>(std::ceil(bar.rect.right())) - 1;
            const int begin = static_cast<int>(qMax<qint64>(0, barFirstPixel - firstPixel));
            const int end = static_cast<int>(qMin<qint64>(displaySampleCount - 1, barLastPixel - firstPixel));
            for (int sampleIndex = begin; sampleIndex <= end; ++sampleIndex) {
                WaveformEnvelopeSample& sample = samples[sampleIndex];
                if (sample.valid) {
                    sample.top = qMin(sample.top, bar.rect.top());
                    sample.bottom = qMax(sample.bottom, bar.rect.bottom());
                } else {
                    sample.top = bar.rect.top();
                    sample.bottom = bar.rect.bottom();
                    sample.color = bar.color;
                    sample.valid = true;
                }
            }
        }
    }

    if (samples.size() == 1) {
        WaveformEnvelopeSample duplicate = samples.constFirst();
        samples[0].x -= 0.5;
        duplicate.x += 0.5;
        samples.append(duplicate);
    }

    int activeSegmentCount = 0;
    for (int sampleIndex = 0; sampleIndex + 1 < samples.size(); ++sampleIndex) {
        if (samples.at(sampleIndex).valid || samples.at(sampleIndex + 1).valid) {
            ++activeSegmentCount;
        }
    }
    if (activeSegmentCount == 0) {
        return nullptr;
    }

    auto* geometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_ColoredPoint2D(),
        samples.size() * kVerticesPerSample,
        activeSegmentCount * kIndicesPerSegment,
        QSGGeometry::UnsignedIntType);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
    geometry->setIndexDataPattern(QSGGeometry::StaticPattern);

    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
        const WaveformEnvelopeSample& sample = samples.at(sampleIndex);
        QColor color = sample.color;
        if (!color.isValid()) {
            if (sampleIndex > 0 && samples.at(sampleIndex - 1).color.isValid()) {
                color = samples.at(sampleIndex - 1).color;
            } else if (sampleIndex + 1 < samples.size()
                       && samples.at(sampleIndex + 1).color.isValid()) {
                color = samples.at(sampleIndex + 1).color;
            }
        }
        const PremultipliedVertexColor vertexColor =
            sample.valid ? waveformVertexColor(color, 1.0) : PremultipliedVertexColor{};
        const int base = sampleIndex * kVerticesPerSample;
        setWaveformVertex(vertices, base + 0, sample.x, sample.top, vertexColor);
        setWaveformVertex(vertices, base + 1, sample.x, sample.bottom, vertexColor);
    }

    auto* indices = geometry->indexDataAsUInt();
    int indexOffset = 0;
    for (int sampleIndex = 0; sampleIndex + 1 < samples.size(); ++sampleIndex) {
        if (!samples.at(sampleIndex).valid && !samples.at(sampleIndex + 1).valid) {
            continue;
        }
        const quint32 left = static_cast<quint32>(sampleIndex * kVerticesPerSample);
        const quint32 right = static_cast<quint32>((sampleIndex + 1) * kVerticesPerSample);
        indices[indexOffset++] = left;
        indices[indexOffset++] = right;
        indices[indexOffset++] = left + 1;
        indices[indexOffset++] = right;
        indices[indexOffset++] = right + 1;
        indices[indexOffset++] = left + 1;
    }

    auto* material = new QSGVertexColorMaterial();
    material->setFlag(QSGMaterial::Blending, true);
    auto* node = new QSGGeometryNode();
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setFlag(QSGNode::OwnsMaterial, true);
    return node;
}

struct TimelineQuickWaveformRootNode : public QSGNode {
    quint64 revision = 0;
    quint64 layoutRevision = 0;
    quint64 appearanceRevision = 0;
};

QSGClipNode* ensureClipRoot(TimelineQuickWaveformRootNode* root)
{
    if (root == nullptr) {
        return nullptr;
    }
    auto* clipRoot = dynamic_cast<QSGClipNode*>(root->firstChild());
    if (clipRoot != nullptr) {
        return clipRoot;
    }
    if (QSGNode* child = root->firstChild(); child != nullptr) {
        root->removeChildNode(child);
        delete child;
    }
    clipRoot = new QSGClipNode();
    clipRoot->setIsRectangular(true);
    root->appendChildNode(clipRoot);
    return clipRoot;
}

QSGTransformNode* ensureTransformRoot(QSGClipNode* clipRoot)
{
    if (clipRoot == nullptr) {
        return nullptr;
    }
    auto* transformRoot = dynamic_cast<QSGTransformNode*>(clipRoot->firstChild());
    if (transformRoot != nullptr) {
        return transformRoot;
    }
    if (QSGNode* child = clipRoot->firstChild(); child != nullptr) {
        clipRoot->removeChildNode(child);
        delete child;
    }
    transformRoot = new QSGTransformNode();
    transformRoot->appendChildNode(new QSGNode());
    clipRoot->appendChildNode(transformRoot);
    return transformRoot;
}

QSGNode* ensureContentRoot(QSGTransformNode* transformRoot)
{
    if (transformRoot == nullptr) {
        return nullptr;
    }
    QSGNode* contentRoot = transformRoot->firstChild();
    if (contentRoot == nullptr) {
        contentRoot = new QSGNode();
        transformRoot->appendChildNode(contentRoot);
    }
    return contentRoot;
}

void clearChildren(QSGNode* node)
{
    if (node == nullptr) {
        return;
    }
    while (QSGNode* child = node->firstChild()) {
        node->removeChildNode(child);
        delete child;
    }
}

}  // namespace

QSGNode* TimelineQuickWaveformLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state) const
{
    auto* root = dynamic_cast<TimelineQuickWaveformRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickWaveformRootNode();
    }
    QSGClipNode* clipRoot = ensureClipRoot(root);
    QSGTransformNode* transformRoot = ensureTransformRoot(clipRoot);
    QSGNode* contentRoot = ensureContentRoot(transformRoot);
    if (clipRoot != nullptr) {
        clipRoot->setClipRect(QRectF(
            state.timelineLeft,
            state.timelineTop,
            qMax(0, state.viewportSize.width() - state.timelineLeft),
            state.timelineHeight));
    }
    if (transformRoot != nullptr) {
        QMatrix4x4 matrix;
        // Keep the envelope on the same exact sub-pixel scroll as notes and grid lines.
        matrix.translate(-static_cast<float>(state.horizontalScrollValue), 0.0f);
        transformRoot->setMatrix(matrix);
    }
    if (root->revision == state.waveformRevision
        && root->layoutRevision == state.layoutRevision
        && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(contentRoot);
    if (QSGGeometryNode* envelope = buildWaveformEnvelopeNode(state)) {
        contentRoot->appendChildNode(envelope);
    }
    root->revision = state.waveformRevision;
    root->layoutRevision = state.layoutRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
