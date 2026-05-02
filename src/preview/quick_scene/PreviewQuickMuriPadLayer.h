#pragma once

#include "core/scene/PreviewFrameState.h"

class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;

class PreviewQuickMuriPadLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::preview::scene::PreviewFrameState& state,
        const QSize& renderSize,
        QQuickWindow* window,
        PreviewTextureRepository* textures
    ) const;
};
