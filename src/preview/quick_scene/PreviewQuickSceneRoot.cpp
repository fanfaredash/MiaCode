#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewLayerOrder.h"

#include <QQuickWindow>
#include <QSGNode>
#include "preview/quick_scene/PreviewQuickLayerRenderNode.h"

namespace {

QSGNode* buildLegacyMaskedNode(
    PreviewRuntime* runtime,
    PreviewTextureRepository& textures,
    QQuickWindow* window,
    const QSize& renderSize,
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags
)
{
    if (runtime == nullptr || window == nullptr) {
        return nullptr;
    }
    Q_UNUSED(textures);
    auto* node = new PreviewQuickLayerRenderNode();
    node->configure(runtime, renderSize, qMax<qreal>(1.0, window->devicePixelRatio()), layerFlags);
    return node;
}

}  // namespace

PreviewQuickSceneRoot::PreviewQuickSceneRoot(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void PreviewQuickSceneRoot::setRuntime(PreviewRuntime* runtime)
{
    runtime_ = runtime;
    update();
}

QSGNode* PreviewQuickSceneRoot::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData)
{
    Q_UNUSED(updatePaintNodeData);

    delete oldNode;
    auto* root = new QSGNode();
    if (runtime_ == nullptr || window() == nullptr) {
        return root;
    }

    textures_.setWindow(window());
    const miacode::preview::scene::PreviewFrameState& state = runtime_->frameState();
    if (QSGNode* stageNode = stageBackgroundLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(stageNode);
    }
    if (QSGNode* backdropNode = backdropLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(backdropNode);
    }
    if (QSGNode* muriPadNode =
            muriPadLayer_.updateNode(nullptr, runtime_, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(muriPadNode);
    }
    if (QSGNode* muriActionNode =
            muriActionLayer_.updateNode(nullptr, runtime_, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(muriActionNode);
    }
    if (QSGNode* judgeFireworkNode =
            judgeFireworkLayer_.updateNode(nullptr, runtime_, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(judgeFireworkNode);
    }
    if (QSGNode* legacyLowerNode = buildLegacyMaskedNode(
            runtime_,
            textures_,
            window(),
            boundingRect().size().toSize(),
            miacode::preview::scene::kPreviewLegacyLowerBridgeLayers)) {
        root->appendChildNode(legacyLowerNode);
    }
    if (QSGNode* touchJudgeNode =
            touchJudgeLayer_.updateNode(nullptr, runtime_, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(touchJudgeNode);
    }
    if (QSGNode* legacyUpperNode = buildLegacyMaskedNode(
            runtime_,
            textures_,
            window(),
            boundingRect().size().toSize(),
            miacode::preview::scene::kPreviewLegacyUpperBridgeLayers)) {
        root->appendChildNode(legacyUpperNode);
    }
    return root;
}

void PreviewQuickSceneRoot::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}
