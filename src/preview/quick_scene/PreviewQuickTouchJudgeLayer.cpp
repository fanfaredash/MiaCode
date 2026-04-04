#include "preview/quick_scene/PreviewQuickTouchJudgeLayer.h"

#include "preview/quick_scene/PreviewQuickLayerRenderNode.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewLayerOrder.h"

#include <QQuickWindow>

namespace {

bool hasTouchMarkers(const QVector<TimelineNoteMarker>& noteMarkers)
{
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (marker.type == QLatin1String("touch")) {
            return true;
        }
    }
    return false;
}

}  // namespace

QSGNode* PreviewQuickTouchJudgeLayer::updateNode(
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
        || !hasTouchMarkers(state.noteMarkers)) {
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
        miacode::preview::scene::JudgeTouchLayer
    );
    return node;
}
