#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewFrameState.h"

#include <QQuickWindow>
#include <QSGNode>

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
    textures_.setWindow(window());
    textures_.beginFrame();
    if (runtime_ == nullptr || window() == nullptr) {
        return root;
    }

    const miacode::preview::scene::PreviewFrameState& state = runtime_->frameState();
    if (QSGNode* stageNode = stageBackgroundLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(stageNode);
    }
    if (QSGNode* backdropNode = backdropLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(backdropNode);
    }
    if (QSGNode* muriPadNode =
            muriPadLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(muriPadNode);
    }
    if (QSGNode* muriActionNode =
            muriActionLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(muriActionNode);
    }
    if (QSGNode* judgeFireworkNode =
            judgeFireworkLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(judgeFireworkNode);
    }
    if (QSGNode* guideNode = guideLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(guideNode);
    }
    if (QSGNode* trackNode = trackLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(trackNode);
    }
    if (QSGNode* slideMotionNode =
            slideMotionLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(slideMotionNode);
    }
    if (QSGNode* judgeEffectNode =
            judgeEffectLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(judgeEffectNode);
    }
    if (QSGNode* touchJudgeNode =
            touchJudgeLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(touchJudgeNode);
    }
    if (QSGNode* headNode = headLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(headNode);
    }
    if (QSGNode* touchNode = touchLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(touchNode);
    }
    if (QSGNode* touchHoldNode = touchHoldLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(touchHoldNode);
    }
    if (QSGNode* chartReviewNode =
            chartReviewLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(chartReviewNode);
    }
    if (QSGNode* maimuriDxJudgeNode =
            maimuriDxJudgeLayer_.updateNode(nullptr, state, boundingRect().size().toSize(), window(), &textures_)) {
        root->appendChildNode(maimuriDxJudgeNode);
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
