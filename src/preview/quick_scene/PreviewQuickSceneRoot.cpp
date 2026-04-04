#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewFrameState.h"

#include <QQuickWindow>
#include <QSGNode>

namespace {

constexpr int kPreviewQuickSceneLayerSlotCount = 15;

QSGNode* ensureLayerSlotRoot(QSGNode* oldNode)
{
    auto childCountFor = [](QSGNode* node) {
        int count = 0;
        for (QSGNode* child = node != nullptr ? node->firstChild() : nullptr; child != nullptr; child = child->nextSibling()) {
            ++count;
        }
        return count;
    };

    if (oldNode != nullptr && childCountFor(oldNode) == kPreviewQuickSceneLayerSlotCount) {
        return oldNode;
    }

    delete oldNode;
    auto* root = new QSGNode();
    for (int index = 0; index < kPreviewQuickSceneLayerSlotCount; ++index) {
        root->appendChildNode(new QSGNode());
    }
    return root;
}

QSGNode* layerSlotAt(QSGNode* root, int index)
{
    if (root == nullptr || index < 0) {
        return nullptr;
    }

    QSGNode* slot = root->firstChild();
    for (int currentIndex = 0; slot != nullptr && currentIndex < index; ++currentIndex) {
        slot = slot->nextSibling();
    }
    return slot;
}

template <typename UpdateFn>
void updateLayerSlot(QSGNode* slot, bool enabled, UpdateFn&& updateFn)
{
    if (slot == nullptr) {
        return;
    }

    QSGNode* oldChild = slot->firstChild();
    QSGNode* newChild = enabled ? updateFn(oldChild) : nullptr;

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

    auto* root = ensureLayerSlotRoot(oldNode);
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

    const QSize renderSize = boundingRect().size().toSize();
    int slotIndex = 0;
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::StageBackgroundLayer),
        [&](QSGNode* oldChild) {
            return stageBackgroundLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::BackdropLayer),
        [&](QSGNode* oldChild) {
            return backdropLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::MuriPadStateLayer),
        [&](QSGNode* oldChild) {
            return muriPadLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::MuriActionLayer),
        [&](QSGNode* oldChild) {
            return muriActionLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeFireworkLayer),
        [&](QSGNode* oldChild) {
            return judgeFireworkLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::GuideLayer),
        [&](QSGNode* oldChild) {
            return guideLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TrackLayer),
        [&](QSGNode* oldChild) {
            return trackLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::SlideMotionLayer),
        [&](QSGNode* oldChild) {
            return slideMotionLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeLayer),
        [&](QSGNode* oldChild) {
            return judgeEffectLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeTouchLayer),
        [&](QSGNode* oldChild) {
            return touchJudgeLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::HeadLayer),
        [&](QSGNode* oldChild) {
            return headLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TouchLayer),
        [&](QSGNode* oldChild) {
            return touchLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TouchHoldLayer),
        [&](QSGNode* oldChild) {
            return touchHoldLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::ChartReviewLayer),
        [&](QSGNode* oldChild) {
            return chartReviewLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlot(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::MaimuriDxJudgeLayer),
        [&](QSGNode* oldChild) {
            return maimuriDxJudgeLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    return root;
}

void PreviewQuickSceneRoot::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}
