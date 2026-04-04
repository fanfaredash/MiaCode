#include "preview/quick_scene/PreviewQuickBackdropLayer.h"

#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewSceneGeometry.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleTextureNode>

QSGNode* PreviewQuickBackdropLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    if (state.assets.outlineImage.isNull() || window == nullptr || textures == nullptr) {
        delete oldNode;
        return nullptr;
    }

    delete oldNode;
    auto* node = new QSGSimpleTextureNode();
    node->setOwnsTexture(false);
    node->setTexture(textures->textureForImage(state.assets.outlineImage));
    node->setRect(
        miacode::preview::scene::outlineRectForPlayfield(
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        )
    );
    node->setFiltering(QSGTexture::Linear);
    return node;
}
