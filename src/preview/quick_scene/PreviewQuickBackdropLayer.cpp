#include "preview/quick_scene/PreviewQuickBackdropLayer.h"

#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewSceneGeometry.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

QSGNode* PreviewQuickBackdropLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    if (state.assets.outlineImage.isNull() || window == nullptr || textures == nullptr) {
        return nullptr;
    }

    QSGTexture* texture = textures->textureForImage(state.assets.outlineImage);
    if (texture == nullptr) {
        return nullptr;
    }
    texture->setFiltering(QSGTexture::Linear);

    auto* node = dynamic_cast<QSGSimpleTextureNode*>(oldNode);
    if (node == nullptr) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(false);
    }

    node->setTexture(texture);
    node->setRect(
        miacode::preview::scene::playfieldRectForStage(
            miacode::preview::scene::stageRectForSize(renderSize),
            state.render.layoutSquareScale
        )
    );
    node->setFiltering(QSGTexture::Linear);
    return node;
}
