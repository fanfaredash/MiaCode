#pragma once

#include "preview/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

struct PreviewJudgeFireworkLayerState {
    bool active = false;
    bool drawFirework = false;
    bool drawColorBall = false;
    bool drawColorBallBig = false;
    bool drawFallbackColorBall = false;
    QPointF center;
    QPointF clipCenter;
    qreal clipRadius = 0.0;
    qreal fireworkScale = 0.0;
    qreal outerRadius = 0.0;
    qreal fireworkAlpha = 0.0;
    qreal fireworkRotationDegrees = 0.0;
    qreal holeRadius = 0.0;
    qreal holeMaskRadius = 0.0;
    qreal colorBallScale = 0.0;
    qreal colorBallBigScale = 0.0;
    qreal colorBallAlpha = 0.0;
    qreal colorBallBigAlpha = 0.0;
    qreal colorBallRadius = 0.0;
    qreal colorBallBigRadius = 0.0;
    qreal fallbackColorBallRadius = 0.0;
    qreal fallbackColorBallAlpha = 0.0;
    const QImage* colorBallImage = nullptr;
    QRectF colorBallSourceRect;
};

PreviewJudgeFireworkLayerState buildPreviewJudgeFireworkLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
