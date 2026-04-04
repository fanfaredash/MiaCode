#pragma once

#include "preview/scene/PreviewCircleDescriptor.h"

class QSGNode;

QSGNode* buildPreviewCircleNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewCircleDescriptors& circles
);
