#include "preview/quick_scene/PreviewQuickTouchHoldLayer.h"

#include "preview/quick_scene/PreviewQuickArcNodes.h"
#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/scene/PreviewTouchHoldLayerState.h"

#include <QSGNode>

namespace {

class PreviewQuickTouchHoldRootNode final : public QSGNode
{
public:
    PreviewQuickTouchHoldRootNode()
    {
        arcSlot = new QSGNode();
        spriteSlot = new QSGNode();
        appendChildNode(arcSlot);
        appendChildNode(spriteSlot);
    }

    QSGNode* arcSlot = nullptr;
    QSGNode* spriteSlot = nullptr;
};

PreviewQuickTouchHoldRootNode* ensureTouchHoldRoot(QSGNode* oldNode)
{
    if (auto* root = dynamic_cast<PreviewQuickTouchHoldRootNode*>(oldNode)) {
        return root;
    }
    return new PreviewQuickTouchHoldRootNode();
}

void replaceSlotChild(QSGNode* slot, QSGNode* newChild)
{
    if (slot == nullptr) {
        return;
    }

    QSGNode* child = slot->firstChild();
    while (child != nullptr) {
        QSGNode* next = child->nextSibling();
        if (child != newChild) {
            slot->removeChildNode(child);
            delete child;
        }
        child = next;
    }

    if (newChild != nullptr && newChild->parent() != slot) {
        slot->appendChildNode(newChild);
    }
}

}  // namespace

QSGNode* PreviewQuickTouchHoldLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    auto* root = ensureTouchHoldRoot(oldNode);
    const bool reusedRoot = root == oldNode;

    miacode::preview::scene::PreviewFrameState filteredState = state;
    QVector<TimelineNoteMarker> filteredMarkers;
    if (preparedCache != nullptr && cursor != nullptr) {
        preparedCache->collectMarkers(
            state.noteMarkers,
            preparedCache->touchHoldLayer(),
            cursor->activePreparedIndices,
            &filteredMarkers
        );
        filteredState.noteMarkers = filteredMarkers;
    }
    const miacode::preview::scene::PreviewTouchHoldLayerState layerState =
        miacode::preview::scene::buildPreviewTouchHoldLayerState(
            filteredState,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        );

    replaceSlotChild(
        root->arcSlot,
        buildPreviewArcNodeTree(root->arcSlot->firstChild(), layerState.arcs, window, textures)
    );
    replaceSlotChild(
        root->spriteSlot,
        buildPreviewSpriteNodeTree(
            root->spriteSlot->firstChild(),
            layerState.sprites,
            window,
            textures,
            filteredState.playheadSeconds,
            "touch_hold"
        )
    );

    if (root->arcSlot->firstChild() == nullptr && root->spriteSlot->firstChild() == nullptr) {
        if (!reusedRoot) {
            delete root;
        }
        return nullptr;
    }
    return root;
}
