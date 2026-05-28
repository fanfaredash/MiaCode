#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include "common/DebugOptions.h"
#include "common/DebugLog.h"
#include "common/Mmcss.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewHudState.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/scene/PreviewPreparedSceneCache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QAtomicInteger>
#include <QMetaObject>
#include <QPainter>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGNode>

namespace {

constexpr int kPreviewQuickSceneLayerSlotCount = 16;

quint64 nextPreviewQuickSceneRootInstanceId()
{
    static QAtomicInteger<quint64> nextId = 1;
    return nextId.fetchAndAddRelaxed(1);
}

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

void updateCenterDisplaySlot(
    QSGNode* slot,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    miacode::preview_gameplay::CenterDisplayMode& cachedMode,
    double& cachedDeluxeRate,
    double& cachedFinaleRate,
    int& cachedCombo,
    int& cachedDxScore,
    int& cachedDxScoreMax,
    int& cachedBreakCurrent,
    int& cachedBreakTotal,
    QSize& cachedRenderSize,
    qreal& cachedDpr)
{
    if (slot == nullptr || window == nullptr) {
        return;
    }
    if (state.render.centerDisplayMode == miacode::preview_gameplay::CenterDisplayMode::Off) {
        QSGNode* old = slot->firstChild();
        if (old != nullptr) {
            delete static_cast<QSGSimpleTextureNode*>(old)->texture();
            slot->removeChildNode(old);
            delete old;
        }
        cachedMode = miacode::preview_gameplay::CenterDisplayMode::Off;
        return;
    }
    const qreal dpr = window->effectiveDevicePixelRatio();
    const QSize imageSize(qRound(renderSize.width() * dpr), qRound(renderSize.height() * dpr));
    const bool isConstantDisplay =
        state.render.centerDisplayMode == miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101;
    const bool isBreakOnlyDisplay =
        state.render.centerDisplayMode == miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100;
    const double playheadSeconds = qIsFinite(state.hudPlayheadSecondsOverride)
        ? state.hudPlayheadSecondsOverride
        : state.playheadSeconds;
    miacode::preview::scene::PreviewHudStats stats;
    if (!isConstantDisplay && state.progressStatsCache != nullptr) {
        stats = state.progressStatsCache->hudStatsAt(playheadSeconds);
    }
    if (!isConstantDisplay && !isBreakOnlyDisplay) {
        if (cachedMode == state.render.centerDisplayMode
            && qFuzzyCompare(cachedDeluxeRate + 1.0, stats.deluxeRate + 1.0)
            && qFuzzyCompare(cachedFinaleRate + 1.0, stats.finaleRate + 1.0)
            && cachedCombo == stats.combo
            && cachedDxScore == stats.dxScore
            && cachedDxScoreMax == stats.dxScoreMax
            && cachedRenderSize == renderSize
            && qFuzzyCompare(cachedDpr + 1.0, dpr + 1.0)) {
            return;
        }
        cachedDeluxeRate = stats.deluxeRate;
        cachedFinaleRate = stats.finaleRate;
        cachedCombo = stats.combo;
        cachedDxScore = stats.dxScore;
        cachedDxScoreMax = stats.dxScoreMax;
    }
    if (isConstantDisplay || isBreakOnlyDisplay) {
        bool unchanged = cachedMode == state.render.centerDisplayMode
            && cachedRenderSize == renderSize
            && qFuzzyCompare(cachedDpr + 1.0, dpr + 1.0);
        if (isBreakOnlyDisplay && unchanged) {
            unchanged = cachedBreakCurrent == stats.deluxeBreakCurrent
                && cachedBreakTotal == stats.deluxeBreakTotal;
            cachedBreakCurrent = stats.deluxeBreakCurrent;
            cachedBreakTotal = stats.deluxeBreakTotal;
        }
        if (unchanged) {
            return;
        }
    }
    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    miacode::preview::hud::paintCenterDisplay(painter, state, renderSize);
    painter.end();
    QSGTexture* texture = window->createTextureFromImage(image);
    if (texture == nullptr) {
        return;
    }
    QSGSimpleTextureNode* node = dynamic_cast<QSGSimpleTextureNode*>(slot->firstChild());
    if (node == nullptr) {
        QSGNode* old = slot->firstChild();
        if (old != nullptr) {
            delete static_cast<QSGSimpleTextureNode*>(old)->texture();
            slot->removeChildNode(old);
            delete old;
        }
        node = new QSGSimpleTextureNode();
        slot->appendChildNode(node);
    }
    node->setTexture(texture);
    node->setRect(0, 0, renderSize.width(), renderSize.height());
    cachedMode = state.render.centerDisplayMode;
    cachedRenderSize = renderSize;
    cachedDpr = dpr;
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
    , instanceId_(nextPreviewQuickSceneRootInstanceId())
{
    setFlag(ItemHasContents, true);
    // Phase 4a — let PreviewDCompSurface auto-discover this item via
    // QObject::findChild on the QQuickWindow. Decoupled from the
    // dcomp/ side: surface looks up by objectName instead of taking a
    // direct dependency on PreviewQuickSceneRoot's type.
    setObjectName(QStringLiteral("preview_dcomp_track_target"));
    appendQuickSceneLog(
        QStringLiteral("scene_root_construct"),
        QString("%1 item_visible=%2")
            .arg(instanceTag())
            .arg(isVisible() ? 1 : 0)
    );
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow*) {
        appendQuickSceneLog(
            QStringLiteral("scene_root_window_changed"),
            QString("%1 window=%2 item_visible=%3")
                .arg(instanceTag())
                .arg(pointerHex(window()))
                .arg(isVisible() ? 1 : 0)
        );
        syncVisibleHostWindowBinding("window_changed");
        update();
    });
    connect(this, &QQuickItem::visibleChanged, this, [this]() {
        appendQuickSceneLog(
            QStringLiteral("scene_root_visible_changed"),
            QString("%1 visible=%2 window=%3")
                .arg(instanceTag())
                .arg(isVisible() ? 1 : 0)
                .arg(pointerHex(window()))
        );
        syncVisibleHostWindowBinding("visible_changed");
        update();
    });
}

PreviewQuickSceneRoot::~PreviewQuickSceneRoot()
{
    if (frameSwapConnection_) {
        QObject::disconnect(frameSwapConnection_);
    }
    if (windowVisibilityConnection_) {
        QObject::disconnect(windowVisibilityConnection_);
    }
    if (runtimeUpdateConnection_) {
        QObject::disconnect(runtimeUpdateConnection_);
    }
    appendQuickSceneLog(
        QStringLiteral("scene_root_destroy"),
        QString("%1 runtime=%2 bound_window=%3")
            .arg(instanceTag())
            .arg(pointerHex(runtime_))
            .arg(pointerHex(boundWindow_))
    );
    if (runtime_ != nullptr && boundWindow_ != nullptr) {
        runtime_->clearVisibleHostWindow(boundWindow_);
    }
}

QString PreviewQuickSceneRoot::instanceTag() const
{
    return QStringLiteral("instance=%1").arg(instanceId_);
}

void PreviewQuickSceneRoot::clearPendingTextureStatsForPresentation()
{
    QMutexLocker locker(&latestTextureStatsMutex_);
    pendingTextureStats_.clear();
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
    clearPendingTextureStatsForPresentation();
    runtime_ = runtime;
    if (runtime_ != nullptr) {
        frameState_ = nullptr;
        if (!miacode::debug_options::previewDCompQuiesceQsgEnabled()) {
            runtimeUpdateConnection_ = QObject::connect(runtime_, &PreviewRuntime::frameStateChanged, this, [this]() {
                update();
            });
        }
        runtime_->setFrameSize(boundingRect().size().toSize());
    }
    syncVisibleHostWindowBinding("set_runtime");
    appendQuickSceneLog(
        QStringLiteral("set_runtime"),
        QString("%1 runtime=%2 frame_state=%3")
            .arg(instanceTag())
            .arg(pointerHex(runtime_))
            .arg(pointerHex(frameState_))
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

void PreviewQuickSceneRoot::setDCompFallbackActive(bool active)
{
    if (dcompFallbackActive_ == active) {
        return;
    }
    dcompFallbackActive_ = active;
    // Force a re-paint so the next updatePaintNode honours the new
    // setting immediately. With fallback on, the legacy QSG path
    // resumes producing pixels; with it off, the DComp short-circuit
    // returns nullptr again. Either way the user-visible state needs
    // to switch within one frame of the QML toggle.
    update();
    emit dcompFallbackActiveChanged();
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
    clearPendingTextureStatsForPresentation();
    frameState_ = frameState;
    if (frameState_ != nullptr) {
        runtime_.clear();
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

void PreviewQuickSceneRoot::enqueueTextureStatsForPresentation(const PreviewTextureStats& stats)
{
    QMutexLocker locker(&latestTextureStatsMutex_);
    pendingTextureStats_.enqueue(stats);
    while (pendingTextureStats_.size() > 8) {
        pendingTextureStats_.dequeue();
    }
}

PreviewTextureStats PreviewQuickSceneRoot::takePendingTextureStatsForPresentation() const
{
    QMutexLocker locker(&latestTextureStatsMutex_);
    if (pendingTextureStats_.isEmpty()) {
        return PreviewTextureStats();
    }
    return pendingTextureStats_.dequeue();
}

void PreviewQuickSceneRoot::syncVisibleHostWindowBinding(const char* reason)
{
    const QString reasonText = QString::fromLatin1(reason != nullptr ? reason : "unspecified");
    if (frameSwapConnection_) {
        QObject::disconnect(frameSwapConnection_);
        frameSwapConnection_ = QMetaObject::Connection();
    }
    if (windowVisibilityConnection_) {
        QObject::disconnect(windowVisibilityConnection_);
        windowVisibilityConnection_ = QMetaObject::Connection();
    }
    if (runtime_ != nullptr && boundWindow_ != nullptr) {
        appendQuickSceneLog(
            QStringLiteral("visible_host_clear"),
            QString("%1 reason=%2 window=%3 item_visible=%4 window_visible=%5 runtime=%6")
                .arg(instanceTag())
                .arg(reasonText)
                .arg(pointerHex(boundWindow_))
                .arg(isVisible() ? 1 : 0)
                .arg(boundWindow_->isVisible() ? 1 : 0)
                .arg(pointerHex(runtime_))
        );
        runtime_->clearVisibleHostWindow(boundWindow_);
    }

    const QPointer<QQuickWindow> previousBoundWindow = boundWindow_;
    boundWindow_ = window();
    if (previousBoundWindow != boundWindow_) {
        clearPendingTextureStatsForPresentation();
    }
    if (runtime_ == nullptr || boundWindow_ == nullptr || !isVisible() || !boundWindow_->isVisible()) {
        appendQuickSceneLog(
            QStringLiteral("visible_host_skip"),
            QString("%1 reason=%2 runtime=%3 window=%4 item_visible=%5 window_visible=%6")
                .arg(instanceTag())
                .arg(reasonText)
                .arg(pointerHex(runtime_))
                .arg(pointerHex(boundWindow_))
                .arg(isVisible() ? 1 : 0)
                .arg(boundWindow_ != nullptr && boundWindow_->isVisible() ? 1 : 0)
        );
        if (boundWindow_ != nullptr) {
            windowVisibilityConnection_ = QObject::connect(boundWindow_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility) {
                syncVisibleHostWindowBinding("window_visibility_changed");
                update();
            });
        }
        return;
    }

    appendQuickSceneLog(
        QStringLiteral("visible_host_bind"),
        QString("%1 reason=%2 window=%3 item_visible=%4 window_visible=%5 runtime=%6")
            .arg(instanceTag())
            .arg(reasonText)
            .arg(pointerHex(boundWindow_))
            .arg(isVisible() ? 1 : 0)
            .arg(boundWindow_->isVisible() ? 1 : 0)
            .arg(pointerHex(runtime_))
    );
    {
        // One-time log of which graphics API Qt actually settled on. The static enum is
        // available here even before sceneGraphInitialized fires; the matching driver
        // string requires QRhi which may not exist yet — query lazily on first present.
        const QSGRendererInterface* iface = boundWindow_->rendererInterface();
        const auto apiToString = [](QSGRendererInterface::GraphicsApi api) -> QString {
            switch (api) {
                case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D11");
                case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D12");
                case QSGRendererInterface::OpenGL:     return QStringLiteral("OpenGL");
                case QSGRendererInterface::Vulkan:     return QStringLiteral("Vulkan");
                case QSGRendererInterface::Metal:      return QStringLiteral("Metal");
                case QSGRendererInterface::Software:   return QStringLiteral("Software");
                case QSGRendererInterface::Unknown:    return QStringLiteral("Unknown");
                default:                               return QStringLiteral("OtherEnum");
            }
        };
        appendQuickSceneLog(
            QStringLiteral("rhi_backend"),
            QString("graphics_api=%1 enum=%2")
                .arg(iface != nullptr ? apiToString(iface->graphicsApi()) : QStringLiteral("(null)"))
                .arg(iface != nullptr ? static_cast<int>(iface->graphicsApi()) : -1)
        );
    }
    runtime_->setVisibleHostWindow(boundWindow_);
    windowVisibilityConnection_ = QObject::connect(boundWindow_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility) {
        syncVisibleHostWindowBinding("window_visibility_changed");
        update();
    });
    frameSwapConnection_ = QObject::connect(boundWindow_, &QQuickWindow::frameSwapped, this, [this]() {
        if (runtime_ == nullptr || boundWindow_ == nullptr || window() != boundWindow_) {
            return;
        }
        if (miacode::debug_options::previewProfileOutputEnabled()) {
            runtime_->notePresentedTextureStats(takePendingTextureStatsForPresentation());
        }
        runtime_->notifyVisibleFramePresented();
    });
    // Register Qt's QSG render thread with Windows MMCSS (Multimedia Class Scheduler
    // Service) under the "Games" task class. This asks the OS scheduler to protect the
    // thread from being preempted by background work — the standard fix used by every
    // native rhythm game and DAW on Windows for the same bimodal-stall pattern we're
    // hitting (render thread occasionally gets scheduled out for a full quantum, surfacing
    // as render_submit_ms = 22ms with pre_render_wait_ms ≈ 0).
    //
    // We hook beforeSynchronizing rather than sceneGraphInitialized because the latter
    // fires once and we may have already missed it by the time syncVisibleHostWindowBinding
    // runs (depending on QML construction order and when the scene graph spins up).
    // beforeSynchronizing fires every frame on the render thread, so the first invocation
    // after our connection is a guaranteed-on-render-thread checkpoint. The atomic guard
    // ensures we only call into Win32 once per scene-graph lifetime; the lambda becomes a
    // single atomic load + early return on every subsequent frame.
    mmcssRegistrationAttempted_.store(false, std::memory_order_relaxed);
    mmcssBootstrapConnection_ = QObject::connect(
        boundWindow_, &QQuickWindow::beforeSynchronizing, this,
        [this]() {
            bool expected = false;
            if (!mmcssRegistrationAttempted_.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                return;
            }
            const miacode::mmcss::RegistrationResult result =
                miacode::mmcss::registerCurrentThread(QStringLiteral("Games"));
            appendQuickSceneLog(
                QStringLiteral("mmcss_register"),
                QString("registered=%1 task_class=%2 reason=%3 errno=%4")
                    .arg(result.registered ? 1 : 0)
                    .arg(result.taskClassUsed.isEmpty() ? QStringLiteral("(none)") : result.taskClassUsed)
                    .arg(result.skipReason.isEmpty() ? QStringLiteral("(ok)") : result.skipReason)
                    .arg(result.lastErrorCode));
        },
        Qt::DirectConnection);
    // sceneGraphInvalidated runs on the render thread before it shuts down (e.g. window
    // hide). Unregister so the OS releases its MMCSS reservation; reset the guard so a
    // subsequent rebind picks up MMCSS again on the new render thread.
    sceneGraphInvalidatedConnection_ = QObject::connect(
        boundWindow_, &QQuickWindow::sceneGraphInvalidated, this,
        [this]() {
            miacode::mmcss::unregisterCurrentThread();
            mmcssRegistrationAttempted_.store(false, std::memory_order_release);
        },
        Qt::DirectConnection);
    if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        // Direct-connected render-thread hooks. Qt Quick's threaded render loop runs
        // beforeSynchronizing → afterSynchronizing → beforeRendering → afterRendering → frameSwapped
        // in order on the render thread (updatePaintNode also runs on that thread, before this chain).
        // Timing each phase lets us break the cycle wall_ms into: paint-node build, sync handoff,
        // render command submission, GPU execute + vsync wait.
        if (!renderPhaseTimer_.isValid()) {
            renderPhaseTimer_.start();
        }
        renderBeforeSyncConnection_ = QObject::connect(
            boundWindow_, &QQuickWindow::beforeSynchronizing, this,
            [this]() { renderPhaseSyncStartNs_ = renderPhaseTimer_.nsecsElapsed(); },
            Qt::DirectConnection);
        renderAfterSyncConnection_ = QObject::connect(
            boundWindow_, &QQuickWindow::afterSynchronizing, this,
            [this]() { renderPhaseSyncEndNs_ = renderPhaseTimer_.nsecsElapsed(); },
            Qt::DirectConnection);
        renderBeforeRenderConnection_ = QObject::connect(
            boundWindow_, &QQuickWindow::beforeRendering, this,
            [this]() { renderPhaseRenderStartNs_ = renderPhaseTimer_.nsecsElapsed(); },
            Qt::DirectConnection);
        renderAfterRenderConnection_ = QObject::connect(
            boundWindow_, &QQuickWindow::afterRendering, this,
            [this]() { renderPhaseRenderEndNs_ = renderPhaseTimer_.nsecsElapsed(); },
            Qt::DirectConnection);
        renderFrameSwapProfileConnection_ = QObject::connect(
            boundWindow_, &QQuickWindow::frameSwapped, this,
            [this]() { recordRenderPhaseProfile(); },
            Qt::DirectConnection);
    }
}

QSGNode* PreviewQuickSceneRoot::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData)
{
    Q_UNUSED(updatePaintNodeData);

    // Phase 4b — when DComp-exclusive mode is on, the DComp surface is
    // the authoritative chart renderer; this QSG path produces nothing.
    // Discard any existing scene-graph subtree and return null so Qt
    // skips this layer entirely. The QQuickItem itself stays visible
    // (so the DComp surface's findChild + mapToScene tracking still
    // works), but its bounding rect contributes no pixels to the QSG
    // scene. PreviewQuickHudLayer + PreviewStageMediaItem are sibling
    // items and continue to render normally.
    //
    // Phase 4d-fix — exception: when per-pixel alpha is on (the default
    // case where exclusive auto-enables), we still want the QSG
    // stage_background dim shader to run so the user's "Background
    // brightness" sliders affect QML's PreviewStageMediaItem (which
    // shows through DComp's transparent areas). The chart-sprite QSG
    // layers stay skipped — only the dim slot is processed. See the
    // `keepDimOnly` short-circuit below the dim slot for the second
    // half of this conditional.
    // Issue #4 fix — `dcompFallbackActive_` overrides the DComp-exclusive
    // short-circuit. QML sets this on the fullscreen QuickShellPreviewSurface
    // instance because the DComp popup HWND can't follow the secondary
    // fullscreen window (it's owned by the editor and would be z-ordered
    // behind the fullscreen window). With fallback active, the legacy
    // QSG path renders chart content into the fullscreen window directly,
    // matching the embedded preview's appearance.
    const bool exclusive = !dcompFallbackActive_
        && miacode::debug_options::previewDCompExclusiveEnabled();
    const bool keepDimOnly = exclusive
        && miacode::debug_options::previewDCompPerPixelAlphaEnabled();
    if (exclusive && !keepDimOnly) {
        if (oldNode != nullptr) {
            delete oldNode;
        }
        return nullptr;
    }

    const bool renderDiagEnabled = miacode::debug_options::previewFramePacingDiagnosticsEnabled();
    if (renderDiagEnabled) {
        if (!renderPhaseTimer_.isValid()) {
            renderPhaseTimer_.start();
        }
        renderPhaseUpdatePaintStartNs_ = renderPhaseTimer_.nsecsElapsed();
    }

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
        if (miacode::debug_options::previewProfileOutputEnabled()) {
            enqueueTextureStatsForPresentation(textureStats());
        }
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
    // Phase 4d-fix — when per-pixel alpha is on (exclusive auto-on),
    // stop here. The dim-layer slot above is the only QSG output the
    // chart preview needs; chart-sprite layers below are owned by
    // DComp. This preserves the user's Background brightness controls
    // while keeping DComp the exclusive chart renderer.
    if (keepDimOnly) {
        // Trim any stale chart-sprite slots from a prior frame that
        // ran without exclusive mode — otherwise they'd still be
        // attached to root from before the toggle.
        const int currentChildCount = root->childCount();
        for (int i = currentChildCount - 1; i >= slotIndex; --i) {
            QSGNode* extra = root->childAtIndex(i);
            root->removeChildNode(extra);
            delete extra;
        }
        return root;
    }
    updateCenterDisplaySlot(
        layerSlotAt(root, slotIndex++),
        *state,
        renderSize,
        window(),
        cachedCenterDisplayMode_,
        cachedCenterDisplayDeluxeRate_,
        cachedCenterDisplayFinaleRate_,
        cachedCenterDisplayCombo_,
        cachedCenterDisplayDxScore_,
        cachedCenterDisplayDxScoreMax_,
        cachedCenterDisplayBreakCurrent_,
        cachedCenterDisplayBreakTotal_,
        cachedCenterDisplayRenderSize_,
        cachedCenterDisplayDpr_);
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
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        enqueueTextureStatsForPresentation(textureStats());
    }
    if (renderDiagEnabled) {
        renderPhaseUpdatePaintEndNs_ = renderPhaseTimer_.nsecsElapsed();
    }
    return root;
}

void PreviewQuickSceneRoot::recordRenderPhaseProfile()
{
    // Runs on the render thread (DirectConnection from frameSwapped). Read the phase
    // timestamps captured by the sibling signal handlers and log a single compact summary.
    // Rate-limited: log once per sample interval, plus log slow frames unconditionally to
    // catch spikes. All timestamps are monotonic nsecs from the same QElapsedTimer.
    if (runtime_ == nullptr || !renderPhaseTimer_.isValid()) {
        return;
    }
    const qint64 swapNs = renderPhaseTimer_.nsecsElapsed();
    const qint64 paintStart = renderPhaseUpdatePaintStartNs_;
    const qint64 paintEnd = renderPhaseUpdatePaintEndNs_;
    const qint64 syncStart = renderPhaseSyncStartNs_;
    const qint64 syncEnd = renderPhaseSyncEndNs_;
    const qint64 renderStart = renderPhaseRenderStartNs_;
    const qint64 renderEnd = renderPhaseRenderEndNs_;

    auto toMs = [](qint64 ns) -> double {
        return ns >= 0 ? static_cast<double>(ns) / 1000000.0 : -1.0;
    };
    auto deltaMs = [](qint64 endNs, qint64 startNs) -> double {
        if (endNs < 0 || startNs < 0 || endNs < startNs) {
            return -1.0;
        }
        return static_cast<double>(endNs - startNs) / 1000000.0;
    };

    const double paintMs = deltaMs(paintEnd, paintStart);
    const double syncMs = deltaMs(syncEnd, syncStart);
    const double renderSubmitMs = deltaMs(renderEnd, renderStart);
    const double swapGpuMs = deltaMs(swapNs, renderEnd);
    const double totalMs = deltaMs(swapNs, paintStart);
    // pre_render_wait_ms = gap between afterSynchronizing and beforeRendering. In Qt's
    // threaded RHI render loop the swap-chain image acquire happens here, so a fat value
    // is the smoking gun for vsync / DXGI flip-queue back-pressure (we'll see this spike
    // even when the per-node command-buffer build is fast). A near-zero value means the
    // ~22ms in render_submit_ms is genuinely command-buffer construction.
    const double preRenderWaitMs = deltaMs(renderStart, syncEnd);

    // Find the layer with highest buildMs this frame. Acceptable to iterate — at most ~15 entries.
    double topLayerBuildMs = 0.0;
    QString topLayerName;
    double totalLayerBuildMs = 0.0;
    for (const PreviewTextureLayerStats& stat : layerProfileStats_) {
        totalLayerBuildMs += stat.buildMs;
        if (stat.buildMs > topLayerBuildMs) {
            topLayerBuildMs = stat.buildMs;
            topLayerName = stat.name;
        }
    }

    const qint64 nowMs = static_cast<qint64>(swapNs / 1000000LL);
    const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
    // "Slow" = total frame pipeline > ~2 target intervals. Use 30ms as a fixed floor since the
    // render-thread code has no direct access to target interval and vsync is typically 16.67ms.
    constexpr double kSlowFrameMs = 30.0;
    const bool slowFrame = totalMs >= kSlowFrameMs;
    const bool sampleReady =
        renderPhaseLastLogMs_ < 0 || (nowMs - renderPhaseLastLogMs_) >= sampleMs;
    if (!slowFrame && !sampleReady) {
        return;
    }
    renderPhaseLastLogMs_ = nowMs;

    appendQuickSceneLog(
        QStringLiteral("render_frame_profile"),
        QString("total_ms=%1 paint_ms=%2 sync_ms=%3 pre_render_wait_ms=%4 render_submit_ms=%5 "
                "swap_gpu_ms=%6 layer_sum_ms=%7 top_layer=%8 top_layer_ms=%9 layer_count=%10 slow=%11")
            .arg(totalMs < 0 ? QStringLiteral("na") : QString::number(totalMs, 'f', 3))
            .arg(paintMs < 0 ? QStringLiteral("na") : QString::number(paintMs, 'f', 3))
            .arg(syncMs < 0 ? QStringLiteral("na") : QString::number(syncMs, 'f', 3))
            .arg(preRenderWaitMs < 0 ? QStringLiteral("na") : QString::number(preRenderWaitMs, 'f', 3))
            .arg(renderSubmitMs < 0 ? QStringLiteral("na") : QString::number(renderSubmitMs, 'f', 3))
            .arg(swapGpuMs < 0 ? QStringLiteral("na") : QString::number(swapGpuMs, 'f', 3))
            .arg(QString::number(totalLayerBuildMs, 'f', 3))
            .arg(topLayerName.isEmpty() ? QStringLiteral("none") : topLayerName)
            .arg(QString::number(topLayerBuildMs, 'f', 3))
            .arg(layerProfileStats_.size())
            .arg(slowFrame ? 1 : 0)
    );
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
