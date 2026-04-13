#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>
#include <QSize>
#include <QString>

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
#include "preview/quick_scene/PreviewQuickTouchLayer.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewLayerOrder.h"

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

private:
    QString instanceTag() const;
    void syncVisibleHostWindowBinding(const char* reason = "unspecified");

    PreviewRuntime* runtime_ = nullptr;
    QMetaObject::Connection runtimeUpdateConnection_;
    QMetaObject::Connection frameSwapConnection_;
    QMetaObject::Connection windowVisibilityConnection_;
    QPointer<QQuickWindow> boundWindow_;
    const miacode::preview::scene::PreviewFrameState* frameState_ = nullptr;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
    QVector<PreviewTextureLayerStats> layerProfileStats_;
    PreviewTextureRepository textures_;
    PreviewQuickStageBackgroundLayer stageBackgroundLayer_;
    PreviewQuickBackdropLayer backdropLayer_;
    PreviewQuickMuriPadLayer muriPadLayer_;
    PreviewQuickMuriActionLayer muriActionLayer_;
    PreviewQuickJudgeFireworkLayer judgeFireworkLayer_;
    PreviewQuickGuideLayer guideLayer_;
    PreviewQuickTrackLayer trackLayer_;
    PreviewQuickSlideMotionLayer slideMotionLayer_;
    PreviewQuickJudgeEffectLayer judgeEffectLayer_;
    PreviewQuickTouchJudgeLayer touchJudgeLayer_;
    PreviewQuickHeadLayer headLayer_;
    PreviewQuickTouchLayer touchLayer_;
    PreviewQuickTouchHoldLayer touchHoldLayer_;
    PreviewQuickChartReviewLayer chartReviewLayer_;
    PreviewQuickMaimuriDxJudgeLayer maimuriDxJudgeLayer_;
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
    bool lastLoggedHasWindow_ = false;
    bool lastLoggedHasState_ = false;
    QSize lastLoggedRenderSize_;
    quintptr lastLoggedWindowHandle_ = 0;
};
