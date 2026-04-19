#include "timeline/quick/TimelineQuickHeaderLayer.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleTextureNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCache.h"

namespace {

struct TimelineQuickHeaderRootNode : public QSGNode {
    quint64 gridRevision = 0;
    quint64 headerRevision = 0;
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

}  // namespace

QSGNode* TimelineQuickHeaderLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state,
    QQuickWindow* window,
    TimelineQuickTextureCache* textures) const
{
    Q_UNUSED(window);
    auto* root = dynamic_cast<TimelineQuickHeaderRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickHeaderRootNode();
    }
    if (root->gridRevision == state.gridRevision
        && root->headerRevision == state.headerRevision
        && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(root);

    for (const auto& rect : state.laneOverlayRects) {
        root->appendChildNode(buildTimelineRectNode(rect));
    }
    for (const auto& line : state.gridLines) {
        root->appendChildNode(buildTimelineLineNode(line));
    }
    for (const auto& triangle : state.headerMarkers) {
        root->appendChildNode(buildTimelineTriangleNode(triangle));
    }
    if (state.hasEntryMarker) {
        root->appendChildNode(buildTimelineTriangleNode(state.entryMarker));
    }
    if (textures != nullptr) {
        for (const auto& label : state.headerLabels) {
            const TimelineQuickTextureHandle handle = textures->textTexture(label);
            if (handle.texture == nullptr) {
                continue;
            }
            auto* node = new QSGSimpleTextureNode();
            node->setTexture(handle.texture);
            node->setRect(QRectF(label.topLeft, handle.logicalSize));
            root->appendChildNode(node);
        }
    }
    root->gridRevision = state.gridRevision;
    root->headerRevision = state.headerRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
