#include "timeline/quick/TimelineQuickGridLayer.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleTextureNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCache.h"

namespace {

struct TimelineQuickGridRootNode : public QSGNode {
    quint64 revision = 0;
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

}  // namespace

QSGNode* TimelineQuickGridLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state,
    QQuickWindow* window,
    TimelineQuickTextureCache* textures) const
{
    Q_UNUSED(window);
    Q_UNUSED(textures);
    auto* root = dynamic_cast<TimelineQuickGridRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickGridRootNode();
    }
    if (root->revision == state.gridRevision
        && root->layoutRevision == state.layoutRevision
        && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(root);
    for (const auto& rect : state.baseBackgroundRects) {
        root->appendChildNode(buildTimelineRectNode(rect));
    }
    root->revision = state.gridRevision;
    root->layoutRevision = state.layoutRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
