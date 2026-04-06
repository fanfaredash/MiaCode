#include "preview/quick_scene/PreviewQuickTouchHoldLayer.h"

#include "preview/quick_scene/PreviewQuickArcNodes.h"
#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/scene/PreviewTouchHoldLayerState.h"

#include <QSGNode>

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
    delete oldNode;
    auto* root = new QSGNode();
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
    if (QSGNode* arcs = buildPreviewArcNodeTree(nullptr, layerState.arcs, window, textures)) {
        root->appendChildNode(arcs);
    }
    if (QSGNode* sprites = buildPreviewSpriteNodeTree(
            nullptr,
            layerState.sprites,
            window,
            textures,
            filteredState.playheadSeconds,
            "touch_hold"
        )) {
        root->appendChildNode(sprites);
    }
    if (root->firstChild() == nullptr) {
        delete root;
        return nullptr;
    }
    return root;
}
