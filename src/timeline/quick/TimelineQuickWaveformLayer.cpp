#include "timeline/quick/TimelineQuickWaveformLayer.h"

#include <QMatrix4x4>

#include <cmath>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGTransformNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"

namespace {

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
    const miacode::timeline::TimelineSceneState& state,
    qreal devicePixelRatio) const
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
        // Snapped translate, unlike every other layer — deliberate, and snapped to the
        // PHYSICAL pixel grid rather than the logical one.
        //
        // The waveform is a column dataset (top level 128 columns/s) drawn as abutting
        // hard-edged translucent bars, and nothing in this app is multisampled. Translating
        // that by a different fraction every frame resamples it with no filtering: each bar's
        // rasterised width flips between floor and ceil and the band visibly crawls. Snapping
        // keeps every frame's rasterisation identical, which removes the crawl.
        //
        // The cost is that the band steps while the notes/grid glide, so it appears to sway
        // against them by the snap quantum. Rasterisation happens in device pixels, so that
        // quantum only needs to be one PHYSICAL pixel — snapping to logical pixels instead
        // would make it dpr times larger (2x on Retina) for no benefit.
        //
        // Do NOT copy this to the grid/notes/overlay layers: those DO share positions with
        // each other, and snapping one would make notes visibly detach from their grid line.
        // See cross-chain-linkage.md §14b.
        const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
        const qreal snappedScroll = std::round(state.horizontalScrollValue * dpr) / dpr;
        matrix.translate(-static_cast<float>(snappedScroll), 0.0f);
        transformRoot->setMatrix(matrix);
    }
    if (root->revision == state.waveformRevision
        && root->layoutRevision == state.layoutRevision
        && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(contentRoot);
    // Phase-4e-old-opt — waveform bars are typically all the same
    // colour (theme's audio-track tint), so this collapses to ONE
    // QSGGeometryNode regardless of how many bars are emitted. With
    // long songs at high zoom this can be 10k+ rectangles otherwise.
    {
        TimelineQuickFlatColorBatchBuilder waveformBatch(contentRoot);
        for (const auto& rect : state.waveformBars) {
            waveformBatch.appendRect(rect.color, rect.rect);
        }
        waveformBatch.flush();
    }
    root->revision = state.waveformRevision;
    root->layoutRevision = state.layoutRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
