#pragma once

#include <QQuickItem>

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
#include "preview/scene/PreviewLayerOrder.h"

class PreviewRuntime;
namespace miacode::preview::scene {
struct PreviewFrameState;
}

class PreviewQuickSceneRoot : public QQuickItem
{
    Q_OBJECT

public:
    explicit PreviewQuickSceneRoot(QQuickItem* parent = nullptr);

    void setRuntime(PreviewRuntime* runtime);
    void setFrameState(const miacode::preview::scene::PreviewFrameState* frameState);
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    PreviewRuntime* runtime_ = nullptr;
    const miacode::preview::scene::PreviewFrameState* frameState_ = nullptr;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
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
};
