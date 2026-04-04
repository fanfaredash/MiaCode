#include "preview/quick_scene/PreviewQuickSpriteNodes.h"

#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QVector>

namespace {

class PreviewQuickSpriteNode final : public QSGOpacityNode
{
public:
    PreviewQuickSpriteNode()
    {
        transformNode_ = new QSGTransformNode();
        textureNode_ = new QSGSimpleTextureNode();
        textureNode_->setOwnsTexture(false);
        transformNode_->appendChildNode(textureNode_);
        appendChildNode(transformNode_);
    }

    QSGTransformNode* transformNode() const
    {
        return transformNode_;
    }

    QSGSimpleTextureNode* textureNode() const
    {
        return textureNode_;
    }

private:
    QSGTransformNode* transformNode_ = nullptr;
    QSGSimpleTextureNode* textureNode_ = nullptr;
};

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
    if (window == nullptr || textures == nullptr) {
        return nullptr;
    }

    QVector<const miacode::preview::scene::PreviewSpriteDescriptor*> visibleSprites;
    visibleSprites.reserve(sprites.size());
    for (const miacode::preview::scene::PreviewSpriteDescriptor& sprite : sprites) {
        if (sprite.image == nullptr
            || sprite.image->isNull()
            || sprite.opacity <= 0.0
            || sprite.width <= 0.0
            || sprite.height <= 0.0) {
            continue;
        }
        visibleSprites.append(&sprite);
    }
    if (visibleSprites.isEmpty()) {
        return nullptr;
    }

    auto* root = oldNode != nullptr ? oldNode : new QSGNode();
    QVector<QSGNode*> existingChildren;
    for (QSGNode* child = root->firstChild(); child != nullptr; child = child->nextSibling()) {
        existingChildren.append(child);
    }

    int nodeIndex = 0;
    for (const miacode::preview::scene::PreviewSpriteDescriptor* sprite : visibleSprites) {
        QSGNode* existing = nodeIndex < existingChildren.size() ? existingChildren.at(nodeIndex) : nullptr;
        auto* spriteNode = dynamic_cast<PreviewQuickSpriteNode*>(existing);
        if (spriteNode == nullptr) {
            auto* newNode = new PreviewQuickSpriteNode();
            if (existing != nullptr) {
                root->insertChildNodeBefore(newNode, existing);
                root->removeChildNode(existing);
                delete existing;
                existingChildren[nodeIndex] = newNode;
            } else {
                root->appendChildNode(newNode);
                existingChildren.append(newNode);
            }
            spriteNode = newNode;
        }

        spriteNode->setOpacity(sprite->opacity);
        spriteNode->transformNode()->setMatrix(spriteTransform(*sprite));

        QSGTexture* texture = textures->textureForImage(*sprite->image, sprite->cacheable);
        spriteNode->textureNode()->setTexture(texture);
        spriteNode->textureNode()->setFiltering(QSGTexture::Linear);
        spriteNode->textureNode()->setRect(
            QRectF(-sprite->width / 2.0, -sprite->height / 2.0, sprite->width, sprite->height)
        );
        if (texture != nullptr && sprite->sourceRect.isValid() && !sprite->sourceRect.isEmpty()) {
            spriteNode->textureNode()->setSourceRect(sprite->sourceRect);
        } else {
            spriteNode->textureNode()->setSourceRect(QRectF());
        }

        ++nodeIndex;
    }

    for (int index = existingChildren.size() - 1; index >= nodeIndex; --index) {
        QSGNode* child = existingChildren.at(index);
        root->removeChildNode(child);
        delete child;
    }
    return root;
}
