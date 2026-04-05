#pragma once

#include "preview/scene/PreviewSpriteDescriptor.h"

class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;

QSGNode* buildPreviewSpriteNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewSpriteDescriptors& sprites,
    QQuickWindow* window,
    PreviewTextureRepository* textures,
    double playheadSeconds,
    const char* profileLayerName
);
