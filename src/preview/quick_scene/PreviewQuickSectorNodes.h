#pragma once

#include "preview/scene/PreviewSectorDescriptor.h"

class QSGNode;

QSGNode* buildPreviewSectorNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewSectorDescriptors& sectors
);
