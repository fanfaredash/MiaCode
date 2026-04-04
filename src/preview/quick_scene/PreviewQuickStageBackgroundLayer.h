#pragma once

#include "preview/scene/PreviewFrameState.h"

class QImage;
class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;

class PreviewQuickStageBackgroundLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::preview::scene::PreviewFrameState& state,
        const QSize& renderSize,
        QQuickWindow* window,
        PreviewTextureRepository* textures
    ) const;

private:
    mutable QImage brightnessMaskCache_;
    mutable QSize brightnessMaskCacheSize_;
    mutable double brightnessMaskCacheOuter_ = -1.0;
    mutable double brightnessMaskCacheInner_ = -1.0;
    mutable double brightnessMaskCacheLayoutScale_ = -1.0;
    mutable double brightnessMaskCacheRingRatio_ = -1.0;
    mutable bool brightnessMaskCacheSmooth_ = false;
};
