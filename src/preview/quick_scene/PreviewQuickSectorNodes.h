#pragma once

#include "core/scene/PreviewSectorDescriptor.h"

class QSGNode;

QSGNode* buildPreviewSectorNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewSectorDescriptors& sectors
);
