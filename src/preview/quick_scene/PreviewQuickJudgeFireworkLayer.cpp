#include "preview/quick_scene/PreviewQuickJudgeFireworkLayer.h"

#include "preview/quick_scene/PreviewTextureRepository.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "core/scene/PreviewSceneGeometry.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGNode>
#include <QSGTexture>
#include <QtMath>

#include <cstring>

namespace {

constexpr int kFireworkMaterialUniformBufferSize = 144;
constexpr qreal kFireworkQuadRadiusMin = 1.0;
constexpr int kFireworkClipSegments = 64;
constexpr int kFireworkRenderFlagDrawStripe = 0x1;
constexpr int kFireworkRenderFlagDrawBigBall = 0x2;
constexpr int kFireworkRenderFlagDrawSmallBall = 0x4;
constexpr int kFireworkRenderFlagUseTexture = 0x8;
constexpr int kFireworkRenderFlagDrawFallbackBall = 0x10;

class PreviewQuickJudgeFireworkMaterial;

struct PreviewQuickJudgeFireworkVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

const QSGGeometry::AttributeSet& previewQuickJudgeFireworkVertexAttributes()
{
    static QSGGeometry::Attribute attributes[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
    };
    static QSGGeometry::AttributeSet attributeSet = {
        2,
        static_cast<int>(sizeof(PreviewQuickJudgeFireworkVertex)),
        attributes
    };
    return attributeSet;
}

QImage& fallbackFireworkTextureImage()
{
    static QImage image = []() {
        QImage img(1, 1, QImage::Format_RGBA8888_Premultiplied);
        img.fill(Qt::white);
        return img;
    }();
    return image;
}

class PreviewQuickEllipseClipNode final : public QSGClipNode
{
public:
    PreviewQuickEllipseClipNode()
    {
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kFireworkClipSegments * 3);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);
        setIsRectangular(false);
    }

    void updateClip(const QPointF& center, qreal radius)
    {
        auto* vertices = geometry_->vertexDataAsPoint2D();
        for (int segmentIndex = 0; segmentIndex < kFireworkClipSegments; ++segmentIndex) {
            const qreal t0 = static_cast<qreal>(segmentIndex) / static_cast<qreal>(kFireworkClipSegments);
            const qreal t1 = static_cast<qreal>(segmentIndex + 1) / static_cast<qreal>(kFireworkClipSegments);
            const qreal angle0 = -t0 * 360.0;
            const qreal angle1 = -t1 * 360.0;
            const QPointF p0(
                center.x() + radius * qCos(qDegreesToRadians(angle0)),
                center.y() + radius * qSin(qDegreesToRadians(angle0))
            );
            const QPointF p1(
                center.x() + radius * qCos(qDegreesToRadians(angle1)),
                center.y() + radius * qSin(qDegreesToRadians(angle1))
            );
            const int vertexIndex = segmentIndex * 3;
            vertices[vertexIndex + 0].set(static_cast<float>(center.x()), static_cast<float>(center.y()));
            vertices[vertexIndex + 1].set(static_cast<float>(p0.x()), static_cast<float>(p0.y()));
            vertices[vertexIndex + 2].set(static_cast<float>(p1.x()), static_cast<float>(p1.y()));
        }
        geometry_->markVertexDataDirty();
        setClipRect(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0));
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry* geometry_ = nullptr;
};

class PreviewQuickJudgeFireworkMaterialShader final : public QSGMaterialShader
{
public:
    PreviewQuickJudgeFireworkMaterialShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/src/preview/quick_scene/shaders/PreviewFireworkMaterial.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/src/preview/quick_scene/shaders/PreviewFireworkMaterial.frag.qsb"));
        setFlag(UpdatesGraphicsPipelineState, true);
    }

    bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial* oldMaterial) override;
    void updateSampledImage(
        RenderState& state,
        int binding,
        QSGTexture** texture,
        QSGMaterial* newMaterial,
        QSGMaterial* oldMaterial
    ) override;
    bool updateGraphicsPipelineState(
        RenderState& state,
        GraphicsPipelineState* ps,
        QSGMaterial* newMaterial,
        QSGMaterial* oldMaterial
    ) override;
};

class PreviewQuickJudgeFireworkMaterial final : public QSGMaterial
{
public:
    PreviewQuickJudgeFireworkMaterial()
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
        return new PreviewQuickJudgeFireworkMaterialShader();
    }

    int compare(const QSGMaterial* other) const override
    {
        const auto* otherMaterial = static_cast<const PreviewQuickJudgeFireworkMaterial*>(other);
        if (texture_ == otherMaterial->texture_
            && qFuzzyCompare(quadRadius_ + 1.0f, otherMaterial->quadRadius_ + 1.0f)
            && qFuzzyCompare(clipRadius_ + 1.0f, otherMaterial->clipRadius_ + 1.0f)
            && qFuzzyCompare(outerRadius_ + 1.0f, otherMaterial->outerRadius_ + 1.0f)
            && qFuzzyCompare(fireworkAlpha_ + 1.0f, otherMaterial->fireworkAlpha_ + 1.0f)
            && qFuzzyCompare(fireworkRotationDegrees_ + 1.0f, otherMaterial->fireworkRotationDegrees_ + 1.0f)
            && qFuzzyCompare(holeRadius_ + 1.0f, otherMaterial->holeRadius_ + 1.0f)
            && qFuzzyCompare(holeMaskRadius_ + 1.0f, otherMaterial->holeMaskRadius_ + 1.0f)
            && qFuzzyCompare(colorBallRadius_ + 1.0f, otherMaterial->colorBallRadius_ + 1.0f)
            && qFuzzyCompare(colorBallBigRadius_ + 1.0f, otherMaterial->colorBallBigRadius_ + 1.0f)
            && qFuzzyCompare(colorBallAlpha_ + 1.0f, otherMaterial->colorBallAlpha_ + 1.0f)
            && qFuzzyCompare(colorBallBigAlpha_ + 1.0f, otherMaterial->colorBallBigAlpha_ + 1.0f)
            && qFuzzyCompare(fallbackColorBallRadius_ + 1.0f, otherMaterial->fallbackColorBallRadius_ + 1.0f)
            && qFuzzyCompare(fallbackColorBallAlpha_ + 1.0f, otherMaterial->fallbackColorBallAlpha_ + 1.0f)
            && qFuzzyCompare(renderFlags_ + 1.0f, otherMaterial->renderFlags_ + 1.0f)
            && qFuzzyCompare(sourceAspect_ + 1.0f, otherMaterial->sourceAspect_ + 1.0f)
            && sourceRectNormalized_ == otherMaterial->sourceRectNormalized_
            && useTexture_ == otherMaterial->useTexture_) {
            return 0;
        }
        return 1;
    }

    void setState(
        QSGTexture* texture,
        bool useTexture,
        float quadRadius,
        float clipRadius,
        float outerRadius,
        float fireworkAlpha,
        float fireworkRotationDegrees,
        float holeRadius,
        float holeMaskRadius,
        float colorBallRadius,
        float colorBallBigRadius,
        float colorBallAlpha,
        float colorBallBigAlpha,
        float fallbackColorBallRadius,
        float fallbackColorBallAlpha,
        float renderFlags,
        float sourceAspect,
        const QRectF& sourceRectNormalized
    )
    {
        texture_ = texture;
        useTexture_ = useTexture;
        quadRadius_ = quadRadius;
        clipRadius_ = clipRadius;
        outerRadius_ = outerRadius;
        fireworkAlpha_ = fireworkAlpha;
        fireworkRotationDegrees_ = fireworkRotationDegrees;
        holeRadius_ = holeRadius;
        holeMaskRadius_ = holeMaskRadius;
        colorBallRadius_ = colorBallRadius;
        colorBallBigRadius_ = colorBallBigRadius;
        colorBallAlpha_ = colorBallAlpha;
        colorBallBigAlpha_ = colorBallBigAlpha;
        fallbackColorBallRadius_ = fallbackColorBallRadius;
        fallbackColorBallAlpha_ = fallbackColorBallAlpha;
        renderFlags_ = renderFlags;
        sourceAspect_ = sourceAspect;
        sourceRectNormalized_ = sourceRectNormalized;
    }

    QSGTexture* texture() const { return texture_; }
    bool useTexture() const { return useTexture_; }
    float quadRadius() const { return quadRadius_; }
    float clipRadius() const { return clipRadius_; }
    float outerRadius() const { return outerRadius_; }
    float fireworkAlpha() const { return fireworkAlpha_; }
    float fireworkRotationDegrees() const { return fireworkRotationDegrees_; }
    float holeRadius() const { return holeRadius_; }
    float holeMaskRadius() const { return holeMaskRadius_; }
    float colorBallRadius() const { return colorBallRadius_; }
    float colorBallBigRadius() const { return colorBallBigRadius_; }
    float colorBallAlpha() const { return colorBallAlpha_; }
    float colorBallBigAlpha() const { return colorBallBigAlpha_; }
    float fallbackColorBallRadius() const { return fallbackColorBallRadius_; }
    float fallbackColorBallAlpha() const { return fallbackColorBallAlpha_; }
    float renderFlags() const { return renderFlags_; }
    float sourceAspect() const { return sourceAspect_; }
    QRectF sourceRectNormalized() const { return sourceRectNormalized_; }

private:
    QSGTexture* texture_ = nullptr;
    bool useTexture_ = false;
    float quadRadius_ = 0.0f;
    float clipRadius_ = 0.0f;
    float outerRadius_ = 0.0f;
    float fireworkAlpha_ = 0.0f;
    float fireworkRotationDegrees_ = 0.0f;
    float holeRadius_ = 0.0f;
    float holeMaskRadius_ = 0.0f;
    float colorBallRadius_ = 0.0f;
    float colorBallBigRadius_ = 0.0f;
    float colorBallAlpha_ = 0.0f;
    float colorBallBigAlpha_ = 0.0f;
    float fallbackColorBallRadius_ = 0.0f;
    float fallbackColorBallAlpha_ = 0.0f;
    float renderFlags_ = 0.0f;
    float sourceAspect_ = 1.0f;
    QRectF sourceRectNormalized_;
};

bool PreviewQuickJudgeFireworkMaterialShader::updateUniformData(
    RenderState& state,
    QSGMaterial* newMaterial,
    QSGMaterial* oldMaterial
)
{
    Q_UNUSED(oldMaterial);

    QByteArray* uniformData = state.uniformData();
    if (uniformData == nullptr || uniformData->size() < kFireworkMaterialUniformBufferSize) {
        return false;
    }

    if (state.isMatrixDirty()) {
        const QMatrix4x4 matrix = state.combinedMatrix();
        std::memcpy(uniformData->data(), matrix.constData(), sizeof(float) * 16);
    }

    const auto* material = static_cast<const PreviewQuickJudgeFireworkMaterial*>(newMaterial);
    const float opacity = state.opacity();
    const float geometryScalars[3] = {
        material->quadRadius(),
        material->clipRadius(),
        material->outerRadius()
    };
    const float fireworkAndHole[4] = {
        material->fireworkAlpha(),
        material->fireworkRotationDegrees(),
        material->holeRadius(),
        material->holeMaskRadius()
    };
    const float colorBallSmall[4] = {
        material->colorBallRadius(),
        material->colorBallAlpha(),
        material->sourceAspect(),
        material->fallbackColorBallRadius()
    };
    const float colorBallBig[4] = {
        material->colorBallBigRadius(),
        material->colorBallBigAlpha(),
        material->fallbackColorBallAlpha(),
        material->renderFlags()
    };
    const float sourceRect[4] = {
        static_cast<float>(material->sourceRectNormalized().x()),
        static_cast<float>(material->sourceRectNormalized().y()),
        static_cast<float>(material->sourceRectNormalized().width()),
        static_cast<float>(material->sourceRectNormalized().height())
    };

    std::memcpy(uniformData->data() + 64, &opacity, sizeof(float));
    std::memcpy(uniformData->data() + 68, geometryScalars, sizeof(geometryScalars));
    std::memcpy(uniformData->data() + 80, fireworkAndHole, sizeof(fireworkAndHole));
    std::memcpy(uniformData->data() + 96, colorBallSmall, sizeof(colorBallSmall));
    std::memcpy(uniformData->data() + 112, colorBallBig, sizeof(colorBallBig));
    std::memcpy(uniformData->data() + 128, sourceRect, sizeof(sourceRect));
    return true;
}

void PreviewQuickJudgeFireworkMaterialShader::updateSampledImage(
    RenderState& state,
    int binding,
    QSGTexture** texture,
    QSGMaterial* newMaterial,
    QSGMaterial* oldMaterial
)
{
    Q_UNUSED(oldMaterial);

    if (binding != 1 || texture == nullptr) {
        return;
    }

    const auto* material = static_cast<const PreviewQuickJudgeFireworkMaterial*>(newMaterial);
    *texture = material->texture();
    if (*texture != nullptr) {
        (*texture)->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    }
}

bool PreviewQuickJudgeFireworkMaterialShader::updateGraphicsPipelineState(
    RenderState& state,
    GraphicsPipelineState* ps,
    QSGMaterial* newMaterial,
    QSGMaterial* oldMaterial
)
{
    Q_UNUSED(state);
    Q_UNUSED(newMaterial);
    Q_UNUSED(oldMaterial);

    if (ps == nullptr) {
        return false;
    }

    ps->blendEnable = true;
    ps->srcColor = GraphicsPipelineState::One;
    ps->dstColor = GraphicsPipelineState::One;
    ps->separateBlendFactors = true;
    ps->srcAlpha = GraphicsPipelineState::One;
    ps->dstAlpha = GraphicsPipelineState::One;
    ps->opColor = GraphicsPipelineState::BlendOp::Add;
    ps->opAlpha = GraphicsPipelineState::BlendOp::Add;
    return true;
}

class PreviewQuickJudgeFireworkNode final : public QSGGeometryNode
{
public:
    PreviewQuickJudgeFireworkNode()
    {
        geometry_ = new QSGGeometry(previewQuickJudgeFireworkVertexAttributes(), 4);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);

        material_ = new PreviewQuickJudgeFireworkMaterial();
        setMaterial(material_);
        setFlag(QSGNode::OwnsMaterial, true);
    }

    void updateNode(
        const miacode::preview::scene::PreviewJudgeFireworkLayerState& layerState,
        QSGTexture* texture,
        bool useTexture,
        const QRectF& normalizedSourceRect,
        float sourceAspect
    )
    {
        const qreal resolvedQuadRadius = qMax<qreal>(
            kFireworkQuadRadiusMin,
            qMax(
                layerState.outerRadius,
                qMax(
                    layerState.holeMaskRadius,
                    qMax(
                        qMax(layerState.colorBallRadius, layerState.colorBallBigRadius),
                        layerState.fallbackColorBallRadius
                    )
                )
            )
        );
        int renderFlags = 0;
        if (layerState.drawFirework) {
            renderFlags |= kFireworkRenderFlagDrawStripe;
        }
        if (layerState.drawColorBallBig) {
            renderFlags |= kFireworkRenderFlagDrawBigBall;
        }
        if (layerState.drawColorBall) {
            renderFlags |= kFireworkRenderFlagDrawSmallBall;
        }
        if (useTexture) {
            renderFlags |= kFireworkRenderFlagUseTexture;
        }
        if (layerState.drawFallbackColorBall) {
            renderFlags |= kFireworkRenderFlagDrawFallbackBall;
        }
        const float quadRadius = static_cast<float>(resolvedQuadRadius);
        auto* vertices = reinterpret_cast<PreviewQuickJudgeFireworkVertex*>(geometry_->vertexData());
        vertices[0] = {static_cast<float>(layerState.center.x() - quadRadius), static_cast<float>(layerState.center.y() - quadRadius), 0.0f, 0.0f};
        vertices[1] = {static_cast<float>(layerState.center.x() + quadRadius), static_cast<float>(layerState.center.y() - quadRadius), 1.0f, 0.0f};
        vertices[2] = {static_cast<float>(layerState.center.x() - quadRadius), static_cast<float>(layerState.center.y() + quadRadius), 0.0f, 1.0f};
        vertices[3] = {static_cast<float>(layerState.center.x() + quadRadius), static_cast<float>(layerState.center.y() + quadRadius), 1.0f, 1.0f};
        geometry_->markVertexDataDirty();

        material_->setState(
            texture,
            useTexture,
            quadRadius,
            static_cast<float>(layerState.clipRadius),
            static_cast<float>(layerState.outerRadius),
            static_cast<float>(layerState.fireworkAlpha),
            static_cast<float>(layerState.fireworkRotationDegrees),
            static_cast<float>(layerState.holeRadius),
            static_cast<float>(layerState.holeMaskRadius),
            static_cast<float>(layerState.colorBallRadius),
            static_cast<float>(layerState.colorBallBigRadius),
            static_cast<float>(layerState.colorBallAlpha),
            static_cast<float>(layerState.colorBallBigAlpha),
            static_cast<float>(layerState.fallbackColorBallRadius),
            static_cast<float>(layerState.fallbackColorBallAlpha),
            static_cast<float>(renderFlags),
            sourceAspect,
            normalizedSourceRect
        );
        markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }

private:
    QSGGeometry* geometry_ = nullptr;
    PreviewQuickJudgeFireworkMaterial* material_ = nullptr;
};

class PreviewQuickJudgeFireworkRootNode final : public QSGNode
{
public:
    PreviewQuickJudgeFireworkRootNode()
    {
        contentSlot_ = new QSGNode();
        appendChildNode(contentSlot_);
    }

    PreviewQuickEllipseClipNode* ensureClipNode()
    {
        if (clipNode_ == nullptr) {
            clipNode_ = new PreviewQuickEllipseClipNode();
        }
        return clipNode_;
    }

    PreviewQuickJudgeFireworkNode* ensureFireworkNode()
    {
        if (fireworkNode_ == nullptr) {
            fireworkNode_ = new PreviewQuickJudgeFireworkNode();
        }
        return fireworkNode_;
    }

    void useClip(bool enabled)
    {
        auto* fireworkNode = ensureFireworkNode();
        if (enabled) {
            auto* clipNode = ensureClipNode();
            if (clipNode->parent() != contentSlot_) {
                if (clipNode->parent() != nullptr) {
                    clipNode->parent()->removeChildNode(clipNode);
                }
                contentSlot_->appendChildNode(clipNode);
            }
            if (fireworkNode->parent() != clipNode) {
                if (fireworkNode->parent() != nullptr) {
                    fireworkNode->parent()->removeChildNode(fireworkNode);
                }
                clipNode->appendChildNode(fireworkNode);
            }
            pruneChildren(contentSlot_, clipNode);
            pruneChildren(clipNode, fireworkNode);
            return;
        }

        if (fireworkNode->parent() != contentSlot_) {
            if (fireworkNode->parent() != nullptr) {
                fireworkNode->parent()->removeChildNode(fireworkNode);
            }
            contentSlot_->appendChildNode(fireworkNode);
        }
        if (clipNode_ != nullptr) {
            if (clipNode_->parent() != nullptr) {
                clipNode_->parent()->removeChildNode(clipNode_);
            }
            delete clipNode_;
            clipNode_ = nullptr;
        }
        pruneChildren(contentSlot_, fireworkNode);
    }

    PreviewQuickEllipseClipNode* clipNode() const
    {
        return clipNode_;
    }

    PreviewQuickJudgeFireworkNode* fireworkNode() const
    {
        return fireworkNode_;
    }

private:
    static void pruneChildren(QSGNode* parent, QSGNode* keepChild)
    {
        if (parent == nullptr) {
            return;
        }

        QSGNode* child = parent->firstChild();
        while (child != nullptr) {
            QSGNode* next = child->nextSibling();
            if (child != keepChild) {
                parent->removeChildNode(child);
                delete child;
            }
            child = next;
        }
    }

    QSGNode* contentSlot_ = nullptr;
    PreviewQuickEllipseClipNode* clipNode_ = nullptr;
    PreviewQuickJudgeFireworkNode* fireworkNode_ = nullptr;
};

PreviewQuickJudgeFireworkRootNode* ensureJudgeFireworkRoot(QSGNode* oldNode)
{
    if (auto* root = dynamic_cast<PreviewQuickJudgeFireworkRootNode*>(oldNode)) {
        return root;
    }
    return new PreviewQuickJudgeFireworkRootNode();
}

QRectF normalizedSourceRectFor(
    const QImage* sourceImage,
    const QRectF& sourceRect
)
{
    if (sourceImage == nullptr || sourceImage->isNull()) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }
    if (!sourceRect.isValid() || sourceRect.isEmpty()) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const qreal width = qMax<qreal>(1.0, sourceImage->width());
    const qreal height = qMax<qreal>(1.0, sourceImage->height());
    return QRectF(
        sourceRect.x() / width,
        sourceRect.y() / height,
        sourceRect.width() / width,
        sourceRect.height() / height
    );
}

qreal sourceAspectFor(const QImage* sourceImage, const QRectF& sourceRect)
{
    if (sourceImage == nullptr || sourceImage->isNull()) {
        return 1.0;
    }
    const qreal sourceWidth = sourceRect.isValid() && !sourceRect.isEmpty()
        ? sourceRect.width()
        : static_cast<qreal>(sourceImage->width());
    const qreal sourceHeight = sourceRect.isValid() && !sourceRect.isEmpty()
        ? sourceRect.height()
        : static_cast<qreal>(sourceImage->height());
    if (sourceWidth <= 0.0 || sourceHeight <= 0.0) {
        return 1.0;
    }
    return qMax<qreal>(0.01, sourceHeight / sourceWidth);
}

}  // namespace

QSGNode* PreviewQuickJudgeFireworkLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    if (window == nullptr || textures == nullptr) {
        return nullptr;
    }

    const miacode::preview::scene::PreviewActiveMarkerView activeMarkers =
        preparedCache != nullptr && cursor != nullptr
        ? miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers, preparedCache->judgeFireworkLayer(), *cursor)
        : miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers);

    const miacode::preview::scene::PreviewJudgeFireworkLayerState layerState =
        miacode::preview::scene::buildPreviewJudgeFireworkLayerState(
            state,
            activeMarkers,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        );
    if (!layerState.active) {
        return nullptr;
    }

    const QImage* sourceImage = layerState.colorBallImage;
    const bool useTexture = sourceImage != nullptr && !sourceImage->isNull();
    QSGTexture* texture = textures->textureForImage(
        useTexture ? *sourceImage : fallbackFireworkTextureImage(),
        true
    );
    if (texture == nullptr) {
        return nullptr;
    }
    texture->setFiltering(QSGTexture::Linear);

    auto* root = ensureJudgeFireworkRoot(oldNode);
    root->useClip(layerState.clipRadius > 0.0);
    if (root->clipNode() != nullptr) {
        root->clipNode()->updateClip(layerState.clipCenter, layerState.clipRadius);
    }

    root->ensureFireworkNode()->updateNode(
        layerState,
        texture,
        useTexture,
        normalizedSourceRectFor(sourceImage, layerState.colorBallSourceRect),
        static_cast<float>(sourceAspectFor(sourceImage, layerState.colorBallSourceRect))
    );
    return root;
}
