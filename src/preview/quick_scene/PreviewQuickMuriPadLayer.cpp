#include "preview/quick_scene/PreviewQuickMuriPadLayer.h"

#include "preview/quick_scene/PreviewQuickLayerRenderNode.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewLayerOrder.h"

#include <QQuickWindow>

QSGNode* PreviewQuickMuriPadLayer::updateNode(
    QSGNode* oldNode,
    PreviewRuntime* runtime,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    delete oldNode;
    if (runtime == nullptr
        || window == nullptr
        || textures == nullptr
        || !state.muriRenderOptions.showJudgeMarkers
        || state.muriAnalysisReport.padWindows.isEmpty()) {
        return nullptr;
    }
    Q_UNUSED(textures);
    auto* node = oldNode != nullptr ? dynamic_cast<PreviewQuickLayerRenderNode*>(oldNode) : nullptr;
    if (node == nullptr) {
        delete oldNode;
        node = new PreviewQuickLayerRenderNode();
    }
    node->configure(
        runtime,
        renderSize,
        qMax<qreal>(1.0, window->devicePixelRatio()),
        miacode::preview::scene::MuriPadStateLayer
    );
    return node;
}
