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
    if (runtime_ != nullptr) {
        frameState_ = nullptr;
    }
    update();
}

void PreviewQuickSceneRoot::setFrameState(const miacode::preview::scene::PreviewFrameState* frameState)
{
    frameState_ = frameState;
    if (frameState_ != nullptr) {
        runtime_ = nullptr;
    }
    update();
}

void PreviewQuickSceneRoot::setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags)
{
    if (layerFlags_ == layerFlags) {
        return;
    }
    layerFlags_ = layerFlags;
    update();
}

QSGNode* PreviewQuickSceneRoot::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData)
{
    Q_UNUSED(updatePaintNodeData);

    delete oldNode;
    auto* root = new QSGNode();
    textures_.setWindow(window());
    textures_.beginFrame();
    const miacode::preview::scene::PreviewFrameState* state = nullptr;
    if (runtime_ != nullptr) {
        state = &runtime_->frameState();
    } else {
        state = frameState_;
    }
    if (state == nullptr || window() == nullptr) {
        return root;
    }

    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::StageBackgroundLayer)) {
        if (QSGNode* stageNode = stageBackgroundLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(stageNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::BackdropLayer)) {
        if (QSGNode* backdropNode = backdropLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(backdropNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::MuriPadStateLayer)) {
        if (QSGNode* muriPadNode =
                muriPadLayer_.updateNode(nullptr, *state, boundingRect().size().toSize(), window(), &textures_)) {
            root->appendChildNode(muriPadNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::MuriActionLayer)) {
        if (QSGNode* muriActionNode = muriActionLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(muriActionNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::JudgeFireworkLayer)) {
        if (QSGNode* judgeFireworkNode = judgeFireworkLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(judgeFireworkNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::GuideLayer)) {
        if (QSGNode* guideNode = guideLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(guideNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TrackLayer)) {
        if (QSGNode* trackNode = trackLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(trackNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::SlideMotionLayer)) {
        if (QSGNode* slideMotionNode = slideMotionLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(slideMotionNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeLayer)) {
        if (QSGNode* judgeEffectNode = judgeEffectLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(judgeEffectNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::JudgeTouchLayer)) {
        if (QSGNode* touchJudgeNode = touchJudgeLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(touchJudgeNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::HeadLayer)) {
        if (QSGNode* headNode = headLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(headNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TouchLayer)) {
        if (QSGNode* touchNode = touchLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(touchNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::TouchHoldLayer)) {
        if (QSGNode* touchHoldNode = touchHoldLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(touchHoldNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::ChartReviewLayer)) {
        if (QSGNode* chartReviewNode = chartReviewLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(chartReviewNode);
        }
    }
    if (miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::MaimuriDxJudgeLayer)) {
        if (QSGNode* maimuriDxJudgeNode = maimuriDxJudgeLayer_.updateNode(
                nullptr,
                *state,
                boundingRect().size().toSize(),
                window(),
                &textures_)) {
            root->appendChildNode(maimuriDxJudgeNode);
        }
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
