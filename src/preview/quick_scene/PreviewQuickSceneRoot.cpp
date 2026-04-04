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
    if (QSGNode* guideNode = guideLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(guideNode);
    }
    if (QSGNode* legacyCompositeNode = buildLegacyMaskedNode(
            runtime_,
            textures_,
            window(),
            boundingRect().size().toSize(),
            miacode::preview::scene::kPreviewLegacyLowerBridgeLayers)) {
        root->appendChildNode(legacyCompositeNode);
    }
    if (QSGNode* legacyHeadNode = buildLegacyMaskedNode(
            runtime_,
            textures_,
            window(),
            boundingRect().size().toSize(),
            miacode::preview::scene::kPreviewLegacyHeadBridgeLayers)) {
        root->appendChildNode(legacyHeadNode);
    }
    if (QSGNode* touchNode = touchLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(touchNode);
    }
    if (QSGNode* touchHoldNode = touchHoldLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(touchHoldNode);
    }
    if (QSGNode* legacyPostTouchNode = buildLegacyMaskedNode(
            runtime_,
            textures_,
            window(),
            boundingRect().size().toSize(),
            miacode::preview::scene::kPreviewLegacyPostTouchBridgeLayers)) {
        root->appendChildNode(legacyPostTouchNode);
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
