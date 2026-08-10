#pragma once

#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutex>
#include <QPointer>
#include <QQueue>
#include <QQuickItem>
#include "common/PreviewGameplayConfig.h"
#include <QSize>
#include <QString>
#include <atomic>

#include "preview/quick_scene/PreviewQuickBackdropLayer.h"
#include "preview/quick_scene/PreviewQuickChartReviewLayer.h"
#include "preview/quick_scene/PreviewQuickGuideLayer.h"
#include "preview/quick_scene/PreviewQuickHeadLayer.h"
#include "preview/quick_scene/PreviewQuickJudgeEffectLayer.h"
#include "preview/quick_scene/PreviewQuickJudgeFireworkLayer.h"
#include "preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h"
#include "preview/quick_scene/PreviewQuickMuriActionLayer.h"
#include "preview/quick_scene/PreviewQuickMuriPadLayer.h"
#include "preview/quick_scene/PreviewQuickSlideMotionLayer.h"
#include "preview/quick_scene/PreviewQuickStageBackgroundLayer.h"
#include "preview/quick_scene/PreviewQuickTrackLayer.h"
#include "preview/quick_scene/PreviewQuickTouchJudgeLayer.h"
#include "preview/quick_scene/PreviewQuickTouchHoldLayer.h"
#include "preview/quick_scene/PreviewQuickTouchHoverLayer.h"
#include "preview/quick_scene/PreviewQuickTouchLayer.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "core/scene/PreviewLayerOrder.h"

class PreviewRuntime;
namespace miacode::preview::scene {
struct PreviewFrameState;
}

class PreviewQuickSceneRoot : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* runtime READ runtimeObject WRITE setRuntimeObject NOTIFY runtimeChanged)

public:
    explicit PreviewQuickSceneRoot(QQuickItem* parent = nullptr);
    ~PreviewQuickSceneRoot() override;

    void setRuntime(PreviewRuntime* runtime);
    QObject* runtimeObject() const;
    void setRuntimeObject(QObject* runtimeObject);
    void setFrameState(const miacode::preview::scene::PreviewFrameState* frameState);
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);

    void invalidateTextureCache();
    PreviewTextureStats textureStats() const;

signals:
    void runtimeChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;

private:
    QString instanceTag() const;
    QString touchPadAtItemPoint(const QPointF& itemPoint) const;
    void updateHoveredTouchPadAtItemPoint(const QPointF& itemPoint);
    void clearPendingTextureStatsForPresentation();
    void enqueueTextureStatsForPresentation(const PreviewTextureStats& stats);
    PreviewTextureStats takePendingTextureStatsForPresentation() const;
    void syncVisibleHostWindowBinding(const char* reason = "unspecified");
    void recordRenderPhaseProfile();

    QPointer<PreviewRuntime> runtime_;
    Qt::MouseButton touchPadAuthoringPressedButton_ = Qt::NoButton;
    QMetaObject::Connection runtimeUpdateConnection_;
    QMetaObject::Connection frameSwapConnection_;
    QMetaObject::Connection windowVisibilityConnection_;
    QMetaObject::Connection renderBeforeSyncConnection_;
    QMetaObject::Connection renderAfterSyncConnection_;
    QMetaObject::Connection renderBeforeRenderConnection_;
    QMetaObject::Connection renderAfterRenderConnection_;
    QMetaObject::Connection renderFrameSwapProfileConnection_;
    QMetaObject::Connection sceneGraphInvalidatedConnection_;
    QMetaObject::Connection mmcssBootstrapConnection_;
    std::atomic<bool> mmcssRegistrationAttempted_{false};
    QPointer<QQuickWindow> boundWindow_;
    const miacode::preview::scene::PreviewFrameState* frameState_ = nullptr;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
    QVector<PreviewTextureLayerStats> layerProfileStats_;
    std::atomic<bool> textureResetRequested_{false};
    PreviewQuickStageBackgroundLayer stageBackgroundLayer_;
    PreviewQuickBackdropLayer backdropLayer_;
    PreviewQuickMuriPadLayer muriPadLayer_;
    PreviewQuickMuriActionLayer muriActionLayer_;
    PreviewQuickJudgeFireworkLayer judgeFireworkLayer_;
    PreviewQuickGuideLayer guideLayer_;
    PreviewQuickTouchHoverLayer touchHoverLayer_;
    PreviewQuickTrackLayer trackLayer_;
    PreviewQuickSlideMotionLayer slideMotionLayer_;
    PreviewQuickJudgeEffectLayer judgeEffectLayer_;
    PreviewQuickTouchJudgeLayer touchJudgeLayer_;
    PreviewQuickHeadLayer headLayer_;
    PreviewQuickTouchLayer touchLayer_;
    PreviewQuickTouchHoldLayer touchHoldLayer_;
    PreviewQuickChartReviewLayer chartReviewLayer_;
    PreviewQuickMaimuriDxJudgeLayer maimuriDxJudgeLayer_;
    // Slide-shape ("just" ring) sub-slots: same builders/cursors as above but
    // filtered to SlideShapeOnly and composited on a lower slot, so slide-OK
    // sprites sit below notes/judge-text as in MajdataPlay.
    PreviewQuickChartReviewLayer chartReviewShapeLayer_;
    PreviewQuickMaimuriDxJudgeLayer maimuriDxJudgeShapeLayer_;
    miacode::preview::scene::PreviewPreparedSceneCache preparedCache_;
    miacode::preview::scene::PreviewLayerWindowCursor guideCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor headCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor trackCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor slideMotionCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor judgeEffectCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor judgeFireworkCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor touchCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor touchJudgeCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor touchHoldCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor chartReviewCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor maimuriDxJudgeCursor_;
    quint64 updatePaintNodeCount_ = 0;
    quint64 geometryChangeCount_ = 0;
    quint64 instanceId_ = 0;
    mutable QMutex latestTextureStatsMutex_;
    mutable QQueue<PreviewTextureStats> pendingTextureStats_;
    PreviewTextureStats latestTextureStats_;
    bool lastLoggedHasWindow_ = false;
    bool lastLoggedHasState_ = false;
    QSize lastLoggedRenderSize_;
    quintptr lastLoggedWindowHandle_ = 0;
    // Render-thread phase timing (diagnostic-gated). Lives entirely on the render thread:
    // QQuickWindow fires all sync/render signals in sequence on the same render thread, and
    // QElapsedTimer::nsecsElapsed() is safe to read from one thread after start() is called.
    QElapsedTimer renderPhaseTimer_;
    qint64 renderPhaseUpdatePaintStartNs_ = -1;
    qint64 renderPhaseUpdatePaintEndNs_ = -1;
    qint64 renderPhaseSyncStartNs_ = -1;
    qint64 renderPhaseSyncEndNs_ = -1;
    qint64 renderPhaseRenderStartNs_ = -1;
    qint64 renderPhaseRenderEndNs_ = -1;
    qint64 renderPhaseLastLogMs_ = -1;
    // Center display cache — avoid per-frame QImage+texture regeneration.
    miacode::preview_gameplay::CenterDisplayMode cachedCenterDisplayMode_ =
        miacode::preview_gameplay::CenterDisplayMode::Off;
    double cachedCenterDisplayPlayheadSeconds_ = -1.0;
    QSize cachedCenterDisplayRenderSize_;
    qreal cachedCenterDisplayDpr_ = 1.0;
    // Stats-derived values that determine whether the center display text changed.
    double cachedCenterDisplayDeluxeRate_ = -1.0;
    double cachedCenterDisplayFinaleRate_ = -1.0;
    int cachedCenterDisplayCombo_ = -1;
    int cachedCenterDisplayDxScore_ = -1;
    int cachedCenterDisplayDxScoreMax_ = -1;
    int cachedCenterDisplayBreakCurrent_ = -1;
    int cachedCenterDisplayBreakTotal_ = -1;
};
