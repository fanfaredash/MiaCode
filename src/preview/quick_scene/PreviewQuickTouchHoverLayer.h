#pragma once

#include "core/scene/PreviewFrameState.h"

class PreviewTextureRepository;
class QQuickWindow;
class QSGNode;
class QSize;

class PreviewQuickTouchHoverLayer {
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::preview::scene::PreviewFrameState& state,
        const QSize& renderSize,
        QQuickWindow* window,
        PreviewTextureRepository* textures
    ) const;
};
