#include "timeline/quick/TimelineQuickWaveformLayer.h"

#include <QMatrix4x4>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGTransformNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"

namespace {

struct TimelineQuickWaveformRootNode : public QSGNode {
    quint64 revision = 0;
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
        matrix.translate(-static_cast<float>(state.horizontalScrollValue), 0.0f);
        transformRoot->setMatrix(matrix);
    }
    if (root->revision == state.waveformRevision && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(contentRoot);
    for (const auto& rect : state.waveformBars) {
        contentRoot->appendChildNode(buildTimelineRectNode(rect));
    }
    root->revision = state.waveformRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
