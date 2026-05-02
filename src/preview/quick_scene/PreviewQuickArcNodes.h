#pragma once

#include "core/scene/PreviewArcDescriptor.h"

class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;

QSGNode* buildPreviewArcNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewArcDescriptors& arcs,
    QQuickWindow* window,
    PreviewTextureRepository* textures
);
