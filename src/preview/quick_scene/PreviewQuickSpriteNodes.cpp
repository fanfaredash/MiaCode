#include "preview/quick_scene/PreviewQuickSpriteNodes.h"

#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewAnimatedSpriteHelpers.h"

#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGNode>
#include <QSGTexture>
#include <QVector>
#include <QtMath>

#include <cstring>

namespace {

using miacode::preview::scene::PreviewAnimatedSpriteEffect;
using miacode::preview::scene::PreviewSpriteDescriptor;

struct PreviewQuickSpriteVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float opacity = 1.0f;
    float effect = 0.0f;
};

struct PreviewQuickSpriteBatchKey {
    QSGTexture* texture = nullptr;

    bool operator==(const PreviewQuickSpriteBatchKey& other) const
    {
        return texture == other.texture;
    }
};

struct PreviewQuickSpriteBatch {
    PreviewQuickSpriteBatchKey key;
    QVector<PreviewQuickSpriteVertex> vertices;
    int spriteCount = 0;
};

constexpr int kPreviewQuickSpriteUniformBufferSize = 80;

const QSGGeometry::AttributeSet& previewQuickSpriteVertexAttributes()
{
    static QSGGeometry::Attribute attributes[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::create(2, 1, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(3, 1, QSGGeometry::FloatType),
    };
    static QSGGeometry::AttributeSet attributeSet = {
        4,
        static_cast<int>(sizeof(PreviewQuickSpriteVertex)),
        attributes
    };
    return attributeSet;
}

QRectF normalizedSourceRect(
    QSGTexture* texture,
    const PreviewSpriteDescriptor& sprite
)
{
    if (texture == nullptr) {
        return QRectF();
    }
    if (sprite.sourceRect.isValid() && !sprite.sourceRect.isEmpty()) {
        return texture->convertToNormalizedSourceRect(sprite.sourceRect);
    }
    return texture->normalizedTextureSubRect();
}

QPointF rotateLocalPoint(const QPointF& point, qreal degrees)
{
    if (qFuzzyIsNull(degrees)) {
        return point;
    }

    const qreal radians = qDegreesToRadians(degrees);
    const qreal sinValue = qSin(radians);
    const qreal cosValue = qCos(radians);
    return QPointF(
        point.x() * cosValue - point.y() * sinValue,
        point.x() * sinValue + point.y() * cosValue
    );
}

void appendSpriteVertices(
    QVector<PreviewQuickSpriteVertex>* vertices,
    const PreviewSpriteDescriptor& sprite,
    const QRectF& uvRect
)
{
    if (vertices == nullptr) {
        return;
    }

    const qreal halfWidth = sprite.width / 2.0;
    const qreal halfHeight = sprite.height / 2.0;
    const QPointF localTopLeft(-halfWidth, -halfHeight);
    const QPointF localTopRight(halfWidth, -halfHeight);
    const QPointF localBottomLeft(-halfWidth, halfHeight);
    const QPointF localBottomRight(halfWidth, halfHeight);

    const QPointF topLeft = sprite.center + rotateLocalPoint(localTopLeft, sprite.rotationDegrees);
    const QPointF topRight = sprite.center + rotateLocalPoint(localTopRight, sprite.rotationDegrees);
    const QPointF bottomLeft = sprite.center + rotateLocalPoint(localBottomLeft, sprite.rotationDegrees);
    const QPointF bottomRight = sprite.center + rotateLocalPoint(localBottomRight, sprite.rotationDegrees);

    const float leftU = static_cast<float>(uvRect.left());
    const float rightU = static_cast<float>(uvRect.left() + uvRect.width());
    const float topV = static_cast<float>(uvRect.top());
    const float bottomV = static_cast<float>(uvRect.top() + uvRect.height());
    const float opacity = static_cast<float>(sprite.opacity);
    const float effect = static_cast<float>(sprite.effect);

    vertices->append({static_cast<float>(topLeft.x()), static_cast<float>(topLeft.y()), leftU, topV, opacity, effect});
    vertices->append({static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), rightU, topV, opacity, effect});
    vertices->append({static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), leftU, bottomV, opacity, effect});
    vertices->append({static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), rightU, topV, opacity, effect});
    vertices->append({static_cast<float>(bottomRight.x()), static_cast<float>(bottomRight.y()), rightU, bottomV, opacity, effect});
    vertices->append({static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), leftU, bottomV, opacity, effect});
}

class PreviewQuickSpriteMaterialShader final : public QSGMaterialShader
{
public:
    PreviewQuickSpriteMaterialShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/src/preview/quick_scene/shaders/PreviewSpriteMaterial.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/src/preview/quick_scene/shaders/PreviewSpriteMaterial.frag.qsb"));
    }

    bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial* oldMaterial) override;
    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture, QSGMaterial* newMaterial, QSGMaterial* oldMaterial) override;
};

class PreviewQuickSpriteMaterial final : public QSGMaterial
{
public:
    PreviewQuickSpriteMaterial()
    {
        setFlag(Blending, true);
        setFlag(RequiresFullMatrix, true);
        setFlag(NoBatching, true);
    }

    QSGMaterialType* type() const override
    {
        static QSGMaterialType materialType;
        return &materialType;
    }

    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override
    {
        return new PreviewQuickSpriteMaterialShader();
    }

    int compare(const QSGMaterial* other) const override
    {
        const auto* otherMaterial = static_cast<const PreviewQuickSpriteMaterial*>(other);
        if (texture_ == otherMaterial->texture_) {
            return 0;
        }
        return texture_ < otherMaterial->texture_ ? -1 : 1;
    }

    void setBatchState(QSGTexture* texture, qreal wave, qreal absWave)
    {
        texture_ = texture;
        wave_ = wave;
        absWave_ = absWave;
    }

    QSGTexture* texture() const
    {
        return texture_;
    }

    qreal wave() const
    {
        return wave_;
    }

    qreal absWave() const
    {
        return absWave_;
    }

private:
    QSGTexture* texture_ = nullptr;
    qreal wave_ = 0.0;
    qreal absWave_ = 0.0;
};

bool PreviewQuickSpriteMaterialShader::updateUniformData(
    RenderState& state,
    QSGMaterial* newMaterial,
    QSGMaterial* oldMaterial
)
{
    Q_UNUSED(oldMaterial);

    QByteArray* uniformData = state.uniformData();
    if (uniformData == nullptr || uniformData->size() < kPreviewQuickSpriteUniformBufferSize) {
        return false;
    }

    if (state.isMatrixDirty()) {
        const QMatrix4x4 matrix = state.combinedMatrix();
        std::memcpy(uniformData->data(), matrix.constData(), sizeof(float) * 16);
    }

    const auto* material = static_cast<const PreviewQuickSpriteMaterial*>(newMaterial);

    if (state.isOpacityDirty()) {
        const float opacity = state.opacity();
        std::memcpy(uniformData->data() + 64, &opacity, sizeof(float));
    }

    const float wave = static_cast<float>(material->wave());
    const float absWave = static_cast<float>(material->absWave());
    const float effect = 0.0f;
    std::memcpy(uniformData->data() + 68, &wave, sizeof(float));
    std::memcpy(uniformData->data() + 72, &absWave, sizeof(float));
    std::memcpy(uniformData->data() + 76, &effect, sizeof(float));
    return true;
}

void PreviewQuickSpriteMaterialShader::updateSampledImage(
    RenderState& state,
    int binding,
    QSGTexture** texture,
    QSGMaterial* newMaterial,
    QSGMaterial* oldMaterial
)
{
    Q_UNUSED(state);
    Q_UNUSED(oldMaterial);

    if (binding != 1 || texture == nullptr) {
        return;
    }

    const auto* material = static_cast<const PreviewQuickSpriteMaterial*>(newMaterial);
    *texture = material->texture();
    if (*texture != nullptr) {
        (*texture)->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    }
}

class PreviewQuickSpriteBatchNode final : public QSGGeometryNode
{
public:
    PreviewQuickSpriteBatchNode()
    {
        geometry_ = new QSGGeometry(previewQuickSpriteVertexAttributes(), 0);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);

        material_ = new PreviewQuickSpriteMaterial();
        setMaterial(material_);
        setFlag(QSGNode::OwnsMaterial, true);
    }

    void updateBatch(const PreviewQuickSpriteBatch& batch, qreal wave, qreal absWave)
    {
        geometry_->allocate(batch.vertices.size());
        if (!batch.vertices.isEmpty()) {
            std::memcpy(
                geometry_->vertexData(),
                batch.vertices.constData(),
                static_cast<size_t>(batch.vertices.size()) * sizeof(PreviewQuickSpriteVertex)
            );
        }
        geometry_->markVertexDataDirty();

        material_->setBatchState(batch.key.texture, wave, absWave);
        markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }

private:
    QSGGeometry* geometry_ = nullptr;
    PreviewQuickSpriteMaterial* material_ = nullptr;
};

}  // namespace

QSGNode* buildPreviewSpriteNodeTree(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewSpriteDescriptors& sprites,
    QQuickWindow* window,
    PreviewTextureRepository* textures,
    double playheadSeconds,
    const char* profileLayerName
)
{
    if (window == nullptr || textures == nullptr) {
        delete oldNode;
        return nullptr;
    }

    QVector<PreviewQuickSpriteBatch> batches;
    batches.reserve(sprites.size());

    int visibleSpriteCount = 0;
    for (const PreviewSpriteDescriptor& sprite : sprites) {
        if (sprite.image == nullptr
            || sprite.image->isNull()
            || sprite.opacity <= 0.0
            || sprite.width <= 0.0
            || sprite.height <= 0.0) {
            continue;
        }

        QSGTexture* texture = textures->textureForImage(*sprite.image, sprite.cacheable);
        if (texture == nullptr) {
            continue;
        }
        texture->setFiltering(QSGTexture::Linear);

        const PreviewQuickSpriteBatchKey key{texture};
        if (batches.isEmpty() || !(batches.last().key == key)) {
            batches.append(PreviewQuickSpriteBatch{key});
            batches.last().vertices.reserve(6);
        }

        PreviewQuickSpriteBatch& batch = batches.last();
        appendSpriteVertices(&batch.vertices, sprite, normalizedSourceRect(texture, sprite));
        batch.spriteCount += 1;
        visibleSpriteCount += 1;
    }

    textures->noteSpriteBatchStats(profileLayerName, visibleSpriteCount, batches.size());
    if (batches.isEmpty()) {
        delete oldNode;
        return nullptr;
    }

    const qreal wave = miacode::preview::scene::previewAnimatedSpriteWave(playheadSeconds);
    const qreal absWave = qAbs(wave);

    auto* root = oldNode != nullptr ? oldNode : new QSGNode();
    QVector<QSGNode*> existingChildren;
    for (QSGNode* child = root->firstChild(); child != nullptr; child = child->nextSibling()) {
        existingChildren.append(child);
    }

    int nodeIndex = 0;
    for (const PreviewQuickSpriteBatch& batch : batches) {
        QSGNode* existing = nodeIndex < existingChildren.size() ? existingChildren.at(nodeIndex) : nullptr;
        auto* batchNode = dynamic_cast<PreviewQuickSpriteBatchNode*>(existing);
        if (batchNode == nullptr) {
            auto* newNode = new PreviewQuickSpriteBatchNode();
            if (existing != nullptr) {
                root->insertChildNodeBefore(newNode, existing);
                root->removeChildNode(existing);
                delete existing;
                existingChildren[nodeIndex] = newNode;
            } else {
                root->appendChildNode(newNode);
                existingChildren.append(newNode);
            }
            batchNode = newNode;
        }

        batchNode->updateBatch(batch, wave, absWave);
        ++nodeIndex;
    }

    for (int index = existingChildren.size() - 1; index >= nodeIndex; --index) {
        QSGNode* child = existingChildren.at(index);
        root->removeChildNode(child);
        delete child;
    }
    return root;
}
