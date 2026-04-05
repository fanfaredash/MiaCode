#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewFrameState.h"

#include <QElapsedTimer>
#include <QQuickWindow>
#include <QSGNode>

namespace {

constexpr int kPreviewQuickSceneLayerSlotCount = 15;

QString pointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

void appendQuickSceneLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/quick_scene"),
        text
    );
}

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

PreviewTextureLayerStats& ensureLayerProfileStat(
    QVector<PreviewTextureLayerStats>* layerStats,
    const char* layerName
)
{
    Q_ASSERT(layerStats != nullptr);

    const QString name = QString::fromLatin1(layerName != nullptr ? layerName : "");
    for (PreviewTextureLayerStats& layerStat : *layerStats) {
        if (layerStat.name == name) {
            return layerStat;
        }
    }

    layerStats->append(PreviewTextureLayerStats{name});
    return layerStats->last();
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

template <typename UpdateFn>
void updateLayerSlotProfiled(
    QSGNode* slot,
    bool enabled,
    const char* layerName,
    QVector<PreviewTextureLayerStats>* layerStats,
    UpdateFn&& updateFn
)
{
    QElapsedTimer timer;
    timer.start();
    updateLayerSlot(slot, enabled, std::forward<UpdateFn>(updateFn));
    ensureLayerProfileStat(layerStats, layerName).buildMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
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
    appendQuickSceneLog(
        QStringLiteral("set_runtime"),
        QString("runtime=%1 frame_state=%2").arg(pointerHex(runtime_)).arg(pointerHex(frameState_))
    );
    update();
}

void PreviewQuickSceneRoot::setFrameState(const miacode::preview::scene::PreviewFrameState* frameState)
{
    frameState_ = frameState;
    if (frameState_ != nullptr) {
        runtime_ = nullptr;
    }
    appendQuickSceneLog(
        QStringLiteral("set_frame_state"),
        QString("frame_state=%1 runtime=%2").arg(pointerHex(frameState_)).arg(pointerHex(runtime_))
    );
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

void PreviewQuickSceneRoot::invalidateTextureCache()
{
    const PreviewTextureStats statsBeforeClear = textures_.stats();
    appendQuickSceneLog(
        QStringLiteral("invalidate_texture_cache"),
        QString(
            "cached_hits=%1 cached_creates=%2 transient_hits=%3 transient_creates=%4 sprite_count=%5 sprite_batches=%6"
        )
            .arg(statsBeforeClear.cachedHitCount)
            .arg(statsBeforeClear.cachedCreateCount)
            .arg(statsBeforeClear.transientHitCount)
            .arg(statsBeforeClear.transientCreateCount)
            .arg(statsBeforeClear.spriteCount)
            .arg(statsBeforeClear.spriteBatchCount)
    );
    textures_.clear();
}

PreviewTextureStats PreviewQuickSceneRoot::textureStats() const
{
    PreviewTextureStats stats = textures_.stats();
    for (const PreviewTextureLayerStats& buildStat : layerProfileStats_) {
        PreviewTextureLayerStats& merged = ensureLayerProfileStat(&stats.layerStats, buildStat.name.toLatin1().constData());
        merged.buildMs = buildStat.buildMs;
    }
    return stats;
}

QSGNode* PreviewQuickSceneRoot::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData)
{
    Q_UNUSED(updatePaintNodeData);

    auto* root = ensureLayerSlotRoot(oldNode);
    textures_.setWindow(window());
    textures_.beginFrame();
    layerProfileStats_.clear();
    const miacode::preview::scene::PreviewFrameState* state = nullptr;
    if (runtime_ != nullptr) {
        state = &runtime_->frameState();
    } else {
        state = frameState_;
    }
    const QSize renderSize = boundingRect().size().toSize();
    const bool hasState = state != nullptr;
    const bool hasWindow = window() != nullptr;
    const quintptr windowHandle = reinterpret_cast<quintptr>(window());
    const bool shouldLogPaint =
        updatePaintNodeCount_ < 6
        || lastLoggedHasState_ != hasState
        || lastLoggedHasWindow_ != hasWindow
        || lastLoggedRenderSize_ != renderSize
        || lastLoggedWindowHandle_ != windowHandle;
    if (shouldLogPaint) {
        appendQuickSceneLog(
            QStringLiteral("update_paint_node"),
            QString(
                "count=%1 has_state=%2 has_window=%3 render_size=%4x%5 window=%6"
            )
                .arg(updatePaintNodeCount_ + 1)
                .arg(hasState ? 1 : 0)
                .arg(hasWindow ? 1 : 0)
                .arg(renderSize.width())
                .arg(renderSize.height())
                .arg(pointerHex(window()))
        );
    }
    lastLoggedHasState_ = hasState;
    lastLoggedHasWindow_ = hasWindow;
    lastLoggedRenderSize_ = renderSize;
    lastLoggedWindowHandle_ = windowHandle;
    ++updatePaintNodeCount_;

    if (!hasState || !hasWindow) {
        return root;
    }

    int slotIndex = 0;
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::StageBackgroundLayer),
        "stage_background",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return stageBackgroundLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::BackdropLayer),
        "backdrop",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return backdropLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::MuriPadStateLayer),
        "muri_pad",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return muriPadLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::MuriActionLayer),
        "muri_action",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return muriActionLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeFireworkLayer),
        "judge_firework",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return judgeFireworkLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::GuideLayer),
        "guide",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return guideLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TrackLayer),
        "track",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return trackLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::SlideMotionLayer),
        "slide_motion",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return slideMotionLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeLayer),
        "judge_effect",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return judgeEffectLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeTouchLayer),
        "touch_judge",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return touchJudgeLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::HeadLayer),
        "head",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return headLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TouchLayer),
        "touch",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return touchLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TouchHoldLayer),
        "touch_hold",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return touchHoldLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::ChartReviewLayer),
        "chart_review",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return chartReviewLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::MaimuriDxJudgeLayer),
        "maimuri_dx_judge",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return maimuriDxJudgeLayer_.updateNode(oldChild, *state, renderSize, window(), &textures_);
        });
    return root;
}

void PreviewQuickSceneRoot::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        ++geometryChangeCount_;
        appendQuickSceneLog(
            QStringLiteral("geometry_change"),
            QString("count=%1 old=%2x%3 new=%4x%5")
                .arg(geometryChangeCount_)
                .arg(oldGeometry.width(), 0, 'f', 2)
                .arg(oldGeometry.height(), 0, 'f', 2)
                .arg(newGeometry.width(), 0, 'f', 2)
                .arg(newGeometry.height(), 0, 'f', 2)
        );
        update();
    }
}
