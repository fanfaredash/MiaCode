#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewPreparedSceneCache.h"

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
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow*) {
        syncVisibleHostWindowBinding();
        update();
    });
    connect(this, &QQuickItem::visibleChanged, this, [this]() {
        syncVisibleHostWindowBinding();
        update();
    });
}

PreviewQuickSceneRoot::~PreviewQuickSceneRoot()
{
    if (runtime_ != nullptr && boundWindow_ != nullptr) {
        runtime_->clearVisibleHostWindow(boundWindow_);
    }
}

void PreviewQuickSceneRoot::setRuntime(PreviewRuntime* runtime)
{
    if (runtime_ == runtime) {
        return;
    }
    if (runtime_ != nullptr && boundWindow_ != nullptr) {
        runtime_->clearVisibleHostWindow(boundWindow_);
    }
    if (runtimeUpdateConnection_) {
        QObject::disconnect(runtimeUpdateConnection_);
    }
    runtime_ = runtime;
    if (runtime_ != nullptr) {
        frameState_ = nullptr;
        runtimeUpdateConnection_ = QObject::connect(runtime_, &PreviewRuntime::frameStateChanged, this, [this]() {
            update();
        });
        runtime_->setFrameSize(boundingRect().size().toSize());
    }
    syncVisibleHostWindowBinding();
    appendQuickSceneLog(
        QStringLiteral("set_runtime"),
        QString("runtime=%1 frame_state=%2").arg(pointerHex(runtime_)).arg(pointerHex(frameState_))
    );
    emit runtimeChanged();
    update();
}

QObject* PreviewQuickSceneRoot::runtimeObject() const
{
    return runtime_;
}

void PreviewQuickSceneRoot::setRuntimeObject(QObject* runtimeObject)
{
    setRuntime(qobject_cast<PreviewRuntime*>(runtimeObject));
}

void PreviewQuickSceneRoot::setFrameState(const miacode::preview::scene::PreviewFrameState* frameState)
{
    if (runtime_ != nullptr && boundWindow_ != nullptr) {
        runtime_->clearVisibleHostWindow(boundWindow_);
    }
    if (runtimeUpdateConnection_) {
        QObject::disconnect(runtimeUpdateConnection_);
        runtimeUpdateConnection_ = QMetaObject::Connection();
    }
    if (frameSwapConnection_) {
        QObject::disconnect(frameSwapConnection_);
        frameSwapConnection_ = QMetaObject::Connection();
    }
    frameState_ = frameState;
    if (frameState_ != nullptr) {
        runtime_ = nullptr;
        boundWindow_.clear();
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

void PreviewQuickSceneRoot::syncVisibleHostWindowBinding()
{
    if (frameSwapConnection_) {
        QObject::disconnect(frameSwapConnection_);
        frameSwapConnection_ = QMetaObject::Connection();
    }
    if (windowVisibilityConnection_) {
        QObject::disconnect(windowVisibilityConnection_);
        windowVisibilityConnection_ = QMetaObject::Connection();
    }
    if (runtime_ != nullptr && boundWindow_ != nullptr) {
        runtime_->clearVisibleHostWindow(boundWindow_);
    }

    boundWindow_ = window();
    if (runtime_ == nullptr || boundWindow_ == nullptr || !isVisible() || !boundWindow_->isVisible()) {
        if (boundWindow_ != nullptr) {
            windowVisibilityConnection_ = QObject::connect(boundWindow_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility) {
                syncVisibleHostWindowBinding();
                update();
            });
        }
        return;
    }

    const bool runtimeOwnsBoundWindow =
        runtime_->hostWindow() != nullptr && runtime_->hostWindow() == boundWindow_;
    if (runtimeOwnsBoundWindow) {
        windowVisibilityConnection_ = QObject::connect(boundWindow_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility) {
            syncVisibleHostWindowBinding();
            update();
        });
        return;
    }

    runtime_->setVisibleHostWindow(boundWindow_);
    windowVisibilityConnection_ = QObject::connect(boundWindow_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility) {
        syncVisibleHostWindowBinding();
        update();
    });
    frameSwapConnection_ = QObject::connect(boundWindow_, &QQuickWindow::frameSwapped, this, [this]() {
        if (runtime_ == nullptr || boundWindow_ == nullptr || window() != boundWindow_) {
            return;
        }
        runtime_->notifyVisibleFramePresented();
    });
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

    const bool cacheRebuilt = preparedCache_.sync(*state);
    if (cacheRebuilt) {
        guideCursor_.reset();
        headCursor_.reset();
        trackCursor_.reset();
        slideMotionCursor_.reset();
        judgeEffectCursor_.reset();
        judgeFireworkCursor_.reset();
        touchCursor_.reset();
        touchJudgeCursor_.reset();
        touchHoldCursor_.reset();
        chartReviewCursor_.reset();
        maimuriDxJudgeCursor_.reset();
    }

    const double playheadSeconds = state->playheadSeconds;
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.guideLayer(), playheadSeconds, &guideCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.headLayer(), playheadSeconds, &headCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.slideLikeLayer(), playheadSeconds, &trackCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.slideLikeLayer(), playheadSeconds, &slideMotionCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.judgeEffectLayer(), playheadSeconds, &judgeEffectCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.judgeFireworkLayer(), playheadSeconds, &judgeFireworkCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.touchLayer(), playheadSeconds, &touchCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.touchJudgeLayer(), playheadSeconds, &touchJudgeCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.touchHoldLayer(), playheadSeconds, &touchHoldCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.chartReviewLayer(), playheadSeconds, &chartReviewCursor_);
    miacode::preview::scene::syncPreviewLayerWindowCursor(preparedCache_.maimuriDxJudgeLayer(), playheadSeconds, &maimuriDxJudgeCursor_);

    const auto applyWindowCounts =
        [this](const char* layerName, qint64 candidateCount, qint64 activeCount) {
            PreviewTextureLayerStats& layerStat = ensureLayerProfileStat(&layerProfileStats_, layerName);
            layerStat.candidateCount = candidateCount;
            layerStat.activeCount = activeCount;
        };

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
            return judgeFireworkLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &judgeFireworkCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "judge_firework",
        preparedCache_.judgeFireworkLayer().entries.size(),
        judgeFireworkCursor_.activePreparedIndices.size()
    );
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::GuideLayer),
        "guide",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return guideLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &guideCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts("guide", preparedCache_.guideLayer().entries.size(), guideCursor_.activePreparedIndices.size());
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TrackLayer),
        "track",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return trackLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &trackCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "track",
        preparedCache_.slideLikeLayer().entries.size(),
        trackCursor_.activePreparedIndices.size()
    );
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::SlideMotionLayer),
        "slide_motion",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return slideMotionLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &slideMotionCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "slide_motion",
        preparedCache_.slideLikeLayer().entries.size(),
        slideMotionCursor_.activePreparedIndices.size()
    );
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeLayer),
        "judge_effect",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return judgeEffectLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &judgeEffectCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "judge_effect",
        preparedCache_.judgeEffectLayer().entries.size(),
        judgeEffectCursor_.activePreparedIndices.size()
    );
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::JudgeTouchLayer),
        "touch_judge",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return touchJudgeLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &touchJudgeCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "touch_judge",
        preparedCache_.touchJudgeLayer().entries.size(),
        touchJudgeCursor_.activePreparedIndices.size()
    );
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::HeadLayer),
        "head",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return headLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &headCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts("head", preparedCache_.headLayer().entries.size(), headCursor_.activePreparedIndices.size());
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TouchLayer),
        "touch",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return touchLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &touchCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts("touch", preparedCache_.touchLayer().entries.size(), touchCursor_.activePreparedIndices.size());
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::TouchHoldLayer),
        "touch_hold",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return touchHoldLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &touchHoldCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "touch_hold",
        preparedCache_.touchHoldLayer().entries.size(),
        touchHoldCursor_.activePreparedIndices.size()
    );
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::ChartReviewLayer),
        "chart_review",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return chartReviewLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &chartReviewCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "chart_review",
        preparedCache_.chartReviewLayer().entries.size(),
        chartReviewCursor_.activePreparedIndices.size()
    );
    updateLayerSlotProfiled(
        layerSlotAt(root, slotIndex++),
        miacode::preview::scene::previewRenderLayerEnabled(layerFlags_, miacode::preview::scene::MaimuriDxJudgeLayer),
        "maimuri_dx_judge",
        &layerProfileStats_,
        [&](QSGNode* oldChild) {
            return maimuriDxJudgeLayer_.updateNode(
                oldChild,
                *state,
                &preparedCache_,
                &maimuriDxJudgeCursor_,
                renderSize,
                window(),
                &textures_);
        });
    applyWindowCounts(
        "maimuri_dx_judge",
        preparedCache_.maimuriDxJudgeLayer().entries.size(),
        maimuriDxJudgeCursor_.activePreparedIndices.size()
    );
    return root;
}

void PreviewQuickSceneRoot::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        if (runtime_ != nullptr) {
            runtime_->setFrameSize(newGeometry.size().toSize());
        }
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
