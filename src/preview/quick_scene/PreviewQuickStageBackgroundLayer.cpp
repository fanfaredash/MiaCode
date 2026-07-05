#include "preview/quick_scene/PreviewQuickStageBackgroundLayer.h"

#include "common/PreviewVideoGeometryConfig.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "core/scene/PreviewSceneGeometry.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGNode>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtMath>

#include <cstring>

namespace {

enum class StageMediaSourceKind {
    None,
    ResolvedFrame,
    StaticImage,
    RetainedFallback,
};

struct StageMediaFrameResult {
    QImage image;
    StageMediaSourceKind sourceKind = StageMediaSourceKind::None;
    bool cacheable = false;
    double toImageMs = 0.0;
    quint64 retainedTextureKey = 0;
};

struct StageDimParams {
    QRectF stageRect;
    QSize renderSize;
    float outerDarkAlpha = 0.0f;
    float innerDarkAlpha = 0.0f;
    float layoutSquareScale = 1.0f;
    float ringRatio = 1.0f;
    float smoothBrightness = 0.0f;
};

constexpr int kStageDimUniformBufferSize = 112;
constexpr int kInnerCircleClipSegments = 96;

class StageBackgroundDimMaterial;

class StageBackgroundDimMaterialShader final : public QSGMaterialShader
{
public:
    StageBackgroundDimMaterialShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/src/preview/quick_scene/shaders/PreviewStageDimMaterial.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/src/preview/quick_scene/shaders/PreviewStageDimMaterial.frag.qsb"));
    }

    bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial* oldMaterial) override;
};

class StageBackgroundDimMaterial final : public QSGMaterial
{
public:
    StageBackgroundDimMaterial()
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
        return new StageBackgroundDimMaterialShader();
    }

    int compare(const QSGMaterial* other) const override
    {
        return this == other ? 0 : 1;
    }

    void setParams(const StageDimParams& params)
    {
        params_ = params;
    }

    StageDimParams params() const
    {
        return params_;
    }

private:
    StageDimParams params_;
};

bool StageBackgroundDimMaterialShader::updateUniformData(
    RenderState& state,
    QSGMaterial* newMaterial,
    QSGMaterial* oldMaterial
)
{
    Q_UNUSED(oldMaterial);

    QByteArray* uniformData = state.uniformData();
    if (uniformData == nullptr || uniformData->size() < kStageDimUniformBufferSize) {
        return false;
    }

    if (state.isMatrixDirty()) {
        const QMatrix4x4 matrix = state.combinedMatrix();
        std::memcpy(uniformData->data(), matrix.constData(), sizeof(float) * 16);
    }

    const auto* material = static_cast<const StageBackgroundDimMaterial*>(newMaterial);
    const StageDimParams params = material->params();
    const float opacity = state.opacity();
    const float stageRectValues[4] = {
        static_cast<float>(params.stageRect.x()),
        static_cast<float>(params.stageRect.y()),
        static_cast<float>(params.stageRect.width()),
        static_cast<float>(params.stageRect.height()),
    };
    const float geometryValues[4] = {
        static_cast<float>(params.renderSize.width()),
        static_cast<float>(params.renderSize.height()),
        params.layoutSquareScale,
        params.ringRatio,
    };

    std::memcpy(uniformData->data() + 64, &opacity, sizeof(float));
    std::memcpy(uniformData->data() + 68, &params.outerDarkAlpha, sizeof(float));
    std::memcpy(uniformData->data() + 72, &params.innerDarkAlpha, sizeof(float));
    std::memcpy(uniformData->data() + 76, &params.smoothBrightness, sizeof(float));
    std::memcpy(uniformData->data() + 80, stageRectValues, sizeof(stageRectValues));
    std::memcpy(uniformData->data() + 96, geometryValues, sizeof(geometryValues));
    return true;
}

class StageBackgroundDimNode final : public QSGGeometryNode
{
public:
    StageBackgroundDimNode()
    {
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);

        material_ = new StageBackgroundDimMaterial();
        setMaterial(material_);
        setFlag(QSGNode::OwnsMaterial, true);
    }

    void updateNode(const QSize& renderSize, const StageDimParams& params)
    {
        const float width = static_cast<float>(qMax(1, renderSize.width()));
        const float height = static_cast<float>(qMax(1, renderSize.height()));
        auto* vertices = geometry_->vertexDataAsPoint2D();
        vertices[0].set(0.0f, 0.0f);
        vertices[1].set(width, 0.0f);
        vertices[2].set(0.0f, height);
        vertices[3].set(width, height);
        geometry_->markVertexDataDirty();

        material_->setParams(params);
        markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }

private:
    QSGGeometry* geometry_ = nullptr;
    StageBackgroundDimMaterial* material_ = nullptr;
};

class StageBackgroundCircleMediaNode final : public QSGClipNode
{
public:
    StageBackgroundCircleMediaNode()
    {
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), kInnerCircleClipSegments * 3);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(geometry_);
        setFlag(QSGNode::OwnsGeometry, true);
        setIsRectangular(false);

        mediaNode_ = new QSGSimpleTextureNode();
        mediaNode_->setOwnsTexture(false);
        appendChildNode(mediaNode_);
    }

    QSGSimpleTextureNode* mediaNode() const
    {
        return mediaNode_;
    }

    void updateClip(const QRectF& circleRect)
    {
        const QPointF center = circleRect.center();
        const qreal radius = qMax<qreal>(1.0, qMin(circleRect.width(), circleRect.height()) * 0.5);
        auto* vertices = geometry_->vertexDataAsPoint2D();
        for (int segmentIndex = 0; segmentIndex < kInnerCircleClipSegments; ++segmentIndex) {
            const qreal t0 = static_cast<qreal>(segmentIndex) / static_cast<qreal>(kInnerCircleClipSegments);
            const qreal t1 = static_cast<qreal>(segmentIndex + 1) / static_cast<qreal>(kInnerCircleClipSegments);
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
    QSGSimpleTextureNode* mediaNode_ = nullptr;
};

class StageBackgroundRootNode : public QSGNode
{
public:
    StageBackgroundRootNode()
    {
        baseNode = new QSGSimpleRectNode();
        appendChildNode(baseNode);
    }

    QSGSimpleRectNode* baseNode = nullptr;
    QSGSimpleTextureNode* mediaNode = nullptr;
    StageBackgroundCircleMediaNode* innerCircleMediaNode = nullptr;
    StageBackgroundDimNode* dimNode = nullptr;
};

const QString& stageBackgroundMediaTextureSlotName()
{
    static const QString slotName = QStringLiteral("stage_background_media");
    return slotName;
}

StageMediaFrameResult stageMediaImage(const miacode::preview::scene::PreviewFrameState& state)
{
    StageMediaFrameResult result;
    if (!state.media.resolvedStageImage.isNull()) {
        result.image = state.media.resolvedStageImage;
        result.sourceKind = StageMediaSourceKind::ResolvedFrame;
        result.cacheable = state.media.resolvedStageImageCacheable;
        result.toImageMs = state.media.resolvedStageImageToImageMs;
        result.retainedTextureKey = state.media.stageMediaSerial > 0
            ? state.media.stageMediaSerial
            : static_cast<quint64>(state.media.resolvedStageImage.cacheKey());
        return result;
    }
    if (!state.media.mediaFrame.isNull()) {
        result.image = state.media.mediaFrame;
        result.sourceKind = StageMediaSourceKind::StaticImage;
        result.cacheable = true;
        return result;
    }
    if (!state.media.retainedVideoFallbackFrame.isNull()) {
        result.image = state.media.retainedVideoFallbackFrame;
        result.sourceKind = StageMediaSourceKind::RetainedFallback;
        result.retainedTextureKey = static_cast<quint64>(state.media.retainedVideoFallbackFrame.cacheKey());
    }
    return result;
}

bool stageMediaUsesCachedTexture(const StageMediaFrameResult& media)
{
    return media.sourceKind == StageMediaSourceKind::StaticImage && media.cacheable;
}

StageBackgroundRootNode* ensureStageBackgroundRoot(QSGNode* oldNode)
{
    if (oldNode != nullptr) {
        return static_cast<StageBackgroundRootNode*>(oldNode);
    }
    return new StageBackgroundRootNode();
}

QSGSimpleTextureNode* ensureMediaNode(StageBackgroundRootNode* root)
{
    Q_ASSERT(root != nullptr);
    if (root->mediaNode == nullptr) {
        root->mediaNode = new QSGSimpleTextureNode();
        if (root->dimNode != nullptr) {
            root->insertChildNodeBefore(root->mediaNode, root->dimNode);
        } else {
            root->appendChildNode(root->mediaNode);
        }
    }
    return root->mediaNode;
}

StageBackgroundDimNode* ensureDimNode(StageBackgroundRootNode* root)
{
    Q_ASSERT(root != nullptr);
    if (root->dimNode == nullptr) {
        root->dimNode = new StageBackgroundDimNode();
        root->appendChildNode(root->dimNode);
    }
    return root->dimNode;
}

StageBackgroundCircleMediaNode* ensureInnerCircleMediaNode(StageBackgroundRootNode* root)
{
    Q_ASSERT(root != nullptr);
    if (root->innerCircleMediaNode == nullptr) {
        root->innerCircleMediaNode = new StageBackgroundCircleMediaNode();
        if (root->dimNode != nullptr) {
            root->insertChildNodeBefore(root->innerCircleMediaNode, root->dimNode);
        } else {
            root->appendChildNode(root->innerCircleMediaNode);
        }
    }
    return root->innerCircleMediaNode;
}

void removeMediaNode(StageBackgroundRootNode* root)
{
    if (root == nullptr || root->mediaNode == nullptr) {
        return;
    }
    root->removeChildNode(root->mediaNode);
    delete root->mediaNode;
    root->mediaNode = nullptr;
}

void removeInnerCircleMediaNode(StageBackgroundRootNode* root)
{
    if (root == nullptr || root->innerCircleMediaNode == nullptr) {
        return;
    }
    root->removeChildNode(root->innerCircleMediaNode);
    delete root->innerCircleMediaNode;
    root->innerCircleMediaNode = nullptr;
}

void removeDimNode(StageBackgroundRootNode* root)
{
    if (root == nullptr || root->dimNode == nullptr) {
        return;
    }
    root->removeChildNode(root->dimNode);
    delete root->dimNode;
    root->dimNode = nullptr;
}

}  // namespace

QSGNode* PreviewQuickStageBackgroundLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    Q_UNUSED(window);

    QElapsedTimer nodeTimer;
    nodeTimer.start();

    PreviewStageBackgroundFrameProfile profile;
    auto* root = ensureStageBackgroundRoot(oldNode);
    const QRectF stageRect = miacode::preview::scene::stageRectForSize(renderSize);
    const StageMediaFrameResult media = stageMediaImage(state);
    const bool usesExternalMedia =
        state.media.presentationMode == miacode::preview::scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem;
    const QImage mediaImage = usesExternalMedia ? QImage() : media.image;
    const bool hasMedia = !mediaImage.isNull();
    const double outerDarkAlpha = qBound(0.0, 1.0 - state.render.backgroundBrightnessOuter, 1.0);
    const double innerDarkAlpha = qBound(0.0, 1.0 - state.render.backgroundBrightnessInner, 1.0);
    if (!usesExternalMedia
        && media.sourceKind == StageMediaSourceKind::ResolvedFrame
        && state.media.stageMediaSerial > 0
        && state.media.stageMediaSerial != lastProfiledResolvedStageMediaSerial_) {
        profile.mediaToImageMs = media.toImageMs;
        lastProfiledResolvedStageMediaSerial_ = state.media.stageMediaSerial;
    } else {
        profile.mediaToImageMs = 0.0;
    }
    profile.mediaFrameCount = hasMedia ? 1 : 0;
#ifdef HAVE_QT_MULTIMEDIA
    profile.videoFrameCount = usesExternalMedia ? 0 : (state.media.videoFrame.isValid() ? 1 : 0);
#endif
    profile.staticImageFrameCount =
        !usesExternalMedia && media.sourceKind == StageMediaSourceKind::StaticImage ? 1 : 0;

    root->baseNode->setRect(stageRect);
    if (usesExternalMedia && state.media.stageMediaAvailable) {
        root->baseNode->setColor(Qt::transparent);
    } else {
        root->baseNode->setColor(hasMedia ? QColor(QStringLiteral("#000000")) : QColor(QStringLiteral("#1F2833")));
    }

    if ((!hasMedia || usesExternalMedia) && textures != nullptr) {
        textures->releaseRetainedTexture(stageBackgroundMediaTextureSlotName());
        lastDynamicStageMediaKey_ = 0;
    }

    if (hasMedia && !usesExternalMedia && window != nullptr && textures != nullptr) {
        QElapsedTimer mediaTextureTimer;
        mediaTextureTimer.start();
        auto* mediaNode = ensureMediaNode(root);
        QSGTexture* texture = nullptr;
        if (stageMediaUsesCachedTexture(media)) {
            textures->releaseRetainedTexture(stageBackgroundMediaTextureSlotName());
            lastDynamicStageMediaKey_ = 0;
            texture = textures->textureForImage(mediaImage, true);
        } else {
            if (media.retainedTextureKey == lastDynamicStageMediaKey_) {
                texture = textures->retainedTexture(stageBackgroundMediaTextureSlotName());
            }
            if (texture == nullptr) {
                texture = textures->retainedTextureForImage(stageBackgroundMediaTextureSlotName(), mediaImage);
            }
            if (texture != nullptr) {
                lastDynamicStageMediaKey_ = media.retainedTextureKey;
            }
        }
        profile.mediaTextureMs = static_cast<double>(mediaTextureTimer.nsecsElapsed()) / 1000000.0;
        if (texture != nullptr) {
            const bool innerCircleFitOuterFill =
                state.render.backgroundScaleMode == PreviewBackgroundScaleMode::InnerCircleFitOuterFill;
            const miacode::preview::scene::PreviewMediaPlacement placement =
                miacode::preview::scene::mediaPlacement(
                    mediaImage.size(),
                    stageRect,
                    innerCircleFitOuterFill
                        ? PreviewBackgroundScaleMode::FillCrop
                        : state.render.backgroundScaleMode
                );
            mediaNode->setOwnsTexture(false);
            mediaNode->setTexture(texture);
            mediaNode->setRect(placement.targetRect);
            mediaNode->setSourceRect(texture->convertToNormalizedSourceRect(placement.sourceRect));
            mediaNode->setFiltering(QSGTexture::Linear);
            if (innerCircleFitOuterFill) {
                const QRectF innerCircleRect =
                    miacode::preview::scene::centeredMaxSquareRectForStage(stageRect);
                const miacode::preview::scene::PreviewMediaPlacement innerPlacement =
                    miacode::preview::scene::mediaPlacement(
                        mediaImage.size(),
                        stageRect,
                        PreviewBackgroundScaleMode::SquareFitContain
                    );
                auto* innerCircleNode = ensureInnerCircleMediaNode(root);
                auto* innerMediaNode = innerCircleNode->mediaNode();
                innerCircleNode->updateClip(innerCircleRect);
                innerMediaNode->setOwnsTexture(false);
                innerMediaNode->setTexture(texture);
                innerMediaNode->setRect(innerPlacement.targetRect);
                innerMediaNode->setSourceRect(texture->convertToNormalizedSourceRect(innerPlacement.sourceRect));
                innerMediaNode->setFiltering(QSGTexture::Linear);
            } else {
                removeInnerCircleMediaNode(root);
            }
        } else {
            removeMediaNode(root);
            removeInnerCircleMediaNode(root);
        }
    } else {
        removeMediaNode(root);
        removeInnerCircleMediaNode(root);
    }

    if (outerDarkAlpha > 1e-6 || innerDarkAlpha > 1e-6) {
        QElapsedTimer dimUpdateTimer;
        dimUpdateTimer.start();
        auto* dimNode = ensureDimNode(root);
        StageDimParams params;
        params.stageRect = stageRect;
        params.renderSize = renderSize;
        params.outerDarkAlpha = static_cast<float>(outerDarkAlpha);
        params.innerDarkAlpha = static_cast<float>(innerDarkAlpha);
        params.layoutSquareScale = static_cast<float>(
            miacode::preview_video::normalizedLayoutSquareScale(state.render.layoutSquareScale)
        );
        params.ringRatio = static_cast<float>(
            miacode::preview_video::effectiveLayoutRingDiameterRatio(state.assets.layoutRingDiameterRatio)
        );
        params.smoothBrightness = state.render.smoothBrightness ? 1.0f : 0.0f;
        dimNode->updateNode(renderSize, params);
        profile.dimUniformUpdateMs = static_cast<double>(dimUpdateTimer.nsecsElapsed()) / 1000000.0;
        profile.dimFrameCount = 1;
        profile.dimUniformUpdateCount = 1;
    } else {
        removeDimNode(root);
    }

    profile.nodeUpdateMs = static_cast<double>(nodeTimer.nsecsElapsed()) / 1000000.0;
    if (textures != nullptr) {
        textures->setStageBackgroundFrameProfile(profile);
    }
    return root;
}
