#include "preview/quick_scene/PreviewQuickSpriteNodes.h"

#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>

namespace {

QMatrix4x4 spriteTransform(const miacode::preview::scene::PreviewSpriteDescriptor& sprite)
{
    QMatrix4x4 matrix;
    matrix.translate(sprite.center.x(), sprite.center.y());
    matrix.rotate(sprite.rotationDegrees, 0.0f, 0.0f, 1.0f);
    return matrix;
}

}  // namespace

QSGNode* buildPreviewSpriteNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewSpriteDescriptors& sprites,
    QQuickWindow* window,
    PreviewTextureRepository* textures
)
{
    delete oldNode;
    if (window == nullptr || textures == nullptr || sprites.isEmpty()) {
        return nullptr;
    }

    auto* root = new QSGNode();
    for (const miacode::preview::scene::PreviewSpriteDescriptor& sprite : sprites) {
        if (sprite.image == nullptr || sprite.image->isNull() || sprite.opacity <= 0.0 || sprite.width <= 0.0 || sprite.height <= 0.0) {
            continue;
        }

        auto* opacityNode = new QSGOpacityNode();
        opacityNode->setOpacity(sprite.opacity);

        auto* transformNode = new QSGTransformNode();
        transformNode->setMatrix(spriteTransform(sprite));

        auto* textureNode = new QSGSimpleTextureNode();
        textureNode->setOwnsTexture(false);
        textureNode->setTexture(textures->textureForImage(*sprite.image, sprite.cacheable));
        textureNode->setFiltering(QSGTexture::Linear);
        textureNode->setRect(QRectF(-sprite.width / 2.0, -sprite.height / 2.0, sprite.width, sprite.height));
        if (sprite.sourceRect.isValid() && !sprite.sourceRect.isEmpty() && sprite.image->width() > 0 && sprite.image->height() > 0) {
            textureNode->setSourceRect(QRectF(
                sprite.sourceRect.x() / static_cast<qreal>(sprite.image->width()),
                sprite.sourceRect.y() / static_cast<qreal>(sprite.image->height()),
                sprite.sourceRect.width() / static_cast<qreal>(sprite.image->width()),
                sprite.sourceRect.height() / static_cast<qreal>(sprite.image->height())
            ));
        }

        transformNode->appendChildNode(textureNode);
        opacityNode->appendChildNode(transformNode);
        root->appendChildNode(opacityNode);
    }
    return root;
}
