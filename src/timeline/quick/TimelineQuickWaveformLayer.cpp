#include "timeline/quick/TimelineQuickWaveformLayer.h"

#include <QSGNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"

namespace {

struct TimelineQuickWaveformRootNode : public QSGNode {
    quint64 revision = 0;
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

QSGNode* TimelineQuickWaveformLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state) const
{
    auto* root = dynamic_cast<TimelineQuickWaveformRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickWaveformRootNode();
    }
    if (root->revision == state.waveformRevision && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(root);
    for (const auto& rect : state.waveformBars) {
        root->appendChildNode(buildTimelineRectNode(rect));
    }
    root->revision = state.waveformRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
