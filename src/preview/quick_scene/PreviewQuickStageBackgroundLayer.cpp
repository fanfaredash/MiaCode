#include "preview/quick_scene/PreviewQuickStageBackgroundLayer.h"

#include "common/PreviewVideoGeometryConfig.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewSceneGeometry.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <cmath>

namespace {

enum class StageMediaSourceKind {
    None,
    VideoFrame,
    StaticImage,
    RetainedFallback,
};

struct StageMediaFrameResult {
    QImage image;
    StageMediaSourceKind sourceKind = StageMediaSourceKind::None;
    double toImageMs = 0.0;
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
    QSGSimpleTextureNode* maskNode = nullptr;
};

const QString& stageBackgroundMaskTextureSlotName()
{
    static const QString slotName = QStringLiteral("stage_background_mask");
    return slotName;
}

StageMediaFrameResult stageMediaImage(const miacode::preview::scene::PreviewFrameState& state)
{
    StageMediaFrameResult result;
#ifdef HAVE_QT_MULTIMEDIA
    if (state.media.videoFrame.isValid()) {
        QElapsedTimer timer;
        timer.start();
        const QImage image = state.media.videoFrame.toImage();
        result.toImageMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        if (!image.isNull()) {
            result.image = image;
            result.sourceKind = StageMediaSourceKind::VideoFrame;
            return result;
        }
    }
#endif
    if (!state.media.mediaFrame.isNull()) {
        result.image = state.media.mediaFrame;
        result.sourceKind = StageMediaSourceKind::StaticImage;
        return result;
    }
    if (!state.media.retainedVideoFallbackFrame.isNull()) {
        result.image = state.media.retainedVideoFallbackFrame;
        result.sourceKind = StageMediaSourceKind::RetainedFallback;
    }
    return result;
}

bool stageMediaUsesCachedTexture(const StageMediaFrameResult& media)
{
    return media.sourceKind == StageMediaSourceKind::StaticImage;
}

QImage buildBrightnessMask(
    const QSize& renderSize,
    const QRectF& stageRect,
    double outerDarkAlpha,
    double innerDarkAlpha,
    double layoutSquareScale,
    double ringRatio,
    bool smoothBrightness
)
{
    QImage mask(renderSize, QImage::Format_RGBA8888);
    mask.fill(Qt::transparent);
    const double layoutSide = miacode::preview_video::layoutSquareSideForCanvasHeight(
        static_cast<double>(renderSize.height()),
        layoutSquareScale
    );
    const double centerX = stageRect.center().x();
    const double centerY = stageRect.center().y();
    for (int y = 0; y < renderSize.height(); ++y) {
        uchar* row = mask.scanLine(y);
        const double dy = static_cast<double>(y) - centerY;
        for (int x = 0; x < renderSize.width(); ++x) {
            const double dx = static_cast<double>(x) - centerX;
            const double radius = std::sqrt(dx * dx + dy * dy);
            const int alpha = qBound(
                0,
                qRound(
                    miacode::preview_video::dimAlphaForRadius(
                        radius,
                        outerDarkAlpha,
                        innerDarkAlpha,
                        layoutSide,
                        ringRatio,
                        smoothBrightness
                    ) * 255.0
                ),
                255
            );
            const int offset = x * 4;
            row[offset + 0] = 0;
            row[offset + 1] = 0;
            row[offset + 2] = 0;
            row[offset + 3] = static_cast<uchar>(alpha);
        }
    }
    return mask;
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
        if (root->maskNode != nullptr) {
            root->insertChildNodeBefore(root->mediaNode, root->maskNode);
        } else {
            root->appendChildNode(root->mediaNode);
        }
    }
    return root->mediaNode;
}

QSGSimpleTextureNode* ensureMaskNode(StageBackgroundRootNode* root)
{
    Q_ASSERT(root != nullptr);
    if (root->maskNode == nullptr) {
        root->maskNode = new QSGSimpleTextureNode();
        root->appendChildNode(root->maskNode);
    }
    return root->maskNode;
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

void removeMaskNode(StageBackgroundRootNode* root)
{
    if (root == nullptr || root->maskNode == nullptr) {
        return;
    }
    root->removeChildNode(root->maskNode);
    delete root->maskNode;
    root->maskNode = nullptr;
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
    QElapsedTimer nodeTimer;
    nodeTimer.start();

    PreviewStageBackgroundFrameProfile profile;
    auto* root = ensureStageBackgroundRoot(oldNode);
    const QRectF stageRect = miacode::preview::scene::stageRectForSize(renderSize);
    const StageMediaFrameResult media = stageMediaImage(state);
    const QImage mediaImage = media.image;
    const bool hasMedia = !mediaImage.isNull();
    const double outerDarkAlpha = qBound(0.0, 1.0 - state.render.backgroundBrightnessOuter, 1.0);
    const double innerDarkAlpha = qBound(0.0, 1.0 - state.render.backgroundBrightnessInner, 1.0);
    profile.mediaToImageMs = media.toImageMs;
    profile.mediaFrameCount = hasMedia ? 1 : 0;
#ifdef HAVE_QT_MULTIMEDIA
    profile.videoFrameCount = state.media.videoFrame.isValid() ? 1 : 0;
#endif
    profile.staticImageFrameCount = media.sourceKind == StageMediaSourceKind::StaticImage ? 1 : 0;

    root->baseNode->setRect(stageRect);
    root->baseNode->setColor(hasMedia ? QColor(QStringLiteral("#000000")) : QColor(QStringLiteral("#1F2833")));

    if (hasMedia && window != nullptr && textures != nullptr) {
        QElapsedTimer mediaTextureTimer;
        mediaTextureTimer.start();
        auto* mediaNode = ensureMediaNode(root);
        const QSGTexture* texture =
            textures->textureForImage(mediaImage, stageMediaUsesCachedTexture(media));
        profile.mediaTextureMs = static_cast<double>(mediaTextureTimer.nsecsElapsed()) / 1000000.0;
        if (texture != nullptr) {
            mediaNode->setOwnsTexture(false);
            mediaNode->setTexture(const_cast<QSGTexture*>(texture));
            mediaNode->setRect(
                miacode::preview::scene::mediaTargetRect(
                    mediaImage.size(),
                    stageRect,
                    state.render.backgroundScaleMode == PreviewBackgroundScaleMode::FitContain
                )
            );
            mediaNode->setFiltering(QSGTexture::Linear);
        } else {
            removeMediaNode(root);
        }
    } else {
        removeMediaNode(root);
    }

    if (outerDarkAlpha > 1e-6 || innerDarkAlpha > 1e-6) {
        const double normalizedLayoutScale = miacode::preview_video::normalizedLayoutSquareScale(state.render.layoutSquareScale);
        const double ringRatio = miacode::preview_video::effectiveLayoutRingDiameterRatio(state.assets.layoutRingDiameterRatio);
        profile.maskFrameCount = 1;
        if (brightnessMaskCache_.isNull()
            || brightnessMaskCacheSize_ != renderSize
            || qAbs(brightnessMaskCacheOuter_ - outerDarkAlpha) > 1e-6
            || qAbs(brightnessMaskCacheInner_ - innerDarkAlpha) > 1e-6
            || qAbs(brightnessMaskCacheLayoutScale_ - normalizedLayoutScale) > 1e-6
            || qAbs(brightnessMaskCacheRingRatio_ - ringRatio) > 1e-6
            || brightnessMaskCacheSmooth_ != state.render.smoothBrightness) {
            QElapsedTimer maskBuildTimer;
            maskBuildTimer.start();
            // Keep the existing dimming contract intact; optimize only cache lifetimes around it.
            brightnessMaskCache_ = buildBrightnessMask(
                renderSize,
                stageRect,
                outerDarkAlpha,
                innerDarkAlpha,
                normalizedLayoutScale,
                ringRatio,
                state.render.smoothBrightness
            );
            profile.maskBuildMs = static_cast<double>(maskBuildTimer.nsecsElapsed()) / 1000000.0;
            profile.maskRebuildCount = 1;
            brightnessMaskCacheSize_ = renderSize;
            brightnessMaskCacheOuter_ = outerDarkAlpha;
            brightnessMaskCacheInner_ = innerDarkAlpha;
            brightnessMaskCacheLayoutScale_ = normalizedLayoutScale;
            brightnessMaskCacheRingRatio_ = ringRatio;
            brightnessMaskCacheSmooth_ = state.render.smoothBrightness;
        }

        if (window != nullptr && textures != nullptr) {
            QElapsedTimer maskTextureTimer;
            maskTextureTimer.start();
            auto* maskNode = ensureMaskNode(root);
            QSGTexture* maskTexture =
                textures->retainedTextureForImage(stageBackgroundMaskTextureSlotName(), brightnessMaskCache_);
            profile.maskTextureUploadMs = static_cast<double>(maskTextureTimer.nsecsElapsed()) / 1000000.0;
            if (maskTexture != nullptr) {
                maskNode->setOwnsTexture(false);
                maskNode->setTexture(maskTexture);
                maskNode->setRect(QRectF(QPointF(0.0, 0.0), QSizeF(renderSize)));
                maskNode->setFiltering(QSGTexture::Linear);
            } else {
                removeMaskNode(root);
            }
        } else {
            removeMaskNode(root);
        }
    } else {
        if (textures != nullptr) {
            textures->releaseRetainedTexture(stageBackgroundMaskTextureSlotName());
        }
        removeMaskNode(root);
    }

    profile.nodeUpdateMs = static_cast<double>(nodeTimer.nsecsElapsed()) / 1000000.0;
    if (textures != nullptr) {
        textures->setStageBackgroundFrameProfile(profile);
    }
    return root;
}
