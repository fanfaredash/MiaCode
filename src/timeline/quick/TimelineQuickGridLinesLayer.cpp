#include "timeline/quick/TimelineQuickGridLinesLayer.h"

#include <QMatrix4x4>

#include <cmath>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGTransformNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"

namespace {

struct TimelineQuickGridLinesRootNode : public QSGNode {
    quint64 gridRevision = 0;
    quint64 layoutRevision = 0;
    quint64 appearanceRevision = 0;
};

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

QSGClipNode* ensureClipRoot(TimelineQuickGridLinesRootNode* root)
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

}  // namespace

QSGNode* TimelineQuickGridLinesLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state,
    QQuickWindow* window) const
{
    auto* root = dynamic_cast<TimelineQuickGridLinesRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickGridLinesRootNode();
    }

    QSGClipNode* clipRoot = ensureClipRoot(root);
    QSGTransformNode* transformRoot = ensureTransformRoot(clipRoot);
    QSGNode* contentRoot = ensureContentRoot(transformRoot);

    // Clip to the timeline content rect (excludes the lane-number sidebar
    // and the header band). Mirrors what TimelineQuickHeaderLayer's
    // gridClipRoot used to do.
    const QRectF timelineClipRect(
        state.timelineLeft,
        state.timelineTop,
        qMax(0, state.viewportSize.width() - state.timelineLeft),
        state.timelineHeight);
    if (clipRoot != nullptr) {
        clipRoot->setClipRect(timelineClipRect);
    }

    // Apply the horizontal scroll translate so grid-line content
    // emitted in absolute world-X coords lands at the right viewport-X.
    //
    // Snapped to the DEVICE pixel grid, same treatment as the waveform band and for the same
    // reason: tryAppendOrthogonalLine turns every grid line into a flat-colour rect 1.0-2.0px
    // wide, and nothing in this app is multisampled. At a fractional translate that changes
    // every frame, each line's rasterised coverage flips between N and N+1 device pixel
    // columns, so the lines visibly shimmer in apparent thickness even though their motion is
    // correct. Snapping freezes the coverage. Snap in device pixels, not logical ones —
    // rasterisation happens in device pixels, and a logical snap would make the residual dpr
    // times coarser (2x on Retina) for no benefit.
    //
    // Residual: the lines now step while the (linear-filtered, therefore genuinely smooth)
    // note sprites glide, so a note sitting on a beat can sit up to half a device pixel off
    // its line, oscillating. That is half the swing of the logical-pixel snap and well under
    // the shimmer it replaces. See cross-chain-linkage.md §14b.
    const qreal dpr = window != nullptr && window->effectiveDevicePixelRatio() > 0.0
        ? window->effectiveDevicePixelRatio()
        : 1.0;
    const qreal snappedScroll = std::round(state.horizontalScrollValue * dpr) / dpr;
    QMatrix4x4 transform;
    transform.translate(-static_cast<float>(snappedScroll), 0.0f);
    if (transformRoot != nullptr) {
        transformRoot->setMatrix(transform);
    }

    const bool appearanceChanged = root->appearanceRevision != state.appearanceRevision;
    if (appearanceChanged
        || root->layoutRevision != state.layoutRevision
        || root->gridRevision != state.gridRevision) {
        clearChildren(contentRoot);
        // Same per-color batching as the previous in-headerLayer path.
        // The Blending=true override in TimelineQuickFlatColorBatchBuilder
        // applies here too, ensuring even alpha-255 bar lines stay on
        // the translucent draw path.
        TimelineQuickFlatColorBatchBuilder gridBatch(contentRoot);
        for (const auto& line : state.gridLines) {
            if (!gridBatch.tryAppendOrthogonalLine(line)) {
                gridBatch.flush();
                contentRoot->appendChildNode(buildTimelineLineNode(line));
            }
        }
        gridBatch.flush();
        root->gridRevision = state.gridRevision;
    }
    root->layoutRevision = state.layoutRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
