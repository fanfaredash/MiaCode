#pragma once

#include "preview/scene/PreviewFrameState.h"

class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;

class PreviewQuickMuriActionLayer
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
